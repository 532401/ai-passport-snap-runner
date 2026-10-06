#include "snap_music.h"
#include <math.h>
#include <string.h>

unsigned snap_music_codec_volume(unsigned volume) {
    if (!volume) return 0;
    if (volume >= 50) return 100;
    return (unsigned)lroundf(100.0f + 40.0f * log10f(volume / 50.0f));
}

void snap_music_init(snap_music_t *m, const void *pcm, size_t bytes, unsigned delay_ms) {
    memset(m, 0, sizeof(*m));
    m->pcm = pcm;
    m->samples = bytes / 2;
    m->lead_samples = delay_ms * (SNAP_SAMPLE_RATE / 1000);
}

void snap_music_request(snap_music_t *m, snap_music_request_t request) {
    if (request.generation != m->request.generation || request.playing != m->request.playing) {
        m->start = (size_t)request.seek_ms * (SNAP_SAMPLE_RATE / 1000);
        if (m->start > m->samples) m->start = m->samples;
        m->sent = 0;
        m->prime_left = m->lead_samples;
        m->failed = false;
    }
    m->request = request;
}

bool snap_music_audible(const snap_music_t *m) {
    return m->request.playing && !m->failed && !m->prime_left;
}

void snap_music_frame(const snap_music_t *m, int16_t out[SNAP_FRAME_SAMPLES]) {
    memset(out, 0, SNAP_FRAME_SAMPLES * sizeof(*out));
    if (!snap_music_audible(m)) return;
    for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++) {
        const size_t at = m->start + m->sent + i;
        if (at >= m->samples) break;
        const unsigned raw = m->pcm[2 * at] | ((unsigned)m->pcm[2 * at + 1] << 8);
        out[i] = raw < 32768 ? (int16_t)raw : (int16_t)((int)raw - 65536);
    }
}

void snap_music_commit(snap_music_t *m, bool ok) {
    if (!ok) m->failed = true;
    if (!m->request.playing || m->failed) return;
    if (m->prime_left) {
        m->prime_left = m->prime_left > SNAP_FRAME_SAMPLES ? m->prime_left - SNAP_FRAME_SAMPLES : 0;
    } else if (m->sent < m->samples + m->lead_samples) {
        m->sent += SNAP_FRAME_SAMPLES;
    }
}

uint32_t snap_music_position_ms(const snap_music_t *m) {
    const size_t played = m->sent > m->lead_samples ? m->sent - m->lead_samples : 0;
    size_t at = m->start + played;
    if (at > m->samples) at = m->samples;
    return (uint32_t)(at * 1000 / SNAP_SAMPLE_RATE);
}

void snap_mic_filter(snap_mic_filter_t *f, int16_t pcm[SNAP_FRAME_SAMPLES]) {
    /* Butterworth high-pass SOS at 3200/16000 Hz; generated from RBJ
     * biquads with Q=0.5411961001 and Q=1.3065629649. */
    static const float coeff[2][5] = {
        {0.34839083f, -0.69678167f, 0.34839083f, -0.32897568f, 0.06458765f},
        {0.47986127f, -0.95972254f, 0.47986127f, -0.45311952f, 0.46632556f},
    };
    for (unsigned i = 0; i < SNAP_FRAME_SAMPLES; i++) {
        float x = pcm[i];
        for (unsigned stage = 0; stage < 2; stage++) {
            const float *c = coeff[stage];
            const float y = c[0] * x + f->z1[stage];
            f->z1[stage] = c[1] * x - c[3] * y + f->z2[stage];
            f->z2[stage] = c[2] * x - c[4] * y;
            x = y;
        }
        pcm[i] = (int16_t)fmaxf(-32768, fminf(32767, x));
    }
}
