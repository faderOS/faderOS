from test_paths import probe
import asyncio,json
from pathlib import Path
import websockets
ROOT=Path(__file__).resolve().parents[2]
async def main():
    active=[False,True,False];calls=[];errors=[];connections=0
    kinds=['Stream','Record','VirtualCam'];events=['StreamStateChanged','RecordStateChanged','VirtualcamStateChanged']
    async def serve(ws):
        nonlocal connections
        connections+=1;tasks=[]
        async def event(i):
            await ws.send(json.dumps({'op':5,'d':{'eventType':events[i],'eventData':{'outputActive':active[i]}}}))
        async def finish(i,on):
            await asyncio.sleep(.12);active[i]=on;await event(i)
        async def external():
            await asyncio.sleep(.15);active[2]=True;await event(2);await asyncio.sleep(.15);await ws.close()
        try:
            await ws.send(json.dumps({'op':0,'d':{'rpcVersion':1}}));await ws.recv();await ws.send(json.dumps({'op':2,'d':{}}))
            async for raw in ws:
                r=json.loads(raw)['d'];kind=r['requestType'];data={};ok=True
                if kind=='GetVersion':data={'obsVersion':'32.2.0','obsWebSocketVersion':'5.6.3'}
                elif kind=='GetStudioModeEnabled':data={'studioModeEnabled':False}
                elif kind=='GetSceneList':data={'currentProgramSceneName':'A','scenes':[]}
                elif kind=='GetSceneTransitionList':data={'transitions':[]}
                elif kind=='CallVendorRequest':data={'responseData':{'success':False}}
                elif kind in ['Get'+k+'Status' for k in kinds]:data={'outputActive':active[['Get'+k+'Status' for k in kinds].index(kind)]}
                elif kind in ['Start'+k for k in kinds]+['Stop'+k for k in kinds]:
                    assert connections==1,'command replayed after reconnect'
                    calls.append(kind)
                    if len(calls)==7:ok=False;tasks.append(asyncio.create_task(external()))
                    else:
                        on=kind.startswith('Start');i=kinds.index(kind[5:] if on else kind[4:]);tasks.append(asyncio.create_task(finish(i,on)))
                else:raise AssertionError(kind)
                await ws.send(json.dumps({'op':7,'d':{'requestId':r['requestId'],'requestStatus':{'result':ok,'code':100 if ok else 500},'responseData':data}}))
        except websockets.exceptions.ConnectionClosed:pass
        except Exception as e:errors.append(e);raise
        finally:
            if tasks:await asyncio.gather(*tasks,return_exceptions=True)
    async with websockets.serve(serve,'127.0.0.1',0) as server:
        p=await asyncio.create_subprocess_exec(probe('outputs-probe'),str(server.sockets[0].getsockname()[1]))
        try:rc=await asyncio.wait_for(p.wait(),20)
        finally:
            if p.returncode is None:p.kill();await p.wait()
        assert rc==0 and not errors,(rc,errors)
    assert calls==['StartStream','StopStream','StopRecord','StartRecord','StartVirtualCam','StopVirtualCam','StartStream'],calls
    print('OUTPUTS mock PASS: all three outputs; no live OBS actions')
asyncio.run(main())
