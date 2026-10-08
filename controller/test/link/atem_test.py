from test_paths import probe
#!/usr/bin/env python3
"""Protocol peer: multiple M/Es, >4 sources, loss/reorder/duplicates, reconnect."""
import sys, json, socket, struct, subprocess, tempfile, threading, time
from pathlib import Path

def field(name, data=b''):
    return struct.pack('!HH4s', 8+len(data), 0, name.encode())+data

def packet(flags, sid, seq=0, ack=0, body=b''):
    return struct.pack('!6H', flags<<11|12+len(body), sid, ack, 0, 0, seq)+body

def run(disconnect=False, ftb=False, keys=False, manual=False, wipe=False, dve=False, sting=False, no_dve=False, dip=False, modifiers=False, live=False, pause=False, controls=False):
    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',0));sock.settimeout(.01)
    outputs=[False,False];video_mode=12
    stop=threading.Event();errors=[];stats={'cuts':0,'retries':0,'gaps':0,'controls':0,'fades':0,'captures':0,'clears':0,'mask_requests':0,'sessions':0}
    mes=[{'pg':2001,'pv':2002,'rate':25,'mix':False,'paused':False,'previewtrans':False,'pos':0,'style':0,'dip':[45,2002],'dve':[37,8,24,3,4,1,1,500,700,1,0,1],'wipe':[31,1,450,2001,6200,0,5000,5000,0,1],'black':False,'fade':False,'ftbrate':25,'layers':1,'keys':[dict(on=False,type=0,fill=1,key=2,clip=0,gain=1000,premultiplied=True,invert=False,mask=False,border=False,shadow=False,dvemask=False,patterninvert=False) for _ in range(4)]} for _ in range(4)]
    sid=0x8001;peer=None;seq=0;last_client=0;history={};blackout=0;mix_end=0;fade_end=0;dropped=False;gap_dropped=False
    def send(body=b'', drop=False):
        nonlocal seq
        seq=(seq+1)&0x7fff;data=packet(1,sid,seq=seq,body=body);history[seq]=data
        if not drop:sock.sendto(data,peer)
        return data
    def media_tally():
        values=[]
        for source in (3010,3011):
            program=any(m['pg']==source or (m['mix'] and m['pv']==source and not m['previewtrans']) or any(k['on'] and source in (k['fill'],k['key']) for k in m['keys']) for m in mes)
            preview=any(m['pv']==source for m in mes)
            values.append(struct.pack('!HB',source,int(program)|(int(preview)<<1)))
        return field('TlSr',struct.pack('!H',2)+b''.join(values)) if controls else b''
    def state(me):
        m=mes[me]
        return media_tally()+field('TDpP',struct.pack('!BBH',me,*m['dip']))+dve_state(me)+wipe_state(me)+field('TrPr',bytes([me,m['previewtrans'],0,0]))+field('PrgI',struct.pack('!BBH',me,0,m['pg']))+field('PrvI',struct.pack('!BBHB3x',me,0,m['pv'],int(m['mix'] and bool(m['layers']&1))))+field('TrPs',struct.pack('!BBBBH2x',me,m['mix'],0 if manual else 12 if m['mix'] else 0,0,m['pos'] if manual else 5000 if m['mix'] else 0))+field('TrSS',bytes([me,m['style'],m['layers'],m['style'],m['layers'],0,0,0]))+b''.join(key_state(me,i) for i in range(4))+field('TMxP',bytes([me,m['rate'],0,0]))+field('FtbP',bytes([me,m['ftbrate'],0,0]))+field('FtbS',bytes([me,m['black'],m['fade'],10 if m['fade'] else 0]))
    def dve_state(me):
        d=mes[me]['dve']
        return field('TDvP',struct.pack('!BBBBHHBBHHBBB3x',me,*d))
    def wipe_state(me):
        w=mes[me]['wipe']
        return field('TWpP',struct.pack('!BBBB6HBB2x',me,w[0],w[1],0,*w[2:8],w[8],w[9]))
    def key_state(me,i):
        k=mes[me]['keys'][i]
        return field('KeOn',bytes([me,i,k['on'],0]))+field('KeBP',bytes([me,i,k['type'],0,1,0])+struct.pack('!HH',k['fill'],k['key'])+bytes([k['mask'],0])+bytes(8))+field('KeLm',bytes([me,i,k['premultiplied'],0])+struct.pack('!HH',k['clip'],k['gain'])+bytes([k['invert'],0,0,0]))+field('KeDV',bytes([me,i])+bytes(22)+bytes([k['border'],k['shadow']])+bytes(21)+bytes([k['dvemask']])+bytes(12))+field('KePt',bytes([me,i])+bytes(12)+bytes([k['patterninvert'],0]))
    def take(me):
        m=mes[me]
        if m['layers']&1:m['pg'],m['pv']=m['pv'],m['pg']
        for i,k in enumerate(m['keys']):
            if m['layers']&(2<<i):k['on']=not k['on']
    def initial():
        top=bytes([4,48,4,2 if controls else 12,0,1 if controls else 2,2,0,4,0 if no_dve else 2,1 if sting else 0,1])+bytes(16)
        body=field('_ver',struct.pack('!I',0x20020))+field('_pin',(b'ATEM Mini Pro test' if controls else b'ATEM multi-ME test').ljust(40,b'\0'))+field('_top',top)+field('_MvC',bytes([10,0,0,0]))
        send(body)
        if controls:send(field('_VMC',struct.pack('!H2x',2)+bytes([10])+bytes(12)+bytes([12])+bytes(12))+field('VidM',bytes([video_mode,0,0,0])))
        for i in range(4):send(field('_MeC',bytes([i,6,0,0]))+state(i))
        body=(b''.join(field('MvIn',struct.pack('!BBHBBBB',0,w,source,1,1,0,0))+field('SaMw',bytes([0,w,0,0]))+field('VuMC',bytes([0,w,0,0])) for w,source in [(2,10010),(3,10011),(4,2)])+field('AuxS',struct.pack('!BBH',0,0,10010))+field('AuxS',struct.pack('!BBH',1,0,9001))+field('StRS',struct.pack('!H',1))+field('RTMS',struct.pack('!H',2))+field('MvIn',bytes([0,0,0,1,1,1,0,0]))+field('SaMw',bytes([0,0,0,0]))+field('VuMC',bytes([0,0,0,0]))+field('MvIn',bytes([1,3,0,2,0,1,0,0]))+field('SaMw',bytes([1,3,0,0]))) if controls else b''
        for i in list(range(1,41))+[2001,2002,10010,10020,10030,10040]+([3010,3011,9001,10011] if controls else []):
            data=struct.pack('!H',i)+f'Input {i}'.encode().ljust(20,b'\0')+f'{i}'.encode()[:4].ljust(4,b'\0')+bytes(6)+bytes([128 if i>=10010 else 4 if i==3010 else 5 if i==3011 else 0,0,16 if (i<41 and not controls) or i in (3010,3011) else 17 if controls and i<41 else 1 if controls and i in (9001,10010,10011) else 0,((1<<((i-10010)//10))-1) if i>=10010 else 15])
            body+=field('InPr',data)
            if len(body)>1100:send(body);body=b''
        send(body+field('_mpl',bytes([20,0,0,0]))+field('MPCE',bytes([0,1,0,0]))+b''.join(field('MPfe',bytes([0,0])+struct.pack('!H',i)+bytes([i!=7])+bytes(18)+bytes([0])) for i in range(20))+field('InCm'))
    def worker():
        nonlocal peer,seq,last_client,blackout,mix_end,fade_end,dropped,gap_dropped,video_mode
        ready=False;last_ping=time.monotonic()
        try:
            while not stop.is_set():
                now=time.monotonic()
                if ready and now>=blackout:
                    if fade_end and now>=fade_end:
                        m=mes[2];m['black']=not m['black'];m['fade']=False;fade_end=0;send(state(2))
                    if mix_end and now>=mix_end and not mes[2]['paused']:
                        take(2);mes[2]['mix']=False;mix_end=0;send(state(2))
                    if now-last_ping>.04:send(field('Time',bytes(8)));last_ping=now
                try:d,addr=sock.recvfrom(4096)
                except socket.timeout:continue
                h=struct.unpack('!6H',d[:12]);flags=h[0]>>11
                if flags&2:
                    stats['sessions']+=1
                    peer=addr;seq=0;last_client=0;history.clear();ready=False;blackout=0
                    sock.sendto(packet(2,h[1],body=b'\x02'+bytes(7)),peer);continue
                if addr!=peer or now<blackout:continue
                if not ready and flags&16:ready=True;initial();continue
                if flags&8:
                    stats['gaps']+=1
                    for k,v in history.items():
                        if k>=h[3]:sock.sendto(v,peer)
                if not flags&1:continue
                client=h[5]
                if not dropped and not disconnect:dropped=True;continue
                if client==last_client:
                    stats['retries']+=1;sock.sendto(packet(16,sid,ack=client),peer);continue
                assert client==((last_client+1)&0x7fff),(client,last_client)
                last_client=client;stats['controls']+=1
                at=12;cut=False
                while at<len(d):
                    size,_,name=struct.unpack('!HH4s',d[at:at+8]);p=d[at+8:at+size];name=name.decode();at+=size
                    if name=='CVdM':
                        assert len(p)==4 and p[0] in (10,12) and p[1:]==bytes(3);video_mode=p[0];send(field('VidM',p));continue
                    if name=='Capt':
                        assert not p;stats['captures']+=1
                        if stats['captures']==1:send(field('MPfe',bytes([0,0])+struct.pack('!H',7)+bytes([1])+bytes([17])*16+bytes(2)+bytes([0])))
                        elif stats['captures']==3:send(b''.join(field('MPfe',bytes([0,0])+struct.pack('!H',i)+bytes([1])+bytes([18])*16+bytes(2)+bytes([0])) for i in (7,8)))
                        continue
                    if name in ('StrR','RcTM'):
                        assert len(p)==4 and p[0] in (0,1) and p[1:]==bytes(3);i=0 if name=='StrR' else 1;outputs[i]=bool(p[0]);send(field('StRS' if i==0 else 'RTMS',struct.pack('!H',(4 if outputs[i] else 1) if i==0 else (3 if outputs[i] else 2))));continue
                    if name=='CAuS':
                        assert len(p)==4 and p[0]==1 and p[1]<2 and struct.unpack('!H',p[2:])[0] in (1,10010,10011,9001);assert p[1]==0 or struct.unpack('!H',p[2:])[0] in (10010,10011,9001);send(field('AuxS',bytes([p[1],0])+p[2:]));continue
                    if name in ('SaMw','VuMS'):
                        assert len(p)==4 and p[0]==0 and p[1] in (0,4)
                        send(field('SaMw' if name=='SaMw' else 'VuMC',p));continue
                    if name=='CSTL':
                        assert len(p)==4 and p[1:]==bytes(3) and p[0] in (0,1,19);stats['clears']+=1
                        if p[0]!=1:send(field('MPfe',bytes([0,0])+struct.pack('!H',p[0])+bytes(20)))
                        continue
                    if name=='MPSS':
                        assert len(p)==8 and p[:3]==bytes([3,0,1]) and p[3]<20 and p[3]!=7;send(field('MPCE',bytes([0,1,p[3],0])));continue
                    me=p[4] if name=='CKDV' else p[1] if name=='CKMs' else p[2] if name in ('CTWp','CTDv') else p[1] if name in ('CTTp','CTDp','FtbC','CKTp','CKLm','CKPt') else p[0];assert me==2,('wrong ME',name,me)
                    m=mes[me]
                    if name=='FtbC':assert p[0]==1;m['ftbrate']=p[2]
                    elif name=='FtbA':assert not m['fade'];m['fade']=True;fade_end=now+.5;stats['fades']+=1
                    elif name=='CPgI':m['pg']=struct.unpack('!H',p[2:4])[0]
                    elif name=='CPvI':m['pv']=struct.unpack('!H',p[2:4])[0]
                    elif name=='CTMx':m['rate']=p[1]
                    elif name=='CTTp':
                        if p[0]&1:m['style']=p[2]
                        if p[0]&2:m['layers']=p[3]
                    elif name=='CKOn':m['keys'][p[1]]['on']=bool(p[2])
                    elif name=='CKeF':m['keys'][p[1]]['fill']=struct.unpack('!H',p[2:4])[0]
                    elif name=='CKeC':m['keys'][p[1]]['key']=struct.unpack('!H',p[2:4])[0]
                    elif name=='CKTp':assert p[0]==1;m['keys'][p[2]]['type']=p[3]
                    elif name=='CKPt':
                        assert p[0]==64 and len(p)==16;m['keys'][p[2]]['patterninvert']=bool(p[14])
                    elif name=='CKMs':
                        assert p[0]==1 and len(p)==12;stats['mask_requests']+=1
                        if not controls or stats['mask_requests']!=3:m['keys'][p[2]]['mask']=bool(p[3])
                    elif name=='CKDV':
                        assert len(p)==64;mask=struct.unpack('!I',p[:4])[0];assert mask in (32,64,1<<20);k=m['keys'][p[5]]
                        if mask==32:k['border']=bool(p[28])
                        elif mask==64:k['shadow']=bool(p[29])
                        else:k['dvemask']=bool(p[51])
                    elif name=='CKLm':
                        assert p[0]==15;k=m['keys'][p[2]];k.update(premultiplied=bool(p[3]),clip=struct.unpack('!H',p[4:6])[0],gain=struct.unpack('!H',p[6:8])[0],invert=bool(p[8]))
                    elif name=='CTDp':
                        assert len(p)==8
                        if p[0]&1:m['dip'][0]=p[2]
                        if p[0]&2:m['dip'][1]=struct.unpack('!H',p[4:6])[0]
                    elif name=='CTDv':
                        assert not no_dve and len(p)==20;mask=struct.unpack('!H',p[:2])[0]
                        values=[p[3],p[4],p[5],*struct.unpack('!HH',p[6:10]),p[10],p[11],*struct.unpack('!HH',p[12:16]),p[16],p[17],p[18]]
                        for i,v in enumerate(values):
                            if mask&(1<<i):m['dve'][i]=v
                    elif name=='CTWp':
                        assert len(p)==20;mask=struct.unpack('!H',p[:2])[0]
                        values=[p[3],p[4],*struct.unpack('!6H',p[6:18]),p[18],p[19]]
                        for i,v in enumerate(values):
                            if mask&(1<<i):m['wipe'][i]=v
                    elif name=='CTPr':m['previewtrans']=bool(p[1])
                    elif name=='CTPs':
                        m['pos']=struct.unpack('!H',p[2:4])[0];assert m['pos']<=10000
                        if live and m['pos']==1234*10000//4095:m['pg']=7;m['pv']=8
                        m['mix']=m['pos'] not in (0,10000)
                        if m['pos']==10000:
                            if not m['previewtrans']:take(me)
                            m['pos']=0

                    elif name=='DCut':take(me);m['mix']=False;m['paused']=False;mix_end=0;stats['cuts']+=1;cut=True
                    elif name=='DAut':
                        if m['style']==4:assert sting
                        if m['mix']:
                            m['paused']=not m['paused']
                        else:m['mix']=True;m['paused']=False
                        mix_end=now+.3
                    else:raise AssertionError(name)
                if disconnect and cut:blackout=now+10;continue
                # Drop first command ACK: client retries same id, not a second hot punch.
                if stats['controls']!=1:sock.sendto(packet(16,sid,ack=client),peer)
                if not gap_dropped:
                    gap_dropped=True;send(state(2),drop=True);send(field('Time',bytes(8)))
                else:
                    data=send(state(2));sock.sendto(data,peer) # duplicate state
        except BaseException as e:errors.append(e)
    thread=threading.Thread(target=worker);thread.start()
    try:
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'map.json';path.write_text(json.dumps({'version':1,'me':2,'sources':(['3010','3011','1']+['']*21) if controls else ['']*24}))
            result=subprocess.run([probe('atem-probe'),'127.0.0.1',str(sock.getsockname()[1]),str(path),'controls' if controls else 'pause-cut' if pause else 'live-buses' if live else 'modifiers' if modifiers else 'dip' if dip else 'no-dve' if no_dve else 'sting' if sting else 'dve' if dve else 'wipe' if wipe else 'manual' if manual else 'keys' if keys else 'ftb' if ftb else 'disconnect' if disconnect else 'exercise'],capture_output=True,text=True,timeout=35)
            assert result.returncode==0,result.stdout+result.stderr
            status=json.JSONDecoder().raw_decode(result.stdout)[0]
            assert len(status['inputs'])==(50 if controls else 46) and status['capabilities']['mixEffects']==4 and status['capabilities']['multiviewers']==2
            assert len(status['mixEffects'])==4 and status['mixEffects'][2]['upstreamKeyers']==6
            assert stats['cuts']==(3 if pause else 3 if keys else 0 if controls or ftb or manual or wipe or dve or sting or no_dve or dip or modifiers or live else 1 if disconnect else 2),stats
            if dve:assert mes[2]['dve'][1]==8 and mes[2]['dve'][3:10]==[3,4,1,1,500,700,1],mes[2]['dve']
            if sting:assert mes[2]['dve']==[37,8,24,3,4,1,1,500,700,1,0,1]
            if wipe:
                assert mes[2]['wipe'][2:5]==[450,2001,6200] and mes[2]['wipe'][6:8]==[5000,5000],mes[2]['wipe']
            if modifiers:assert mes[2]['wipe'][2:8]==[2500,2001,7000,0,1000,8000],mes[2]['wipe']
            if dip:assert mes[2]['dip']==[35,2001] and mes[2]['rate']==25 and mes[2]['wipe'][0]==31
            if controls:assert stats['clears']==3 and stats['sessions']==1,stats
            if ftb:assert stats['fades']==2,stats
            if not disconnect and not no_dve:assert stats['retries']>0 and stats['gaps']>0,stats
            print(result.stdout.splitlines()[-1],stats)
    finally:stop.set();thread.join();sock.close()
    assert not errors,errors
if len(sys.argv)>1 and sys.argv[1]=='controls':
    run(controls=True)
    raise SystemExit
run();run(True);run(ftb=True);run(keys=True)

run(manual=True)

run(wipe=True)

run(dve=True);run(sting=True);run(no_dve=True)

run(dip=True);run(modifiers=True)

run(live=True)

run(pause=True)

run(controls=True)
