#include "snap_session.h"

#include <string.h>

void snap_session_init(snap_session_t *s, snap_range_t stored, unsigned best, uint32_t seed) {
    memset(s, 0, sizeof(*s));
    s->page = SNAP_HOME;
    s->range = snap_range_valid(stored) ? stored : (snap_range_t){0};
    s->high_score = best > 999999 ? 999999 : best;
    s->seed = seed;
    snap_game_start(&s->game, seed, true);
}

static void calibrate(snap_session_t *s, uint64_t now_ms) {
    memset(&s->calibration, 0, sizeof(s->calibration));
    s->page = SNAP_CAL_LOUD;
    s->hint = SNAP_HINT_WAIT;
    s->accept_after_ms = now_ms + 1000;
    s->level = 0;
}

static void start(snap_session_t *s, bool practice) {
    snap_game_start(&s->game, ++s->seed, practice);
    s->page = practice ? SNAP_PRACTICE : SNAP_PLAYING;
    s->hint = SNAP_HINT_READY;
    s->level = 0;
}

void snap_session_key(snap_session_t *s, snap_key_t key, uint64_t now_ms) {
    /* Physical key clicks must not act as snaps; also discard transition audio. */
    if (s->accept_after_ms < now_ms + 300) s->accept_after_ms = now_ms + 300;
    if (s->page == SNAP_PLAYING) {
        if (key == SNAP_KEY_OK) {
            s->page = SNAP_PAUSED;
            s->game.paused = true;
        }
        return;
    }
    if (s->page == SNAP_PAUSED) {
        if (key == SNAP_KEY_OK && s->mic_ready) {
            s->page = SNAP_PLAYING;
            s->game.paused = false;
        } else if (key == SNAP_KEY_DOWN) {
            s->page = SNAP_HOME;
        }
        return;
    }
    if (s->page == SNAP_HOME) {
        if (!s->mic_ready) return;
        if (key == SNAP_KEY_DOWN || !snap_range_valid(s->range)) calibrate(s, now_ms);
        else if (key == SNAP_KEY_UP) start(s, true);
        else start(s, false);
        return;
    }
    if (s->page == SNAP_PRACTICE || s->page == SNAP_OVER) {
        if (key == SNAP_KEY_UP) s->page = SNAP_HOME;
        else if (s->mic_ready) {
            if (key == SNAP_KEY_DOWN) calibrate(s, now_ms);
            else start(s, false);
        }
        return;
    }
    if (key == SNAP_KEY_UP) {
        s->page = SNAP_HOME;
    } else if (key == SNAP_KEY_OK && s->mic_ready) {
        if (s->page == SNAP_CAL_SOFT_WAIT) {
            s->page = SNAP_CAL_SOFT;
            s->hint = SNAP_HINT_WAIT;
            s->accept_after_ms = now_ms + 1000;
        } else if (s->page == SNAP_CAL_FAILED) {
            calibrate(s, now_ms);
        }
    }
}

void snap_session_sound(snap_session_t *s, float amplitude, bool clipped,
                        bool noisy, uint64_t now_ms) {
    if (!s->mic_ready || now_ms < s->accept_after_ms) return;
    if (s->page == SNAP_CAL_LOUD || s->page == SNAP_CAL_SOFT) {
        s->feedback_until_ms = now_ms + 700;
        if (clipped) { s->hint = SNAP_HINT_CLIPPED; return; }
        if (noisy) { s->hint = SNAP_HINT_NOISY; return; }
        if (!snap_calibration_add(&s->calibration, amplitude, false)) return;
        s->hint = SNAP_HINT_CAPTURED;
        s->accept_after_ms = now_ms + 700;
        if (s->calibration.count == 3) s->page = SNAP_CAL_SOFT_WAIT;
        if (s->calibration.count == 6) {
            snap_range_t candidate;
            if (snap_calibration_finish(&s->calibration, &candidate)) {
                s->range = candidate;
                s->save_pending = true;
                start(s, true);
            } else s->page = SNAP_CAL_FAILED;
        }
    } else if (s->page == SNAP_PLAYING || s->page == SNAP_PRACTICE) {
        s->level = snap_level(s->range, amplitude);
        s->feedback_until_ms = now_ms + 500;
        s->hint = snap_game_jump(&s->game, s->level) ? SNAP_HINT_JUMP : SNAP_HINT_AIR;
    }
}

void snap_session_step(snap_session_t *s) {
    if (s->page != SNAP_PLAYING && s->page != SNAP_PRACTICE) return;
    snap_game_step(&s->game);
    if (s->page == SNAP_PLAYING && !s->game.lives) {
        s->page = SNAP_OVER;
        if (snap_game_score(&s->game) > s->high_score) {
            s->high_score = snap_game_score(&s->game);
            s->save_pending = true;
        }
    }
}

void snap_session_mic(snap_session_t *s, bool ready, uint64_t now_ms) {
    s->mic_ready = ready;
    if (!ready) {
        s->accept_after_ms = now_ms + 1000;
        if (s->page == SNAP_PLAYING) {
            s->page = SNAP_PAUSED;
            s->game.paused = true;
        }
    }
}
