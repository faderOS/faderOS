from test_paths import probe
import asyncio, pathlib, tempfile, json, xml.etree.ElementTree as ET
from urllib.parse import parse_qs
root=pathlib.Path(__file__).resolve().parents[2]
async def main(drop=False,drop_overlay=False):
    pg,pv=1,2
    ftb=False
    first_effect="Cube"
    fader=0
    functions=[]
    pending=None
    pending_effect=None
    overlays={n:(None,False) for n in range(1,9)}
    pending_overlay=None
    effect_confirmations=0
    async def peer(reader,writer):
        nonlocal pg,pv,pending,ftb,first_effect,fader,pending_effect,effect_confirmations,pending_overlay
        async def send(data):
            raw=data.encode() if isinstance(data,str) else data
            # Exercise TCP fragmentation, including the XML byte count.
            for i in range(0,len(raw),7):
                writer.write(raw[i:i+7]);await writer.drain();await asyncio.sleep(0)
        try:
            await send('VERSION OK 29.0.0.49\r\n')
            while line:=await reader.readline():
                cmd=line.decode().strip()
                if cmd=='SUBSCRIBE TALLY':await send('TALLY OK 12\r\nSUBSCRIBE OK TALLY\r\n')
                elif cmd=='XML':
                    if pending_effect and asyncio.get_running_loop().time()>=pending_effect[1]:
                        first_effect=pending_effect[0];pending_effect=None;effect_confirmations+=1
                    if pending and asyncio.get_running_loop().time()>=pending[2]:pg,pv=pending[:2];pending=None
                    if pending_overlay and asyncio.get_running_loop().time()>=pending_overlay[3]:
                        overlays[pending_overlay[0]]=pending_overlay[1:3];pending_overlay=None
                    r=ET.Element('vmix');ET.SubElement(r,'edition').text='Trial';ins=ET.SubElement(r,'inputs')
                    for n in (1,2):ET.SubElement(ins,'input',number=str(n),key=f'key-{n}',title='Blank ñ')
                    ovs=ET.SubElement(r,'overlays')
                    for n in range(1,17):
                        value,preview=overlays.get(n,(None,False));o=ET.SubElement(ovs,'overlay',number=str(n))
                        if value is not None:
                            o.text=str(value)
                            if preview:o.set('preview','True')
                    ts=ET.SubElement(r,'transitions');ET.SubElement(ts,'transition',number='1',effect=first_effect)
                    ET.SubElement(r,'fadeToBlack').text='True' if ftb else 'False'
                    ET.SubElement(r,'preview').text=str(pv);ET.SubElement(r,'active').text=str(pg)
                    xml=ET.tostring(r)+b'\r\n';await send(f'XML {len(xml)}\r\n'.encode()+xml)
                elif cmd.startswith('FUNCTION '):
                    old=(pg,pv)
                    parts=cmd.split(' ',2);name=parts[1];args=parts[2] if len(parts)>2 else ''; q=parse_qs(args);functions.append((name,q));target=int(q['Input'][0][-1]) if 'Input'in q else pv
                    if name.startswith('PreviewOverlayInput') or name.startswith('OverlayInput'):
                        assert 'Mix' not in q and pending_overlay is None
                        if name.startswith('PreviewOverlayInput'):
                            n=int(name[len('PreviewOverlayInput'):]);value,preview=overlays[n]
                            removing=value==target and preview
                            result=(None,False) if removing else (target,True)
                        else:
                            tail=name[len('OverlayInput'):];off=tail.endswith('Out');n=int(tail[:-3] if off else tail[:-2])
                            assert off or tail.endswith('In')
                            assert ('Input' not in q) if off else ('Input' in q)
                            result=(None,False) if off else (target,False)
                        if drop_overlay and not any(name.startswith('OverlayInput') for name,args in functions[:-1]):
                            overlays[n]=result;writer.close();return
                        pending_overlay=(n,*result,asyncio.get_running_loop().time()+.06)
                        await send('FUNCTION OK Completed\r\n');continue
                    if name=='FadeToBlack':
                        assert not q;ftb=not ftb;await send('FUNCTION OK Completed\r\n');continue
                    if name=='SetTransitionEffect1':
                        assert 'Mix' not in q and pending_effect is None
                        # Acknowledgement precedes application on vMix's UI thread.
                        pending_effect=(q['Value'][0],asyncio.get_running_loop().time()+.08)
                        await send('FUNCTION OK Completed\r\n');continue
                    if name=='SetFader':
                        assert pending_effect is None, 'T-bar moved before the requested effect was applied'
                        assert 'Mix' not in q;fader=int(q['Value'][0]);assert 0<=fader<=255
                        if fader==255:pending=(pv,pg,asyncio.get_running_loop().time()+.06);fader=0
                        await send('TALLY OK 11\r\nFUNCTION OK Completed\r\n');continue
                    assert q['Mix']==['0']
                    if name=='PreviewInput':pv=target
                    elif name=='CutDirect':pg=target
                    elif name=='Cut':
                        pg,pv=pv,pg
                        for n,(value,preview) in overlays.items():
                            if preview:overlays[n]=(value,False)
                    elif name not in ('PreviewInput','CutDirect','Cut'):
                        if name.startswith('Stinger'):assert 'Duration' not in q
                        else:assert q['Duration']==['350']
                        pg,pv=target,pg
                    else:raise AssertionError(name)
                    if drop and name=='Cut':
                        writer.close();return
                    pending=(pg,pv,asyncio.get_running_loop().time()+.06);pg,pv=old
                    await send('TALLY OK 11\r\nFUNCTION OK Completed\r\n')
                else:raise AssertionError(cmd)
        except (ConnectionError, asyncio.IncompleteReadError):pass
        finally:
            writer.close()
            try:await writer.wait_closed()
            except ConnectionError:pass
    server=await asyncio.start_server(peer,'127.0.0.1',0)
    with tempfile.TemporaryDirectory() as tmp:
        legacy={'version':1,'sources':['key-1','key-2']+['']*22,'transitions':{'mix':'Fade','dme':'Merge','slide':'Slide','swipe':'Fly','wipes':{'99':{'normal':'Fly','reverse':''}}}}
        pathlib.Path(tmp+'/map.json').write_text(json.dumps(legacy))
        proc=await asyncio.create_subprocess_exec(probe('vmix-probe'),'127.0.0.1',str(server.sockets[0].getsockname()[1]),tmp+'/map.json','disconnect' if drop else 'overlay-disconnect' if drop_overlay else 'exercise')
        assert await proc.wait()==0
        saved=json.loads(pathlib.Path(tmp+'/map.json').read_text());assert saved['transitions']['version']==3
        assert saved['transitions']['wipes']['99']['normal']=='Fly'
        assert saved['transitions']['mix_types']==['Fade','AlphaFade','CrossZoom']
    server.close();await server.wait_closed()
    if drop:
        assert sum(n=='Cut' for n,q in functions)==1 and (pg,pv)==(2,1)
    elif drop_overlay:
        assert sum(n.startswith('OverlayInput') for n,q in functions)==1
        assert overlays[1]==(1,False) and (pg,pv)==(1,2)
    else:
        effects=[n for n,q in functions if not n.startswith(('OverlayInput','PreviewOverlayInput')) and n not in ('PreviewInput','CutDirect','Cut','FadeToBlack','SetFader','SetTransitionEffect1')]
        assert effects[:6]==['Fade','Wipe','Merge','Slide','Fly','Zoom']
        assert effects[6:9]==['Fade','AlphaFade','CrossZoom']
        assert effects[9:17]==['Merge','Zoom','Slide','Fly','FlyRotate','Cube','CubeZoom','VerticalSlide']
        assert effects[17:25]==['SlideReverse','VerticalSlideReverse','Wipe','VerticalWipe','BarnDoor','RollerDoor','WipeReverse','VerticalWipeReverse']
        assert first_effect=='Cube' and fader==0 and pending_effect is None
        assert effect_confirmations==8 # four starts plus their confirmed restorations
        faders=[int(q['Value'][0]) for n,q in functions if n=='SetFader'];assert faders.count(255)==2 and len(faders)<200
        assert effects[25:]==[f'Stinger{i}' for i in range(1,9)] and (pg,pv)==(1,2)
        assert all(value is None for value,preview in overlays.values())

    print('VMIX mock PASS: fragmented frames, unsolicited tally, duplicate names, no replay, restoration')
asyncio.run(main())

asyncio.run(main(drop=True))

asyncio.run(main(drop_overlay=True))
