#include "snap_detector.h"
#include "snap_model.h"
#include "snap_session.h"
#include "snap_chart.h"
#include "snap_music.h"

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

/* Every chart event must be survivable at the advertised medium force,
 * even with opposite early/late errors. Also prove that maximum jumps land
 * before the next cue, and never use invincibility to hide an unsafe chart. */
static void map_test(void) {
    assert(snap_chart_count() >= 30);
    for (unsigned i = 1; i < snap_chart_count(); i++)
        assert(snap_chart_get(i)->jump_ms - snap_chart_get(i - 1)->jump_ms >= 2500);
    for (unsigned level = 3; level <= 10; level++) {
        for (int error = -80; error <= 80; error += 80) {
            snap_game_t game;
            snap_game_start(&game, 42, false);
            unsigned next = 0;
            for (unsigned frame = 0; frame < 16000 && !game.complete; frame++) {
                if (next < snap_chart_count()) {
                    int target = (int)snap_chart_get(next)->jump_ms +
                                 (next % 2 ? -error : error);
                    if ((int)game.song_ms >= target) {
                        assert(game.grounded);
                        assert(snap_game_jump(&game, level));
                        next++;
                    }
                }
                snap_game_step(&game);
                assert(game.lives == 3 && game.invincible == 0);
            }
            assert(game.complete && next == snap_chart_count());
            const snap_game_t complete = game;
            snap_game_step(&game);
            assert(memcmp(&game, &complete, sizeof(game)) == 0);
        }
    }
    snap_game_t a, b;
    snap_game_start(&a, 42, false);
    snap_game_start(&b, 987, false);
    assert(memcmp(a.hazards, b.hazards, sizeof(a.hazards)) == 0);
    /* No snaps still loses exactly three lives; music does not auto-jump. */
    for (unsigned i = 0; i < 6000 && a.lives; i++) snap_game_step(&a);
    assert(a.lives == 0 && !a.complete);
}

static void transport_test(void) {
    assert(snap_music_codec_volume(0) == 0);
    assert(snap_music_codec_volume(50) == 100);
    assert(snap_music_codec_volume(1000) == 100);
    unsigned previous = 0;
    for (unsigned volume = 1; volume <= 50; volume++) {
        const unsigned codec = snap_music_codec_volume(volume);
        assert(codec >= previous && codec <= 100);
        /* The real codec curve must reproduce browser amplitude within its
         * 0.5 dB register step, including the default and quiet settings. */
        const float actual = powf(10.0f, (-50.0f + codec * 0.5f) / 20.0f);
        assert(fabsf(actual / (volume / 50.0f) - 1.0f) < 0.03f);
        previous = codec;
    }
    uint8_t data[32000];
    for (unsigned i = 0; i < sizeof(data); i++) data[i] = (uint8_t)i;
    snap_music_t m;
    snap_music_init(&m, data, sizeof(data), 90);
    snap_music_request_t r = {.generation = 1, .playing = true, .volume = 30};
    snap_music_request(&m, r);
    int16_t frame[SNAP_FRAME_SAMPLES];
    for (unsigned i = 0; i < 18; i++) {
        snap_music_frame(&m, frame);
        assert(frame[0] == 0 && snap_music_position_ms(&m) == 0);
        snap_music_commit(&m, true);
    }
    snap_music_frame(&m, frame);
    assert(frame[0] == 256);
    for (unsigned i = 0; i < 40; i++) snap_music_commit(&m, true);
    assert(snap_music_position_ms(&m) == 110);
    r.playing = false; r.seek_ms = 110;
    snap_music_request(&m, r);
    for (unsigned i = 0; i < 100; i++) {
        snap_music_frame(&m, frame);
        assert(frame[0] == 0);
        snap_music_commit(&m, true);
        assert(snap_music_position_ms(&m) == 110);
    }
    r.playing = true; snap_music_request(&m, r);
    for (unsigned i = 0; i < 18; i++) snap_music_commit(&m, true);
    snap_music_frame(&m, frame);
    const unsigned offset = 110 * 16 * 2;
    assert((uint16_t)frame[0] == (unsigned)(data[offset] | data[offset + 1] << 8));
    snap_music_commit(&m, false);
    assert(m.failed && snap_music_position_ms(&m) == 110);
    snap_music_frame(&m, frame); assert(frame[0] == 0);
    r.generation++; r.seek_ms = 0; snap_music_request(&m, r);
    assert(!m.failed && snap_music_position_ms(&m) == 0);
    for (unsigned i = 0; i < 300; i++) snap_music_commit(&m, true);
    assert(snap_music_position_ms(&m) == 1000);
    snap_music_frame(&m, frame); assert(frame[0] == 0);

    snap_session_t s;
    snap_session_init(&s, (snap_range_t){1000,10000}, 0, 1);
    snap_session_mic(&s, true, 0);
    snap_session_key(&s, SNAP_KEY_OK, 1000);
    assert(snap_session_sync_music(&s, 100));
    assert(s.game.song_ms == 100);
    assert(!snap_session_sync_music(&s, 90));
    assert(!snap_session_sync_music(&s, 1000));
    assert(s.game.song_ms == 100);
    snap_session_key(&s, SNAP_KEY_OK, 1100);
    assert(snap_session_sync_music(&s, 200));
    assert(s.game.song_ms == 100); /* Paused audio clock cannot advance the world. */
    snap_session_key(&s, SNAP_KEY_OK, 1200);
    for (unsigned i = 0; i < 30; i++) snap_session_key(&s, SNAP_KEY_UP, 1300);
    assert(s.volume == 50 && s.page == SNAP_PLAYING);
    for (unsigned i = 0; i < 30; i++) snap_session_key(&s, SNAP_KEY_DOWN, 1300);
    assert(s.volume == 0 && s.page == SNAP_PLAYING);
    s.game.song_ms = snap_chart_duration_ms() / 10 * 10;
    for (unsigned i = 0; i < SNAP_HAZARD_COUNT; i++) s.game.hazards[i].hit = true;
    assert(snap_session_sync_music(&s, snap_chart_duration_ms()));
    assert(s.page == SNAP_OVER && s.game.complete && s.save_pending);
    assert(s.high_score == snap_game_score(&s.game));
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

/* Use the shipped soundtrack as a linear speaker-leakage fixture. This
 * validates the frequency split, not the board's nonlinear acoustic path. */
static void music_leakage_test(void) {
    for (unsigned inject = 0; inject < 2; inject++) {
        FILE *file = fopen("assets/music/mountain-king.pcm", "rb");
        assert(file);
        snap_mic_filter_t filter = {0};
        snap_detector_t detector;
        snap_detector_init(&detector);
        unsigned events = 0, pulses = 0, frame = 0;
        uint8_t bytes[SNAP_FRAME_SAMPLES * 2];
        while (fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes)) {
            int16_t pcm[SNAP_FRAME_SAMPLES];
            const bool hit = inject && frame > 200 && frame % 600 == 0;
            if (hit) pulses++;
            for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++) {
                unsigned u = bytes[2 * i] | (unsigned)bytes[2 * i + 1] << 8;
                float value = u < 32768 ? (float)u : (float)((int)u - 65536);
                if (hit) value += 6000 * expf(-(float)i / 18) * (i % 2 ? -1 : 1);
                pcm[i] = (int16_t)fmaxf(-32768, fminf(32767, value));
            }
            snap_mic_filter(&filter, pcm);
            snap_sound_t sound;
            events += snap_detector_process(&detector, pcm, &sound);
            frame++;
        }
        fclose(file);
        printf("Music leakage fixture: injected=%u detected=%u\n", pulses, events);
        /* At least 95% recall on this linear mixed fixture; zero events when
         * music plays alone. Human/room accuracy is a separate device check. */
        assert(inject ? events <= pulses && events * 100 >= pulses * 95 : events == 0);
    }
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
    s.game.song_ms = 23810;
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
    transport_test();
    map_test();
    detector_test();
    music_leakage_test();
    session_test();
    puts("Snap: calibration, detector, fixed beat chart, timing margin, audio transport, lifecycle PASS");
    return 0;
}
