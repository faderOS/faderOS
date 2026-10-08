#!/usr/bin/env python3
"""9600-baud RAM updater client. --probe never stages, erases or programs flash."""
import argparse
import fcntl
import hashlib
import os
from pathlib import Path
import secrets
import select
import struct
import sys
import termios
import time
import tty
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'controller/python'))
from protocol import encode,decode
from install import image_sectors

class UpdateError(RuntimeError): pass
class Host:
 def __init__(self,port,log):
  self.fd=os.open(port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
  try:
   fcntl.flock(self.fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
   tty.setraw(self.fd); t=termios.tcgetattr(self.fd); t[4]=t[5]=termios.B9600
   t[2]=(t[2]|termios.CLOCAL|termios.CREAD)&~termios.CRTSCTS
   termios.tcsetattr(self.fd,termios.TCSANOW,t)
   self.log=log.open('xb')
  except BaseException: os.close(self.fd); raise
  self.session=secrets.randbits(32) or 1; self.seq=0; self.buffer=bytearray(); self.discard=False
 def close(self): self.log.close(); os.close(self.fd)
 def write(self,data):
  end=time.monotonic()+3
  while data:
   if time.monotonic()>end: raise UpdateError('serial write timeout')
   if select.select([], [self.fd], [], .1)[1]:
    try: n=os.write(self.fd,data)
    except BlockingIOError: continue
    if not n: raise UpdateError('serial disconnected')
    data=data[n:]
 def request(self,kind,seq,p=b'',responses=(0x90,),timeout=15):
  packet=encode(kind,seq,self.session,p)
  for attempt in range(3):
   self.write(b'\0'+packet) # delimit stale partial frames, including initial banner
   end=time.monotonic()+timeout
   while time.monotonic()<end:
    if not select.select([self.fd],[],[],.05)[0]: continue
    chunk=os.read(self.fd,4096)
    if not chunk: raise UpdateError('serial disconnected; RAM updater waits for reconnect')
    self.log.write(chunk); self.log.flush()
    result=None
    for byte in chunk:
     if byte:
      if len(self.buffer)>=210: self.discard=True; self.buffer.clear()
      if not self.discard: self.buffer.append(byte)
      continue
     raw=bytes(self.buffer); self.buffer.clear()
     if self.discard: self.discard=False; continue
     try: typ,number,session,data=decode(raw)
     except ValueError: continue
     if session!=self.session: continue
     if typ==0x82: self.write(encode(5,number,session)); continue
     if typ in responses and number==seq: result=(typ,data)
    if result is not None: return result
  raise UpdateError(f'no ACK for {kind:02x}/{seq}; no automatic reboot or baud change')
 def hello(self): return self.request(1,0,responses=(0x81,0x91),timeout=3)
 def enter(self):
  kind,data=self.hello()
  if kind==0x81:
   if len(data)!=59 or not (int.from_bytes(data[2:4],'big')&0x40):
    raise UpdateError('installed link firmware lacks RAM update; bootstrap once via Sony monitor')
   self.write(encode(5,0,self.session))
   try:
    _,ack=self.request(0x30,1,b'UPD1',responses=(0x80,),timeout=3)
    if ack!=b'\0': raise UpdateError(f'ENTER_UPDATE rejected: {ack.hex()}')
   except UpdateError as error:
    # ACK may be lost at handover; only a valid updater HELLO can recover it.
    print(f'Handover ACK unavailable ({error}); querying RAM updater...',flush=True)
   time.sleep(.25)
   kind,data=self.hello()
  if kind!=0x91 or len(data)!=12 or data[:2]!=b'\0\1' or data[3]!=1 or int.from_bytes(data[8:12],'big')!=0x20000:
   raise UpdateError(f'unsupported updater/flash ID: {kind:02x} {data.hex()}')
  self.seq=0
  print('RAM UPDATER READY / 9600 / both AM29F040 identified',flush=True)
 def command(self,kind,data=b'',timeout=15):
  self.seq=self.seq%65535+1
  _,ack=self.request(kind,self.seq,data,timeout=timeout)
  if ack!=b'\0': raise UpdateError(f'updater rejected {kind:02x}, status {ack.hex()}')
 def install(self,image,boot=True,stage_only=False):
  sectors=image_sectors(image); mask=sum(1<<n for n in sectors)
  crc=[zlib.crc32(image[n*0x20000:(n+1)*0x20000]) for n in range(7)]
  self.command(0x40,bytes([mask])+struct.pack('>7I',*crc))
  for sector in ([1] if stage_only else sectors[1:]+[0]):
   data=image[sector*0x20000:(sector+1)*0x20000]
   print(f'STAGING sector {sector} (RAM only), CRC32={crc[sector]:08x}',flush=True)
   self.command(0x41,bytes([sector]))
   for offset in range(0,len(data),128):
    block=data[offset:offset+128]
    if block==b'\xff'*len(block): continue
    self.command(0x42,struct.pack('>I',offset)+block)
   if stage_only:
    self.command(0x46)
    print('RAM CRC VERIFIED; no erase or flash programming. Remaining in RAM updater; power-cycle to boot unchanged flash.',flush=True)
    return
   print(f'COMMIT sector {sector}: validate RAM, erase/program if changed, readback CRC...',flush=True)
   self.command(0x43,bytes([sector]),timeout=90)
   print(f'VERIFIED sector {sector}',flush=True)
  self.command(0x44,timeout=90)
  print('UPDATE VERIFIED: complete application and resident',flush=True)
  if boot:
   self.command(0x45)
   print('Boot requested. Reconnect faderOS at 9600; check physical panel and boot banner.',flush=True)
  else: print('Remaining in RAM updater (--no-boot).',flush=True)

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--port',required=True)
 actions=p.add_mutually_exclusive_group(required=True)
 actions.add_argument('--probe',action='store_true')
 actions.add_argument('--enter',action='store_true',help='Enter RAM updater without staging or flash writes')
 actions.add_argument('--image',type=Path)
 p.add_argument('--no-boot',action='store_true')
 p.add_argument('--stage-only',action='store_true',help='With --image: upload and CRC-check app sector 1 in RAM; no flash writes')
 p.add_argument('--log',type=Path)
 args=p.parse_args(); host=None
 if args.stage_only and not args.image: p.error('--stage-only requires --image')
 try:
  image=None
  if args.image:
   image=args.image.read_bytes(); image_sectors(image)
   print('Image SHA256:',hashlib.sha256(image).hexdigest(),flush=True)
  log=args.log or Path(f'bkds-update-{time.time_ns()}.serial.log')
  print('Transcript:',log,flush=True)
  host=Host(args.port,log)
  if args.probe:
   kind,data=host.hello()
   if kind==0x81:
    if len(data)!=59: raise UpdateError('invalid link HELLO')
    print('LINK / RAM update capability:',bool(int.from_bytes(data[2:4],'big')&0x40))
   elif kind==0x91:
    if len(data)!=12: raise UpdateError('invalid updater HELLO')
    print('RAM UPDATER / flash recognized:',data[3]==1)
   return 0
  host.enter()
  if args.enter: return 0
  host.install(image,not args.no_boot,args.stage_only)
  return 0
 except (OSError,ValueError,UpdateError,KeyboardInterrupt) as error:
  print(f'STOPPED: {error}. No automatic retry after this error. If programming began, reconnect to RAM updater; after power loss an invalid app enters resident recovery. A damaged resident needs Sony EPROM recovery.',file=sys.stderr)
  return 1
 finally:
  if host: host.close()

if __name__=='__main__': sys.exit(main())
