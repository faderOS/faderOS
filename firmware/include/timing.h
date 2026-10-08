#ifndef BKDS_TIMING_H
#define BKDS_TIMING_H
#include <stdint.h>
void clock_init(void);
void clock_heartbeats(void);
uint32_t clock_ms(void);
extern volatile uint32_t ext1_count;
#endif
