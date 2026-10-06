#include "snap_ui.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

static uint16_t framebuffer[240 * 320];
static uint16_t draw_buffer[240 * 40];
static unsigned flush_count;
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    flush_count++;
    uint16_t *p = (uint16_t *)pixels;
    for (int y = area->y1; y <= area->y2; y++)
        for (int x = area->x1; x <= area->x2; x++) framebuffer[y * 240 + x] = *p++;
    lv_display_flush_ready(display);
}
static void save(const char *directory, const char *name) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n240 320\n255\n");
    for (int y = 0; y < 320; y++) {
        for (int x = 0; x < 240; x++) {
            unsigned v = framebuffer[y * 240 + x];
            int dx = x < 30 ? 30 - x : x >= 210 ? x - 209 : 0;
            int dy = y < 30 ? 30 - y : y >= 290 ? y - 289 : 0;
            if (dx && dy && dx * dx + dy * dy > 30 * 30) v = 0;
            fputc(((v >> 11) & 31) * 255 / 31, f);
            fputc(((v >> 5) & 63) * 255 / 63, f);
            fputc((v & 31) * 255 / 31, f);
        }
    }
    fclose(f);
}
static void validate_layout(lv_obj_t *object) {
    if (lv_obj_check_type(object, &lv_label_class)) {
        const char *text = lv_label_get_text(object);
        if (*text) {
            lv_area_t a;
            lv_obj_get_coords(object, &a);
            if (a.y2 >= 314 || a.x2 > 230) {
                fprintf(stderr, "Layout overflow: %s (%d,%d)-(%d,%d)\n", text,
                        (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2);
                assert(0);
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++)
        validate_layout(lv_obj_get_child(object, i));
}
int main(int argc, char **argv) {
    assert(argc == 2);
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    assert(snap_ui_create());
    snap_session_t session;
    snap_session_init(&session, (snap_range_t){1000, 10000}, 728, 12);
    snap_ui_status_t status = {.mic_ok = true, .storage_ok = true, .buttons_ok = true,
                               .battery = 86, .rms = 800, .noise = 40, .now_ms = 2000};
    snap_ui_update(&session, &status);
    lv_refr_now(display);
    const unsigned idle_flushes = flush_count;
    for (unsigned i = 0; i < 100; i++) {
        status.now_ms += 20;
        snap_ui_update(&session, &status);
        lv_refr_now(display);
    }
    assert(flush_count == idle_flushes && "Static home must not repeatedly flush the LCD");
    const char *names[] = {"home", "loud", "soft-ready", "soft", "invalid", "practice", "run", "pause", "over"};
    for (unsigned page = SNAP_HOME; page <= SNAP_OVER; page++) {
        session.page = page;
        session.calibration.count = page == SNAP_CAL_SOFT ? 4 : 2;
        session.level = 7;
        session.game.distance = 3860;
        session.game.hazards[0] = (snap_hazard_t){125, 19, 36, SNAP_ROCK, false};
        session.game.hazards[1] = (snap_hazard_t){193, 40, 0, SNAP_PIT, false};
        session.game.y = page == SNAP_PLAYING ? 48 : 0;
        session.game.grounded = page != SNAP_PLAYING;
        snap_ui_update(&session, &status);
        lv_refr_now(display);
        validate_layout(lv_screen_active());
        save(argv[1], names[page]);
    }
    session.page = SNAP_CAL_LOUD;
    session.hint = SNAP_HINT_CLIPPED;
    session.feedback_until_ms = status.now_ms + 1000;
    snap_ui_update(&session, &status);
    lv_refr_now(display);
    save(argv[1], "clipped");
    status.mic_ok = false;
    session.page = SNAP_PAUSED;
    snap_ui_update(&session, &status);
    lv_refr_now(display);
    save(argv[1], "mic-error");
    status.mic_ok = true;
    status.storage_ok = false;
    status.buttons_ok = false;
    status.battery = -1;
    snap_ui_update(&session, &status);
    lv_refr_now(display);
    save(argv[1], "errors");
    status.storage_ok = status.buttons_ok = true;
    status.music_fault = true;
    snap_ui_update(&session, &status);
    lv_refr_now(display);
    validate_layout(lv_screen_active());
    save(argv[1], "music-error");
    status.music_fault = false;
    session.page = SNAP_OVER;
    session.game.complete = true;
    snap_ui_update(&session, &status);
    lv_refr_now(display);
    validate_layout(lv_screen_active());
    save(argv[1], "complete");
    session.page = SNAP_PLAYING;
    snap_game_start(&session.game, 5, true);
    for (unsigned i = 0; i < 300; i++) {
        if (i % 60 == 0) snap_game_jump(&session.game, 10);
        snap_game_step(&session.game);
        snap_game_step(&session.game);
        status.now_ms += 20;
        lv_tick_inc(20);
        snap_ui_update(&session, &status);
        lv_refr_now(display);
    }
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    printf("Both fonts: all glyphs PASS; all pages rendered; 300 frames; LVGL used=%zu free=%zu\n",
           memory.total_size - memory.free_size, memory.free_size);
    return 0;
}
