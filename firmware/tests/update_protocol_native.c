/* Native framing harness only: flash engine tested separately; no MMIO. */
#include <stdint.h>
#include <string.h>
#include "../update/main.c"
static uint8_t output[4096];
static unsigned output_size,commands,resets;
void update_hw_init(void) {}
void update_tx(uint8_t c) { if (output_size<sizeof output) output[output_size++]=c; }
void update_drain(void) {}
int update_rx(void) { return -1; }
uint32_t update_ms(void) { return 0; }
void update_session_reset(void) { ++resets; }
unsigned update_mask(void) { return 3; }
unsigned update_done(void) { return 0; }
int update_device_ok(void) { return 1; }
int update_command(unsigned type,const uint8_t *p,unsigned n) { (void)type;(void)p;(void)n;++commands;return 0; }
void test_reset(void) {
    session=last_crc=last_seq=size=discard=reply_n=output_size=commands=resets=0;
}
unsigned test_commands(void) { return commands; }
unsigned test_resets(void) { return resets; }
unsigned test_exchange(const uint8_t *p,unsigned n,uint8_t *out) {
    output_size=0;
    for (unsigned i=0;i<n;i++) {
        if (!p[i]) { if (size&&!discard) receive();size=discard=0; }
        else if (!discard) { if (size==sizeof wire) { discard=1;size=0; } else wire[size++]=p[i]; }
    }
    memcpy(out,output,output_size);return output_size;
}

void update_display_init(void) {}
void update_display(const char *p,unsigned s,unsigned n) { (void)p;(void)s;(void)n; }
void update_display_result(unsigned t,const uint8_t *p,unsigned n,unsigned s) { (void)t;(void)p;(void)n;(void)s; }
