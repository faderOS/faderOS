// Isolated from libc <termios.h>: Linux termios2 has a different struct ABI.
#include <asm/termbits.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <cstdio>
bool custom_baud_matches(unsigned requested,unsigned rx,unsigned tx)
{
    // Toshiba TMP68301: 16 MHz / 26 / 1 / 8 = 76923.0769 baud.
    // Physical PL2303 readback is 76923 for nominal 76800. Accept this
    // measured quantization explicitly, not arbitrary silent driver rounding.
    const auto matches=[requested](unsigned actual) {
        return actual==requested || (requested==76800 && actual==76923);
    };
    return matches(rx) && matches(tx);
}
int set_custom_baud(int fd,unsigned rate)
{
    termios2 t{};
    if(ioctl(fd,TCGETS2,&t)<0) { std::perror("TCGETS2 before change"); return -1; }
    t.c_cflag=(t.c_cflag&~(CBAUD|CIBAUD|CRTSCTS))|BOTHER|CLOCAL|CREAD;
    t.c_ispeed=t.c_ospeed=rate;
    if(ioctl(fd,TCSETS2,&t)<0) { std::perror("TCSETS2 custom rate"); return -1; }
    if(ioctl(fd,TCGETS2,&t)<0) { std::perror("TCGETS2 after change"); return -1; }
    if(t.c_ispeed!=rate||t.c_ospeed!=rate) {
        std::fprintf(stderr,"SCI2 driver readback: requested %u, RX %u, TX %u baud\n",
                     rate,unsigned(t.c_ispeed),unsigned(t.c_ospeed));
    }
    if(!custom_baud_matches(rate,t.c_ispeed,t.c_ospeed)) { errno=EINVAL; return -1; }
    return 1;
}
