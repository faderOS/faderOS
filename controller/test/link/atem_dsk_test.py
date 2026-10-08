from test_paths import probe
#!/usr/bin/env python3
"""DSK protocol integration: legacy/modern AUTO, multiple DSKs, echo validation."""
import json, socket, struct, subprocess, tempfile, threading, time
from pathlib import Path

def field(name, data=b''):
    return struct.pack('!HH4s', 8+len(data), 0, name.encode())+data

def packet(flags, sid, seq=0, ack=0, body=b''):
    return struct.pack('!6H', flags<<11|12+len(body), sid, ack, 0, 0, seq)+body

def run(version, count):
    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',0));sock.settimeout(.01)
    stop=threading.Event();errors=[];seq=0;peer=None;last_client=0;autos=0
    dsks=[dict(on=False,mixing=False,fill=2001,key=2002,frames=25,tie=False,premultiplied=True,clip=0,gain=1000,invert=False,mask=False,end=0) for _ in range(count)]
    def send(body):
        nonlocal seq
        seq+=1;sock.sendto(packet(1,0x8001,seq=seq,body=body),peer)
    def state(i):
        d=dsks[i]
        return field('DskB',struct.pack('!BBHH2x',i,0,d['fill'],d['key']))+field('DskP',struct.pack('!BBBBHHBB10x',i,d['tie'],d['frames'],d['premultiplied'],d['clip'],d['gain'],d['invert'],d['mask']))+field('DskS',bytes([i,d['on'],d['mixing'],d['mixing'],10 if d['mixing'] else 0,0,0,0]))
    def worker():
        nonlocal seq,peer,last_client,autos
        ready=False;last_ping=0
        try:
            while not stop.is_set():
                now=time.monotonic()
                if ready:
                    for i,d in enumerate(dsks):
                        if d['end'] and now>=d['end']:d['end']=0;d['mixing']=False;d['on']=not d['on'];send(state(i))
                    if now-last_ping>.05:send(field('Time',bytes(8)));last_ping=now
                try:data,addr=sock.recvfrom(4096)
                except socket.timeout:continue
                h=struct.unpack('!6H',data[:12]);flags=h[0]>>11
                if flags&2:
                    peer=addr;seq=0;last_client=0;ready=False;sock.sendto(packet(2,h[1],body=b'\x02'+bytes(7)),peer);continue
                if not ready and flags&16:
                    ready=True;body=field('_ver',struct.pack('!I',version))+field('_top',bytes([1,2,count])+bytes(25))+field('PrgI',struct.pack('!BBH',0,0,2001))+field('PrvI',struct.pack('!BBH4x',0,0,2002))
                    for src in [2001,2002]:body+=field('InPr',struct.pack('!H',src)+str(src).encode().ljust(20,b'\0')+b'COL1'+bytes(8)+bytes([16,1]))
                    for i in range(count):body+=state(i)
                    send(body+field('InCm'));continue
                if not flags&1:continue
                client=h[5];sock.sendto(packet(16,0x8001,ack=client),peer)
                if client==last_client:continue
                assert client==last_client+1;last_client=client;at=12
                while at<len(data):
                    size,_,name=struct.unpack('!HH4s',data[at:at+8]);p=data[at+8:at+size];name=name.decode();at+=size
                    i=p[1] if name in ('CDsG','CDsM') or name=='DDsA' and version>=0x2001d else p[0];d=dsks[i]
                    if name=='CDsL':d['on']=bool(p[1])
                    elif name=='CDsR':d['frames']=p[1]
                    elif name=='CDsF':d['fill']=struct.unpack('!H',p[2:4])[0]
                    elif name=='CDsC':d['key']=struct.unpack('!H',p[2:4])[0]
                    elif name=='CDsT':d['tie']=bool(p[1])
                    elif name=='CDsM':
                        assert p[0]==1 and len(p)==12;d['mask']=bool(p[2])
                    elif name=='CDsG':
                        assert p[0]==15;d.update(premultiplied=bool(p[2]),clip=struct.unpack('!H',p[4:6])[0],gain=struct.unpack('!H',p[6:8])[0],invert=bool(p[8]))
                    elif name=='DDsA':
                        if version>=0x2001d:assert p[0]==1 and bool(p[2])!=d['on']
                        else:assert p[1:]==bytes(3)
                        assert not d['mixing'];d['mixing']=True;d['end']=now+.3;autos+=1
                    else:raise AssertionError(name)
                    send(state(i))
        except BaseException as e:errors.append(e)
    thread=threading.Thread(target=worker);thread.start()
    try:
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'map.json';path.write_text(json.dumps({'version':1,'me':0,'sources':['']*24}))
            r=subprocess.run([probe('atem-probe'),'127.0.0.1',str(sock.getsockname()[1]),str(path),'dsk'],capture_output=True,text=True,timeout=15)
            assert r.returncode==0,r.stdout+r.stderr+repr(errors)
            assert autos==count*2
            print(hex(version),count,r.stdout.splitlines()[-1])
    finally:stop.set();thread.join();sock.close()
    assert not errors,errors
run(0x20020,1)
run(0x2001c,2)
