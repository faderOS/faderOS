#include "panel.h"
#define R(a) (*(volatile uint8_t *)(a))
static void row(uint32_t data, unsigned r, uint8_t value)
{
    R(data+2)=0x60u+r;
    R(data)=value;
    /* Idle after each lamp write so a later scan start cannot clock the
       data bus into a still-selected LED row. */
    R(data+2)=0xe0;
}
void ky_init(void)
{
    /* Sony flash 6397E and EPROM 20D7B6, confirmed in disassembly.
       Configure each controller before any blank, lamp write or key scan.
       Do not rely on the EPROM having taken its diagnostic-init path. */
    static const uint32_t control[4]={0x800033,0x800013,0x800023,0x600003};
    static const uint8_t sequence[6]={0x04,0x22,0xd3,0x50,0xa0,0xe0};
    for (unsigned chip=0;chip<4;chip++)
        for (unsigned i=0;i<6;i++) R(control[chip])=sequence[i];
}
void ky_scan(panel_keys *k)
{
    /* Match Sony 615A0 MMIO order, including explicit control reads.
       Earlier variants without controller initialization are inconclusive.
       0xE0 belongs after the scan, not between 0x50 and the data reads. */
    uint8_t chip11[4],chip21[4];
    (void)R(0x800033); R(0x800033)=0x50;
    (void)R(0x800013); R(0x800013)=0x50;
    (void)R(0x800023); R(0x800023)=0x50;
    (void)R(0x600003); R(0x600003)=0x50;
    for (unsigned i=0;i<8;i++) {
        (void)R(0x800033); k->chip31[i]=R(0x800031);
        if (i<4) {
            (void)R(0x800013); chip11[i]=R(0x800011);
            (void)R(0x800023); chip21[i]=R(0x800021);
            (void)R(0x600003); k->ky306[i]=R(0x600001);
        }
    }
    (void)R(0x800033); R(0x800033)=0xe0;
    (void)R(0x800013); R(0x800013)=0xe0;
    (void)R(0x800023); R(0x800023)=0xe0;
    (void)R(0x600003); R(0x600003)=0xe0;
    /* Hardware rows 1 and 2 are swapped versus the LED latches. IDs stay
       in LED order, same as FUN_000450ce / FUN_00045188. */
    static const uint8_t order[4]={0,2,1,3};
    for (unsigned i=0;i<4;i++) {
        k->chip11[order[i]]=chip11[i];
        k->chip21[order[i]]=chip21[i];
    }
    k->ky308[0]=R(0x300001); k->ky308[1]=R(0x300011); k->ky308[2]=R(0x300017)&0x3f;
}
static void direct(const uint8_t *low, const uint8_t *high)
{
    static const struct { uint8_t reg,y,g; } map[19]={
        {0,2,1},{0,8,4},{0,32,16},{0,128,64},{1,2,1},{1,8,4},
        {2,64,0},{2,128,0},{4,1,0},{4,8,0},{4,4,0},{4,2,0},
        {3,128,64},{2,2,1},{2,8,4},{3,2,1},{3,8,4},{3,32,16},{2,32,16}};
    static const uint32_t regs[5]={0x300005,0x300009,0x300013,0x300015,0x300019};
    /* Physical scan bits differ from the LED slot order. */
    static const uint8_t inputs[19]={0,1,2,3,4,5,20,21,16,19,18,17,11,12,13,8,9,10,14};
    uint8_t bytes[5]={0};
    for (unsigned i=0;i<19;i++) {
        unsigned r=inputs[i]/8, bit=1u<<(inputs[i]%8);
        if (high[r]&bit) bytes[map[i].reg] |= map[i].g ? map[i].g : map[i].y;
        else if (low[r]&bit) bytes[map[i].reg] |= map[i].y;
    }
    for (unsigned i=0;i<5;i++) R(regs[i])=bytes[i];
}
void ky_feedback(const panel_keys *k, unsigned color)
{
    R(0x8000a1)=color==2 ? 0xf0 : 0;
    for (unsigned i=0;i<8;i++) row(0x800031,i,k->chip31[i]);
    for (unsigned i=0;i<4;i++) {
        row(0x800011,i,color==1 ? k->chip11[i] : 0);
        row(0x800011,i+4,color==2 ? k->chip11[i] : 0);
        row(0x800021,i,color==1 ? k->chip21[i] : 0);
        row(0x800021,i+4,color==2 ? k->chip21[i] : 0);
        row(0x600001,i,color==1 ? k->ky306[i] : 0);
        row(0x600001,i+4,color==2 ? k->ky306[i] : 0);
    }

    const uint8_t zero[3]={0};
    direct(color==1 ? k->ky308 : zero, color==2 ? k->ky308 : zero);
}
void ky_indicators(uint8_t mask) { R(0x8000b1)=mask&0xf0; }
void ky_blank(void)
{
    /* Sony FUN_0004418a / FUN_000440c4: 0x70 broadcast, then data zeros.
       Per-row 0x60 is the fallback if broadcast does not auto-index. */
    static const uint32_t chips[4]={0x800031u,0x800011u,0x800021u,0x600001u};
    static const uint32_t ky308[5]={0x300005u,0x300009u,0x300013u,0x300015u,0x300019u};
    R(0x8000a1)=0;
    R(0x8000b1)=0;
    for (unsigned c=0;c<4;c++) {
        R(chips[c]+2)=0x70;
        delay_us(1);
        for (unsigned i=0;i<8;i++) R(chips[c])=0;
        for (unsigned i=0;i<8;i++) { R(chips[c]+2)=0x60u+i; R(chips[c])=0; }
        R(chips[c]+2)=0xe0;
    }
    for (unsigned i=0;i<5;i++) R(ky308[i])=0;
}
void ky_lamp_test(unsigned color)
{
    panel_keys keys;
    uint8_t *p=(uint8_t *)&keys;
    for (unsigned i=0;i<sizeof keys;i++) p[i]=color ? 0xff : 0;
    ky_feedback(&keys,color==2 ? 2 : 1);
    ky_indicators(color ? 0xf0 : 0);
}

int ky_planes(const panel_keys *low,const panel_keys *high)
{
    unsigned colors=0;
    for (unsigned bank=0;bank<4;bank++) {
        unsigned r=bank*2;
        unsigned l=low->chip31[r]|(low->chip31[r+1]&15);
        unsigned h=high->chip31[r]|(high->chip31[r+1]&15);
        if (l && h) return 0; /* One shared color selector per bank. */
        if (h) colors|=0x80u>>bank;
    }
    R(0x8000a1)=colors;
    for (unsigned i=0;i<8;i++) row(0x800031,i,low->chip31[i]|high->chip31[i]);
    for (unsigned i=0;i<4;i++) {
        row(0x800011,i,low->chip11[i]); row(0x800011,i+4,high->chip11[i]);
        row(0x800021,i,low->chip21[i]); row(0x800021,i+4,high->chip21[i]);
        row(0x600001,i,low->ky306[i]); row(0x600001,i+4,high->ky306[i]);
    }
    direct(low->ky308,high->ky308);
    return 1;
}
