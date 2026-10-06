#include "snap_ui.h"
#include "snap_chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(snap_font_16);
LV_FONT_DECLARE(snap_font_22);
#include "snap_glyphs.h"

#define INK 0x101C32
#define PAPER 0xECF4E8
#define MUTED 0x99AFBC
#define MINT 0x8BE8BF
#define CORAL 0xFF987D

static lv_obj_t *s_screen;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_info;
static lv_obj_t *s_footer;
static lv_obj_t *s_battery;
static lv_obj_t *s_brand;
static lv_obj_t *s_art;
static snap_session_t s_view;
static snap_ui_status_t s_status;
static bool s_has_view;

static void rect(lv_layer_t *layer, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    d.bg_opa = LV_OPA_COVER;
    d.radius = 0;
    const lv_area_t area = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &d, &area);
}

static void heart(lv_layer_t *layer, int x, int y, uint32_t color) {
    rect(layer, x + 2, y, 4, 2, color);
    rect(layer, x + 8, y, 4, 2, color);
    rect(layer, x, y + 2, 14, 4, color);
    rect(layer, x + 2, y + 6, 10, 2, color);
    rect(layer, x + 4, y + 8, 6, 2, color);
    rect(layer, x + 6, y + 10, 2, 2, color);
}

static void sprite(lv_layer_t *layer, int x, int foot, unsigned frame, bool airborne) {
    const int y = foot - 24;
    rect(layer, x - 12, y + 12, 6, 7, 0x4CA992);
    rect(layer, x - 8, y, 5, 8, MINT);
    rect(layer, x + 4, y - 3, 5, 10, MINT);
    rect(layer, x - 8, y + 6, 20, 14, MINT);
    rect(layer, x + 2, y + 9, 3, 4, INK);
    rect(layer, x + 9, y + 9, 3, 4, INK);
    rect(layer, x + 3, y + 15, 7, 2, 0x3E887F);
    rect(layer, x - 8, y + 20, 17, 3, CORAL);
    rect(layer, x - 15, y + 20, 7, 3, CORAL);
    const int stride = airborne ? 0 : ((frame & 1) ? 3 : -3);
    rect(layer, x - 6 - stride, y + 23, 6, 3, PAPER);
    rect(layer, x + 4 + stride, y + 23, 6, 3, PAPER);
}

static bool is_world(snap_page_t page) {
    return page == SNAP_HOME || page == SNAP_PRACTICE || page == SNAP_PLAYING ||
           page == SNAP_PAUSED || page == SNAP_OVER;
}

static void draw(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(s_art, &bounds);
    const int ox = bounds.x1, oy = bounds.y1;
    const snap_page_t page = s_view.page;
    const bool active = page == SNAP_PLAYING || page == SNAP_PRACTICE;
    const bool run = active || page == SNAP_PAUSED || page == SNAP_OVER;
    const snap_game_t *game = &s_view.game;
    if (is_world(page)) {
        const int ground = 210;
        const int top = run ? 103 : 132;
        const int drift = (int)(game->distance * 0.14f) % 240;
        for (unsigned i = 0; i < 9; i++) {
            const int x = ((int)(i * 37 + 29) - drift + 480) % 240;
            rect(layer, ox + x, oy + top + (int)(i * 23 % 66), 2, 2, 0x536580);
        }
        rect(layer, ox + 185, oy + top + 4, 20, 20, 0xF2CF84);
        rect(layer, ox + 182, oy + top + 1, 13, 15, INK);
        for (int i = 0; i < 5; i++) {
            const int x = i * 60 - (int)(game->distance * 0.25f) % 60;
            rect(layer, ox + x, oy + ground - 28 - i % 2 * 10, 38, 30 + i % 2 * 10, 0x22324A);
            rect(layer, ox + x + 8, oy + ground - 40 - i % 2 * 10, 22, 18, 0x22324A);
        }
        rect(layer, ox, oy + ground, 240, 4, MINT);
        rect(layer, ox, oy + ground + 4, 240, 14, 0x305050);
        for (int i = 0; i < 13; i++) {
            const int x = i * 20 - (int)game->distance % 20;
            rect(layer, ox + x, oy + ground + 9, 7, 2, 0x466B62);
        }
        if (run && page != SNAP_PRACTICE) {
            for (unsigned i = 0; i < SNAP_HAZARD_COUNT; i++) {
                const snap_hazard_t *h = &game->hazards[i];
                if (h->x > 245 || h->x + h->width < -5 || h->hit) continue;
                const int x = (int)h->x, w = (int)h->width;
                if (h->kind == SNAP_PIT) {
                    rect(layer, ox + x, oy + ground, w, 18, INK);
                    rect(layer, ox + x - 3, oy + ground, 3, 4, CORAL);
                    rect(layer, ox + x + w, oy + ground, 3, 4, CORAL);
                } else {
                    const int height = (int)h->height;
                    rect(layer, ox + x, oy + ground - height, w, height, 0xB86B78);
                    rect(layer, ox + x + 2, oy + ground - height, w - 4, 4, CORAL);
                    rect(layer, ox + x + 3, oy + ground - height + 7, 3, 5, 0xE88D8C);
                }
            }
        }
        const bool blink = active && game->invincible > 0 && (s_status.now_ms / 90 % 2);
        if (!blink || !run) {
            sprite(layer, ox + (run ? (int)SNAP_PLAYER_X : 70),
                   oy + ground - (run ? (int)game->y : 0),
                   (unsigned)(game->distance / 9), !game->grounded && run);
        }
        if (page == SNAP_PLAYING || page == SNAP_PAUSED) {
            const int progress = (int)(120ULL * game->song_ms / snap_chart_duration_ms());
            rect(layer, ox + 100, oy + 86, 120, 4, 0x334457);
            rect(layer, ox + 100, oy + 86, progress, 4, MINT);
            const int cue = snap_game_cue_in_ms(game);
            if (cue < 800 && cue >= -150) {
                const int spread = cue > 0 ? 6 + cue / 40 : 6;
                const uint32_t color = cue > 100 ? PAPER : CORAL;
                rect(layer, ox + 58 - spread, oy + ground - 3, 3, 8, color);
                rect(layer, ox + 58 + spread, oy + ground - 3, 3, 8, color);
            }
        }
        if (active) {
            for (unsigned i = 0; i < 3; i++)
                heart(layer, ox + 20 + (int)i * 22, oy + 82,
                      i < game->lives ? CORAL : 0x334457);
        }
    } else {
        const unsigned count = s_view.calibration.count;
        unsigned filled = count > 3 ? count - 3 : count;
        if (page == SNAP_CAL_SOFT) filled = count - 3;
        for (unsigned i = 0; i < 3; i++) {
            int x = 37 + (int)i * 60;
            rect(layer, ox + x, oy + 174, 46, 32, i < filled ? MINT : 0x263B51);
            if (i < filled) {
                rect(layer, ox + x + 13, oy + 190, 6, 4, INK);
                rect(layer, ox + x + 19, oy + 183, 5, 11, INK);
                rect(layer, ox + x + 24, oy + 180, 5, 6, INK);
            }
        }
    }
    if (page != SNAP_HOME && page != SNAP_CAL_FAILED && page != SNAP_OVER && page != SNAP_PAUSED) {
        unsigned lit = s_view.level;
        if (page == SNAP_CAL_LOUD || page == SNAP_CAL_SOFT || page == SNAP_CAL_SOFT_WAIT)
            lit = (unsigned)fminf(10, s_status.rms / 250.0f);
        for (unsigned i = 0; i < 10; i++) {
            const int height = 5 + (int)i;
            rect(layer, ox + 20 + (int)i * 20, oy + 265 - height, 15, height,
                 i < lit ? MINT : 0x2D4054);
        }
    }
}

static lv_obj_t *label(int x, int y, int width, const lv_font_t *font, uint32_t color) {
    lv_obj_t *obj = lv_label_create(s_screen);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(obj, 3, 0);
    lv_label_set_text(obj, "");
    return obj;
}

bool snap_ui_check_fonts(void) {
    const lv_font_t *fonts[] = {&snap_font_16, &snap_font_22};
    for (unsigned f = 0; f < 2; f++) {
        for (unsigned i = 0; i < sizeof(snap_glyphs) / sizeof(snap_glyphs[0]); i++) {
            lv_font_glyph_dsc_t glyph;
            if (!lv_font_get_glyph_dsc(fonts[f], &glyph, snap_glyphs[i], 0) ||
                glyph.is_placeholder) return false;
        }
    }
    return true;
}

bool snap_ui_create(void) {
    if (!snap_ui_check_fonts()) return false;
    s_has_view = false;
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, 240, 320);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(INK), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    s_art = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_art);
    lv_obj_set_size(s_art, 240, 320);
    lv_obj_remove_flag(s_art, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_art, draw, LV_EVENT_DRAW_MAIN, NULL);
    s_brand = label(20, 16, 150, &snap_font_16, MINT);
    lv_label_set_text(s_brand, "SNAP / 响指跃行");
    s_battery = label(180, 16, 42, &snap_font_16, MUTED);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    s_title = label(20, 46, 202, &snap_font_22, PAPER);
    s_body = label(20, 82, 202, &snap_font_16, MUTED);
    s_info = label(20, 232, 202, &snap_font_16, MINT);
    s_footer = label(20, 270, 202, &snap_font_16, PAPER);
    lv_screen_load(s_screen);
    return true;
}

static void set(lv_obj_t *obj, const char *text) {
    if (strcmp(lv_label_get_text(obj), text) != 0) lv_label_set_text(obj, text);
}

void snap_ui_update(const snap_session_t *session, const snap_ui_status_t *status) {
    const bool changed_page = !s_has_view || s_view.page != session->page;
    const bool world_changed = session->page == SNAP_PLAYING || session->page == SNAP_PRACTICE;
    const bool meter_changed = (unsigned)fminf(10, s_status.rms / 250.0f) !=
                               (unsigned)fminf(10, status->rms / 250.0f);
    const bool samples_changed = s_view.calibration.count != session->calibration.count;
    s_has_view = true;
    s_view = *session;
    s_status = *status;
    char title[80], body[160], info[112], footer[112], battery[16];
    title[0] = body[0] = info[0] = footer[0] = 0;
    const unsigned score = snap_game_score(&session->game);
    switch (session->page) {
    case SNAP_HOME:
        snprintf(title, sizeof(title), "山妖洞穴");
        snprintf(body, sizeof(body), "跟随重拍，响指起跳\n最高纪录 %u 米", session->high_score);
        snprintf(info, sizeof(info), "%s", snap_range_valid(session->range) ? "校准已就绪" : "先认识你的响指");
        snprintf(footer, sizeof(footer), "%s", snap_range_valid(session->range) ?
                 "确定 开跑  /  上键 练习\n下键 重新校准" : "确定 开始校准");
        break;
    case SNAP_CAL_LOUD:
        snprintf(title, sizeof(title), "01 / 最响的响指");
        snprintf(body, sizeof(body), "保持距离，尽力打三下\n每次间隔一秒\n已记录 %u / 3", session->calibration.count);
        snprintf(footer, sizeof(footer), "上键 返回");
        break;
    case SNAP_CAL_SOFT_WAIT:
        snprintf(title, sizeof(title), "接下来，轻一点");
        snprintf(body, sizeof(body), "强响指已记录\n现在准备三下最轻响指\n保持相同距离");
        snprintf(info, sizeof(info), "确定后开始采集");
        snprintf(footer, sizeof(footer), "确定 继续  /  上键 返回");
        break;
    case SNAP_CAL_SOFT:
        snprintf(title, sizeof(title), "02 / 最轻的响指");
        snprintf(body, sizeof(body), "轻轻打三下，保持清脆\n每次间隔一秒\n已记录 %u / 3", session->calibration.count - 3);
        snprintf(footer, sizeof(footer), "上键 返回");
        break;
    case SNAP_CAL_FAILED:
        snprintf(title, sizeof(title), "强弱太接近了");
        snprintf(body, sizeof(body), "需要更明显的力度差异\n请保持安静和相同距离\n已有校准不会被覆盖");
        snprintf(info, sizeof(info), "再试一次就好");
        snprintf(footer, sizeof(footer), "确定 重试  /  上键 返回");
        break;
    case SNAP_PRACTICE:
        snprintf(title, sizeof(title), "练习 / 放心试跳");
        snprintf(info, sizeof(info), "力度 %02u / 10", session->level);
        snprintf(footer, sizeof(footer), "确定 开跑  /  上键 返回\n下键 重新校准");
        break;
    case SNAP_PLAYING:
        snprintf(title, sizeof(title), "%06u 米", score);
        snprintf(info, sizeof(info), "力度 %02u  音量 %u", session->level, session->volume);
        snprintf(footer, sizeof(footer), "重拍起跳  确定暂停\n上 / 下键 调音量");
        break;
    case SNAP_PAUSED:
        snprintf(title, sizeof(title), "暂停 / %u 米", score);
        snprintf(body, sizeof(body), "准备好了再继续");
        snprintf(info, sizeof(info), status->music_fault ? "音频中断，确定重试" : "音乐和地图已暂停");
        snprintf(footer, sizeof(footer), "确定 继续  /  下键 返回");
        break;
    case SNAP_OVER:
        snprintf(title, sizeof(title), session->game.complete ? "通关！ %u 米" : "跑了 %u 米", score);
        snprintf(body, sizeof(body), "最高纪录 %u 米", session->high_score);
        snprintf(info, sizeof(info), "%s", session->game.complete ? "整首完成，休息一下" : "下一次，再远一点");
        snprintf(footer, sizeof(footer), "确定 重来  /  上键 返回\n下键 重新校准");
        break;
    }
    if (session->page == SNAP_CAL_LOUD || session->page == SNAP_CAL_SOFT) {
        const char *hint = "请打响指";
        if (status->noise > 450) hint = "环境较吵，请安静后重试";
        else if (status->now_ms < session->feedback_until_ms) {
            if (session->hint == SNAP_HINT_CLIPPED) hint = "声音削顶，请离远一点";
            else if (session->hint == SNAP_HINT_NOISY) hint = "环境较吵，请安静后重试";
            else if (session->hint == SNAP_HINT_CAPTURED) hint = "记录成功";
        } else if (status->now_ms < session->accept_after_ms) hint = "请稍等，保持安静";
        snprintf(info, sizeof(info), "%s", hint);
    }
    if (!status->mic_ok) snprintf(info, sizeof(info), "麦克风异常，请重启");
    else if (status->mic_warming) snprintf(info, sizeof(info), "正在听环境，请保持安静");
    else if (!status->storage_ok) snprintf(info, sizeof(info), "保存失败，本次仍可游玩");
    if (!status->buttons_ok) snprintf(footer, sizeof(footer), "按键异常，请重启");
    if (status->battery < 0) snprintf(battery, sizeof(battery), "--%%");
    else snprintf(battery, sizeof(battery), "%d%%", status->battery);
    set(s_title, title);
    set(s_body, body);
    set(s_info, info);
    set(s_footer, footer);
    set(s_battery, battery);
    if (changed_page) lv_obj_invalidate(s_art);
    else if (world_changed) {
        const lv_area_t moving_area = {0, 76, 239, 266};
        lv_obj_invalidate_area(s_art, &moving_area);
    } else if (!is_world(session->page)) {
        if (samples_changed) {
            const lv_area_t samples_area = {20, 174, 219, 205};
            lv_obj_invalidate_area(s_art, &samples_area);
        }
        if (meter_changed) {
            const lv_area_t meter_area = {20, 250, 219, 265};
            lv_obj_invalidate_area(s_art, &meter_area);
        }
    }
}
