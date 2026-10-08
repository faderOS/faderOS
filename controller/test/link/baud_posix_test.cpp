#include <cassert>
#include <cstdlib>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
extern int set_custom_baud(int,unsigned);
extern bool custom_baud_matches(unsigned,unsigned,unsigned);
int main()
{
    assert(custom_baud_matches(76800,76800,76800));
    assert(custom_baud_matches(76800,76923,76923));
    assert(!custom_baud_matches(76800,76923,115200));
    assert(!custom_baud_matches(76800,38400,76923));
    assert(!custom_baud_matches(76800,0,0));
    assert(!custom_baud_matches(38400,76923,76923));
    int master=posix_openpt(O_RDWR|O_NOCTTY);
    assert(master>=0); assert(grantpt(master)==0); assert(unlockpt(master)==0);
    int fd=open(ptsname(master),O_RDWR|O_NOCTTY); assert(fd>=0);
    assert(set_custom_baud(fd,76800)==1);
    termios t{};assert(tcgetattr(fd,&t)==0);
    assert(cfsetispeed(&t,B9600)==0);assert(cfsetospeed(&t,B9600)==0);
    assert(tcsetattr(fd,TCSANOW,&t)==0);assert(tcgetattr(fd,&t)==0);
    assert(cfgetispeed(&t)==B9600&&cfgetospeed(&t)==B9600);
    assert(set_custom_baud(fd,76800)==1);
    close(fd);close(master);
    assert(set_custom_baud(-1,76800)==-1);
}
