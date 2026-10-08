#include "display.h"
#include "panel.h"
/* Linked into the independent RAM image. No busy reads, dynamic allocation or
   flash strings. Render only changed cells; caller is between request and ACK,
   when the stop-and-wait host is not transmitting another data frame. */
static char shown[80];
static unsigned sector=8;
static void line(unsigned row,const char *text)
{
    unsigned address=80;
    for (unsigned i=0;i<40;i++) {
        char c=*text ? *text++ : ' ';
        if (shown[row*40+i]==c) continue;
        if (address!=i) lcd_write_byte(0x80+row*0x40+i,0);
        lcd_write_byte((uint8_t)c,2);
        shown[row*40+i]=c;address=i+1;
    }
}
void update_display(const char *phase,unsigned index,unsigned percent)
{
    char text[41];unsigned n=0;
    while (*phase && n<24) text[n++]=*phase++;
    if (index<7) { text[n++]=' ';text[n++]='S';text[n++]='0'+index; }
    if (percent<=100) {
        text[n++]=' ';
        if (percent==100) { text[n++]='1';text[n++]='0';text[n++]='0'; }
        else { unsigned tens=0;while (percent>=10) { percent-=10;tens++; }
            if (tens) text[n++]='0'+tens;
            text[n++]='0'+percent; }
        text[n++]='%';
    }
    text[n]=0;line(1,text);
}
void update_display_init(void)
{
    lcd_init();
    line(0,"FIRMWARE UPDATE - KEEP POWER ON");
    update_display("WAITING FOR HOST",8,101);
}
void update_display_result(unsigned type,const uint8_t *p,unsigned n,unsigned status)
{
    if (status) {
        const char *message=status==2 ? "ERROR: CRC / IMAGE" :
            status==3 ? "ERROR: FLASH" : "ERROR: COMMAND / ORDER";
        update_display(message,sector,101);return;
    }
    if (type==0x40) { sector=8;update_display("PLAN ACCEPTED",8,101); }
    if (type==0x41 && n==1) { sector=p[0];update_display("RECEIVING",sector,0); }
    if (type==0x42 && n>=5) {
        uint32_t offset=((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
        /* Percentage of sector address covered, including implicit FF gaps. */
        unsigned percent=((offset+n-4)*100u)>>17;
        update_display("RECEIVING",sector,percent);
    }
    if (type==0x46) update_display("RAM CRC OK - NO WRITE",sector,101);
    if (type==0x43) update_display("SECTOR VERIFIED",sector,100);
    if (type==0x44) update_display("UPDATE VERIFIED",8,101);
    if (type==0x45) update_display("STARTING FIRMWARE",8,101);
}
