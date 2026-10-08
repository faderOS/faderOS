from test_paths import probe
import socket,struct,threading,time,subprocess,tempfile
from pathlib import Path

def field(name,payload):return struct.pack('!HH4s',8+len(payload),0,name.encode())+payload
def packet(flags,sid,seq=0,ack=0,body=b''):return struct.pack('!6H',(flags<<11)|12+len(body),sid,ack,0,0,seq)+body
stop=threading.Event();errors=[];sessions=[0]*3;commands=[0]*3
sockets=[];threads=[]
def peer(index,sock):
    sock.settimeout(.01);addr=None;sid=0;seq=0;ready=False;last=0;at=0;external=False;history={}
    def send(body):
        nonlocal seq
        seq+=1;data=packet(1,sid,seq=seq,body=body);history[seq]=data;sock.sendto(data,addr)
    try:
        while not stop.is_set():
            now=time.monotonic()
            if ready and now-at>1 and not external:
                external=True
                if index==1:send(field('TMxP',bytes([0,42,0,0])))
            try:data,remote=sock.recvfrom(4096)
            except socket.timeout:continue
            h=struct.unpack('!6H',data[:12]);flags=h[0]>>11
            if flags&2:
                sessions[index]+=1;addr=remote;sid=h[1];seq=0;last=0;ready=False;history={};sock.sendto(packet(2,sid,body=b'\x02'+bytes(7)),addr);continue
            if flags&8:
                for k,v in history.items():
                    if k>=h[3]:sock.sendto(v,addr)
            if not ready and flags&16:
                ready=True;at=now
                top=bytes([1,2,0,0,0,1,0])+bytes(21)
                send(field('_ver',struct.pack('!I',0x20020))+field('_top',top)+field('_MeC',bytes(4))+field('TrSS',bytes([0,0,1,0,1,0,0,0]))+field('PrgI',bytes([0,0,0,1]))+field('PrvI',bytes([0,0,0,2,0]))+field('TrPs',bytes(8))+field('TMxP',bytes([0,25,0,0]))+field('InCm',b''))
            if flags&1:
                sock.sendto(packet(16,sid,ack=h[5]),addr)
                if h[5]==last:continue
                last=h[5];commands[index]+=1
                assert index==2 and data[16:20]==b'CTMx' and data[20:24]==bytes([0,77,0,0])
                send(field('TMxP',bytes([0,77,0,0])))
    except Exception as exc:errors.append(exc);print("PEER",index,repr(exc),flush=True)
try:
    for i in range(3):
        sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',0));sockets.append(sock);thread=threading.Thread(target=peer,args=(i,sock));thread.start();threads.append(thread)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([probe('servers-probe'),*[str(s.getsockname()[1]) for s in sockets],tmp+'/mapping'],check=True,timeout=15)
    assert sessions==[1,1,1],sessions
    assert commands==[0,0,1],commands
    assert not errors,errors
finally:
    stop.set()
    for thread in threads:thread.join()
    for sock in sockets:sock.close()
