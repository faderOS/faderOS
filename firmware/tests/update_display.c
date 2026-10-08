#include <assert.h>
#include <string.h>
#include "../update/display.h"
static char cells[128];
static unsigned cursor,writes;
void lcd_init(void) { memset(cells,' ',sizeof cells);cursor=0; }
void lcd_write_byte(unsigned char value,unsigned char rs)
{
    if (!rs) { assert(value&0x80);cursor=value&0x7f; }
    else { assert(cursor<104);cells[cursor++]=value;writes++; }
}
int main(void)
{
    update_display_init();
    assert(!memcmp(cells,"FIRMWARE UPDATE - KEEP POWER ON",30));
    assert(!memcmp(cells+64,"WAITING FOR HOST",16));
    unsigned before=writes;
    update_display("WAITING FOR HOST",8,101);assert(writes==before);
    unsigned char s=1;
    update_display_result(0x41,&s,1,0);
    assert(!memcmp(cells+64,"RECEIVING S1 0%",15));
    unsigned char data[132]={0,1,255,128};
    update_display_result(0x42,data,sizeof data,0);
    assert(!memcmp(cells+64,"RECEIVING S1 100%",17));
    update_display_result(0x43,&s,1,3);
    assert(!memcmp(cells+64,"ERROR: FLASH S1",15));
    update_display_result(0x44,0,0,0);
    assert(!memcmp(cells+64,"UPDATE VERIFIED",15));
    for (unsigned i=15;i<40;i++) assert(cells[64+i]==' ');
    return 0;
}
