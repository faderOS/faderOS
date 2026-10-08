#include "board.h"
#include "link.h"
#include "lcd_icons.h"
#include <stddef.h>
#include <string.h>
#define LIMIT 192
#define SNAP 55
#define QUEUE 16
/* Same CRC-32/ISO-HDLC as image.c; nibble lookup reduces per-frame CPU
   work without changing the resident/image validator or wire format. */
static uint32_t wire_crc32(const uint8_t *p,unsigned n)
{
    static const uint32_t table[16]={
        0x00000000u,0x1db71064u,0x3b6e20c8u,0x26d930acu,
        0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,
        0xedb88320u,0xf00f9344u,0xd6d6a3e8u,0xcb61b38cu,
        0x9b64c2b0u,0x86d3d2d4u,0xa00ae278u,0xbdbdf21cu};
    uint32_t c=0xffffffffu;
    while(n--) {c^=*p++;c=(c>>4)^table[c&15];c=(c>>4)^table[c&15];}
    return ~c;
}
static uint16_t u16(const uint8_t *p) { return (p[0]<<8)|p[1]; }
static uint32_t u32(const uint8_t *p) { return ((uint32_t)u16(p)<<16)|u16(p+2); }
static void p16(uint8_t *p,uint16_t v) { p[0]=v>>8; p[1]=v; }
static void p32(uint8_t *p,uint32_t v) { p16(p,v>>16); p16(p+2,v); }
static uint8_t rx[210],tx[210],latest[SNAP];
static unsigned rn,discard,tn,tp;
static uint32_t baud_target,baud_at;
static unsigned update_pending;
static unsigned baud_phase; /* 1: drain ACK, 2: 200ms guard, 3: await confirmation */
uint32_t link_baud=9600;
static unsigned split_analog;
extern volatile uint32_t serial_errors;
static uint32_t diag_at,diag_polls,diag_samples,diag_bytes,diag_tbar,diag_joy,diag_gap;
static uint16_t diag_seq;
static uint32_t sample_at,fast_at[2];
static uint16_t fast_seq[2];
static uint8_t fast_sent[4];
static uint32_t session,alive,analog_at,retry_at,command_crc;
static uint16_t command_seq,event_seq;
static uint8_t reply_type,reply_status,cached_status,reported_analog[28];
static uint16_t reply_seq;
static panel_keys low,high;
static uint32_t encoders[6];
static struct { uint16_t seq; uint8_t data[SNAP]; } queue[QUEUE];
static unsigned qr,qn;
static int waiting,stream_ready;
static uint8_t hello_payload[59];
/* Schema 1, NUL-padded version[16], build date[12]. */
static const struct { uint8_t schema; char version[16],date[12]; } firmware_info={1,BKDS_LINK_VERSION,__DATE__};
_Static_assert(sizeof firmware_info==29,"firmware info wire layout");
/* Valid logical button/LED coordinates, catalog revision 1. */
static const uint8_t masks[23]={255,15,255,239,255,239,255,239,255,255,255,255,255,255,255,223,31,31,31,31,63,127,63};
static void safe_outputs(void)
{
    memset(&low,0,sizeof low); memset(&high,0,sizeof high);
    ky_blank();
    ky_planes(&low,&high); ky_indicators(0); segments_test(15,0);
    gate_clear(PANEL_BUZZER);
    lcd_line(0,"faderOS BKDS-2010 FW " BKDS_LINK_VERSION); lcd_line(1,"WAITING FOR HOST");
}
static void confirm_host(void)
{
    /* Only the handshake owns this status message; later host LCD writes win. */
    if (!stream_ready) {
        lcd_line(0,"faderOS BKDS-2010 FW " BKDS_LINK_VERSION);
        lcd_line(1,"HOST CONNECTED");
    }
    stream_ready=1;
}
static void disconnect(void)
{
    if (link_baud!=9600 || baud_phase) serial_set_baud(9600);
    update_pending=0;
    link_baud=9600; baud_phase=0; rn=discard=tn=tp=0;
    split_analog=0; session=0; qn=0; waiting=stream_ready=0; reply_type=0; safe_outputs();
}
void link_init(void) { safe_outputs(); tx[0]=0; tn=1; serial_link_init(); }
static void enqueue(void)
{
    if (!session || update_pending) return;
    if (qn==QUEUE) { disconnect(); return; }
    unsigned i=(qr+qn)%QUEUE;
    memcpy(reported_analog,latest+27,28);
    event_seq=event_seq==65535 ? 1 : event_seq+1;
    queue[i].seq=event_seq; memcpy(queue[i].data,latest,SNAP); qn++;
}
void link_sample(const panel_keys *keys,const panel_analogs *a,uint32_t now)
{
    uint8_t valid[23];
    for (unsigned i=0;i<23;i++) valid[i]=((const uint8_t *)keys)[i]&masks[i];
    int buttons=memcmp(latest+4,valid,23)!=0;
    if(split_analog) {
        diag_samples++;
        uint32_t gap=now-sample_at;if(gap>diag_gap) diag_gap=gap;
        if(u16(latest+27)!=a->tbar) diag_tbar++;
        if(latest[29]!=a->x||latest[30]!=a->y) diag_joy++;
    }
    sample_at=now; p32(latest,now); memcpy(latest+4,valid,23);
    p16(latest+27,a->tbar); latest[29]=a->x; latest[30]=a->y;
    for (unsigned i=0;i<6;i++) { encoders[i]+=a->delta[i]; p32(latest+31+4*i,encoders[i]); }
    /* Button transitions never coalesce. Encoder totals survive analog coalescing. */
    if (buttons) enqueue();
    else if ((uint32_t)(now-analog_at)>=(split_analog?40u:100u) && !qn &&
             (split_analog?memcmp(reported_analog+4,latest+31,24):memcmp(reported_analog,latest+27,28))) enqueue();
    if (buttons || qn) analog_at=now;
}
static void packet(uint8_t type,uint16_t seq,const uint8_t *payload,unsigned len)
{
    uint8_t raw[206]; raw[0]=1; raw[1]=type; p16(raw+2,seq);
    p32(raw+4,session); p16(raw+8,len); memcpy(raw+10,payload,len);
    p32(raw+10+len,wire_crc32(raw,10+len));
    unsigned pos=1,mark=0,code=1;
    for (unsigned i=0;i<len+14;i++) {
        if (raw[i]) { tx[pos++]=raw[i]; code++; }
        else { tx[mark]=code; mark=pos++; code=1; }
    }
    tx[mark]=code; tx[pos++]=0; tp=0; tn=pos;
}
static uint8_t command(uint8_t type,const uint8_t *p,unsigned n)
{
    if (type==8) return n?1:0;
    if (type==0x30) {
        if (n!=4 || u32(p)!=0x55504431u || baud_phase || link_baud!=9600) return 1;
        update_pending=1; return 0;
    }
    if (update_pending) return 1;
    if (type==4) {
        if (n!=4 || baud_phase || link_baud!=9600) return 1;
        uint32_t rate=u32(p);
        if ((rate!=19200 && rate!=38400 && rate!=76800) || *(volatile uint8_t *)0xfffd8d!=26) return 1;
        baud_target=rate; baud_phase=1; return 0;
    }
    if (type==6) {
        if (n!=4 || baud_phase!=3 || u32(p)!=link_baud) return 1;
        baud_phase=0; return 0;
    }
    if (baud_phase) return 1;
    if (type==7) {
        if(n!=1||p[0]!=1) return 1;
        split_analog=1;memcpy(fast_sent,latest+27,4);
        diag_at=sample_at;diag_polls=diag_samples=diag_bytes=diag_tbar=diag_joy=diag_gap=0;diag_seq=0;
        fast_at[0]=fast_at[1]=0;fast_seq[0]=fast_seq[1]=0;return 0;
    }
    if (type==2) return 0;
    if (type==3 && !n) { enqueue(); return 0; }
    if (type==0x10) {
        if (!n || (n&1)) return 1;
        panel_keys l=low,h=high;
        for (unsigned i=0;i<n;i+=2) {
            unsigned id=p[i],state=p[i+1],r=id/8,b=1u<<(id%8);
            if (id>=184 || !(masks[r]&b) || state>2) return 1;
            ((uint8_t *)&l)[r]&=~b; ((uint8_t *)&h)[r]&=~b;
            if (state==1) ((uint8_t *)&l)[r]|=b;
            if (state==2) ((uint8_t *)&h)[r]|=b;
        }
        if (!ky_planes(&l,&h)) return 4;
        low=l; high=h; return 0;
    }
    if (type==0x11) {
        if (n!=41 || p[0]>1) return 1;
        for (unsigned i=1;i<n;i++) if (p[i]<32 || p[i]>126) return 1;
        char text[41]; memcpy(text,p+1,40); text[40]=0; lcd_line(p[0],text); return 0;
    }
    if (type==0x15) {
        if (n<2 || p[0]>=80 || n-1>80u-p[0]) return 1;
        for (unsigned i=1;i<n;i++) if (p[i]<32 || p[i]>126) return 1;
        lcd_patch(p[0],p+1,n-1); return 0;
    }
    if (type==0x16) {
        if(n<9||n>65||(n-1)%8||p[0]>=8||(n-1)/8>8u-p[0]) return 1;
        for(unsigned i=1;i<n;i++) if(p[i]>31) return 1;
        for(unsigned i=0;i<(n-1)/8;i++) lcd_glyph(p[0]+i,p+1+i*8);
        return 0;
    }
    if (type==0x18) {
        if(n<2||n>9||p[0]>=8||n-1>8u-p[0]) return 1;
        for(unsigned i=1;i<n;i++) if(p[i]<1||p[i]>8) return 1;
        for(unsigned i=1;i<n;i++) lcd_glyph(p[0]+i-1,lcd_icons[p[i]-1]);
        return 0;
    }
    if (type==0x17) {
        if(n<2||p[0]>=80||n-1>80u-p[0]) return 1;
        for(unsigned i=1;i<n;i++) if(p[i]>=8&&p[i]<32) return 1;
        lcd_patch(p[0],p+1,n-1);return 0;
    }
    if (type==0x12) {
        if (n!=7 || p[6]>7) return 1;
        for (unsigned i=0;i<6;i++) if (p[i]&0x70) return 1;
        segments_raw(p,p[6]); return 0;
    }
    if (type==0x13 && n==1 && !(p[0]&15)) { ky_indicators(p[0]); return 0; }
    if (type==0x14 && n==1 && p[0]<=1) {
        /* Touch only the buzzer: the gate and the heartbeat bits share the
           latch and the periodic ISRs own their own bits in it. */
        if (p[0]) gate_set(PANEL_BUZZER); else gate_clear(PANEL_BUZZER);
        return 0;
    }
    return 1;
}
static void receive(uint32_t now)
{
    uint8_t raw[206]; unsigned i=0,n=0;
    while (i<rn) {
        unsigned code=rx[i++]; if (!code || i+code-1>rn) return;
        for (unsigned k=1;k<code;k++) { if (n==sizeof raw) return; raw[n++]=rx[i++]; }
        if (code!=255 && i<rn) { if (n==sizeof raw) return; raw[n++]=0; }
    }
    if (n<14 || raw[0]!=1 || u16(raw+8)!=n-14 || n-14>LIMIT) return;
    uint32_t crc=u32(raw+n-4); if (crc!=wire_crc32(raw,n-4)) return;
    unsigned type=raw[1],len=n-14; uint16_t seq=u16(raw+2); uint32_t s=u32(raw+4);
    if (type==1 && !seq && !len && s && !baud_phase) {
        if (s!=session) {
            qn=qr=0; waiting=stream_ready=0; event_seq=command_seq=0; session=s;
            split_analog=0; safe_outputs(); p16(hello_payload,1); p16(hello_payload+2,0x07ff);
            memcpy(hello_payload+4,latest,SNAP);
        }
        alive=now; reply_type=0x81; reply_seq=0; return;
    }
    if (!session || session!=s) return;
    if (type==5 && !len) {
        if (!seq) { confirm_host(); alive=now; return; }
        if (qn && waiting && seq==queue[qr].seq) { qr=(qr+1)%QUEUE; qn--; waiting=0; }
        alive=now; return;
    }
    if (reply_type) return; /* Stop-and-wait commands; host retries if necessary. */
    reply_type=0x80; reply_seq=seq;
    if (seq && seq==command_seq && crc==command_crc) { reply_status=cached_status; if(type==8&&!reply_status)reply_type=0x87; alive=now; confirm_host(); return; }
    if (!seq || seq!=(command_seq==65535 ? 1 : command_seq+1)) { reply_status=3; return; }
    command_seq=seq; command_crc=crc; alive=now; confirm_host();
    reply_status=command(type,raw+10,len); cached_status=reply_status;
    if(type==8&&!reply_status)reply_type=0x87;
}
static void fast_analog(uint32_t now)
{
    /* 20 wire bytes per sample (COBS + CRC). No ACK/replay: latest value wins.
       Periodic 500ms refresh repairs a lost final sample without a backlog. */
    unsigned periods[2]={link_baud>=38400?7u:link_baud>=19200?16u:32u,
                         link_baud>=38400?40u:link_baud>=19200?80u:160u};
    for(unsigned i=0;i<2;i++) {
        const uint8_t *value=latest+27+2*i;
        uint32_t elapsed=now-fast_at[i];
        if(elapsed<periods[i] || (!memcmp(fast_sent+2*i,value,2)&&elapsed<500)) continue;
        uint8_t payload[4];p16(payload,(uint16_t)sample_at);memcpy(payload+2,value,2);
        fast_seq[i]++;packet((uint8_t)(0x83+i),fast_seq[i],payload,4);
        memcpy(fast_sent+2*i,value,2);fast_at[i]=now;return;
    }
}
static void transmit_pending(void)
{
    /* Queue a complete small frame in one visit. Never wait for UART space;
       long replies continue later, without creating a second pending frame. */
    unsigned budget=64;
    while(tp<tn && budget-- && serial_write(tx[tp])) {tp++;if(split_analog)diag_bytes++;}
}
void link_poll(uint32_t now)
{
    int b; unsigned budget=64;
    if(split_analog) diag_polls++;
    while (budget-- && (b=serial_read())>=0) {
        if (!b) { if (!discard && rn) receive(now); rn=discard=0; }
        else if (!discard) { if (rn==sizeof rx) { discard=1; rn=0; } else rx[rn++]=b; }
    }
    if (!update_pending && session && (uint32_t)(now-alive)>5000) disconnect();
    if (baud_phase==2) {
        if ((uint32_t)(now-baud_at)>=200) {
            if (!serial_set_baud(baud_target)) { disconnect(); return; }
            link_baud=baud_target; baud_phase=3; baud_at=now; rn=discard=0;
        }
        return;
    }
    if (baud_phase==3 && (uint32_t)(now-baud_at)>=1500) { disconnect(); return; }
    if (tp<tn) {transmit_pending();return;}
    /* One frame at a time, including bytes still queued in the TX ISR.
       Keep newest analog values instead of building a serial backlog. */
    if (!serial_tx_empty()) return;
    if (update_pending && !reply_type && serial_tx_empty()) update_enter();
    if (baud_phase==1 && !reply_type && serial_tx_empty()) { baud_phase=2; baud_at=now; return; }
    if (reply_type) {
        if (reply_type==0x81) {
            packet(0x81,0,hello_payload,59);
        } else if(reply_type==0x87) packet(0x87,reply_seq,(const uint8_t *)&firmware_info,29);
        else packet(0x80,reply_seq,&reply_status,1);
        reply_type=0;
    } else if (!update_pending && !baud_phase && session && stream_ready && qn && (!waiting || (uint32_t)(now-retry_at)>=500)) {
        if(split_analog) {
            uint8_t controls[51];memcpy(controls,queue[qr].data,27);memcpy(controls+27,queue[qr].data+31,24);
            packet(0x85,queue[qr].seq,controls,sizeof controls);
        } else packet(0x82,queue[qr].seq,queue[qr].data,SNAP);
        retry_at=now; waiting=1;
    } else if (!update_pending&&!baud_phase&&session&&stream_ready&&split_analog) {
        if((uint32_t)(now-diag_at)>=1000) {
            uint8_t data[32];p32(data,now);p32(data+4,diag_polls);p32(data+8,diag_samples);
            p32(data+12,diag_bytes);p32(data+16,diag_tbar);p32(data+20,diag_joy);
            p32(data+24,serial_errors);p32(data+28,diag_gap);
            packet(0x86,++diag_seq,data,sizeof data);diag_at=now;
        } else fast_analog(now);
    }
    transmit_pending(); /* Start a freshly encoded frame in this same visit. */
}
