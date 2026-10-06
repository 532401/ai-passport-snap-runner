#pragma once

#include "snap_model.h"

typedef enum {
    SNAP_HOME, SNAP_CAL_LOUD, SNAP_CAL_SOFT_WAIT, SNAP_CAL_SOFT,
    SNAP_CAL_FAILED, SNAP_PRACTICE, SNAP_PLAYING, SNAP_PAUSED, SNAP_OVER
} snap_page_t;
typedef enum { SNAP_KEY_UP, SNAP_KEY_DOWN, SNAP_KEY_OK } snap_key_t;
typedef enum {
    SNAP_HINT_READY, SNAP_HINT_CAPTURED, SNAP_HINT_CLIPPED,
    SNAP_HINT_NOISY, SNAP_HINT_WAIT, SNAP_HINT_AIR, SNAP_HINT_JUMP
} snap_hint_t;

typedef struct {
    snap_page_t page;
    snap_hint_t hint;
    snap_range_t range;
    snap_calibration_t calibration;
    snap_game_t game;
    uint64_t accept_after_ms;
    uint64_t feedback_until_ms;
    unsigned high_score;
    unsigned level;
    unsigned volume;
    uint32_t seed;
    bool mic_ready;
    bool save_pending;
} snap_session_t;

void snap_session_init(snap_session_t *s, snap_range_t stored, unsigned best, uint32_t seed);
void snap_session_key(snap_session_t *s, snap_key_t key, uint64_t now_ms);
typedef enum {
    SNAP_INPUT_MIC, SNAP_INPUT_GUARD, SNAP_INPUT_CLIPPED, SNAP_INPUT_NOISY,
    SNAP_INPUT_RANGE, SNAP_INPUT_CAPTURED, SNAP_INPUT_JUMP, SNAP_INPUT_AIR,
    SNAP_INPUT_IDLE
} snap_input_result_t;

snap_input_result_t snap_session_sound(snap_session_t *s, float amplitude, bool clipped,
                        bool noisy, uint64_t now_ms);
/* Advance from an external audio clock; false means seek/stall, so pause. */
bool snap_session_sync_music(snap_session_t *s, uint32_t song_ms);
void snap_session_step(snap_session_t *s);
void snap_session_mic(snap_session_t *s, bool ready, uint64_t now_ms);
