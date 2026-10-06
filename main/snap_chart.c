#include "snap_chart.h"
#include <stddef.h>
#include "snap_chart_data.h"

unsigned snap_chart_count(void) { return sizeof(snap_chart) / sizeof(snap_chart[0]); }
uint32_t snap_chart_duration_ms(void) { return SNAP_SONG_MS; }
const snap_chart_event_t *snap_chart_get(unsigned index) {
    return index < snap_chart_count() ? &snap_chart[index] : NULL;
}
