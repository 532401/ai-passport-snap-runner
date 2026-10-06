#include "snap_model.h"
#include "snap_chart.h"

#include <math.h>
#include <string.h>

static const float chart_speed = 105.0f;

bool snap_range_valid(snap_range_t range) {
    return isfinite(range.min) && isfinite(range.max) && range.min >= 100.0f &&
           range.max <= 65536.0f && range.max / range.min >= 1.6f;
}

bool snap_calibration_add(snap_calibration_t *cal, float amplitude, bool clipped) {
    if (cal->count >= 6 || clipped || !isfinite(amplitude) ||
        amplitude < 100.0f || amplitude > 65536.0f) return false;
    if (cal->count < 3) cal->loud[cal->count] = amplitude;
    else cal->soft[cal->count - 3] = amplitude;
    cal->count++;
    return true;
}

bool snap_calibration_finish(const snap_calibration_t *cal, snap_range_t *range) {
    if (cal->count != 6) return false;
    const snap_range_t candidate = {
        .max = fminf(cal->loud[0], fminf(cal->loud[1], cal->loud[2])),
        .min = (cal->soft[0] + cal->soft[1] + cal->soft[2]) / 3.0f,
    };
    if (!snap_range_valid(candidate)) return false;
    *range = candidate;
    return true;
}

unsigned snap_level(snap_range_t range, float amplitude) {
    if (!snap_range_valid(range) || !isfinite(amplitude)) return 1;
    if (amplitude <= range.min) return 1;
    if (amplitude >= range.max) return 10;
    const float ratio = logf(amplitude / range.min) / logf(range.max / range.min);
    unsigned level = 1U + (unsigned)(ratio * 10.0f);
    return level > 10 ? 10 : level;
}

float snap_jump_height(unsigned level) {
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    return 18.0f + 8.0f * (float)(level - 1);
}

/* Place the centre 280 ms after the intended takeoff, so a medium snap
 * clears either kind. Every position is derived from the song's fixed chart. */
static void generate(snap_game_t *game, unsigned slot) {
    snap_hazard_t *h = &game->hazards[slot];
    const snap_chart_event_t *event = snap_chart_get(game->next_event++);
    if (!event) {
        *h = (snap_hazard_t){.x = 1000000, .hit = true};
        game->hazard_cue_ms[slot] = 0;
        return;
    }
    *h = (snap_hazard_t){
        .x = SNAP_PLAYER_X + chart_speed *
             ((float)event->jump_ms + 280.0f - game->song_ms) / 1000.0f - event->width / 2.0f,
        .width = event->width, .height = event->height,
        .kind = (snap_hazard_kind_t)event->kind,
    };
    game->hazard_cue_ms[slot] = event->jump_ms;
}

void snap_game_start(snap_game_t *game, uint32_t seed, bool practice) {
    memset(game, 0, sizeof(*game));
    game->random = seed; /* Retained as a debug run identity; never generates a map. */
    game->speed = chart_speed;
    game->lives = 3;
    game->grounded = true;
    game->practice = practice;
    for (unsigned i = 0; i < SNAP_HAZARD_COUNT; i++) generate(game, i);
}

int snap_game_cue_in_ms(const snap_game_t *game) {
    for (unsigned i = 0; i < snap_chart_count(); i++) {
        const int delta = (int)snap_chart_get(i)->jump_ms - (int)game->song_ms;
        if (delta >= -150) return delta;
    }
    return 1000000;
}

bool snap_game_jump(snap_game_t *game, unsigned level) {
    if (!game->grounded || game->paused || game->complete || !game->lives) return false;
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    game->velocity = sqrtf(2.0f * SNAP_GRAVITY * snap_jump_height(level));
    game->grounded = false;
    game->last_level = level;
    return true;
}

static void hurt(snap_game_t *game, snap_hazard_t *hazard) {
    hazard->hit = true;
    if (game->invincible > 0.0f) return;
    game->lives--;
    game->invincible = 1.5f;
    game->hurt_flash = 0.30f;
    game->y = 0.0f;
    game->velocity = 0.0f;
    game->grounded = true;
    /* Remove only the hazard hit: a pit becomes a safe recovery platform. */
}

void snap_game_step(snap_game_t *game) {
    if (game->paused || game->complete || !game->lives) return;
    const float dt = 0.01f;
    game->song_ms += 10;
    game->speed = chart_speed;
    const float movement = game->speed * dt;
    game->distance = game->song_ms * chart_speed / 1000.0f;
    game->invincible = fmaxf(0.0f, game->invincible - dt);
    game->hurt_flash = fmaxf(0.0f, game->hurt_flash - dt);
    if (!game->grounded) {
        game->y += game->velocity * dt - 0.5f * SNAP_GRAVITY * dt * dt;
        game->velocity -= SNAP_GRAVITY * dt;
        if (game->y <= 0.0f) {
            game->y = 0.0f;
            game->velocity = 0.0f;
            game->grounded = true;
        }
    }
    if (game->practice) return;
    for (unsigned i = 0; i < SNAP_HAZARD_COUNT; i++) {
        snap_hazard_t *h = &game->hazards[i];
        h->x -= movement;
        if (h->hit) continue;
        if (h->kind == SNAP_PIT) {
            /* A foot over the edge is safe; the player's centre needs support. */
            if (game->grounded && SNAP_PLAYER_X > h->x &&
                SNAP_PLAYER_X < h->x + h->width) hurt(game, h);
        } else if (h->x < SNAP_PLAYER_X + SNAP_PLAYER_HALF_W &&
                   h->x + h->width > SNAP_PLAYER_X - SNAP_PLAYER_HALF_W &&
                   game->y < h->height) {
            hurt(game, h);
        }
        if (!game->lives) return;
    }
    for (unsigned i = 0; i < SNAP_HAZARD_COUNT; i++) {
        if (game->hazards[i].x + game->hazards[i].width >= -12.0f) continue;
        generate(game, i);
    }
    if (game->song_ms >= snap_chart_duration_ms()) game->complete = true;
}

unsigned snap_game_score(const snap_game_t *game) {
    return (unsigned)fminf(999999.0f, game->distance / 10.0f);
}

unsigned snap_loop_delay_ms(uint64_t work_ms) {
    return work_ms < 15 ? 20U - (unsigned)work_ms : 5U;
}
