// Exercise the REAL Posix cache on a PTY with a partially successful custom change.
#define main picohost_main_not_run
#include "../../src/link/main_posix.cpp"
#undef main
#include <cassert>
int set_custom_baud(int fd,unsigned rate)
{
    assert(rate==76800);
    termios t{}; assert(tcgetattr(fd,&t)==0);
    cfsetispeed(&t,B19200);cfsetospeed(&t,B19200);
    assert(tcsetattr(fd,TCSANOW,&t)==0);
    errno=EINVAL; return -1; // Device changed, but validation rejected readback.
}
int main()
{
    int master=posix_openpt(O_RDWR|O_NOCTTY);assert(master>=0);
    assert(grantpt(master)==0);assert(unlockpt(master)==0);
    const char* path=ptsname(master);assert(path);
    {
        Posix port(path,"");assert(port.valid());
        assert(port.set_baud(9600)==1);
        assert(port.set_baud(76800)==-1);
        assert(port.set_baud(9600)==1);
        int observer=open(path,O_RDWR|O_NOCTTY);assert(observer>=0);
        termios t{};assert(tcgetattr(observer,&t)==0);
        assert(cfgetispeed(&t)==B9600&&cfgetospeed(&t)==B9600);
        close(observer);
    }
    close(master);
}
