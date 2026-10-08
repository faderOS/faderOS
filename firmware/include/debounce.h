#ifndef BKDS_DEBOUNCE_H
#define BKDS_DEBOUNCE_H
#include "panel.h"
#define DEBOUNCE_MS 20u
#define SCAN_MS 5u
/* Independent candidate and elapsed time for every key, on both edges. */
typedef struct {
    panel_keys stable, candidate;
    uint32_t since[sizeof(panel_keys)*8], last;
    unsigned initialized;
} debounce_state;
void debounce_update(debounce_state *d, const panel_keys *raw, uint32_t now,
                     panel_keys *pressed, panel_keys *released);
#endif
