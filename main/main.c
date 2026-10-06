// Snap Runner: the application task owns state and NVS; the audio task owns PCM.
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "snap_detector.h"
#include "snap_debug.h"
#include "snap_music.h"
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
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"

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
    uint32_t music_ms;
    uint32_t generation;
    bool music_ok;
    bool music_playing;
} mic_status_t;

typedef struct {
    uint32_t version;
    snap_range_t range;
    uint32_t high_score;
} stored_t;

static QueueHandle_t s_inputs;
static QueueHandle_t s_mic_status;
static QueueHandle_t s_capture_requests;
static QueueHandle_t s_music_requests;
extern const uint8_t music_start[] asm("_binary_mountain_king_pcm_start");
extern const uint8_t music_end[] asm("_binary_mountain_king_pcm_end");
static bool s_audio_ok;
static bool s_buttons_ok;
static bool s_storage_ok;
static bool s_battery_ok;
static nvs_handle_t s_nvs;
static bool s_nvs_open;
static snap_session_t s_session;

/* Producers never wait for USB. Queue overflow loses diagnostics, not input.
 * Only the writer formats/prints, outside the audio, button and LVGL paths. */
#if CONFIG_SNAP_DEBUG_TELEMETRY
static QueueHandle_t s_debug;
static atomic_uint s_debug_dropped;
static void debug_send(snap_debug_record_t *record) {
    if (!s_debug) return;
    record->dropped = atomic_load_explicit(&s_debug_dropped, memory_order_relaxed);
    if (xQueueSend(s_debug, record, 0) != pdTRUE)
        atomic_fetch_add_explicit(&s_debug_dropped, 1, memory_order_relaxed);
}
static void debug_task(void *arg) {
    (void)arg;
    snap_debug_record_t record;
    /* Single writer owns this buffer. Keep it off the 4 KB task stack:
     * newlib's float formatting plus the interrupt frame needs that headroom. */
    static char line[SNAP_DEBUG_LINE_SIZE];
    bool stack_reported = false;
    for (;;) {
        if (xQueueReceive(s_debug, &record, portMAX_DELAY) == pdTRUE &&
            snap_debug_format(line, sizeof(line), &record)) {
            printf("@snap %s\n", line);
            if (!stack_reported && record.kind == SNAP_DEBUG_STATE) {
                ESP_LOGI(TAG, "debug stack remaining=%u bytes after state formatting",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
                stack_reported = true;
            }
        }
    }
}
#else
static void debug_send(snap_debug_record_t *record) { (void)record; }
#endif

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
    int16_t pcm[SNAP_FRAME_SAMPLES], output[SNAP_FRAME_SAMPLES];
    snap_mic_filter_t filter = {0};
    snap_music_t music;
    /* BSP TX has six 240-frame descriptors. Fill that queue before using
     * submitted samples minus its nominal latency as the playback clock.
     * RX pacing alone would otherwise leave the TX queue almost empty. */
    static const int16_t prime_silence[6 * 240] = {0};
    snap_music_init(&music, music_start, (size_t)(music_end - music_start),
                    CONFIG_SNAP_OUTPUT_DELAY_MS);
    unsigned current_volume = 0;
    uint64_t last_io = 0;
    uint64_t last_status = 0;
    uint64_t playback_report_at = 0;
    unsigned output_peak = 0, output_io_max = 0;
    float interval_peak = 0;
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
            filter = (snap_mic_filter_t){0};
            last_io = 0;
        }
        snap_music_request_t request;
        if (xQueueReceive(s_music_requests, &request, 0) == pdTRUE) {
            const bool begin = request.playing &&
                (request.generation != music.request.generation || !music.request.playing);
            snap_music_request(&music, request);
            if (begin) {
                ESP_LOGI(TAG, "music begin generation=%lu seek=%lu samples=%u",
                         (unsigned long)request.generation, (unsigned long)request.seek_ms,
                         (unsigned)music.samples);
                output_peak = output_io_max = 0;
                playback_report_at = now_ms() + 2000;
                bsp_audio_set_volume(0);
                current_volume = 0;
                music.failed = bsp_audio_write(prime_silence, sizeof(prime_silence)) != ESP_OK;
                if (bsp_audio_discard_input() != ESP_OK) music.failed = true;
                filter = (snap_mic_filter_t){0};
                last_io = now_ms();
            }
        }
        /* A producer stall invalidates the sample timeline. Pause instead of
         * letting silent DMA underruns desynchronise music and hazards. */
        if (last_io && now_ms() - last_io > 40 && snap_music_audible(&music))
            music.failed = true;
        const unsigned volume = snap_music_audible(&music) ? music.request.volume : 0;
        if (volume != current_volume) {
            const unsigned codec_volume = snap_music_codec_volume(volume);
            bsp_audio_set_volume(codec_volume);
            current_volume = volume;
            ESP_LOGI(TAG, "music volume=%u codec=%u position=%lu ms",
                     volume, codec_volume, (unsigned long)snap_music_position_ms(&music));
            if (volume) {
                bsp_audio_output_status_t regs;
                const esp_err_t status = bsp_audio_get_output_status(&regs);
                if (status == ESP_OK) {
                    ESP_LOGI(TAG, "audio readback clock=%02x interface=%02x power=%02x "
                             "dac_power=%02x mute=%02x volume=%02x route=%02x",
                             regs.clock, regs.dac_interface, regs.power, regs.dac_power,
                             regs.mute, regs.volume, regs.route);
                } else ESP_LOGE(TAG, "audio readback failed: %s", esp_err_to_name(status));
            }
        }
        const uint64_t io_started = now_ms();
        snap_music_frame(&music, output);
        for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++) {
            const unsigned peak = output[i] < 0 ? -(int)output[i] : output[i];
            if (peak > output_peak) output_peak = peak;
        }
        const esp_err_t written = bsp_audio_write(output, sizeof(output));
        snap_music_commit(&music, written == ESP_OK);
        const esp_err_t result = bsp_audio_read(pcm, sizeof(pcm));
        const uint64_t now = now_ms();
        if (now - io_started > output_io_max) output_io_max = (unsigned)(now - io_started);
        if (now - io_started > 40 && snap_music_audible(&music)) music.failed = true;
        last_io = now;
        if (music.request.playing && now >= playback_report_at) {
            ESP_LOGI(TAG, "music output position=%lu sent=%u peak=%u io_max=%u failed=%u stack=%u",
                     (unsigned long)snap_music_position_ms(&music), (unsigned)music.sent,
                     output_peak, output_io_max, (unsigned)music.failed,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            output_peak = output_io_max = 0;
            playback_report_at = now + 2000;
        }
        if (result != ESP_OK || written != ESP_OK) {
            music.failed = true;
            bsp_audio_set_volume(0);
            current_volume = 0;
            const mic_status_t status = {
                .at_ms = now, .ok = false, .music_ok = false,
                .generation = music.request.generation,
                .music_playing = music.request.playing,
            };
            xQueueOverwrite(s_mic_status, &status);
            snap_detector_init(&detector);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        snap_mic_filter(&filter, pcm);
        snap_sound_t sound;
        const bool detected = snap_detector_process(&detector, pcm, &sound);
        interval_peak = fmaxf(interval_peak, detector.peak);
        if (detected) {
            const app_input_t input = {
                .sound = sound,
                .noise = sqrtf(fmaxf(100.0f, detector.noise_power)),
                .at_ms = now,
            };
            (void)xQueueSend(s_inputs, &input, 0);
        }
        const bool decision = detector.reason == SNAP_DETECT_ACCEPTED ||
                              detector.reason == SNAP_DETECT_DECAY ||
                              detector.reason == SNAP_DETECT_SPECTRUM;
        if (decision) {
            snap_debug_record_t record = {
                .kind = SNAP_DEBUG_DETECT, .at_ms = now,
                .data.audio = {.peak = detector.burst_peak, .rms = detector.burst_max_rms,
                    .noise = sqrtf(fmaxf(100, detector.noise_power)),
                    .reason = detector.reason, .clipped = detector.burst_clipped},
            };
            debug_send(&record);
        }
        {
            const mic_status_t status = {
                .at_ms = now, .rms = detector.rms,
                .noise = sqrtf(fmaxf(100.0f, detector.noise_power)),
                .ok = true, .warming = detector.warmup < 160,
                .music_ms = snap_music_position_ms(&music),
                .generation = music.request.generation, .music_ok = !music.failed,
                .music_playing = music.request.playing,
            };
            xQueueOverwrite(s_mic_status, &status);
        }
        if (now - last_status >= 100) {
            snap_debug_record_t record = {
                .kind = SNAP_DEBUG_AUDIO, .at_ms = now,
                .data.audio = {.peak = interval_peak, .rms = detector.rms,
                    .noise = sqrtf(fmaxf(100, detector.noise_power)), .reason = detector.reason},
            };
            debug_send(&record);
            interval_peak = 0;
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
        const esp_err_t result = nvs_get_blob(s_nvs, "rhythm_profile", &stored, &size);
        if (result != ESP_OK || size != sizeof(stored) || stored.version != 2) {
            stored = (stored_t){0};
            if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) s_storage_ok = false;
        }
    }
    /* A full/foreign NVS partition is never erased automatically. */
    snap_session_init(&s_session, stored.range, stored.high_score, esp_random());
}

static void storage_save(void) {
    const stored_t stored = {
        .version = 2, .range = s_session.range, .high_score = s_session.high_score,
    };
    s_session.save_pending = false;
    if (!s_nvs_open) { s_storage_ok = false; return; }
    esp_err_t result = nvs_set_blob(s_nvs, "rhythm_profile", &stored, sizeof(stored));
    if (result == ESP_OK) result = nvs_commit(s_nvs);
    s_storage_ok = result == ESP_OK;
    if (!s_storage_ok) ESP_LOGW(TAG, "Profile save failed: %s", esp_err_to_name(result));
}

static void application_task(void *arg) {
    (void)arg;
    uint64_t last = now_ms(), activity = last, battery_at = 0, health_at = 0;
    uint64_t render_after = 0, debug_after = 0;
    snap_page_t rendered_page = SNAP_OVER;
    unsigned accumulator = 0;
    unsigned brightness = 75;
    bool capture_requested = true;
    bool music_requested = false;
    uint32_t music_generation = 0, music_run = s_session.seed;
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
        if (s_session.page == SNAP_PLAYING && music_requested && mic.music_playing &&
            mic.generation == music_generation) {
            if (!mic.music_ok || !snap_session_sync_music(&s_session, mic.music_ms)) {
                snap_session_key(&s_session, SNAP_KEY_OK, now);
            }
        }
        app_input_t input;
        while (xQueueReceive(s_inputs, &input, 0) == pdTRUE) {
            if (input.button) {
                activity = now;
                const snap_key_t key = input.key == BSP_BTN_UP ? SNAP_KEY_UP :
                    input.key == BSP_BTN_DOWN ? SNAP_KEY_DOWN : SNAP_KEY_OK;
                snap_session_key(&s_session, key, now);
            } else {
                unsigned result = 9; /* stale: producer event older than 150 ms */
                if (now_ms() - input.at_ms < 150) {
                    const bool eligible = input.at_ms >= s_session.accept_after_ms;
                    result = !eligible ? SNAP_INPUT_GUARD :
                        snap_session_sound(&s_session, input.sound.amplitude, input.sound.clipped,
                                           input.noise > 450.0f, input.at_ms);
                    if (eligible &&
                        s_session.page != SNAP_HOME && s_session.page != SNAP_PAUSED &&
                        s_session.page != SNAP_OVER) activity = now;
                }
                snap_debug_record_t record = {
                    .kind = SNAP_DEBUG_INPUT, .at_ms = input.at_ms,
                    .data.audio = {.peak = input.sound.amplitude, .noise = input.noise,
                        .reason = result, .clipped = input.sound.clipped},
                };
                debug_send(&record);
            }
        }
        const uint64_t elapsed = s_session.page == previous_page ? now - last : 0;
        if (s_session.page != previous_page) accumulator = 0;
        last = now;
        if (elapsed > 250 && s_session.page == SNAP_PLAYING) {
            /* A scheduling stall must not teleport the player into a hazard. */
            snap_session_key(&s_session, SNAP_KEY_OK, now);
        }
        if (s_session.page == SNAP_PRACTICE) {
            accumulator += (unsigned)(elapsed > 100 ? 100 : elapsed);
            while (accumulator >= 10) {
                snap_session_step(&s_session);
                accumulator -= 10;
            }
            activity = now;
        } else accumulator = 0;
        if (s_session.page == SNAP_PLAYING) activity = now;
        const bool play_music = s_session.page == SNAP_PLAYING;
        if (play_music != music_requested || music_run != s_session.seed) music_generation++;
        music_requested = play_music;
        music_run = s_session.seed;
        const snap_music_request_t music_request = {
            .generation = music_generation, .playing = play_music,
            .seek_ms = s_session.game.song_ms, .volume = s_session.volume,
        };
        xQueueOverwrite(s_music_requests, &music_request);
        /* NVS writes only after the playback worker has acknowledged a stop. */
        if (s_session.save_pending && s_session.page != SNAP_PLAYING &&
            !mic.music_playing) storage_save();
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
            .music_fault = !mic.music_ok,
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
        if (now >= debug_after) {
            snap_debug_record_t record = {
                .kind = SNAP_DEBUG_STATE, .at_ms = now, .data.session = s_session,
            };
            debug_send(&record);
            debug_after = now + 200;
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

#if CONFIG_SNAP_BOOT_AUDIO_PROBE
static void boot_audio_probe(void) {
    static const int16_t sine[] = {
        0, 3135, 5793, 7568, 8192, 7568, 5793, 3135,
        0, -3135, -5793, -7568, -8192, -7568, -5793, -3135,
    };
    int16_t frame[SNAP_FRAME_SAMPLES], input[SNAP_FRAME_SAMPLES];
    for (unsigned mode = 0; mode < 2; mode++) {
        const esp_err_t drained = bsp_audio_discard_input();
        ESP_LOGI(TAG, "audio probe %u: %s 1kHz, 500ms, codec=80, input_drain=%d",
                 mode + 1, mode ? "duplex" : "output only", (int)drained);
        bsp_audio_set_volume(80);
        bool ok = true;
        unsigned capture_peak = 0;
        for (unsigned block = 0; block < 100; block++) {
            for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++) frame[i] = sine[i % 16];
            if (bsp_audio_write(frame, sizeof(frame)) != ESP_OK ||
                (mode && bsp_audio_read(input, sizeof(input)) != ESP_OK)) {
                ok = false;
                break;
            }
            if (mode) {
                for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++) {
                    const unsigned peak = input[i] < 0 ? -(int)input[i] : input[i];
                    if (peak > capture_peak) capture_peak = peak;
                }
            }
            if (mode) vTaskDelay(1);
        }
        memset(frame, 0, sizeof(frame));
        for (unsigned block = 0; block < 40; block++) {
            if (bsp_audio_write(frame, sizeof(frame)) != ESP_OK) { ok = false; break; }
        }
        vTaskDelay(pdMS_TO_TICKS(150));
        bsp_audio_set_volume(0);
        ESP_LOGI(TAG, "audio probe %u submitted=%s capture_peak=%u",
                 mode + 1, ok ? "OK" : "FAILED", capture_peak);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    (void)bsp_audio_discard_input();
}
#endif

void app_main(void) {
#if CONFIG_SNAP_DEBUG_TELEMETRY
    s_debug = xQueueCreate(8, sizeof(snap_debug_record_t));
    if (s_debug && xTaskCreate(debug_task, "snap_debug", 4096, NULL, 1, NULL) != pdPASS) {
        vQueueDelete(s_debug);
        s_debug = NULL;
    }
    if (!s_debug) ESP_LOGW(TAG, "Debug telemetry unavailable; game continues");
#endif
    storage_load();
    s_audio_ok = bsp_audio_init() == ESP_OK;
    if (s_audio_ok) s_audio_ok = bsp_audio_set_format(SNAP_SAMPLE_RATE, 16, 1) == ESP_OK;
    if (s_audio_ok) bsp_audio_set_volume(0);
#if CONFIG_SNAP_BOOT_AUDIO_PROBE
    if (s_audio_ok) boot_audio_probe();
#endif
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
    s_music_requests = xQueueCreate(1, sizeof(snap_music_request_t));
    if (!s_inputs || !s_mic_status || !s_capture_requests || !s_music_requests) {
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
    ESP_LOGI(TAG, "Snap Runner rhythm ready; PCM 16 kHz duplex, fixed accent chart, no radio");
}
