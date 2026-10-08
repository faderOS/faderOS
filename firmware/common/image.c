#include "board.h"
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | p[3];
}
/* CRC-32/ISO-HDLC; also used by Python zlib.crc32 in the image packer. */
uint32_t crc32(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xffffffffu;
    while (size--) {
        crc ^= *data++;
        for (unsigned bit=0; bit<8; ++bit)
            crc = (crc>>1) ^ ((crc&1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}
int image_valid(const uint8_t *p, uint32_t capacity)
{
    if (capacity < HEADER_SIZE || be32(p)!=IMAGE_MAGIC || be32(p+4)!=1u)
        return 0;
    uint32_t size=be32(p+8);
    if (size<2 || size>capacity-HEADER_SIZE || (size&1u)) return 0;
    /* This first format has one fixed entry and no optional flags. */
    if (be32(p+12)!=APP_ENTRY || be32(p+20) || be32(p+24) || be32(p+28)) return 0;
    return crc32(p+HEADER_SIZE,size)==be32(p+16);
}
