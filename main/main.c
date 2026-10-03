// Snap Runner: the application task owns state and NVS; the audio task owns PCM.
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "snap_detector.h"
#include "snap_session.h"
#include "snap_ui.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <math.h>
#include <stdint.h>

static const char *TAG = "snap";

typedef struct {
    bool button;
    bsp_btn_t key;
    snap_sound_t sound;
    float noise;
    uint64_t at_ms;
} app_input_t;

typedef struct {
    uint64_t at_ms;
    float rms;
    float noise;
    bool ok;
    bool warming;
    bool sleeping;
} mic_status_t;

typedef struct {
    uint32_t version;
    snap_range_t range;
    uint32_t high_score;
} stored_t;

static QueueHandle_t s_inputs;
static QueueHandle_t s_mic_status;
static QueueHandle_t s_capture_requests;
static bool s_audio_ok;
static bool s_buttons_ok;
static bool s_storage_ok;
static bool s_battery_ok;
static nvs_handle_t s_nvs;
static bool s_nvs_open;
static snap_session_t s_session;

static uint64_t now_ms(void) {
    return (uint64_t)esp_timer_get_time() / 1000;
}

static void on_button(bsp_btn_t key, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (event != BSP_BTN_PRESS) return;
    const app_input_t input = {.button = true, .key = key, .at_ms = now_ms()};
    (void)xQueueSend(s_inputs, &input, 0);
}

/* Used only when no PCM reader exists (before worker creation or by the exiting
 * worker). A failed suspend never falsely authorizes system sleep. */
static void terminal_audio_failure(void) {
    const esp_err_t result = bsp_audio_sleep();
    const mic_status_t status = {
        .at_ms = now_ms(), .ok = false, .sleeping = result == ESP_OK,
    };
    xQueueOverwrite(s_mic_status, &status);
    if (result != ESP_OK)
        ESP_LOGW(TAG, "Audio shutdown unverified; idle sleep inhibited: %s", esp_err_to_name(result));
}

static void microphone_task(void *arg) {
    (void)arg;
    snap_detector_t detector;
    snap_detector_init(&detector);
    int16_t pcm[SNAP_FRAME_SAMPLES];
    uint64_t last_status = 0;
    if (!s_audio_ok) {
        terminal_audio_failure();
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        bool capture = true;
        if (xQueueReceive(s_capture_requests, &capture, 0) == pdTRUE && !capture) {
            // Only this worker owns codec transitions, after its last PCM read.
            const esp_err_t result = bsp_audio_sleep();
            const mic_status_t sleeping = {
                .at_ms = now_ms(), .ok = result == ESP_OK,
                .sleeping = result == ESP_OK,
            };
            xQueueOverwrite(s_mic_status, &sleeping);
            do {
                xQueueReceive(s_capture_requests, &capture, portMAX_DELAY);
            } while (!capture);
            if (bsp_audio_wake() != ESP_OK) {
                terminal_audio_failure();
                vTaskDelete(NULL);
                return;
            }
            snap_detector_init(&detector);
        }
        const esp_err_t result = bsp_audio_read(pcm, sizeof(pcm));
        const uint64_t now = now_ms();
        if (result != ESP_OK) {
            const mic_status_t status = {.at_ms = now, .ok = false};
            xQueueOverwrite(s_mic_status, &status);
            snap_detector_init(&detector);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        snap_sound_t sound;
        if (snap_detector_process(&detector, pcm, &sound)) {
            const app_input_t input = {
                .sound = sound,
                .noise = sqrtf(fmaxf(100.0f, detector.noise_power)),
                .at_ms = now,
            };
            (void)xQueueSend(s_inputs, &input, 0);
        }
        if (now - last_status >= 100) {
            const mic_status_t status = {
                .at_ms = now, .rms = detector.rms,
                .noise = sqrtf(fmaxf(100.0f, detector.noise_power)),
                .ok = true, .warming = detector.warmup < 160,
            };
            xQueueOverwrite(s_mic_status, &status);
            last_status = now;
        }
        /* Reading normally blocks. Also yield when draining a DMA backlog so
         * a high-priority producer cannot starve the game/idle tasks. */
        vTaskDelay(1);
    }
}

static void storage_load(void) {
    stored_t stored = {0};
    const esp_err_t init = nvs_flash_init();
    s_storage_ok = init == ESP_OK;
    if (s_storage_ok) {
        s_nvs_open = nvs_open("snap_runner", NVS_READWRITE, &s_nvs) == ESP_OK;
        s_storage_ok = s_nvs_open;
    }
    if (s_nvs_open) {
        size_t size = sizeof(stored);
        const esp_err_t result = nvs_get_blob(s_nvs, "profile", &stored, &size);
        if (result != ESP_OK || size != sizeof(stored) || stored.version != 1) {
            stored = (stored_t){0};
            if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) s_storage_ok = false;
        }
    }
    /* A full/foreign NVS partition is never erased automatically. */
    snap_session_init(&s_session, stored.range, stored.high_score, esp_random());
}

static void storage_save(void) {
    const stored_t stored = {
        .version = 1, .range = s_session.range, .high_score = s_session.high_score,
    };
    s_session.save_pending = false;
    if (!s_nvs_open) { s_storage_ok = false; return; }
    esp_err_t result = nvs_set_blob(s_nvs, "profile", &stored, sizeof(stored));
    if (result == ESP_OK) result = nvs_commit(s_nvs);
    s_storage_ok = result == ESP_OK;
    if (!s_storage_ok) ESP_LOGW(TAG, "Profile save failed: %s", esp_err_to_name(result));
}

static void application_task(void *arg) {
    (void)arg;
    uint64_t last = now_ms(), activity = last, battery_at = 0, health_at = 0;
    uint64_t render_after = 0;
    snap_page_t rendered_page = SNAP_OVER;
    unsigned accumulator = 0;
    unsigned brightness = 75;
    bool capture_requested = true;
    int battery = -1;
    mic_status_t mic = {0};
    esp_err_t last_sleep_error = ESP_OK;
    for (;;) {
        const snap_page_t previous_page = s_session.page;
        (void)xQueuePeek(s_mic_status, &mic, 0);
        // Sample the clock AFTER the producer snapshot to prevent underflow
        // if the higher-priority microphone published while we were preempted.
        const uint64_t now = now_ms();
        const bool mic_ok = mic.ok && now - mic.at_ms < 500;
        snap_session_mic(&s_session, mic_ok && !mic.warming, now);
        app_input_t input;
        while (xQueueReceive(s_inputs, &input, 0) == pdTRUE) {
            if (input.button) {
                activity = now;
                const snap_key_t key = input.key == BSP_BTN_UP ? SNAP_KEY_UP :
                    input.key == BSP_BTN_DOWN ? SNAP_KEY_DOWN : SNAP_KEY_OK;
                snap_session_key(&s_session, key, now);
            } else if (now_ms() - input.at_ms < 150 && input.at_ms >= s_session.accept_after_ms) {
                snap_session_sound(&s_session, input.sound.amplitude, input.sound.clipped,
                                   input.noise > 450.0f, input.at_ms);
                if (s_session.page != SNAP_HOME && s_session.page != SNAP_PAUSED &&
                    s_session.page != SNAP_OVER) activity = now;
            }
        }
        const uint64_t elapsed = s_session.page == previous_page ? now - last : 0;
        if (s_session.page != previous_page) accumulator = 0;
        last = now;
        if (elapsed > 250 && s_session.page == SNAP_PLAYING) {
            /* A scheduling stall must not teleport the player into a hazard. */
            snap_session_key(&s_session, SNAP_KEY_OK, now);
        }
        if (s_session.page == SNAP_PLAYING || s_session.page == SNAP_PRACTICE) {
            accumulator += (unsigned)(elapsed > 100 ? 100 : elapsed);
            while (accumulator >= 10) {
                snap_session_step(&s_session);
                accumulator -= 10;
            }
            activity = now;
        } else accumulator = 0;
        if (s_session.save_pending) storage_save();
        if (now >= battery_at) {
            battery = s_battery_ok ? bsp_battery_soc() : -1;
            battery_at = now + 30000;
        }
        const unsigned next_brightness = now - activity > 180000 ? 0 :
                                         now - activity > 60000 ? 8 : 75;
        if (brightness != next_brightness) {
            bsp_display_backlight(next_brightness);
            brightness = next_brightness;
        }
        const bool want_capture = next_brightness != 0;
        if (capture_requested != want_capture) {
            xQueueOverwrite(s_capture_requests, &want_capture);
            capture_requested = want_capture;
        }
        if (!want_capture && mic.sleeping) {
            // The baseline supports RTC timer wake. Short sleep slices avoid
            // inventing a GPIO wake contract on the ADC-owned button ladder.
            // Never hold the LVGL lock while waiting for the audio handshake.
            if (bsp_lvgl_lock(100)) {
                esp_err_t result = esp_sleep_enable_timer_wakeup(100000);
                if (result == ESP_OK) result = esp_light_sleep_start();
                (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
                bsp_lvgl_unlock();
                if (result != last_sleep_error) {
                    ESP_LOGW(TAG, "Idle sleep result: %s", esp_err_to_name(result));
                    last_sleep_error = result;
                }
            }
            // Let the button task run; also detect a held key through the BSP.
            vTaskDelay(pdMS_TO_TICKS(30));
            const int mv = bsp_button_read_mv();
            static const int button_windows[BSP_BTN_COUNT][2] = BSP_BTN_MV_TABLE;
            for (unsigned i = 0; i < BSP_BTN_COUNT; i++) {
                if (mv >= button_windows[i][0] && mv < button_windows[i][1])
                    activity = now_ms();
            }
            last = now_ms();
            continue;
        }
        const snap_ui_status_t status = {
            .mic_ok = mic_ok, .mic_warming = mic.warming,
            .storage_ok = s_storage_ok, .buttons_ok = s_buttons_ok,
            .rms = mic.rms, .noise = mic.noise, .battery = battery, .now_ms = now,
        };
        if (brightness && (now >= render_after || s_session.page != rendered_page) &&
            bsp_lvgl_lock(30)) {
            snap_ui_update(&s_session, &status);
            bsp_lvgl_unlock();
            rendered_page = s_session.page;
            const bool playing = s_session.page == SNAP_PLAYING || s_session.page == SNAP_PRACTICE;
            // Render deadlines follow completion, never enqueue missed frames.
            render_after = now_ms() + (playing ? 33 : 100);
        }
        if (now >= health_at) {
            ESP_LOGI(TAG, "heap=%u min=%u largest=%u app_stack=%u",
                     (unsigned)esp_get_free_heap_size(),
                     (unsigned)esp_get_minimum_free_heap_size(),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            health_at = now + 60000;
        }
        // On single-core C3 an overrun must still block, not continuously chase
        // a vTaskDelayUntil deadline and starve IDLE together with LVGL/audio.
        vTaskDelay(pdMS_TO_TICKS(snap_loop_delay_ms(now_ms() - now)));
    }
}

void app_main(void) {
    storage_load();
    s_audio_ok = bsp_audio_init() == ESP_OK;
    if (s_audio_ok) s_audio_ok = bsp_audio_set_format(SNAP_SAMPLE_RATE, 16, 1) == ESP_OK;
    if (s_audio_ok) bsp_audio_set_volume(0);
    s_battery_ok = bsp_battery_init() == ESP_OK;
    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init() || !bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "Display/LVGL initialization failed");
        return;
    }
    const bool ui_ready = snap_ui_create();
    bsp_lvgl_unlock();
    if (!ui_ready) {
        ESP_LOGE(TAG, "Chinese font coverage check failed");
        return;
    }
    s_inputs = xQueueCreate(16, sizeof(app_input_t));
    s_mic_status = xQueueCreate(1, sizeof(mic_status_t));
    s_capture_requests = xQueueCreate(1, sizeof(bool));
    if (!s_inputs || !s_mic_status || !s_capture_requests) {
        ESP_LOGE(TAG, "Input queue allocation failed");
        return;
    }
    s_buttons_ok = bsp_button_init(on_button, NULL) == ESP_OK;
    if (xTaskCreate(microphone_task, "snap_mic", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Microphone worker allocation failed");
        terminal_audio_failure();
    }
    if (xTaskCreate(application_task, "snap_app", 6144, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Application worker allocation failed");
    }
    bsp_display_backlight(75);
    ESP_LOGI(TAG, "Snap Runner ready; PCM 16 kHz, no radio, no speaker output");
}
