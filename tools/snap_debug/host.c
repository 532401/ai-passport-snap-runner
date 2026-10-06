/* All exports are called under the Python bridge's single simulation lock. */
#include "snap_debug.h"
#include "snap_ui.h"
#include "lvgl.h"
#include <math.h>
#include <stdint.h>

static uint16_t framebuffer[240 * 320], draw_buffer[240 * 40];
static uint8_t wire_frame[240 * 320 * 2];
static snap_session_t session;
static lv_display_t *display;
static uint64_t clock_ms;
static float amplitude;
static char json[SNAP_DEBUG_LINE_SIZE];

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *pixels) {
    const uint16_t *p = (const uint16_t *)pixels;
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) framebuffer[y * 240 + x] = *p++;
    lv_display_flush_ready(d);
}

int snap_host_reset(uint32_t seed, int calibrated) {
    if (!display) {
        lv_init();
        display = lv_display_create(240, 320);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(display, flush);
        if (!snap_ui_create()) return 0;
    }
    clock_ms = 0;
    amplitude = 0;
    snap_session_init(&session, calibrated ? (snap_range_t){1000, 10000} : (snap_range_t){0}, 0, seed);
    snap_session_mic(&session, true, clock_ms);
    return 1;
}

void snap_host_step(unsigned steps) {
    for (unsigned i = 0; i < steps; i++) {
        clock_ms += 10;
        lv_tick_inc(10);
        snap_session_step(&session);
    }
    if (session.save_pending) {
        /* The simulator keeps the session in RAM; it never writes device NVS. */
        session.save_pending = false;
    }
}
int snap_host_music_time(uint32_t song_ms) {
    const uint32_t before = session.game.song_ms;
    if (session.page != SNAP_PLAYING) return 1;
    /* HTTP/browser timers are not a real-time PCM producer. Allow bounded
     * scheduling jitter while still simulating every collision step. Keep the
     * firmware's strict 250 ms fault policy unchanged. */
    if (song_ms < before || song_ms - before > 1500) {
        snap_session_key(&session, SNAP_KEY_OK, clock_ms);
        return 0;
    }
    while (session.page == SNAP_PLAYING && song_ms - session.game.song_ms > 200)
        snap_session_sync_music(&session, session.game.song_ms + 200);
    snap_session_sync_music(&session, song_ms);
    const unsigned delta = session.game.song_ms - before;
    clock_ms += delta;
    lv_tick_inc(delta);
    return 1;
}
void snap_host_key(unsigned key) {
    if (key <= SNAP_KEY_OK) snap_session_key(&session, (snap_key_t)key, clock_ms);
}
int snap_host_sound(float peak, int clipped, int noisy) {
    amplitude = peak;
    return snap_session_sound(&session, peak, clipped != 0, noisy != 0, clock_ms);
}
int snap_host_level(unsigned level) {
    if (level < 1 || level > 10) return SNAP_INPUT_RANGE;
    snap_range_t r = snap_range_valid(session.range) ? session.range : (snap_range_t){1000, 10000};
    /* Midpoints avoid float-boundary ambiguity; calibration gets actual peaks. */
    const float peak = r.min * powf(r.max / r.min, ((float)level - 0.5f) / 10.0f);
    return snap_host_sound(peak, 0, 0);
}
const char *snap_host_state(void) {
    snap_debug_record_t r = {.kind = SNAP_DEBUG_STATE, .at_ms = clock_ms, .data.session = session};
    return snap_debug_format(json, sizeof(json), &r) ? json : "{}";
}
const uint8_t *snap_host_frame(void) {
    const snap_ui_status_t status = {.mic_ok = true, .storage_ok = true, .buttons_ok = true,
        .battery = 86, .rms = amplitude, .noise = 20, .now_ms = clock_ms};
    snap_ui_update(&session, &status);
    lv_refr_now(display);
    for (unsigned i = 0; i < 240 * 320; i++) {
        wire_frame[i * 2] = framebuffer[i] & 255;
        wire_frame[i * 2 + 1] = framebuffer[i] >> 8;
    }
    amplitude *= 0.7f;
    return wire_frame;
}
