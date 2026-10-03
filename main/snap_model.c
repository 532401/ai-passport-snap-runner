#include "snap_model.h"

#include <math.h>
#include <string.h>

static const float max_run_speed = 125.0f;

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

static uint32_t next_random(snap_game_t *game) {
    uint32_t x = game->random;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    game->random = x;
    return x;
}

static void generate(snap_game_t *game, snap_hazard_t *hazard, float x) {
    const uint32_t r = next_random(game);
    hazard->x = x;
    hazard->kind = (r % 3 == 0) ? SNAP_PIT : SNAP_ROCK;
    hazard->width = hazard->kind == SNAP_PIT ? 22.0f + (float)(r % 27) :
                                             12.0f + (float)(r % 9);
    hazard->height = 12.0f + (float)((r >> 8) % 31);
    hazard->hit = false;
}

static float next_gap(snap_game_t *game, float previous_end) {
    /* Use the gap's world position, so pre-generated hazards follow the same
     * 1000-m ramp as recycled ones. Never move an already visible hazard. */
    const float distance = game->distance + previous_end - SNAP_PLAYER_X;
    const float progress = fminf(1.0f, fmaxf(0.0f, distance / 10000.0f));
    /* Edge-to-edge safety floor: a full level-10 flight plus 350 ms at the
     * maximum speed, exceeding the microphone's 180 ms refractory period.
     * Shorter jumps cannot require more landing time. */
    const float safe_gap = max_run_speed *
        (2.0f * sqrtf(2.0f * snap_jump_height(10) / SNAP_GRAVITY) + 0.35f);
    const float random_gap = (float)(next_random(game) % 75);
    return safe_gap + 75.0f * (1.0f - progress) +
           random_gap * (1.0f - (2.0f / 3.0f) * progress);
}

void snap_game_start(snap_game_t *game, uint32_t seed, bool practice) {
    memset(game, 0, sizeof(*game));
    game->random = seed ? seed : 0x534e4150U;
    game->speed = 90.0f;
    game->lives = 3;
    game->grounded = true;
    game->practice = practice;
    float x = 330.0f;
    for (unsigned i = 0; i < SNAP_HAZARD_COUNT; i++) {
        generate(game, &game->hazards[i], x);
        const float end = x + game->hazards[i].width;
        x = end + next_gap(game, end);
    }
}

bool snap_game_jump(snap_game_t *game, unsigned level) {
    if (!game->grounded || game->paused || !game->lives) return false;
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
    if (game->paused || !game->lives) return;
    const float dt = 0.01f;
    game->speed = fminf(max_run_speed, 90.0f + game->distance / 1400.0f);
    const float movement = game->speed * dt;
    game->distance += movement;
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
        float end = 240.0f;
        for (unsigned j = 0; j < SNAP_HAZARD_COUNT; j++) {
            end = fmaxf(end, game->hazards[j].x + game->hazards[j].width);
        }
        generate(game, &game->hazards[i], end + next_gap(game, end));
    }
}

unsigned snap_game_score(const snap_game_t *game) {
    return (unsigned)fminf(999999.0f, game->distance / 10.0f);
}

unsigned snap_loop_delay_ms(uint64_t work_ms) {
    return work_ms < 15 ? 20U - (unsigned)work_ms : 5U;
}
