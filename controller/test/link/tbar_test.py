from test_paths import probe
#!/usr/bin/env python3
import asyncio,json
from pathlib import Path
import websockets
ROOT=Path(__file__).resolve().parents[2]
async def main():
    pg,pv='A','B';selected='Cut';active=False;settling=False;tasks=[];completed=cancelled=samples=0;errors=[]
    original={'transitionName':'Wrong','transitionDuration':777}
    overrides={n:dict(original) for n in ('A','B')}
    luma_original={'luma_image':'square.png','luma_invert':False,'luma_softness':.2}
    luma=dict(luma_original)
    names={'Cut':'cut_transition','Fade':'fade_transition','Wipe':'wipe_transition'}
    async def serve(ws):
        nonlocal pg,pv,selected,active,completed,cancelled,samples,luma,settling
        async def event(t,d=None):await ws.send(json.dumps({'op':5,'d':{'eventType':t,'eventData':d or {}}}))
        async def finish(target,previous,native_swap):
            nonlocal pg,pv,settling
            await asyncio.sleep(.08)
            pg=target
            if native_swap:
                pv=previous
                await event('CurrentPreviewSceneChanged',{'sceneName':pv})
            settling=False
            await event('CurrentProgramSceneChanged',{'sceneName':pg})
        try:
            await ws.send(json.dumps({'op':0,'d':{'rpcVersion':1}}));await ws.recv()
            await ws.send(json.dumps({'op':2,'d':{'negotiatedRpcVersion':1}}))
            async for raw in ws:
                r=json.loads(raw)['d'];k=r['requestType'];a=r.get('requestData',{});d={}
                if k=='GetVersion':d={'obsVersion':'32.2.0','obsWebSocketVersion':'5.6.3'}
                elif k=='GetStudioModeEnabled':d={'studioModeEnabled':True}
                elif k=='GetSceneList':d={'currentProgramSceneName':pg,'currentPreviewSceneName':pv,'scenes':[{'sceneName':n,'sceneIndex':i} for i,n in enumerate(('A','B'))]}
                elif k=='GetSceneTransitionList':d={'transitions':[{'transitionName':n,'transitionKind':v} for n,v in names.items()]}
                elif k in ('GetStreamStatus','GetRecordStatus','GetVirtualCamStatus'):d={'outputActive':False}
                elif k=='CallVendorRequest':d={'responseData':{'success':False}}
                elif k=='GetCurrentPreviewScene':d={'sceneName':pv}
                elif k=='GetCurrentProgramScene':d={'sceneName':pg}
                elif k=='GetCurrentSceneTransition':d={'transitionName':selected,'transitionKind':names[selected],'transitionFixed':False,'transitionSettings':dict(luma)}
                elif k=='SetCurrentSceneTransition':
                    assert not settling,'transition restored before frontend completion'
                    selected=a['transitionName']
                elif k=='GetSceneSceneTransitionOverride':d=overrides[a['sceneName']]
                elif k=='SetSceneSceneTransitionOverride':overrides[a['sceneName']]={key:val for key,val in a.items() if key!='sceneName'}
                elif k=='SetCurrentSceneTransitionSettings':
                    if a.get('overlay',True):luma.update(a['transitionSettings'])
                    else:luma=dict(a['transitionSettings'])
                elif k=='SetCurrentPreviewScene':
                    assert not settling,'preview written before frontend completion'
                    pv=a['sceneName'];await event('CurrentPreviewSceneChanged',{'sceneName':pv})
                elif k=='SetTBarPosition':
                    samples+=1;v=a['position'];assert 0<=v<=1
                    assert selected in ('Fade','Wipe') and overrides[pv]['transitionName']==selected
                    if selected=='Wipe':assert luma['luma_invert'] and luma['luma_softness']==.25
                    if not active:active=True;await event('SceneTransitionStarted')
                    if a['release']:
                        assert v in (0,1);active=False
                        if v==1:
                            completed+=1;settling=True
                            tasks.append(asyncio.create_task(finish(pv,pg,completed%2==0)))
                        else:cancelled+=1
                        await event('SceneTransitionEnded')
                    # Network delay forces the sender to coalesce many physical samples.
                    await asyncio.sleep(.015)
                else:raise AssertionError(k)
                await ws.send(json.dumps({'op':7,'d':{'requestId':r['requestId'],'requestStatus':{'result':True,'code':100},'responseData':d}}))
        except websockets.exceptions.ConnectionClosed:pass
        except Exception as e:errors.append(e);raise
    async with websockets.serve(serve,'127.0.0.1',0) as server:
        p=await asyncio.create_subprocess_exec(probe('tbar-probe'),str(server.sockets[0].getsockname()[1]))
        try:rc=await asyncio.wait_for(p.wait(),35)
        finally:
            if p.returncode is None:p.kill();await p.wait()
        if tasks:await asyncio.gather(*tasks)
        assert rc==0 and not errors,(rc,errors)
        assert completed==2 and cancelled==3 and not active,(completed,cancelled,active)
        assert selected=='Cut' and luma==luma_original and all(v==original for v in overrides.values())
        assert samples<60,samples
    print('TBAR mock PASS: forced effects, restoration, latest value, both endpoints, cancellation, shutdown')
asyncio.run(main())
