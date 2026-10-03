#include "snap_detector.h"

#include <math.h>
#include <string.h>

void snap_detector_init(snap_detector_t *d) {
    memset(d, 0, sizeof(*d));
    d->noise_power = 400.0f;
}

bool snap_detector_process(snap_detector_t *d,
                           const int16_t pcm[SNAP_FRAME_SAMPLES], snap_sound_t *sound) {
    float power = 0.0f;
    float hf = 0.0f;
    float peak = 0.0f;
    bool clipped = false;
    for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++) {
        const float sample = pcm[i];
        const float old = d->filtered;
        d->filtered = sample - d->previous_sample + 0.9f * old;
        d->previous_sample = sample;
        const float delta = d->filtered - old;
        power += d->filtered * d->filtered;
        hf += delta * delta;
        peak = fmaxf(peak, fabsf(d->filtered));
        if (sample >= 32000.0f || sample <= -32000.0f) clipped = true;
    }
    power /= SNAP_FRAME_SAMPLES;
    hf /= SNAP_FRAME_SAMPLES;
    d->rms = sqrtf(power);
    d->peak = peak;
    if (d->warmup < 160) {
        d->noise_power += (power - d->noise_power) * 0.04f;
        d->warmup++;
        return false;
    }
    const float noise = sqrtf(fmaxf(100.0f, d->noise_power));
    if (d->cooldown) d->cooldown--;
    if (d->burst_frames) {
        d->burst_frames++;
        d->burst_power += power;
        d->burst_hf += hf;
        d->burst_peak = fmaxf(d->burst_peak, peak);
        d->burst_max_rms = fmaxf(d->burst_max_rms, d->rms);
        d->burst_clipped |= clipped;
        if (d->burst_frames >= 5) d->tail_rms += d->rms;
        if (d->burst_frames < 6) return false;
        d->burst_frames = 0;
        d->cooldown = 36; /* 180 ms, then require a fresh quiet interval. */
        d->quiet = 0;
        const bool transient = d->tail_rms * 0.5f < d->burst_max_rms * 0.45f &&
                               d->burst_hf > d->burst_power * 0.15f;
        if (!transient) return false;
        sound->amplitude = d->burst_peak;
        sound->clipped = d->burst_clipped;
        return true;
    }
    const bool quiet = d->rms < fmaxf(100.0f, noise * 1.7f);
    if (quiet) {
        d->quiet++;
        if (d->quiet >= 6 && !d->cooldown) d->armed = true;
    } else {
        d->quiet = 0;
    }
    if (d->armed && !d->cooldown &&
        peak >= fmaxf(400.0f, noise * 7.0f) &&
        d->rms >= fmaxf(90.0f, noise * 3.5f)) {
        d->armed = false;
        d->burst_frames = 1;
        d->burst_power = power;
        d->burst_hf = hf;
        d->burst_peak = peak;
        d->burst_max_rms = d->rms;
        d->burst_clipped = clipped;
        d->tail_rms = 0.0f;
        return false;
    }
    /* Adapt upward too: a permanently louder environment must not leave the
     * detector at its old floor. Active burst windows never enter the EMA. */
    const float alpha = power > d->noise_power ? 0.002f : 0.015f;
    d->noise_power += (power - d->noise_power) * alpha;
    return false;
}
