#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "snap_detector.h"

typedef struct {
    uint32_t generation;
    uint32_t seek_ms;
    unsigned volume;
    bool playing;
} snap_music_request_t;

/* One PCM worker owns this state. No allocation, codec calls or wall clock. */
typedef struct {
    const uint8_t *pcm;
    size_t samples;
    size_t start;
    size_t sent;
    unsigned lead_samples;
    unsigned prime_left;
    snap_music_request_t request;
    bool failed;
} snap_music_t;

void snap_music_init(snap_music_t *m, const void *pcm, size_t bytes, unsigned delay_ms);
void snap_music_request(snap_music_t *m, snap_music_request_t request);
void snap_music_frame(const snap_music_t *m, int16_t out[SNAP_FRAME_SAMPLES]);
void snap_music_commit(snap_music_t *m, bool ok);
uint32_t snap_music_position_ms(const snap_music_t *m);
bool snap_music_audible(const snap_music_t *m);
/* Map the game's linear 0..50 amplitude to codec-dev's -50..0 dB curve.
 * Zero remains mute; out-of-range values saturate at unity gain. */
unsigned snap_music_codec_volume(unsigned volume);

typedef struct { float z1[2], z2[2]; } snap_mic_filter_t;
/* Four-pole 3.2 kHz high pass. Calibration and gameplay use the same filter.
 * The shipped music has a four-pole 1.5 kHz low pass. This is frequency
 * separation, not acoustic echo cancellation; nonlinear speaker leakage
 * and the actual USB/audio latency require device acceptance. */
void snap_mic_filter(snap_mic_filter_t *f, int16_t pcm[SNAP_FRAME_SAMPLES]);
