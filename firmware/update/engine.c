#include "engine.h"
#include "board.h"
static uint32_t expected[7],cursor;
static unsigned mask,done,staged,complete;
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
unsigned update_mask(void) { return mask; }
unsigned update_done(void) { return done; }
void update_session_reset(void) { staged=8; cursor=0; }
int update_command(unsigned type,const uint8_t *p,unsigned n)
{
    if (type==0x40) { /* BEGIN plan: contiguous mask, sector CRC32s 0..6. */
        if (n!=29 || p[0]<3 || p[0]>127 || (p[0]&(p[0]+1))) return 1;
        if (!update_device_ok()) return 3;
        mask=p[0]; done=complete=0; update_session_reset();
        for (unsigned i=0;i<7;i++) expected[i]=be32(p+1+4*i);
        return 0;
    }
    if (type==0x41) { /* STAGE: RAM only, all bytes start at FF. */
        if (n!=1 || !mask || p[0]>6 || !(mask&(1u<<p[0])) || complete) return 1;
        if (!p[0] && (done&(mask&~1u))!=(mask&~1u)) return 4;
        staged=p[0]; cursor=0;
        uint8_t *b=update_buffer();
        for (uint32_t i=0;i<UPDATE_SECTOR;i++) b[i]=255;
        return 0;
    }
    if (type==0x42) { /* DATA: increasing offsets, gaps stay FF. */
        if (n<5 || n>132 || staged>6) return 1;
        uint32_t at=be32(p);
        if (at<cursor || at>UPDATE_SECTOR || n-4>UPDATE_SECTOR-at) return 1;
        for (unsigned i=4;i<n;i++) update_buffer()[at+i-4]=p[i];
        cursor=at+n-4; return 0;
    }
    if (type==0x46) { /* Validate staged bytes without touching flash. */
        if (n || staged>6) return 1;
        return crc32(update_buffer(),UPDATE_SECTOR)==expected[staged] ? 0 : 2;
    }
    if (type==0x43) { /* COMMIT one fully CRC-validated sector. */
        if (n!=1 || staged>6 || p[0]!=staged) return 1;
        unsigned sector=staged;
        const uint8_t *b=update_buffer();
        if (crc32(b,UPDATE_SECTOR)!=expected[sector]) return 2;
        /* First app sector contains header. Do not accept a plan missing any
           sector covered by the declared image. No pointer outside app region. */
        if (sector==1) {
            uint32_t size=be32(b+8);
            if (be32(b)!=IMAGE_MAGIC || be32(b+4)!=1 || be32(b+12)!=APP_ENTRY ||
                be32(b+20)||be32(b+24)||be32(b+28)||size<2||size%2||size>APP_CAPACITY-HEADER_SIZE) return 1;
            unsigned count=(size+HEADER_SIZE+UPDATE_SECTOR-1)/UPDATE_SECTOR;
            if (mask!=((1u<<(count+1))-1)) return 1;
        }
        if (!sector) {
            if (be32(b)!=0x46fc2700u || !image_valid(update_flash(1),APP_CAPACITY)) return 2;
            /* Recheck every application sector before erasing the resident. */
            for (unsigned i=1;i<7;i++)
                if ((mask&(1u<<i)) && crc32(update_flash(i),UPDATE_SECTOR)!=expected[i]) return 2;
        }
        if (!update_device_ok()) return 3;
        /* Avoid needless erases; ACK loss is also handled by frame dedup. */
        if (crc32(update_flash(sector),UPDATE_SECTOR)!=expected[sector] &&
            update_flash_program(sector,b)!=0) return 3;
        if (crc32(update_flash(sector),UPDATE_SECTOR)!=expected[sector]) return 3;
        done|=1u<<sector; staged=8; return 0;
    }
    if (type==0x44) { /* FINALIZE: no automatic boot; ACK is retryable. */
        if (n || !mask || done!=mask) return 4;
        for (unsigned i=0;i<7;i++)
            if ((mask&(1u<<i)) && crc32(update_flash(i),UPDATE_SECTOR)!=expected[i]) return 2;
        if (!image_valid(update_flash(1),APP_CAPACITY)) return 2;
        complete=1; return 0;
    }
    if (type==0x45) return n==0 && complete ? 0 : 4;
    return 1;
}
