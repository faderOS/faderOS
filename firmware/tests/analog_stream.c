/* Protocol/scheduling test only: no claim about physical MMIO sampling speed. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../common/link.c"
static unsigned us,next_byte,bytes,frames[256],used,bulk;
static uint8_t wire[210];
volatile uint8_t gate_shadow;
volatile uint32_t serial_errors;
void ky_blank(void) {}
int ky_planes(const panel_keys *a,const panel_keys *b) {(void)a;(void)b;return 1;}
void ky_indicators(uint8_t x) {(void)x;}
void segments_test(unsigned a,unsigned b) {(void)a;(void)b;}
void segments_raw(const uint8_t a[6],uint8_t b) {(void)a;(void)b;}
void gate_set(uint8_t x) {(void)x;}
void gate_clear(uint8_t x) {(void)x;}
void lcd_line(unsigned row,const char *s) {(void)row;(void)s;}
static unsigned glyph_calls,cell_calls;
void lcd_glyph(unsigned a,const uint8_t b[8]) {(void)a;(void)b;glyph_calls++;}
void lcd_patch(unsigned a,const uint8_t *b,unsigned c) {(void)a;(void)b;(void)c;cell_calls++;}
_Noreturn void update_enter(void) {abort();}
void serial_link_init(void) {}
int serial_read(void) {return -1;}
int serial_set_baud(uint32_t b) {(void)b;return 1;}
int serial_tx_empty(void) {return 1;}
int serial_write(uint8_t b) {
    if(!bulk&&us<next_byte) return 0;
    next_byte=us+261;bytes++;
    if(b) wire[used++]=b;
    else if(used) {
        uint8_t raw[210];unsigned n=0;
        for(unsigned i=0;i<used;) {unsigned c=wire[i++];for(unsigned j=1;j<c;j++) raw[n++]=wire[i++];if(c!=255&&i<used)raw[n++]=0;}
        assert(n>=14&&u32(raw+n-4)==crc32(raw,n-4));frames[raw[1]]++;
        if(raw[1]==0x83||raw[1]==0x84) assert(n==18);
        if(raw[1]==0x85) {assert(n==65&&raw[14]==1);} // first queued DOWN, retry keeps it
        used=0;
    }
    return 1;
}
int main(void) {
    uint8_t icons[3]={6,1,8};
    assert(command(0x18,icons,3)==0&&glyph_calls==2);
    icons[2]=9;assert(command(0x18,icons,3)==1&&glyph_calls==2);
    icons[2]=8;icons[0]=7;assert(command(0x18,icons,3)==1&&glyph_calls==2);
    glyph_calls=0;
    uint8_t glyph[9]={0,0,4,14,21,4,4,4,0};
    assert(command(0x16,glyph,9)==0&&glyph_calls==1);
    glyph[8]=32;assert(command(0x16,glyph,9)==1&&glyph_calls==1);
    glyph[8]=0;glyph[0]=8;assert(command(0x16,glyph,9)==1&&glyph_calls==1);
    uint8_t batch[17]={6};batch[16]=32;
    assert(command(0x16,batch,17)==1&&glyph_calls==1);
    batch[16]=0;assert(command(0x16,batch,17)==0&&glyph_calls==3);
    batch[0]=7;assert(command(0x16,batch,17)==1&&glyph_calls==3);
    assert(command(0x16,batch,16)==1&&glyph_calls==3);
    uint8_t cells[]={38,0,7,255,32};
    assert(command(0x17,cells,5)==0&&cell_calls==1);
    cells[2]=8;assert(command(0x17,cells,5)==1&&cell_calls==1);
    cells[2]=7;cells[0]=79;assert(command(0x17,cells,5)==1&&cell_calls==1);
    panel_keys keys={0};panel_analogs a={0};
    uint8_t vector[206];uint32_t rng=1;
    for(unsigned round=0;round<100;round++) {
        for(unsigned j=0;j<sizeof vector;j++) {rng=rng*1664525u+1013904223u;vector[j]=rng>>24;}
        for(unsigned n=0;n<=sizeof vector;n++) assert(wire_crc32(vector,n)==crc32(vector,n));
    }
    assert(wire_crc32((const uint8_t*)"123456789",9)==0xcbf43926u);
    session=1;stream_ready=1;link_baud=38400;
    uint8_t enable=1;assert(command(7,&enable,1)==0);
    for(us=0;us<1000000;us++) {
        if(us%5000==0) {a.tbar=(us/5000)*17;a.x=(uint8_t)(us/5000);a.y=255-a.x;link_sample(&keys,&a,us/1000);}
        link_poll(us/1000);
    }
    assert(qn==0&&frames[0x82]==0&&frames[0x85]==0);
    assert(frames[0x83]>=120&&frames[0x83]<=143);
    assert(frames[0x84]>=20&&frames[0x84]<=25);
    assert(bytes<3840);
    printf("split analog UART model: T-bar %u Hz, joystick %u Hz, %u bytes/s\n",frames[0x83],frames[0x84],bytes);
    /* Edge queue still preserves every button transition during analog motion. */
    keys.chip31[0]=1;link_sample(&keys,&a,1001);keys.chip31[0]=0;link_sample(&keys,&a,1006);
    assert(qn==2&&queue[qr].data[4]==1&&queue[(qr+1)%QUEUE].data[4]==0);
    const unsigned tbar_before=frames[0x83],joy_before=frames[0x84];
    for(;us<1600000;us++) link_poll(us/1000);
    assert(frames[0x86]==1&&diag_samples==202&&diag_tbar>=199);
    assert(frames[0x85]>=1&&waiting&&qn==2); // reliable edge waits for ACK
    assert(frames[0x83]>tbar_before&&frames[0x83]<=tbar_before+2);
    assert(frames[0x84]>joy_before&&frames[0x84]<=joy_before+2); // idle final-value repair
    qn=0;waiting=0;tn=tp=0;split_analog=0;analog_at=0;
    a.tbar=4095;link_sample(&keys,&a,1100);assert(qn==1); // legacy fallback
    /* A free 64-byte driver queue gets the entire 20-byte analog frame in the
       same call that builds it, not 16 bytes on a subsequent main-loop pass. */
    qn=0;waiting=0;tn=tp=used=0;split_analog=1;bulk=1;diag_at=2000;
    fast_at[0]=0;latest[27]=0;latest[28]=42;
    unsigned before=frames[0x83];link_poll(2000);
    assert(frames[0x83]==before+1&&tn==tp);
    puts("analog scheduler PASS: separate lossy axes, reliable buttons, legacy mode");
}
