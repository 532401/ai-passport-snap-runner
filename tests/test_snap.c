#include "snap_detector.h"
#include "snap_model.h"
#include "snap_session.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void calibration_test(void) {
    snap_calibration_t cal = {0};
    snap_range_t range = {0};
    assert(!snap_calibration_add(&cal, 12000, true));
    assert(!snap_calibration_add(&cal, NAN, false));
    const float samples[] = {12000, 9000, 11000, 1000, 2000, 3000};
    for (unsigned i = 0; i < 6; i++) {
        assert(!snap_calibration_finish(&cal, &range));
        assert(snap_calibration_add(&cal, samples[i], false));
    }
    assert(!snap_calibration_add(&cal, 500, false));
    assert(snap_calibration_finish(&cal, &range));
    assert(range.max == 9000 && range.min == 2000);
    assert(snap_level(range, 1) == 1);
    assert(snap_level(range, 9000) == 10);
    assert(snap_level(range, 50000) == 10);
    for (unsigned i = 0; i < 10; i++) {
        const float mid = range.min * powf(range.max / range.min, (i + 0.5f) / 10);
        assert(snap_level(range, mid) == i + 1);
    }
    assert(!snap_range_valid((snap_range_t){2000, 2500}));
    assert(!snap_range_valid((snap_range_t){2000, NAN}));
    assert(!snap_range_valid((snap_range_t){0, 9000}));
    cal.soft[0] = cal.soft[1] = cal.soft[2] = 10000;
    assert(!snap_calibration_finish(&cal, &range));
}

static void jump_test(void) {
    float previous = 0;
    for (unsigned level = 1; level <= 10; level++) {
        snap_game_t game;
        snap_game_start(&game, 1, true);
        assert(snap_game_jump(&game, level));
        assert(!snap_game_jump(&game, level));
        float apex = 0;
        unsigned steps = 0;
        while (!game.grounded && steps++ < 150) {
            snap_game_step(&game);
            apex = fmaxf(apex, game.y);
        }
        assert(steps < 150 && game.grounded);
        assert(apex > previous && fabsf(apex - snap_jump_height(level)) < 0.03f);
        previous = apex;
        assert(game.lives == 3);
        assert(snap_game_jump(&game, 1));
    }
}

static void collision_test(void) {
    snap_game_t game;
    snap_game_start(&game, 13, false);
    game.hazards[0] = (snap_hazard_t){SNAP_PLAYER_X, 20, 30, SNAP_ROCK, false};
    snap_game_step(&game);
    assert(game.lives == 2 && game.invincible > 0);
    for (unsigned i = 0; i < 10; i++) snap_game_step(&game);
    assert(game.lives == 2);
    game.hazards[1] = (snap_hazard_t){SNAP_PLAYER_X, 40, 0, SNAP_PIT, false};
    snap_game_step(&game);
    assert(game.lives == 2 && game.grounded);
    game.invincible = 0;
    game.hazards[2] = (snap_hazard_t){SNAP_PLAYER_X, 40, 0, SNAP_PIT, false};
    snap_game_step(&game);
    assert(game.lives == 1 && game.hazards[2].hit);
    game.invincible = 0;
    game.hazards[3] = (snap_hazard_t){SNAP_PLAYER_X, 20, 30, SNAP_ROCK, false};
    snap_game_step(&game);
    assert(game.lives == 0);
    const float end = game.distance;
    for (unsigned i = 0; i < 30; i++) snap_game_step(&game);
    assert(game.distance == end && !snap_game_jump(&game, 10));
    snap_game_start(&game, 2, false);
    game.paused = true;
    const snap_game_t paused = game;
    snap_game_step(&game);
    assert(memcmp(&game, &paused, sizeof(game)) == 0);
}

/* Force one off-screen slot to recycle; measure the public model's new gap. */
static float generated_gap(unsigned seed, float distance) {
    snap_game_t game;
    snap_game_start(&game, seed, false);
    game.distance = distance;
    for (unsigned i = 0; i < SNAP_HAZARD_COUNT; i++)
        game.hazards[i] = (snap_hazard_t){300 + 100 * i, 20, 12, SNAP_ROCK, false};
    game.hazards[0].x = -100;
    snap_game_step(&game);
    const snap_hazard_t *previous = &game.hazards[SNAP_HAZARD_COUNT - 1];
    return game.hazards[0].x - previous->x - previous->width;
}

static void density_test(void) {
    const float safe_gap = 125 * (2 * sqrtf(2 * 90.0f / 800) + 0.35f);
    for (unsigned seed = 1; seed <= 120; seed++) {
        float previous = generated_gap(seed, 0);
        const float initial = previous;
        for (unsigned stage = 1; stage <= 4; stage++) {
            const float gap = generated_gap(seed, stage * 2500.0f);
            assert(gap < previous); /* Same random choice, increasing density. */
            assert(gap >= safe_gap - 0.01f);
            previous = gap;
        }
        assert(initial - previous > 60);
        assert(previous <= safe_gap + 25);
        assert(fabsf(generated_gap(seed, 60000) - previous) < 0.01f);
    }
}

static bool jump_near_hazard(snap_game_t *game, float timing_offset) {
    const snap_hazard_t *next = NULL;
    for (unsigned j = 0; j < SNAP_HAZARD_COUNT; j++) {
        const snap_hazard_t *h = &game->hazards[j];
        if (h->x + h->width < SNAP_PLAYER_X || h->hit) continue;
        if (!next || h->x < next->x) next = h;
    }
    if (!next || !game->grounded) return false;
    const float half_flight = sqrtf(2.0f * snap_jump_height(10) / SNAP_GRAVITY);
    const float to_centre = next->x + next->width / 2 - SNAP_PLAYER_X;
    if (to_centre > game->speed * (half_flight + timing_offset)) return false;
    assert(snap_game_jump(game, 10));
    return true;
}

/* Exercise all ordered kind/width extremes at the absolute spacing floor,
 * including maximum-height rocks and opposite timing errors on each jump. */
static void minimum_gap_test(void) {
    const float gap = 125 * (2 * sqrtf(2 * 90.0f / 800) + 0.35f);
    for (unsigned kinds = 0; kinds < 4; kinds++) {
        for (unsigned sizes = 0; sizes < 4; sizes++) {
            for (unsigned speed = 0; speed < 3; speed++) {
                for (int error = -1; error <= 1; error++) {
                    snap_game_t game;
                    snap_game_start(&game, 1, false);
                    const float starts[] = {0, 24500, 60000};
                    game.distance = starts[speed];
                    float x = 330;
                    for (unsigned i = 0; i < SNAP_HAZARD_COUNT; i++) {
                        const bool pit = ((kinds >> i) & 1) != 0;
                        const bool wide = ((sizes >> i) & 1) != 0;
                        const float width = pit ? (wide ? 48 : 22) : (wide ? 20 : 12);
                        game.hazards[i] = (snap_hazard_t){x, width, 42,
                            pit ? SNAP_PIT : SNAP_ROCK, false};
                        x += width + (i == 0 ? gap : 3000);
                    }
                    unsigned jumps = 0, grounded_steps = 0, frame = 0;
                    for (; frame < 1200; frame++) {
                        if (jumps == 1 && game.grounded) grounded_steps++;
                        if (jump_near_hazard(&game, (jumps ? -error : error) * 0.08f))
                            jumps++;
                        snap_game_step(&game);
                        assert(game.lives == 3);
                        if (jumps == 2 && game.grounded) break;
                    }
                    assert(frame < 1200 && jumps == 2);
                    assert(grounded_steps >= 18); /* At least 180 ms on ground. */
                    assert(game.hazards[1].x + game.hazards[1].width <
                           SNAP_PLAYER_X - SNAP_PLAYER_HALF_W);
                }
            }
        }
    }
}

/* Jump near the clearance-window centre with +/-80 ms timing variation.
 * Exercise generated streams through the density ramp and at maximum speed. */
static void map_test(void) {
    for (unsigned seed = 1; seed <= 120; seed++) {
        snap_game_t game;
        snap_game_start(&game, seed, false);
        const float starts[] = {0, 4000, 9000, 60000};
        game.distance = starts[seed % 4];
        unsigned jumps = 0;
        for (unsigned frame = 0; frame < 12000; frame++) {
            const float timing_offset = ((int)(jumps % 3) - 1) * 0.08f;
            if (jump_near_hazard(&game, timing_offset)) jumps++;
            snap_game_step(&game);
            assert(game.lives == 3);
        }
        assert(jumps > 30);
    }
    snap_game_t a, b;
    snap_game_start(&a, 42, false);
    snap_game_start(&b, 42, false);
    assert(memcmp(&a, &b, sizeof(a)) == 0);
}

static unsigned silence(snap_detector_t *d, unsigned frames) {
    const int16_t pcm[SNAP_FRAME_SAMPLES] = {0};
    snap_sound_t sound;
    unsigned events = 0;
    while (frames--) events += snap_detector_process(d, pcm, &sound);
    return events;
}

static snap_sound_t pulse(snap_detector_t *d, int amplitude) {
    int16_t pcm[SNAP_FRAME_SAMPLES];
    for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++) {
        const float envelope = expf(-(float)i / 18);
        pcm[i] = (int16_t)(amplitude * envelope * (i % 2 ? -1 : 1));
    }
    snap_sound_t sound = {0};
    unsigned events = snap_detector_process(d, pcm, &sound);
    memset(pcm, 0, sizeof(pcm));
    for (unsigned i = 0; i < 7; i++) events += snap_detector_process(d, pcm, &sound);
    assert(events == 1);
    return sound;
}

static void detector_test(void) {
    snap_detector_t d;
    snap_detector_init(&d);
    assert(!silence(&d, 200));
    snap_sound_t soft = pulse(&d, 1800);
    assert(!soft.clipped && soft.amplitude > 1000);
    assert(!silence(&d, 60));
    snap_sound_t loud = pulse(&d, 12000);
    assert(loud.amplitude > soft.amplitude * 5);
    assert(!silence(&d, 60));
    assert(pulse(&d, 32767).clipped);
    assert(!silence(&d, 60));
    int16_t pcm[SNAP_FRAME_SAMPLES];
    snap_sound_t sound;
    unsigned events = 0;
    for (unsigned frame = 0; frame < 1000; frame++) {
        for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++) {
            pcm[i] = (int16_t)(4000 * sinf((frame * SNAP_FRAME_SAMPLES + i) * 0.1f));
        }
        events += snap_detector_process(&d, pcm, &sound);
    }
    assert(events == 0); /* Sustained voiced tone is not a transient. */
    snap_detector_init(&d);
    for (unsigned frame = 0; frame < 400; frame++) {
        for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++) pcm[i] = 800;
        assert(!snap_detector_process(&d, pcm, &sound));
    }
    assert(d.rms < 1); /* DC rejection. */
}

static void session_test(void) {
    snap_session_t s;
    snap_session_init(&s, (snap_range_t){0}, 123, 1);
    snap_session_key(&s, SNAP_KEY_OK, 0);
    assert(s.page == SNAP_HOME); /* No mic: cannot silently start. */
    snap_session_mic(&s, true, 0);
    snap_session_key(&s, SNAP_KEY_OK, 0);
    assert(s.page == SNAP_CAL_LOUD);
    snap_session_key(&s, SNAP_KEY_DOWN, 100);
    assert(s.accept_after_ms == 1000);
    snap_session_sound(&s, 10000, false, false, 200);
    assert(s.calibration.count == 0);
    snap_session_sound(&s, 10000, true, false, 1000);
    assert(s.calibration.count == 0 && s.hint == SNAP_HINT_CLIPPED);
    snap_session_sound(&s, 10000, false, false, 1000);
    snap_session_key(&s, SNAP_KEY_DOWN, 1100);
    snap_session_sound(&s, 10000, false, false, 1450);
    assert(s.calibration.count == 1 && s.accept_after_ms == 1700);
    s.calibration.count = 0;
    s.accept_after_ms = 1500;
    for (unsigned i = 0; i < 3; i++)
        snap_session_sound(&s, 10000, false, false, 1500 + 1000 * i);
    assert(s.page == SNAP_CAL_SOFT_WAIT && s.calibration.count == 3);
    snap_session_sound(&s, 1000, false, false, 4500);
    assert(s.calibration.count == 3);
    snap_session_key(&s, SNAP_KEY_OK, 5000);
    for (unsigned i = 0; i < 3; i++)
        snap_session_sound(&s, 1000, false, false, 6000 + 1000 * i);
    assert(s.page == SNAP_PRACTICE && s.save_pending && snap_range_valid(s.range));
    snap_session_key(&s, SNAP_KEY_OK, 10000);
    assert(s.page == SNAP_PLAYING);
    snap_session_sound(&s, 8000, false, false, 10500);
    assert(!s.game.grounded);
    const float velocity = s.game.velocity;
    snap_session_sound(&s, 10000, false, false, 10800);
    assert(s.game.velocity == velocity && s.hint == SNAP_HINT_AIR);
    snap_session_mic(&s, false, 11000);
    assert(s.page == SNAP_PAUSED);
    snap_session_key(&s, SNAP_KEY_OK, 12000);
    assert(s.page == SNAP_PAUSED);
    snap_session_mic(&s, true, 13000);
    snap_session_key(&s, SNAP_KEY_OK, 14000);
    assert(s.page == SNAP_PLAYING);
    s.game.distance = 2500;
    s.game.lives = 1;
    s.game.grounded = true;
    s.game.y = 0;
    s.game.hazards[0] = (snap_hazard_t){SNAP_PLAYER_X, 20, 30, SNAP_ROCK, false};
    snap_session_step(&s);
    assert(s.page == SNAP_OVER && s.high_score == 250 && s.save_pending);
    snap_session_key(&s, SNAP_KEY_DOWN, 15000);
    assert(s.page == SNAP_CAL_LOUD && snap_range_valid(s.range));
    snap_session_key(&s, SNAP_KEY_UP, 16000);
    assert(s.page == SNAP_HOME && snap_range_valid(s.range));
}

int main(void) {
    assert(snap_loop_delay_ms(0) == 20);
    assert(snap_loop_delay_ms(7) == 13);
    assert(snap_loop_delay_ms(20) == 5);
    assert(snap_loop_delay_ms(300) == 5);
    assert(snap_loop_delay_ms(UINT64_MAX) == 5);
    calibration_test();
    jump_test();
    collision_test();
    density_test();
    minimum_gap_test();
    map_test();
    detector_test();
    session_test();
    puts("Snap: calibration, detector, physics, 120 map seeds, lifecycle PASS");
    return 0;
}
