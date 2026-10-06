#pragma once
#include <stdint.h>

typedef struct {
    uint32_t jump_ms;
    uint16_t width;
    uint8_t height;
    uint8_t kind;
} snap_chart_event_t;

unsigned snap_chart_count(void);
uint32_t snap_chart_duration_ms(void);
const snap_chart_event_t *snap_chart_get(unsigned index);
