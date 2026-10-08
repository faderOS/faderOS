#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "../update/engine.h"
static uint8_t flash[7*UPDATE_SECTOR],target[7*UPDATE_SECTOR],buffer[UPDATE_SECTOR];
static unsigned writes,order[16];
static int device=1,failed;
uint8_t *update_buffer(void) { return buffer; }
const uint8_t *update_flash(unsigned n) { assert(n<7);return flash+n*UPDATE_SECTOR; }
int update_device_ok(void) { return device; }
int update_flash_program(unsigned n,const uint8_t *p) {
    assert(n<7);order[writes++]=n;
    memcpy(flash+n*UPDATE_SECTOR,p,failed?UPDATE_SECTOR/2:UPDATE_SECTOR);
    return failed?-1:0;
}
static void put(uint8_t *p,uint32_t v) { p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
static void begin(unsigned mask) {
    uint8_t plan[29];plan[0]=mask;
    for (unsigned i=0;i<7;i++) put(plan+1+4*i,crc32(target+i*UPDATE_SECTOR,UPDATE_SECTOR));
    assert(update_command(0x40,plan,sizeof plan)==0);
}
static void stage(unsigned n) {
    uint8_t sector=n;assert(update_command(0x41,&sector,1)==0);
    for (uint32_t i=0;i<UPDATE_SECTOR;i+=128) {
        uint8_t chunk[132];put(chunk,i);memcpy(chunk+4,target+n*UPDATE_SECTOR+i,128);
        assert(update_command(0x42,chunk,sizeof chunk)==0);
    }
}
static unsigned commit(unsigned n) { uint8_t sector=n;return update_command(0x43,&sector,1); }
int main(void) {
    memset(target,255,sizeof target);memset(flash,255,sizeof flash);
    put(target,0x46fc2700);
    uint8_t *app=target+UPDATE_SECTOR;
    put(app,IMAGE_MAGIC);put(app+4,1);put(app+8,2);put(app+12,APP_ENTRY);
    app[32]=0x4e;app[33]=0x71;put(app+16,crc32(app+32,2));memset(app+20,0,12);
    begin(3);
    uint8_t zero=0,seven=7,chunk[5]={0,2,0,0,5};
    assert(update_command(0x41,&zero,1)==4);assert(update_command(0x41,&seven,1)==1);
    assert(update_command(0x44,0,0)==4);assert(update_command(0x45,0,0)==4);
    stage(1);assert(update_command(0x46,0,0)==0);assert(writes==0);
    assert(update_command(0x42,chunk,5)==1); /* beyond end */
    buffer[100]^=1;assert(commit(1)==2);assert(writes==0);buffer[100]^=1;
    update_session_reset();assert(commit(1)==1);assert(writes==0);
    stage(1);device=0;assert(commit(1)==3);assert(writes==0);device=1;
    failed=1;assert(commit(1)==3);assert(writes==1);assert(update_done()==0);
    assert(update_command(0x41,&zero,1)==4);failed=0;
    begin(3);stage(1);assert(commit(1)==0);assert(update_done()==2);
    stage(0);flash[UPDATE_SECTOR+32]^=1;assert(commit(0)==2);
    flash[UPDATE_SECTOR+32]^=1;assert(commit(0)==0);
    assert(order[writes-1]==0);assert(update_done()==3);
    assert(update_command(0x44,0,0)==0);assert(update_command(0x45,0,0)==0);
    assert(memcmp(flash,target,sizeof flash)==0);
    unsigned before=writes;begin(3);stage(1);assert(commit(1)==0);stage(0);assert(commit(0)==0);
    assert(writes==before); /* same image never needs erase */
    /* Bad manifest mask and header cannot request arbitrary extra sectors. */
    uint8_t bad[29]={0x83};assert(update_command(0x40,bad,29)==1);
    begin(7);stage(1);assert(commit(1)==1);
    assert(writes==before);
    puts("UPDATE ENGINE PASS: CRC before erase, RAM-only probe, bounds, order, disconnect, partial failure, recovery, skip identical sectors, final validation");
}
