#include "snap_debug.h"
#include "snap_chart.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

static bool append(char *out, size_t cap, size_t *used, const char *format, ...) {
    if (*used >= cap) return false;
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(out + *used, cap - *used, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= cap - *used) return false;
    *used += (size_t)n;
    return true;
}

size_t snap_debug_format(char *out, size_t cap, const snap_debug_record_t *r) {
    static const char *const kinds[] = {"state", "audio", "detect", "input"};
    static const char *const reasons[] = {"warmup", "quiet", "threshold", "cooldown",
        "wait_quiet", "collecting", "accepted", "decay", "spectrum"};
    static const char *const results[] = {"mic", "guard", "clipped", "noisy", "range",
        "captured", "jump", "air", "idle", "stale"};
    static const char *const pages[] = {"home", "loud", "soft_ready", "soft", "invalid",
        "practice", "playing", "paused", "over"};
    size_t used = 0;
    if (!cap || (unsigned)r->kind > SNAP_DEBUG_INPUT) return 0;
#define ADD(...) do { if (!append(out, cap, &used, __VA_ARGS__)) goto fail; } while (0)
#define FINITE(v) do { if (!isfinite(v)) goto fail; } while (0)
    ADD("{\"v\":1,\"type\":\"%s\",\"ms\":%llu,\"dropped\":%u", kinds[r->kind],
        (unsigned long long)r->at_ms, r->dropped);
    if (r->kind == SNAP_DEBUG_STATE) {
        const snap_session_t *s = &r->data.session;
        const snap_game_t *g = &s->game;
        if ((unsigned)s->page > SNAP_OVER) goto fail;
        FINITE(s->range.min); FINITE(s->range.max); FINITE(g->distance);
        FINITE(g->speed); FINITE(g->y); FINITE(g->velocity); FINITE(g->invincible);
        ADD(",\"page\":\"%s\",\"level\":%u,\"min\":%.2f,\"max\":%.2f,"
            "\"samples\":%u,\"mic\":%s,\"seed\":%u,\"distance\":%.3f,\"score\":%u,"
            "\"speed\":%.3f,\"y\":%.3f,\"velocity\":%.3f,\"lives\":%u,"
            "\"grounded\":%s,\"invincible\":%.3f,\"practice\":%s,\"hazards\":[",
            pages[s->page], s->level, (double)s->range.min, (double)s->range.max,
            s->calibration.count, s->mic_ready ? "true" : "false", (unsigned)s->seed,
            (double)g->distance, snap_game_score(g), (double)g->speed, (double)g->y,
            (double)g->velocity, g->lives, g->grounded ? "true" : "false",
            (double)g->invincible, g->practice ? "true" : "false");
        for (unsigned i = 0; i < SNAP_HAZARD_COUNT; i++) {
            const snap_hazard_t *h = &g->hazards[i];
            FINITE(h->x); FINITE(h->width); FINITE(h->height);
            ADD("%s{\"x\":%.3f,\"w\":%.3f,\"h\":%.3f,\"kind\":%u,\"hit\":%s}",
                i ? "," : "", (double)h->x, (double)h->width, (double)h->height,
                (unsigned)h->kind, h->hit ? "true" : "false");
        }
        ADD("],\"songMs\":%u,\"songDurationMs\":%u,\"cueInMs\":%d,"
            "\"complete\":%s,\"volume\":%u", g->song_ms, snap_chart_duration_ms(),
            snap_game_cue_in_ms(g), g->complete ? "true" : "false", s->volume);
    } else {
        FINITE(r->data.audio.peak); FINITE(r->data.audio.rms); FINITE(r->data.audio.noise);
        unsigned reason = r->data.audio.reason;
        if (reason >= (r->kind == SNAP_DEBUG_INPUT ? 10U : 9U)) goto fail;
        ADD(",\"peak\":%.2f,\"rms\":%.2f,\"noise\":%.2f,\"reason\":\"%s\",\"clipped\":%s",
            (double)r->data.audio.peak, (double)r->data.audio.rms, (double)r->data.audio.noise,
            r->kind == SNAP_DEBUG_INPUT ? results[reason] : reasons[reason],
            r->data.audio.clipped ? "true" : "false");
    }
    ADD("}");
    return used;
fail:
    out[0] = '\0';
    return 0;
#undef ADD
#undef FINITE
}
