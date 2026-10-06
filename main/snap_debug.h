#pragma once

#include "snap_detector.h"
#include "snap_session.h"
#include <stddef.h>

#define SNAP_DEBUG_LINE_SIZE 2048
/* Versioned NDJSON after @snap . Values describe observations, never commands. */
typedef enum { SNAP_DEBUG_STATE, SNAP_DEBUG_AUDIO, SNAP_DEBUG_DETECT, SNAP_DEBUG_INPUT } snap_debug_kind_t;
typedef struct {
    snap_debug_kind_t kind;
    uint64_t at_ms;
    unsigned dropped;
    union {
        snap_session_t session;
        struct {
            float peak, rms, noise;
            unsigned reason;
            bool clipped;
        } audio;
    } data;
} snap_debug_record_t;

/* Returns 0 on overflow, invalid enum, or non-finite data; no partial records. */
size_t snap_debug_format(char *out, size_t capacity, const snap_debug_record_t *record);
