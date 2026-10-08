#ifndef BKDS_LCD_ICONS_H
#define BKDS_LCD_ICONS_H
#include <stdint.h>
/* Stable ROM icon IDs 1..8: up, down, left, right, TL, TR, BL, BR.
   The library is independent of the eight active CGRAM slots. */
static const uint8_t lcd_icons[8][8]={
    {4,14,21,4,4,4,4,0},{4,4,4,4,21,14,4,0},
    {0,4,8,31,8,4,0,0},{0,4,2,31,2,4,0,0},
    {31,24,20,18,1,0,0,0},{31,3,5,9,16,0,0,0},
    {0,0,1,18,20,24,31,0},{0,0,16,9,5,3,31,0}
};
#endif
