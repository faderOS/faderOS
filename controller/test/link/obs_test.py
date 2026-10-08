from test_paths import probe
#!/usr/bin/env python3
"""Independent OBS v5 test server, real WebSocket and SHA256 challenge."""
import asyncio
import sys
import base64
import hashlib
import json
from pathlib import Path
import tempfile
import websockets
ROOT=Path(__file__).resolve().parents[2]
async def main(native_cut_swap=False):
    program='Scene 1'; preview='Scene 0'; cuts=0; connections=0; transition_checks=0; failed_auth=0; snapshots=0; punches=0; overrides={}
    def digest(s): return base64.b64encode(hashlib.sha256(s.encode()).digest()).decode()
    async def serve(ws):
        nonlocal program,preview,cuts,connections,transition_checks,failed_auth,snapshots,punches
        connections+=1
        await ws.send(json.dumps({'op':0,'d':{'rpcVersion':1,'authentication':{'salt':'salt','challenge':'challenge'}}}))
        identify=json.loads(await ws.recv())
        assert identify['op']==1
        if identify['d']['authentication']!=digest(digest('test-passwordsalt')+'challenge'):
            failed_auth+=1; await ws.close(code=4009); return
        await ws.send(json.dumps({'op':2,'d':{'negotiatedRpcVersion':1}}))
        async def event(kind,data=None):
            await ws.send(json.dumps({'op':5,'d':{'eventType':kind,'eventData':data or {}}}))
        async def external():
            nonlocal program
            await asyncio.sleep(.7)
            await event('SceneTransitionStarted')
            await asyncio.sleep(.25)
            await event('SceneTransitionEnded')
            await asyncio.sleep(2 if "--mame" in sys.argv else .5); program='Scene 2'
            await ws.send(json.dumps({'op':5,'d':{'eventType':'CurrentProgramSceneChanged','eventData':{'sceneName':program}}}))
            await asyncio.sleep(1.5 if "--mame" in sys.argv else .4); await ws.close()
        async def finish_cut(old_program):
            nonlocal preview
            # Completion occurs after the command reply, like the OBS UI callback.
            await asyncio.sleep(.08)
            if native_cut_swap:
                preview=old_program
                await event('CurrentPreviewSceneChanged',{'sceneName':preview})
            await event('SceneTransitionEnded')
            await external()
        background=None
        try:
            async for text in ws:
                m=json.loads(text); assert m['op']==6; req=m['d']; kind=req['requestType']; data={}
                if kind=='GetVersion':data={'obsVersion':'32.2.0','obsWebSocketVersion':'5.6.3'}
                elif kind in ('GetStreamStatus','GetRecordStatus','GetVirtualCamStatus'):data={'outputActive':False}
                elif kind=='CallVendorRequest': data={'responseData':{'success':False}}
                elif kind=='GetStudioModeEnabled': data={'studioModeEnabled':True}
                elif kind=='GetSceneList':
                    snapshots+=1
                    data={'currentProgramSceneName':program,'currentPreviewSceneName':preview,'scenes':[{'sceneIndex':i,'sceneName':f'Scene {i}'} for i in reversed(range(14))]}
                elif kind=='SetCurrentPreviewScene':
                    assert req['requestData']['sceneName']!=preview, 'redundant preview reload'
                    await asyncio.sleep(.15); preview=req['requestData']['sceneName']
                    await event('CurrentPreviewSceneChanged',{'sceneName':preview})
                    # Delay the RPC response: tally must already follow the event.
                    await asyncio.sleep(.25)
                elif kind=='GetCurrentSceneTransition':
                    transition_checks+=1
                    data={'transitionKind':'fade_transition' if transition_checks==1 and '--mame' not in sys.argv else 'cut_transition','transitionName':'Corte'}
                elif kind=='GetCurrentProgramScene': data={'sceneName':program}
                elif kind=='GetCurrentPreviewScene': data={'sceneName':preview}
                elif kind=='GetSceneTransitionList': data={'transitions':[{'transitionKind':'cut_transition','transitionName':'Corte'},{'transitionKind':'fade_transition','transitionName':'Mezcla'}]}
                elif kind=='GetSceneSceneTransitionOverride': data=overrides.get(req['requestData']['sceneName'],{'transitionName':'Mezcla','transitionDuration':700}) if req['requestData']['sceneName']=='Scene 0' else {'transitionName':None}
                elif kind=='SetSceneSceneTransitionOverride':
                    d=req['requestData'];overrides[d['sceneName']]={k:v for k,v in d.items() if k!='sceneName'}
                elif kind=='SetCurrentProgramScene':
                    assert overrides[req['requestData']['sceneName']]['transitionName']=='Corte'
                    assert preview=='Scene 13',preview
                    program=req['requestData']['sceneName']; punches+=1
                    await event('CurrentProgramSceneChanged',{'sceneName':program})
                elif kind=='TriggerStudioModeTransition':
                    cuts+=1; old_program=program
                    await event('SceneTransitionStarted')
                    # OBS auto-swap OFF, required for hot punch. Host owns the CUT swap.
                    program=preview
                    await event('CurrentProgramSceneChanged',{'sceneName':program})
                    background=asyncio.create_task(finish_cut(old_program))
                else: raise AssertionError(kind)
                await ws.send(json.dumps({'op':7,'d':{'requestType':kind,'requestId':req['requestId'],'requestStatus':{'result':True,'code':100},'responseData':data}}))
        except websockets.exceptions.ConnectionClosed:
            pass
        finally:
            if background: await background
    with tempfile.TemporaryDirectory(prefix='bkds-obs-') as temp:
        mapping=Path(temp)/'mappings.json'
        mapping.write_text(json.dumps({'version':1,'sources':[{'slot':i,'scene':f'Scene {i}'} for i in [0,1,2,13,23]]}))
        Path(str(mapping)+'.invalid').write_text(json.dumps({'version':1,'sources':[{'slot':0,'scene':'Changed'}],'buttons':[{'id':80,'action':'cut'}]}))
        async with websockets.serve(serve,'127.0.0.1',0) as server:
            port=server.sockets[0].getsockname()[1]
            if '--launcher' in sys.argv:
                settings=Path(temp)/'settings'
                settings.write_text(f'1 1 2 {port}\n192.168.1.50\n255.255.255.0\n192.168.1.1\n1.1.1.1\n127.0.0.1\n')
                launcher=await asyncio.create_subprocess_exec(sys.executable,str(ROOT.parent/'firmware/tools/link_demo.py'),'--headless','--config',str(settings),stdin=asyncio.subprocess.PIPE,stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.STDOUT,env={**__import__('os').environ,'BKDS_OBS_PASSWORD':'test-password'})
                try:
                    for _ in range(200):
                        if snapshots: break
                        await asyncio.sleep(.05)
                    assert snapshots>0,'saved OBS selection did not connect'
                    output,_=await asyncio.wait_for(launcher.communicate(b'quit\n'),10)
                    assert launcher.returncode==0 and cuts==0
                    assert b'OBS enabled from saved settings' in output
                    assert b'read-only' in output
                    print('PASS link-demo: saved OBS endpoint auto-connects, no mappings required, no control orders')
                finally:
                    if launcher.returncode is None: launcher.terminate(); await launcher.wait()
                return
            if '--mame' not in sys.argv:

                bad=await asyncio.create_subprocess_exec(probe('obs-probe'),str(port),str(mapping),'--bad-auth')
                assert await asyncio.wait_for(bad.wait(),10)==0 and failed_auth>0
            command=([sys.executable,str(ROOT/'test/link/mame_test.py'),'--obs-port',str(port),'--mappings',str(mapping),'--baud','38400'] if '--mame' in sys.argv else [probe('obs-probe'),str(port),str(mapping)])
            proc=await asyncio.create_subprocess_exec(*command,env={**__import__('os').environ,'BKDS_OBS_PASSWORD':'test-password'})
            try: code=await asyncio.wait_for(proc.wait(),45)
            finally:
                if proc.returncode is None: proc.kill(); await proc.wait()
            assert code==0 and cuts==1 and connections>=2,(code,cuts,connections)
            if '--mame' not in sys.argv:
                assert punches==1 and overrides['Scene 0']=={'transitionName':'Mezcla','transitionDuration':700}
asyncio.run(main())
if len(sys.argv)==1:
    # Also tolerate a user re-enabling native swap before CUT: never reload preview.
    asyncio.run(main(native_cut_swap=True))
