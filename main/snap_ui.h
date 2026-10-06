#pragma once

#include "snap_session.h"
#include "lvgl.h"

typedef struct {
    bool music_fault;
    bool mic_ok;
    bool mic_warming;
    bool storage_ok;
    bool buttons_ok;
    float rms;
    float noise;
    int battery;
    uint64_t now_ms;
} snap_ui_status_t;

/* Caller owns the LVGL lock. Objects and draw-state live for the application
 * lifetime; no audio task ever holds pointers to them. */
bool snap_ui_create(void);
void snap_ui_update(const snap_session_t *session, const snap_ui_status_t *status);
bool snap_ui_check_fonts(void);
