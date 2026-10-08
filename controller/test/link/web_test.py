from test_paths import probe
import json, socket, subprocess, tempfile, time, urllib.request, urllib.error
from pathlib import Path
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'map.json'; path.write_text(json.dumps({'version':1,'sources':[{'slot':0,'scene':'SRC1'}]}))
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    process=subprocess.Popen([probe('web-probe'),str(port),str(path)])
    selected_slot=0
    def get(route='/api/mappings',data=None,origin=None):
        headers={'Content-Type':'application/json','X-Server-Slot':str(selected_slot)}
        if origin:headers['Origin']=origin
        req=urllib.request.Request(f'http://127.0.0.1:{port}'+route,data=None if data is None else json.dumps(data).encode(),headers=headers)
        try:
            with urllib.request.urlopen(req,timeout=3) as r:return r.status,r.read()
        except urllib.error.HTTPError as e:return e.code,e.read()
    try:
        for _ in range(60):
            try:status,body=get();break
            except OSError:time.sleep(.05)
        assert status==200
        assert b'Main bus' in get('/')[1]
        secret='test-password-for-web'
        assert get('/api/obs/auth',{'password':secret},origin='http://evil.example')[0]==403
        assert get('/api/obs/auth',{'password':secret})[0]==200
        credentials=Path(str(path)+'.credentials.json')
        assert credentials.stat().st_mode & 0o777 == 0o600
        assert json.loads(credentials.read_text())['obs_password']==secret
        status_body=get('/api/status')[1]
        assert secret.encode() not in status_body and json.loads(status_body)['passwordSet']
        assert get('/api/obs/auth',{'password':''})[0]==200
        assert not json.loads(get('/api/status')[1])['passwordSet']
        system=json.loads(get('/api/system')[1])
        assert system['activeProtocol']=='OBS' and not system['kavtor']
        assert get('/api/kavtor/command',{'cmd':'cut'})[0]==409
        assert get('/api/atem/keyers',{})[0]==409
        assert get('/api/atem/keyers',{},origin='http://evil.example')[0]==403
        assert b'atem-key-form' in get('/')[1]
        assert get('/api/atem/multiview',{})[0]==409
        assert b'atem-multiview-controls' in get('/')[1]
        assert get('/api/atem/video-format',{})[0]==409
        assert get('/api/atem/video-format',{},origin='http://evil.example')[0]==403
        assert b'atem-video-form' in get('/')[1]
        assert get('/api/atem/dsk',{})[0]==409
        assert get('/api/atem/dsk',{},origin='http://evil.example')[0]==403
        assert b'atem-dsk-form' in get('/')[1]
        endpoints=system['server']['endpoints']
        endpoints['KAVTOR']={'ip':'127.0.0.2','port':9101}
        change={'revision':system['revision'],'server':{'protocol':'KAVTOR','endpoints':endpoints}}
        assert get('/api/system',change)[0]==202
        for _ in range(60):
            system=json.loads(get('/api/system')[1])
            if not system['pending']:break
            time.sleep(.05)
        assert system['server']['protocol']=='KAVTOR' and system['server']['port']==9101
        assert system['server']['endpoints']['OBS']==endpoints['OBS']
        assert get('/api/system',change)[0]==409
        vmix=json.loads(get('/api/vmix/status')[1]);assert len(vmix['profile']['sources'])==24
        vmix['profile']['sources'][0]='guid-one'
        assert get('/api/vmix/mappings',{'revision':vmix['revision'],'profile':vmix['profile']})[0]==200
        assert get('/api/vmix/mappings',{'revision':vmix['revision'],'profile':vmix['profile']})[0]==409
        saved=json.loads(Path(str(path)+'.vmix.json').read_text());assert saved['sources'][0]=='guid-one'
        atem=json.loads(get('/api/atem/status')[1]);assert len(atem['profile']['sources'])==24
        atem['profile']['sources'][0]='2001';atem['profile']['me']=2
        request={'revision':atem['revision'],'profile':atem['profile']}
        assert get('/api/atem/mappings',request,origin='http://evil.example')[0]==403
        assert get('/api/atem/mappings',request)[0]==200
        assert get('/api/atem/mappings',request)[0]==409
        saved=json.loads(Path(str(path)+'.atem.json').read_text());assert saved['sources'][0]=='2001' and saved['me']==2
        assert json.loads(Path(str(path)+'.vmix.json').read_text())['sources'][0]=='guid-one'
        for invalid in ['65536','-1','01','not-an-id']:
            atem['profile']['sources'][1]=invalid
            assert get('/api/atem/mappings',{'revision':1,'profile':atem['profile']})[0]==409
        meta=json.loads(get('/api/status')[1]);assert len(meta['lumaPatterns'])>=30
        assert next(p for p in meta['wipeDefaults'] if p['code']==23)['pattern']==''
        profile=json.loads(body)['profile'];profile['sources'].append({'slot':12,'scene':'<SHIFT & fuente>'})
        profile['transitions']={'dsk_name':'DSK test','dsk_scene':'Overlay','dsk2_name':'Second DSK','dsk2_scene':'Overlay 2','mix':'Disolver personalizado','wipe':'Wipe B','dme':'Mover','dme_slide':'Diapositiva elegida','dme_swipe':'Deslizar elegido','stinger':'Mi stinger','stinger_2':'Segundo stinger','stinger_2_reverse':'Mi stinger','stinger_0':'Segundo stinger','auto_ms':1250,'dsk_ms':400,'ftb_ms':1500,'softness':25,'wipe_pattern':'linear-v.png','wipe_codes':[{'code':23,'pattern':'square.png'},{'code':5,'pattern':'box-botright.png'},{'code':99,'pattern':''}]}
        assert get(data={'revision':0,'profile':profile},origin='http://evil.example')[0]==403
        assert json.loads(path.read_text())['sources']==[{'slot':0,'scene':'SRC1'}]
        assert get(data={'revision':0,'profile':profile})[0]==202
        assert json.loads(path.read_text())==profile
        assert get(data={'revision':0,'profile':profile})[0]==409
        for _ in range(60):
            if not json.loads(get('/api/status')[1])['pending']:break
            time.sleep(.05)
        bad={'version':1,'sources':[{'slot':0,'scene':'a'},{'slot':0,'scene':'b'}]}
        assert get(data={'revision':1,'profile':bad})[0]==400
        assert json.loads(path.read_text())==profile
        for invalid in ({'dsk2_name':42},{'dsk2_scene':'bad\0scene'},{'stinger_2':42},{'stinger_2_reverse':'bad\0name'},{'stinger_10':'bad'},{'dme_slide':42},{'dme_swipe':'bad\0name'},{'dsk_name':42},{'dsk_scene':'bad\0name'},{'auto_ms':49},{'auto_ms':20001},{'auto_ms':300.5},{'mix':42},{'stinger':'bad\0name'},{'unknown':300},{'softness':101},{'softness':-1},{'wipe_pattern':'missing.png'},{'wipe_codes':[{'code':1,'pattern':'../bad.png'}]},{'wipe_codes':[{'code':23,'transition':'A'},{'code':23,'transition':'B'}]},{'wipe_codes':[{'code':0,'transition':'A'}]},{'wipe_codes':[{'code':9,'transition':'A','reverse':42}]}):
            bad={**profile,'transitions':invalid}
            assert get(data={'revision':1,'profile':bad})[0]==400
            assert json.loads(path.read_text())==profile
        assert json.loads(get()[1])['revision']==1
        assert get(data={'revision':1})[0]==400
        assert not list(Path(tmp).glob('*.web-*'))
        assert get('/api/server-slot',{'slot':3})[0]==409
        assert get('/api/server-slot',{'slot':1},origin='http://evil.example')[0]==403
        def select_server(slot):
            global selected_slot
            assert get('/api/server-slot',{'slot':slot})[0]==202
            for _ in range(100):
                state=json.loads(get('/api/system')[1])
                if state.get('serverSlot')==slot and state.get('profileSlot')==slot:
                    selected_slot=slot;return
                time.sleep(.02)
            raise AssertionError('Profile not switched')
        select_server(1)
        selected_slot=0
        assert get('/api/obs/auth',{'password':'wrong-profile'})[0]==409
        selected_slot=1
        assert json.loads(get('/api/info')[1])['serverSlot']==1
        second=json.loads(get()[1]);assert second['profile']['sources']==[]
        assert get('/api/obs/auth',{'password':'second-secret'})[0]==200
        assert json.loads(Path(str(path)+'.server2.credentials.json').read_text())['obs_password']=='second-secret'
        assert json.loads(get('/api/vmix/status')[1])['profile']['sources'][0]==''
        assert json.loads(get('/api/atem/status')[1])['profile']['sources'][0]!='2001'
        second['profile']['sources']=[{'slot':0,'scene':'SECOND'}]
        assert get(data={'revision':second['revision'],'profile':second['profile']})[0]==202
        for _ in range(60):
            if not json.loads(get('/api/status')[1])['pending']:break
            time.sleep(.02)
        select_server(2);assert json.loads(get()[1])['profile']['sources']==[]
        select_server(0);assert json.loads(get()[1])['profile']==profile
        assert json.loads(get('/api/vmix/status')[1])['profile']['sources'][0]=='guid-one'
        assert json.loads(get('/api/atem/status')[1])['profile']['sources'][0]=='2001'
        assert json.loads(path.read_text())==profile
        select_server(1);assert json.loads(get()[1])['profile']['sources']==[{'slot':0,'scene':'SECOND'}]
        assert json.loads(get('/api/status')[1])['passwordSet']
        print('WEB PASS: page, persistence/apply, stale revision, bad mapping, origin, no temp leaks')
    finally:process.terminate();process.wait(timeout=5)
