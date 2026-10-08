/* Rendering/scheduling regression; does not model the physical LCD bus. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../drivers/lcd.c"
static unsigned address,writes;
static char cells[104];
static uint8_t cgram[64];
static int in_cgram;
void gate_write(uint8_t x) {(void)x;}
void analog_init(void) {}
void ky_init(void) {}
void ky_blank(void) {}
void lcd_init(void) {}
void segments_init(void) {}
void lcd_write_byte(uint8_t value,uint8_t rs) {
    writes++;
    if(rs) {
        if(in_cgram){assert(address<64&&value<32);cgram[address++]=value;}
        else {assert(address<sizeof cells);cells[address++]=(char)value;}
    } else if(value&128){in_cgram=0;address=value&127;}
    else {assert(value&64);in_cgram=1;address=value&63;}
}
static void drain(void) {for(unsigned i=0;i<21;i++)lcd_service();assert(pending==0&&glyph_pending==0);}
int main(void) {
    memset(cells,' ',sizeof cells);
    const uint8_t first_zero=0;
    lcd_patch(0,&first_zero,1);assert(pending==1);drain();assert(cells[0]==0);
    const uint8_t space=' ';lcd_patch(0,&space,1);drain();
    lcd_line(0,"FIRST");lcd_line(1,"SECOND");assert(pending==11);drain();
    assert(!memcmp(cells,"FIRST",5)&&!memcmp(cells+64,"SECOND",6));
    unsigned before=writes;
    for(unsigned i=0;i<1000;i++)lcd_service();
    assert(writes==before);
    lcd_line(0,"FIRST");assert(pending==0);
    lcd_patch(0,(const uint8_t*)"x",1);assert(pending==1);
    lcd_patch(0,(const uint8_t*)"F",1);assert(pending==0);
    lcd_patch(38,(const uint8_t*)"ABCD",4);assert(pending==4);drain();
    assert(cells[38]=='A'&&cells[39]=='B'&&cells[64]=='C'&&cells[65]=='D');
    lcd_line(1,"replacement");lcd_service();lcd_line(1,"latest");drain();
    assert(!memcmp(cells+64,"latest",6)&&cells[70]==' ');
    lcd_patch(79,(const uint8_t*)"invalid",7);assert(pending==0);
    const uint8_t glyph[8]={0,4,14,21,4,4,4,0};
    lcd_glyph(0,glyph);assert(glyph_pending==8);lcd_service();
    assert(glyph_pending==4&&!in_cgram);
    uint8_t newer[8];memcpy(newer,glyph,8);newer[0]=31;
    lcd_glyph(0,newer);assert(glyph_pending==5);drain();
    assert(!memcmp(cgram,newer,8));
    before=writes;lcd_glyph(0,newer);drain();assert(writes==before);
    uint8_t bad[8];memcpy(bad,newer,8);bad[7]=32;
    lcd_glyph(0,bad);lcd_glyph(8,newer);assert(glyph_pending==0);
    uint8_t raw[]={0,7,255};lcd_patch(39,raw,3);drain();
    assert((uint8_t)cells[39]==0&&(uint8_t)cells[64]==7&&(uint8_t)cells[65]==255&&!in_cgram);
    /* Slot zero remains valid when replacing previously rendered text. */
    lcd_patch(1,raw,1);assert(pending==1);drain();assert(cells[1]==0);
    puts("LCD PASS: idle, repeated/cancelled patches, row boundary, partial repaint replacement");
}
