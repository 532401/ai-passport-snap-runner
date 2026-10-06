#include "snap_debug.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char line[SNAP_DEBUG_LINE_SIZE];
    snap_debug_record_t r = {.kind = SNAP_DEBUG_STATE, .at_ms = 1234};
    snap_session_init(&r.data.session, (snap_range_t){1000, 10000}, 0, 42);
    snap_session_mic(&r.data.session, true, 0);
    snap_session_key(&r.data.session, SNAP_KEY_OK, 0);
    assert(snap_session_sound(&r.data.session, 10000, false, false, 100) == SNAP_INPUT_GUARD);
    assert(snap_session_sound(&r.data.session, 10000, false, false, 300) == SNAP_INPUT_JUMP);
    assert(snap_session_sound(&r.data.session, 10000, false, false, 301) == SNAP_INPUT_AIR);
    assert(snap_debug_format(line, sizeof(line), &r));
    puts(line);
    char tiny[16];
    assert(!snap_debug_format(tiny, sizeof(tiny), &r) && tiny[0] == 0);
    assert(!snap_debug_format(tiny, 0, &r));
    r.data.session.game.y = NAN;
    assert(!snap_debug_format(line, sizeof(line), &r) && line[0] == 0);
    r.kind = SNAP_DEBUG_DETECT;
    r.data.audio.peak = 12000;
    r.data.audio.rms = 3000;
    r.data.audio.noise = 20;
    r.data.audio.reason = SNAP_DETECT_ACCEPTED;
    r.data.audio.clipped = false;
    assert(snap_debug_format(line, sizeof(line), &r));
    puts(line);
    r.kind = SNAP_DEBUG_INPUT;
    r.data.audio.reason = SNAP_INPUT_JUMP;
    assert(snap_debug_format(line, sizeof(line), &r));
    puts(line);
    r.data.audio.reason = 99;
    assert(!snap_debug_format(line, sizeof(line), &r));
    r.kind = (snap_debug_kind_t)-1;
    assert(!snap_debug_format(line, sizeof(line), &r));

    snap_detector_t d;
    snap_detector_init(&d);
    int16_t pcm[SNAP_FRAME_SAMPLES] = {0};
    snap_sound_t sound;
    for (unsigned i = 0; i < 200; i++) snap_detector_process(&d, pcm, &sound);
    assert(d.reason == SNAP_DETECT_QUIET);
    for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++) pcm[i] = i % 2 ? -8000 : 8000;
    for (unsigned i = 0; i < 6; i++) assert(!snap_detector_process(&d, pcm, &sound));
    assert(d.reason == SNAP_DETECT_DECAY);
    memset(pcm, 0, sizeof(pcm));
    for (unsigned i = 0; i < 200; i++) snap_detector_process(&d, pcm, &sound);
    for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++)
        pcm[i] = (int16_t)(12000 * expf(-(float)i / 18) * (i % 2 ? -1 : 1));
    assert(!snap_detector_process(&d, pcm, &sound));
    memset(pcm, 0, sizeof(pcm));
    for (unsigned i = 0; i < 4; i++) assert(!snap_detector_process(&d, pcm, &sound));
    assert(snap_detector_process(&d, pcm, &sound));
    assert(d.reason == SNAP_DETECT_ACCEPTED);
    assert(!snap_detector_process(&d, pcm, &sound));
    assert(d.reason == SNAP_DETECT_COOLDOWN);
    return 0;
}
