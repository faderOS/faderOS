"""BKDS binary link v1. Standard library only; serial transport is a POSIX PTY."""
import errno
import os
import secrets
import select
import struct
import termios
import time
import tty
import zlib


def encode(kind, seq, session, payload=b''):
    if len(payload)>192:
        raise ValueError('payload too large')
    raw=struct.pack('>BBHIH',1,kind,seq,session,len(payload))+payload
    raw+=struct.pack('>I',zlib.crc32(raw))
    out=bytearray([0]); mark=0; code=1
    for b in raw:
        if b:
            out.append(b); code+=1
        else:
            out[mark]=code; mark=len(out); out.append(0); code=1
    out[mark]=code
    return bytes(out)+b'\0'


def decode(data):
    raw=bytearray(); i=0
    while i<len(data):
        code=data[i]; i+=1
        if not code or i+code-1>len(data): raise ValueError('COBS')
        raw.extend(data[i:i+code-1]); i+=code-1
        if code!=255 and i<len(data): raw.append(0)
    if len(raw)<14: raise ValueError('short')
    version,kind,seq,session,n=struct.unpack('>BBHIH',raw[:10])
    if version!=1 or n>192 or len(raw)!=14+n: raise ValueError('header')
    if zlib.crc32(raw[:-4])!=struct.unpack('>I',raw[-4:])[0]: raise ValueError('CRC')
    return kind,seq,session,bytes(raw[10:-4])


def snapshot(p):
    if len(p)!=55: raise ValueError('snapshot size')
    buttons={i*8+b for i,v in enumerate(p[4:27]) for b in range(8) if v&(1<<b)}
    return dict(ms=struct.unpack('>I',p[:4])[0],buttons=buttons,
                tbar=struct.unpack('>H',p[27:29])[0],x=p[29],y=p[30],
                rotary=struct.unpack('>6I',p[31:]))


class TransportClosed(ConnectionError):
    """The serial device/PTY is gone; protocol retries cannot reopen this fd."""


def transport_error(error):
    if error.errno in (errno.EIO, errno.ENXIO, errno.ENODEV, errno.EPIPE):
        raise TransportClosed('serial port closed or disconnected') from error
    raise error


class Client:
    def __init__(self,path,on_state=None):
        self.fd=os.open(path,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
        try:
            tty.setraw(self.fd)
            attr=termios.tcgetattr(self.fd); attr[4]=attr[5]=termios.B9600
            termios.tcsetattr(self.fd,termios.TCSANOW,attr)
        except (OSError,termios.error):
            os.close(self.fd)
            raise
        self.on_state=on_state or (lambda state,resync: None)
        self.buffer=bytearray(); self.discard=False
        self.session=0; self.seq=0; self.event=0; self.state=None; self.replies={}
        self.last_ping=0; self.ready=False; self.capabilities=0

    def close(self): os.close(self.fd)

    def write(self,data):
        end=time.monotonic()+1
        while data:
            if time.monotonic()>end: raise TimeoutError('serial write')
            if select.select([], [self.fd], [], .1)[1]:
                try: n=os.write(self.fd,data)
                except BlockingIOError: continue
                except OSError as error: transport_error(error)
                if not n: raise TransportClosed('serial write returned zero')
                data=data[n:]

    def poll(self,timeout=.01):
        if not select.select([self.fd],[],[],timeout)[0]: return
        try: data=os.read(self.fd,4096)
        except BlockingIOError: return
        except OSError as error: transport_error(error)
        if not data: raise TransportClosed('serial port reached EOF')
        for b in data:
            if b:
                if len(self.buffer)>=210: self.discard=True; self.buffer.clear()
                if not self.discard: self.buffer.append(b)
                continue
            frame=bytes(self.buffer); self.buffer.clear()
            if self.discard: self.discard=False; continue
            try: kind,seq,session,p=decode(frame)
            except ValueError: continue
            if session!=self.session: continue
            if kind==0x82:
                if seq==self.event: self.write(encode(5,seq,session)); continue
                if seq!=(self.event%65535)+1: raise ConnectionError('event gap; reconnect required')
                state=snapshot(p)
                self.event=seq; self.write(encode(5,seq,session))
                if self.ready:
                    self.state=state; self.on_state(state,False)
            elif (kind==0x80 and seq==self.seq) or (kind==0x81 and seq==0 and not self.ready):
                self.replies[(kind,seq)]=p

    def request(self,kind,seq,p=b'',response=0x80):
        data=encode(kind,seq,self.session,p)
        for _ in range(3):
            self.write(data)
            end=time.monotonic()+.7
            while time.monotonic()<end:
                self.poll(.02)
                reply=self.replies.pop((response,seq),None)
                if reply is not None: return reply
        raise TimeoutError('no reply; session must be renegotiated')

    def connect(self):
        self.ready=False
        self.session=secrets.randbits(32) or 1; self.seq=self.event=0; self.replies.clear()
        # End any partial console text or aborted frame before HELLO.
        self.write(b'\0')
        p=self.request(1,0,response=0x81)
        if len(p)!=59 or p[:2]!=b'\0\1': raise ValueError('unsupported catalog')
        self.capabilities=int.from_bytes(p[2:4],'big')
        self.state=snapshot(p[4:]); self.last_ping=time.monotonic()
        self.ready=True
        self.on_state(self.state,True)
        self.write(encode(5,0,self.session))
        return self.state

    def command(self,kind,p=b''):
        if len(p)>192: raise ValueError('payload too large')
        self.seq=(self.seq%65535)+1
        reply=self.request(kind,self.seq,p)
        if len(reply)!=1: raise ValueError('bad ACK')
        self.last_ping=time.monotonic()
        return reply[0]

    def heartbeat(self):
        if time.monotonic()-self.last_ping>.5:
            if self.command(2): raise ConnectionError('heartbeat rejected')

    def leds(self,pairs):
        return self.command(0x10,bytes(v for pair in pairs for v in pair))

    def lcd(self,row,text):
        data=text.encode('ascii')
        if row not in (0,1) or len(data)>40: raise ValueError('LCD range')
        return self.command(0x11,bytes([row])+data.ljust(40,b' '))
