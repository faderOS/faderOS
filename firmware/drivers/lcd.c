#include "panel.h"
#define R(a) (*(volatile uint8_t *)(a))
#define CTRL 0x300003u
#define DATA 0x300007u
void panel_init(uint8_t gate)
{
    /* Sony FUN_0004001a: analog, then gate bit 3 only, then KY-308 and LCD.
       Writing 0x0A (bit 1) resets this desk in about five seconds. */
    gate_write(0);
    analog_init();
    gate_write(gate);
    R(0x300009)=0; R(DATA)=0; R(0x300005)=0; R(CTRL)=0;
    R(0x300019)=0; R(0x300015)=0; R(0x300013)=0;
    ky_init();
    ky_blank();
    lcd_init();
    segments_init();
}
/* lcd_init clears DDRAM to spaces; slot zero must never be confused with a
   zero-initialized shadow byte before the first text refresh. */
static char desired[81]="                                        "
                        "                                        ";
static char shown[81]="                                        "
                      "                                        ";
static unsigned pending;
static void desire(unsigned i,char value)
{
    /* Count differences, not writes: replacing an unfinished patch can also
       cancel work. Once synchronized, lcd_service must be constant-time. */
    if(desired[i]!=shown[i]) pending--;
    desired[i]=value;
    if(desired[i]!=shown[i]) pending++;
}
void lcd_line(unsigned row, const char *s)
{
    if (row>1) return;
    for (unsigned i=0;i<40;i++) desire(row*40+i,*s ? *s++ : ' ');
}
void lcd_patch(unsigned offset, const uint8_t *data, unsigned count)
{
    if (offset>=80 || count>80-offset) return;
    for (unsigned i=0;i<count;i++) desire(offset+i,(char)data[i]);
}
/* Eight 5x8 CGRAM glyphs, uploaded through the same bounded scheduler. */
static uint8_t glyph_desired[64],glyph_shown[64],glyph_known[8],glyph_requested[8];
static unsigned glyph_pending;
void lcd_glyph(unsigned slot, const uint8_t rows[8])
{
    if(slot>=8) return;
    for(unsigned r=0;r<8;r++) if(rows[r]>31) return;
    for(unsigned r=0;r<8;r++) {
        unsigned i=slot*8+r,known=glyph_known[slot]&(1u<<r);
        if(glyph_requested[slot]&&(!known||glyph_desired[i]!=glyph_shown[i])) glyph_pending--;
        glyph_desired[i]=rows[r];
        if(!known||glyph_desired[i]!=glyph_shown[i]) glyph_pending++;
    }
    glyph_requested[slot]=1;
}
static void glyph_service(void)
{
    unsigned budget=4,address=64;
    for(unsigned i=0;i<64&&budget&&glyph_pending;i++) {
        unsigned slot=i/8,bit=1u<<(i%8);
        if(glyph_requested[slot]&&(!(glyph_known[slot]&bit)||glyph_desired[i]!=glyph_shown[i])) {
            if(address!=i) lcd_write_byte((uint8_t)(0x40+i),0);
            lcd_write_byte(glyph_desired[i],2);
            glyph_shown[i]=glyph_desired[i];glyph_known[slot]|=bit;
            glyph_pending--;budget--;address=i+1;
        }
    }
    /* Never leave the controller pointing at CGRAM for later text writes. */
    lcd_write_byte(0x80,0);
}
void lcd_service(void)
{
    static unsigned cursor;
    if(glyph_pending) {glyph_service();return;}
    if(!pending) return;
    unsigned budget=4, address=80;
    /* A short contiguous burst uses the LCD's auto-increment. Bound the work
       so scanning and serial TX continue between bursts; never clear first. */
    for (unsigned n=0;n<80 && budget && pending;n++) {
        unsigned i=cursor;
        if (++cursor==80) cursor=0;
        if (desired[i]!=shown[i]) {
            if (address!=i || i==40) lcd_write_byte(0x80+(i<40 ? i : i+24),0);
            lcd_write_byte((uint8_t)desired[i],2); shown[i]=desired[i]; pending--;
            address=i+1; budget--;
        }
    }
}
