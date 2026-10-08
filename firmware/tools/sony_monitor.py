"""Sony TMP68301 monitor rev 2.7, SCI2 9600 8N1. No third-party dependencies."""
import fcntl
import os
import re
import select
import termios
import time
import tty

PROMPT=re.compile(rb'(?:^|[\r\n])\x07?(?:D)?>$')
ERROR=re.compile(rb'error|aborted|time.?out|SWI\*',re.I)

class MonitorError(RuntimeError): pass

def record(kind,address,data=b''):
    raw=bytes([len(data)])+address.to_bytes(2,'big')+bytes([kind])+data
    return b':'+(raw+bytes([-sum(raw)&255])).hex().upper().encode()+b'\n'

def sony_hex(data):
    """IL accepts types 00/01/02 and swaps every data WORD on receipt.
    Records are contiguous, even, relative to the IL load address. LF only:
    the ROM consumes exactly one character after each checksum (not CRLF).
    """
    if not data or len(data)%2 or len(data)>0x20000: raise ValueError('IL payload must be even, 2..128 KiB')
    result=bytearray()
    for offset in range(0,len(data),32):
        if offset and offset%0x10000==0: result+=record(2,0,(offset>>4).to_bytes(2,'big'))
        block=bytearray(data[offset:offset+32]); block[::2],block[1::2]=data[offset+1:offset+len(block):2],data[offset:offset+len(block):2]
        result+=record(0,offset&0xffff,block)
    return bytes(result)+record(1,0)

class Monitor:
    def __init__(self,path,transcript):
        self.fd=os.open(path,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
        try:
            fcntl.flock(self.fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
            tty.setraw(self.fd)
            attr=termios.tcgetattr(self.fd); attr[4]=attr[5]=termios.B9600
            attr[2]|=termios.CLOCAL|termios.CREAD
            attr[2]&=~termios.CRTSCTS
            termios.tcsetattr(self.fd,termios.TCSANOW,attr)
            self.log=open(transcript,'xb')
        except BaseException:
            os.close(self.fd); raise
        self.pending=bytearray()
        self.write_started=False
    def close(self): self.log.close(); os.close(self.fd)
    def _read(self):
        data=os.read(self.fd,65536)
        if not data: raise MonitorError('serial device closed or disconnected; check the USB adapter and kernel log')
        self.log.write(data); self.log.flush(); self.pending.extend(data)
    def send(self,data,timeout=60,check_errors=False):
        end=time.monotonic()+timeout
        while data:
            if time.monotonic()>end: raise MonitorError('serial write timeout')
            rd,wr,_=select.select([self.fd],[self.fd],[],.1)
            if rd: self._read()
            if check_errors and ERROR.search(self.pending): raise MonitorError(f'IL rejected stream: {bytes(self.pending[-400:])!r}')
            if wr:
                try: n=os.write(self.fd,data[:512])
                except BlockingIOError: continue
                data=data[n:]
    def wait(self,pattern=PROMPT,timeout=30):
        end=time.monotonic()+timeout
        while True:
            if pattern.search(self.pending):
                data=bytes(self.pending); self.pending.clear(); return data
            if time.monotonic()>end: raise MonitorError(f'monitor timeout: {bytes(self.pending[-300:])!r}')
            if select.select([self.fd],[],[],.05)[0]: self._read()
    def command(self,command,timeout=30,check_errors=True):
        self.send(command.encode()+b'\r')
        out=self.wait(timeout=timeout)
        if check_errors and ERROR.search(out): raise MonitorError(out.decode('ascii',errors='replace'))
        return out
    def settle(self,quiet=.25,timeout=5):
        """Archive startup/stale output before issuing a fresh command.
        Do not flush the tty: retain all received bytes in the transcript.
        A continuously restarting/noisy monitor must time out, not be accepted.
        """
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            if not select.select([self.fd],[],[],min(quiet,max(0,end-time.monotonic())))[0]:
                self.pending.clear()
                return
            self._read()
        raise MonitorError('monitor output did not settle; check for repeated startup or another serial reader')
    def sync(self):
        # Requires a monitor prompt, outside any pending confirmation or command.
        self.settle()
        self.command('')
        self.settle()
        self.eprom_prefix=self.read(0x200000,16)
    def read(self,address,length):
        if address%16 or length%16 or length<=0: raise ValueError('dump must align to 16 bytes')
        result=bytearray()
        for base in range(address,address+length,0x1000):
            count=min(0x1000,address+length-base)
            out=self.command(f'd {base:x} {base+count-1:x}',timeout=60,check_errors=False)
            rows={}
            for line in re.split(rb'[\r\n]+',out):
                m=re.match(rb'^([0-9a-fA-F]{8}) ((?:[0-9a-fA-F]{4} ){8})',line)
                if m:
                    addr=int(m[1],16)
                    if addr in rows: raise MonitorError('duplicate dump address')
                    rows[addr]=bytes.fromhex(m[2].decode())
            if set(rows)!=set(range(base,base+count,16)): raise MonitorError(f'incomplete dump at {base:06x}; received {len(rows)} of {count//16} rows; response tail={out[-240:]!r}')
            result.extend(b''.join(rows[a] for a in sorted(rows)))
        return bytes(result)
    def program_sector(self,sector,data,readback=True):
        if not 0<=sector<=6 or len(data)!=0x20000: raise ValueError('only full sectors 0..6 are writable')
        # IL always erases the full touched sector. Omitted suffix becomes FF.
        size=max(2,(len(data.rstrip(b'\xff'))+1)&~1)
        payload=sony_hex(data[:size]); address=0x40000+sector*0x20000
        self.send(f'il 10000 {address:x}\r'.encode())
        prompt=self.wait(re.compile(rb'\(y\)/n \? $'))
        expected=f'down load [10000] ===> flash copy [{address:x}]'.encode()
        if expected not in prompt or b'<Serial debug port>' not in prompt:
            # Any answer except n confirms IL. Cancel explicitly, without CR.
            self.send(b'n')
            cancelled=self.wait()
            if b'Aborted.' not in cancelled: raise MonitorError(f'IL cancellation not confirmed: {cancelled!r}')
            raise MonitorError(f'IL cancelled before confirmation; check DS1 (load interface must show Serial debug port): {prompt!r}')
        # Stream immediately: no CR after y, no CRLF in records. The monitor polls
        # UART throughout reception; data is paced by the actual 9600 baud UART.
        self.write_started=True # conservative: delivery can fail after partial transmission
        self.send(b'y'+payload,timeout=max(60,len(payload)/700+30),check_errors=True)
        out=self.wait(timeout=max(60,len(payload)/700+60))
        if ERROR.search(out) or any(x not in out for x in [b'Down load OK',b'Flash verify..',b'Complete.']):
            raise MonitorError(f'IL failed; leave debug mode active and recover from backup: {out[-1200:]!r}')
        if not readback: return
        actual=self.read(address,0x20000)
        if actual!=data: raise MonitorError(f'independent readback mismatch in sector {sector}')
