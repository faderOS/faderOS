#include <stddef.h>
/* GCC can emit these helpers for aggregate initialization even freestanding. */
void *memset(void *dst, int value, size_t n)
{
    unsigned char *p=dst;
    while (n--) *p++=(unsigned char)value;
    return dst;
}
void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d=dst;
    const unsigned char *s=src;
    while (n--) *d++=*s++;
    return dst;
}
int memcmp(const void *a,const void *b,size_t n)
{
    const unsigned char *x=a,*y=b;
    while (n--) { if (*x!=*y) return *x-*y; x++; y++; }
    return 0;
}
