from test_paths import probe
#!/usr/bin/env python3
"""OBS substitute with independent global selection and hostile scene overrides."""
import asyncio
import json
from pathlib import Path
import websockets
ROOT = Path(__file__).resolve().parents[2]

async def main():
    program, preview = 'A', 'B'
    original = {'transitionName':'Wrong effect', 'transitionDuration':7777}
    overrides = {'A':dict(original), 'B':dict(original)}
    expected = [('Fade localized',1234),('Wipe B',1235),('Mover localized',1236),('Stinger localized',None),('Slide localized',1238),('Swipe localized',1239),('Wipe B',1450),('Wipe B',1450),('Wipe B',900),('Wipe B',900),('Second stinger',None),('Stinger localized',None),('Second stinger',None)]
    names = [('Unrelated selected cut','cut_transition'),('Fade localized','fade_transition'),
             ('Wipe A','wipe_transition'),('Wipe B','wipe_transition'),
             ('Mover localized','move_transition'),('Stinger localized','obs_stinger_transition'),('Second stinger','obs_stinger_transition'),('Slide localized','slide_transition'),('Swipe localized','swipe_transition')]
    selected='Unrelated selected cut'
    luma_original={'luma_image':'square.png','luma_invert':True,'luma_softness':.37,'other_property':123}
    luma={n:dict(luma_original) for n,k in names if k=='wipe_transition'}
    dme_original={'direction':'down','swipe_in':False,'other_property':456}
    luma.update({n:dict(dme_original) for n,k in names if k in ('slide_transition','swipe_transition')})
    expected_luma={1:('linear-h.png',False,.03),6:('iris.png',False,.25),7:('iris.png',True,1.0),8:('linear-h.png',True,0.0),9:('square.png',False,.03)}
    fired = 0
    errors = []
    async def serve(ws):
        nonlocal program,preview,fired,selected
        task = None
        async def event(kind, data=None):
            await ws.send(json.dumps({'op':5,'d':{'eventType':kind,'eventData':data or {}}}))
        async def finish(stinger):
            nonlocal program
            await asyncio.sleep(.12)
            program=preview
            await event('CurrentProgramSceneChanged',{'sceneName':program})
            await event('SceneTransitionEnded')
            if stinger: await asyncio.sleep(.4)
            await event('SceneTransitionVideoEnded')
        try:
            await ws.send(json.dumps({'op':0,'d':{'rpcVersion':1}}))
            assert json.loads(await ws.recv())['op']==1
            await ws.send(json.dumps({'op':2,'d':{'negotiatedRpcVersion':1}}))
            async for raw in ws:
                req=json.loads(raw)['d'];kind=req['requestType'];arg=req.get('requestData',{});data={};success=True
                if kind=='GetVersion':data={'obsVersion':'32.2.0','obsWebSocketVersion':'5.6.3'}
                elif kind in ('GetStreamStatus','GetRecordStatus','GetVirtualCamStatus'):data={'outputActive':False}
                elif kind=='CallVendorRequest': data={'responseData':{'success':False}}
                elif kind=='GetStudioModeEnabled': data={'studioModeEnabled':True}
                elif kind=='GetSceneList': data={'currentProgramSceneName':program,'currentPreviewSceneName':preview,'scenes':[{'sceneName':n,'sceneIndex':i} for i,n in enumerate(['A','B'])]}
                elif kind=='GetSceneTransitionList': data={'currentSceneTransitionName':selected,'transitions':[{'transitionName':n,'transitionKind':k} for n,k in names]}
                elif kind=='GetCurrentSceneTransition':
                    data={'transitionName':selected,'transitionKind':dict(names)[selected],'transitionSettings':luma.get(selected,{})}
                elif kind=='SetCurrentSceneTransition': selected=arg['transitionName']
                elif kind=='SetCurrentSceneTransitionSettings':
                    assert selected in luma
                    if arg.get('overlay',True): luma[selected].update(arg['transitionSettings'])
                    else:luma[selected]=dict(arg['transitionSettings'])
                    # Simulate a rejection after mutation: restoration must still run.
                    success=arg['transitionSettings'].get('luma_softness')!=.42
                elif kind=='GetCurrentPreviewScene': data={'sceneName':preview}
                elif kind=='GetCurrentProgramScene': data={'sceneName':program}
                elif kind=='GetSceneSceneTransitionOverride': data=dict(overrides[arg['sceneName']])
                elif kind=='SetSceneSceneTransitionOverride': overrides[arg['sceneName']]={k:v for k,v in arg.items() if k!='sceneName'}
                elif kind=='SetCurrentPreviewScene':
                    assert arg['sceneName']!=preview
                    preview=arg['sceneName'];await event('CurrentPreviewSceneChanged',{'sceneName':preview})
                elif kind=='TriggerStudioModeTransition':
                    name,ms=expected[fired];o=overrides[preview]
                    assert o['transitionName']==name,(o,name)
                    if ms is not None: assert o['transitionDuration']==ms
                    else: assert 'transitionDuration' not in o
                    if fired in expected_luma:
                        image,invert,softness=expected_luma[fired]
                        assert selected==name and luma[name]['luma_image']==image
                        assert luma[name]['luma_invert']==invert and luma[name]['luma_softness']==softness
                        assert luma[name]['other_property']==123
                    elif fired in (4,5):
                        assert selected==name
                        assert luma[name]['direction']==('right' if fired==4 else 'down')
                        assert luma[name]['swipe_in']==(fired==5)
                        assert luma[name]['other_property']==456
                    else: assert selected=='Unrelated selected cut'
                    fired+=1;await event('SceneTransitionStarted')
                    task=asyncio.create_task(finish(ms is None))
                else: raise AssertionError(f'Unexpected request/global mutation: {kind}')
                await ws.send(json.dumps({'op':7,'d':{'requestId':req['requestId'],'requestStatus':{'result':success,'code':100 if success else 500},'responseData':data}}))
        except websockets.exceptions.ConnectionClosed: pass
        except Exception as exc:
            errors.append(exc);raise
        finally:
            if task: await task
    async with websockets.serve(serve,'127.0.0.1',0) as server:
        p=await asyncio.create_subprocess_exec(probe('transitions-probe'),str(server.sockets[0].getsockname()[1]))
        try: result=await asyncio.wait_for(p.wait(),30)
        finally:
            if p.returncode is None:p.kill();await p.wait()
        assert result==0 and not errors and fired==13,(result,errors,fired)
        assert all(o==original for o in overrides.values()),overrides
        assert selected=='Unrelated selected cut' and all(v==(dme_original if n in ('Slide localized','Swipe localized') else luma_original) for n,v in luma.items()),(selected,luma)
    print('PASS: scene overrides restored; Luma properties and global selection restored, including rejected update; no duplicate AUTO')
asyncio.run(main())
