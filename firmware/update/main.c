#include "engine.h"
#include "display.h"
#include "board.h"
extern void update_hw_init(void),update_tx(uint8_t),update_drain(void);
extern int update_rx(void);
extern uint32_t update_ms(void);
static uint8_t wire[210],raw[206],reply[40];
static uint32_t session,last_crc;
static uint16_t last_seq;
static unsigned size,discard,reply_n;
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void put32(uint8_t *p,uint32_t v) { p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
static void respond(unsigned type,unsigned seq,const uint8_t *data,unsigned n)
{
    uint8_t b[64],out[68];
    b[0]=1;b[1]=type;b[2]=seq>>8;b[3]=seq;put32(b+4,session);b[8]=0;b[9]=n;
    for (unsigned i=0;i<n;i++) b[10+i]=data[i];
    put32(b+10+n,crc32(b,10+n));
    unsigned mark=0,pos=1,code=1;
    for (unsigned i=0;i<14+n;i++) {
        if (b[i]) { out[pos++]=b[i];code++; }
        else { out[mark]=code;mark=pos++;code=1; }
    }
    out[mark]=code;out[pos++]=0;
    for (unsigned i=0;i<pos;i++) update_tx(out[i]);
    update_drain();
}
static void receive(void)
{
    unsigned i=0,n=0;
    while (i<size) {
        unsigned code=wire[i++];if (!code||i+code-1>size) return;
        for (unsigned k=1;k<code;k++) { if (n==sizeof raw) return;raw[n++]=wire[i++]; }
        if (code!=255&&i<size) { if (n==sizeof raw) return;raw[n++]=0; }
    }
    if (n<14||raw[0]!=1||((unsigned)raw[8]<<8|raw[9])!=n-14) return;
    uint32_t crc=be32(raw+n-4);if (crc!=crc32(raw,n-4)) return;
    unsigned type=raw[1],seq=((unsigned)raw[2]<<8)|raw[3],len=n-14;
    uint32_t nonce=be32(raw+4);
    if (type==1&&!seq&&!len&&nonce) {
        if (nonce!=session) { session=nonce;last_seq=0;update_session_reset(); }
        uint8_t hello[12]={0,1,0,0}; /* updater version; device, mask, done, sector bytes */
        hello[3]=update_device_ok();hello[4]=update_mask();hello[5]=update_done();
        hello[6]=0;hello[7]=0;put32(hello+8,SECTOR_SIZE);
        respond(0x91,0,hello,12);return;
    }
    if (!session||nonce!=session) return;
    if (seq&&seq==last_seq&&crc==last_crc) { respond(0x90,seq,reply,reply_n);return; }
    uint8_t error=5;
    if (!seq||seq!=(last_seq==65535?1u:(unsigned)last_seq+1u)) { respond(0x90,seq,&error,1);return; }
    last_seq=seq;last_crc=crc;
    if (type==0x43) update_display("CHECKING RAM / FLASH",len==1 ? raw[10] : 8,101);
    if (type==0x44) update_display("VERIFYING IMAGE",8,101);
    reply[0]=update_command(type,raw+10,len);reply_n=1;
    update_display_result(type,raw+10,len,reply[0]);
    respond(0x90,seq,reply,reply_n);
    if (type==0x45&&!reply[0]) {
        ((void (*)(void))FLASH_BASE)();
        for (;;) {}
    }
}
_Noreturn void update_main(void)
{
    update_hw_init();update_session_reset();update_display_init();
    const char *banner="\r\nBKDS UPDATE 1 READY 9600\r\n";
    while (*banner) update_tx(*banner++);
    update_tx(0);update_drain();
    uint32_t received_at=update_ms();
    for (;;) {
        int c=update_rx();
        /* A disconnected host can reconnect; never auto-erase or auto-boot. */
        if (size&&(uint32_t)(update_ms()-received_at)>1000) { size=discard=0; }
        if (c==-2) { size=0;discard=1; }
        if (c<0) continue;
        received_at=update_ms();
        if (!c) { if (size&&!discard) receive();size=discard=0; }
        else if (!discard) { if (size==sizeof wire) { size=0;discard=1; } else wire[size++]=c; }
    }
}
