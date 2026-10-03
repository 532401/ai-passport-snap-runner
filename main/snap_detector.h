#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SNAP_SAMPLE_RATE 16000
#define SNAP_FRAME_SAMPLES 80

typedef struct {
    float amplitude;
    bool clipped;
} snap_sound_t;

typedef struct {
    float previous_sample;
    float filtered;
    float noise_power;
    float rms;
    float peak;
    float burst_power;
    float burst_hf;
    float burst_peak;
    float burst_max_rms;
    float tail_rms;
    unsigned warmup;
    unsigned quiet;
    unsigned cooldown;
    unsigned burst_frames;
    bool burst_clipped;
    bool armed;
} snap_detector_t;

void snap_detector_init(snap_detector_t *detector);
/* Exactly 5 ms of PCM per call. Returns one event after a 30 ms burst window.
 * A heuristic transient detector, not a trained snap-versus-clap classifier. */
bool snap_detector_process(snap_detector_t *detector,
                           const int16_t pcm[SNAP_FRAME_SAMPLES], snap_sound_t *sound);
