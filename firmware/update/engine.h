#ifndef UPDATE_ENGINE_H
#define UPDATE_ENGINE_H
#include <stdint.h>
#define UPDATE_SECTOR 0x20000u
/* Backend operations must run in RAM (or native tests). */
uint8_t *update_buffer(void);
const uint8_t *update_flash(unsigned sector);
int update_flash_program(unsigned sector,const uint8_t *bytes);
int update_device_ok(void);
/* Results: 0 OK, 1 arguments/order, 2 CRC, 3 flash/device, 4 incomplete plan. */
int update_command(unsigned type,const uint8_t *p,unsigned n);
void update_session_reset(void);
unsigned update_mask(void);
unsigned update_done(void);
#endif
