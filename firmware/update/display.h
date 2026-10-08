#ifndef UPDATE_DISPLAY_H
#define UPDATE_DISPLAY_H
#include <stdint.h>
void update_display_init(void);
void update_display(const char *phase, unsigned sector, unsigned percent);
void update_display_result(unsigned type, const uint8_t *payload, unsigned size, unsigned status);
#endif
