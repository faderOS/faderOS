#ifndef BKDS_PANEL_H
#define BKDS_PANEL_H
#include <stdint.h>
/* Row-oriented board API. Rows 1/2 on chip11/21 are normalized to LED order. */
typedef struct { uint8_t chip31[8], chip11[4], chip21[4], ky306[4], ky308[3]; } panel_keys;
int ky_planes(const panel_keys *low,const panel_keys *high);
void segments_raw(const uint8_t digits[6],uint8_t sides);
void ky_init(void);
void ky_scan(panel_keys *keys);
void ky_feedback(const panel_keys *keys, unsigned color);
void ky_indicators(uint8_t mask);
void ky_blank(void);
void ky_lamp_test(unsigned color);
void segments_number(uint32_t number);
void segments_test(unsigned digit, unsigned sides);
void segments_init(void);
/* 0xD00001, confirmed on the desk by the 0.8 ladder:
   bit 3 (0x08) panel gate, Sony FUN_0004001a
   bit 2 (0x04) buzzer
   bit 1 (0x02) hardware reset after ~5 s; never set
   bits 4/5/6   Sony square waves (ext1 / timer 2 / timer 1), not required to live */
#define PANEL_GATE 0x08u
#define PANEL_STATUS PANEL_GATE
#define PANEL_BUZZER 0x04u
extern volatile uint8_t gate_shadow;
void gate_write(uint8_t value);
void gate_set(uint8_t mask);
void gate_clear(uint8_t mask);
void panel_init(uint8_t gate);
void lcd_init(void);
void lcd_write_byte(uint8_t value, uint8_t rs);
void lcd_service(void);
void lcd_glyph(unsigned slot, const uint8_t rows[8]);
void lcd_patch(unsigned offset, const uint8_t *data, unsigned count);
void lcd_line(unsigned row, const char *text);
void delay_us(unsigned us);
typedef struct { uint16_t tbar; uint8_t x,y; int16_t delta[6]; } panel_analogs;
void analog_init(void);
void analog_read(panel_analogs *values);
#endif
