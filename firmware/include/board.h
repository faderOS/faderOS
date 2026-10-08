#ifndef BKDS_BOARD_H
#define BKDS_BOARD_H
#include <stdint.h>
#define FLASH_BASE 0x00040000u
#define SECTOR_SIZE 0x00020000u
#define APP_BASE 0x00060000u
#define APP_CAPACITY 0x000c0000u
#define HEADER_SIZE 32u
#define APP_ENTRY (APP_BASE + HEADER_SIZE)
#define IMAGE_MAGIC 0x424b4131u
_Noreturn void update_enter(void);
void uart_init(void);
void uart_puts(const char *s);
void uart_hex(uint32_t value, unsigned digits);
int uart_getc(void);
void platform_init(void);
extern uint32_t platform_boots, platform_last_fault, platform_last_fault_pc;
extern uint32_t platform_entry, platform_resetvec, platform_eprom_ran;
extern uint32_t platform_survived;
uint32_t platform_step_claim(unsigned steps);
void platform_step_passed(uint32_t step);
_Noreturn void firmware_main(void);
uint32_t crc32(const uint8_t *data, uint32_t size);
int image_valid(const uint8_t *image, uint32_t capacity);
#endif
