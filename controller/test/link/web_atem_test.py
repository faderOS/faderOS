from test_paths import probe
"""Exercise web -> native ATEM commands against a local switcher peer."""
import json, socket, struct, threading, subprocess, tempfile, time
import urllib.request, urllib.error
from pathlib import Path

root=Path(__file__).resolve().parents[2]
def field(name,payload=b''):return struct.pack('!HH4s',len(payload)+8,0,name.encode())+payload
def packet(flags,sid,seq=0,ack=0,body=b''):return struct.pack('!6H',(flags<<11)|12+len(body),sid,ack,0,0,seq)+body
stop=threading.Event();errors=[];commands=[]
sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',0));sock.settimeout(.02)
def peer():
    addr=None;sid=0;seq=0;last=0;ping=0;ready=False
    def send(body):
        nonlocal seq
        seq+=1;sock.sendto(packet(1,sid,seq,body=body),addr)
    try:
        while not stop.is_set():
            if ready and time.monotonic()-ping>.1:send(field('Time',bytes(8)));ping=time.monotonic()
            try:data,remote=sock.recvfrom(4096)
            except socket.timeout:continue
            h=struct.unpack('!6H',data[:12]);flags=h[0]>>11
            if flags&2:
                addr=remote;sid=h[1];seq=last=0;ready=False;sock.sendto(packet(2,sid,body=b'\x02'+bytes(7)),addr);continue
            if not ready and flags&16:
                ready=True
                body=field('_ver',struct.pack('!I',0x20020))+field('_pin',b'ATEM Mini Pro test'.ljust(40,b'\0'))+field('_top',bytes([1,4,0,2,0,1,1])+bytes(21))
                body+=field('PrgI',bytes([0,0,0,1]))+field('PrvI',bytes([0,0,0,2,0]))+field('TrPs',bytes(8))
                for window,source in [(0,1),(1,2),(2,10010),(3,10011)]:body+=field('MvIn',struct.pack('!BBHBBBB',0,window,source,1,1,0,0))+field('SaMw',bytes([0,window,0,0]))+field('VuMC',bytes([0,window,0,0]))
                send(body+field('InCm'));continue
            if flags&1:
                sock.sendto(packet(16,sid,ack=h[5]),addr)
                if h[5]==last:continue
                last=h[5];at=12
                while at<len(data):
                    size=struct.unpack('!H',data[at:at+2])[0];name=data[at+4:at+8].decode();p=data[at+8:at+size];at+=size
                    assert name in ('SaMw','VuMS') and len(p)==4 and p[0]==0
                    commands.append((name,p[1],p[2]));send(field('SaMw' if name=='SaMw' else 'VuMC',p))
    except Exception as exc:errors.append(exc)
thread=threading.Thread(target=peer);thread.start()
with socket.socket() as http:http.bind(('127.0.0.1',0));port=http.getsockname()[1]
def api(route='/api/atem/status',body=None):
    request=urllib.request.Request(f'http://127.0.0.1:{port}'+route,data=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json','X-Server-Slot':'0'})
    try:
        with urllib.request.urlopen(request,timeout=2) as response:return response.status,json.loads(response.read())
    except urllib.error.HTTPError as exc:return exc.code,json.loads(exc.read())
def wait(predicate):
    until=time.monotonic()+4
    while time.monotonic()<until:
        try:
            if predicate():return
        except OSError:pass
        time.sleep(.02)
    raise AssertionError('Native web state was not confirmed')
def windows():return api()[1]['multiviews'][0]['windows']
try:
    with tempfile.TemporaryDirectory() as tmp:
        process=subprocess.Popen([probe('web-probe'),str(port),tmp+'/maps.json','127.0.0.1',str(sock.getsockname()[1])])
        try:
            wait(lambda:api()[1]['connected'])
            info=api('/api/info')[1];assert info['protocol']=='ATEM' and info['fields'][0]['value']=='ATEM Mini Pro test'
            assert api('/api/atem/multiview',{'viewer':0,'inputs':True,'property':'safe','enabled':True})[0]==202
            wait(lambda:not api()[1]['busy'] and all(w['safe']==1 for w in windows()[:2]))
            assert all(w['safe']==0 for w in windows()[2:]) # PGM/PVW untouched
            assert api('/api/atem/multiview',{'viewer':0,'window':2,'property':'meters','enabled':True})[0]==202
            wait(lambda:not api()[1]['busy'] and windows()[2]['meters']==1)
            assert api('/api/atem/multiview',{'viewer':0,'window':0,'property':'other','enabled':True})[0]==400
            assert api('/api/atem/multiview',{'viewer':0,'inputs':True,'window':0,'property':'safe','enabled':True})[0]==400
            assert api('/api/atem/multiview',{'viewer':0,'window':15,'property':'safe','enabled':True})[0]==409
            assert commands==[('SaMw',0,1),('SaMw',1,1),('VuMS',2,1)] and not errors,(commands,errors)
            print('WEB ATEM PASS: INFO, grouped input SAFE, individual PGM meters, native confirmation, invalid capabilities rejected')
        finally:process.terminate();process.wait(timeout=5)
finally:stop.set();thread.join();sock.close()
