from test_paths import probe
#!/usr/bin/env python3
import asyncio,json,copy
from pathlib import Path
import websockets
ROOT=Path(__file__).resolve().parents[2]
async def main():
    original={'show_transition':'','show_transition_duration':999,'hide_transition':'','hide_transition_duration':888}
    keyers={name:{**original,'scene':'','selected':'','visible':'','tie':name=='Tied','scenes':[{'name':scene}],'exclude_scenes':[]} for name,scene in [('DSK 1','DSK1'),('Other DSK','Overlay B'),('Tied','DSK1'),('Reject selection','DSK1')]}
    fired=[];errors=[]
    async def serve(ws):
        try:
            await ws.send(json.dumps({'op':0,'d':{'rpcVersion':1}}));assert json.loads(await ws.recv())['op']==1
            await ws.send(json.dumps({'op':2,'d':{}}))
            async for raw in ws:
                r=json.loads(raw)['d'];kind=r['requestType'];arg=r.get('requestData',{});data={}
                if kind=='GetVersion':data={'obsVersion':'32.2.0','obsWebSocketVersion':'5.6.3'}
                elif kind in ('GetStreamStatus','GetRecordStatus','GetVirtualCamStatus'):data={'outputActive':False}
                elif kind=='GetStudioModeEnabled':data={'studioModeEnabled':False}
                elif kind=='GetSceneList':data={'currentProgramSceneName':'PGM','currentPreviewSceneName':None,'scenes':[]}
                elif kind=='GetSceneTransitionList':data={'transitions':[{'transitionName':'Fade localized','transitionKind':'fade_transition'},{'transitionName':'Cut localized','transitionKind':'cut_transition'}]}
                elif kind=='CallVendorRequest':
                    assert arg['vendorName']=='downstream-keyer'
                    request=arg['requestType'];d=arg['requestData'];name=d['dsk_name'];k=keyers.get(name)
                    result={'success':k is not None}
                    if k:
                        if request=='get_downstream_keyer':result.update(copy.deepcopy(k))
                        elif request=='dsk_get_scene':
                            assert k['visible']==k['selected'],'restoration removed the live output'
                            result['scene']=k['selected']
                        elif request=='dsk_set_transition':
                            prefix=d['transition_type']+'_';k[prefix+'transition']=d['transition'];k[prefix+'transition_duration']=d['transition_duration']
                            if not d['transition']:k['visible']=k['scene'] # plugin currentItem fallback
                        elif request=='dsk_select_scene':
                            if name=='Reject selection':result['success']=False
                            else:
                                mode='hide' if not d['scene'] else 'show'
                                effect=k[mode+'_transition'];duration=k[mode+'_transition_duration']
                                assert effect in ('Fade localized','Cut localized')
                                assert duration in (400,500)
                                fired.append((name,effect,d['scene']));k['selected']=k['visible']=d['scene'] # setSelected leaves currentItem unchanged
                        else:raise AssertionError(request)
                    data={'responseData':result}
                else:raise AssertionError(kind)
                await ws.send(json.dumps({'op':7,'d':{'requestId':r['requestId'],'requestStatus':{'result':True,'code':100},'responseData':data}}))
        except websockets.exceptions.ConnectionClosed:pass
        except Exception as e:errors.append(e);raise
    async with websockets.serve(serve,'127.0.0.1',0) as server:
        p=await asyncio.create_subprocess_exec(probe('dsk-probe'),str(server.sockets[0].getsockname()[1]))
        try:rc=await asyncio.wait_for(p.wait(),20)
        finally:
            if p.returncode is None:p.kill();await p.wait()
        assert rc==0 and not errors,(rc,errors)
    assert len(fired)==10,fired
    assert [v[1] for v in fired[:4]]==['Fade localized']*2+['Cut localized']*2
    for name in ('DSK 1','Other DSK'):
        assert keyers[name]['show_transition']=='Cut localized'
        assert keyers[name]['hide_transition']=='Cut localized'
    assert keyers['Tied']['show_transition']==''
    print('DSK mock PASS: current item differs from selection; no destructive restoration; no PGM/preview mutation')
asyncio.run(main())
