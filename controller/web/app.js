const $=id=>document.getElementById(id);
let pageServer=null,profileChanged=false;
function hasUnsavedChanges(){return dirty||serverDirty||vmixDirty||atemDirty||atemDskDirty||atemKeyDirty||layoutDirty||wipeDirty||presetDirty||stingerDirty||rateDirty||atemVideoDirty;}
let dirty=false,serverDirty=false,serverRevision=0,serverLoaded=false,serverSaving=false,serverPending=false,endpoints={},shownProtocol='';
let kavtorAdapter=false,layoutDirty=false,activeProtocol='OBS';
function setDirty(value){dirty=value;$('dirty-label').textContent=value?'Unsaved changes':'Configuration saved';$('dirty-label').classList.toggle('dirty',value);$('save').disabled=!value||!profile}
function markDirty(){setDirty(true)}
const pages={inputs:['Inputs','Assign scenes to bus positions.'],transitions:['Transitions','Transition effects and durations.'],wipes:['WIPE','Luma patterns, softness and keypad codes.'],stingers:['Stingers','Ten USER WIPE positions with direction variants.'],dsk:['Downstream Keyer','Assign both channels and their scenes.']};
const kavtorPages={mix:['Mixing','M/E, buses, next transition, keyers and DSK.'],wipes:['WIPE','Wipe patterns, softness, border, shadow and shape.'],stingers:['Stingers','USER WIPE: ten clips with cut points.'],multiview:['Multiview','Buses, labels, safe areas and audio meters.']};
function route(){const atem=location.hash.startsWith('#atem/');$('atem-page').hidden=!atem;if(atem){for(const id of ['host-page','obs-page','kavtor-page','vmix-page','obs-auth-card'])$(id).hidden=true;const dsk=location.hash==='#atem/dsk',keyers=location.hash==='#atem/keyers',outputs=location.hash==='#atem/outputs',multiview=location.hash==='#atem/multiview',settings=location.hash==='#atem/settings';$('atem-multiview').hidden=!multiview;$('atem-settings').hidden=!settings;$('atem-output-form').hidden=!outputs;$('atem-form').hidden=dsk||keyers||outputs||multiview||settings;$('atem-dsk-form').hidden=!dsk;$('atem-key-form').hidden=!keyers;document.querySelectorAll('#atem-page .tabs a').forEach(a=>a.classList.toggle('active',a.hash===location.hash));$('breadcrumb').textContent='Protocols / ATEM / '+(dsk?'DSK':keyers?'Keyers':outputs?'Outputs':multiview?'Multiview':settings?'Device':'Inputs');document.querySelectorAll('[data-route]').forEach(a=>a.classList.toggle('active',a.dataset.route==='atem'));atemStatus();return}$('obs-auth-card').hidden=location.hash!==''&&location.hash!=='#host';const vmix=location.hash.startsWith('#vmix/');$('vmix-page').hidden=!vmix;if(vmix){for(const id of ['host-page','obs-page','kavtor-page'])$(id).hidden=true;const trans=location.hash==='#vmix/transitions';$('vmix-input-page').hidden=trans;$('vmix-fill').hidden=trans;$('vmix-transition-page').hidden=!trans;document.querySelectorAll('#vmix-page .tabs a').forEach(a=>a.classList.toggle('active',a.hash===location.hash));$('breadcrumb').textContent='Protocols / vMix / '+(trans?'Transitions':'Inputs');document.querySelectorAll('[data-route]').forEach(a=>a.classList.toggle('active',a.dataset.route==='vmix'));vmixStatus();return}const hash=location.hash.slice(1)||'host';const isObs=hash.startsWith('obs/');const iskavtor=hash.startsWith('kavtor/');const key=hash.split('/')[1];const page=iskavtor?(kavtorPages[key]?key:'mix'):(pages[key]?key:'inputs');$('host-page').hidden=isObs||iskavtor;$('obs-page').hidden=!isObs;$('kavtor-page').hidden=!iskavtor;const routeName=isObs?'obs':iskavtor?'kavtor':'host';document.querySelectorAll('[data-route]').forEach(a=>{const active=a.dataset.route===routeName;a.classList.toggle('active',active);if(active)a.setAttribute('aria-current','page');else a.removeAttribute('aria-current')});document.querySelectorAll('#obs-page [data-page]').forEach(p=>p.hidden=p.dataset.page!==page);document.querySelectorAll('#obs-page .tabs a').forEach(a=>{const active=a.hash==='#obs/'+page;a.classList.toggle('active',active);if(active)a.setAttribute('aria-current','page');else a.removeAttribute('aria-current')});document.querySelectorAll('#kavtor-page [data-kavtor-page]').forEach(p=>p.hidden=p.dataset.kavtorPage!==page);document.querySelectorAll('#kavtor-page .tabs a').forEach(a=>{const active=a.hash==='#kavtor/'+page;a.classList.toggle('active',active);if(active)a.setAttribute('aria-current','page');else a.removeAttribute('aria-current')});if(iskavtor){$('kavtor-title').textContent=kavtorPages[page][0];$('kavtor-description').textContent=kavtorPages[page][1];$('breadcrumb').textContent='Protocols / kavtor / '+kavtorPages[page][0]}else if(isObs){$('page-title').textContent=pages[page][0];$('page-description').textContent=pages[page][1];$('breadcrumb').textContent='Protocols / OBS Studio / '+pages[page][0]}else $('breadcrumb').textContent='System / faderOS'}
window.addEventListener('hashchange',route);queueMicrotask(route);
window.addEventListener('beforeunload',e=>{if(hasUnsavedChanges()){e.preventDefault();e.returnValue=''}});
$('form').addEventListener('input',markDirty);$('form').addEventListener('change',markDirty);
function updateObsStatus(j){$('obs-dot').classList.toggle('ok',j.connected);if(activeProtocol!=='OBS')return;$('obs-badge').textContent=j.connected?'OBS · connected':'OBS · disconnected';$('obs-badge').className='badge '+(j.connected?'ok':'warn');$('studio-state').textContent=j.studio?'Active':'Inactive';$('scene-count').textContent=(j.scenes||[]).length;$('transition-count').textContent=(j.transitions||[]).length;}
function showEndpoint(protocol){const e=endpoints[protocol];if(!e)return;$('server-ip').value=e.ip;$('server-port').value=e.port;shownProtocol=protocol}
function stashEndpoint(){if(!shownProtocol)return;endpoints[shownProtocol]={ip:$('server-ip').value.trim(),port:Number($('server-port').value)}}
$('server-protocol').addEventListener('change',()=>{stashEndpoint();showEndpoint($('server-protocol').value);serverDirty=true;$('server-save').disabled=false});
async function systemStatus(force=false){
 try{const j=await api('/api/system');if(j.revision===undefined)throw Error('Host state unavailable');
 if(pageServer===null)pageServer=j.profileSlot??j.serverSlot??0;
 if(j.serverSlot!==pageServer){profileChanged=true;$('profile-notice').hidden=false;$('profile-notice').textContent='The controlled server has changed. Reload this page before editing or sending commands.';return;}
 $('host-version').textContent=j.hostVersion;$('side-version').textContent='faderOS '+j.hostVersion;$('firmware-version').textContent=j.firmwareVersion||'Unavailable';$('firmware-date').textContent=j.firmwareDate||'Firmware identification unavailable';$('serial-rate').textContent=j.baud?j.baud+' baud':'—';$('serial-state').textContent=j.panelConnected?'Panel connected':'Panel disconnected';$('panel-badge').textContent=j.panelConnected?'Panel · connected':'Panel · disconnected';$('panel-badge').className='badge '+(j.panelConnected?'ok':'warn');
 if(force||(!serverDirty&&!serverSaving&&!j.pending)){serverRevision=j.revision;endpoints=j.server.endpoints||{[j.server.protocol]:{ip:j.server.ip,port:j.server.port}};$('server-protocol').value=j.server.protocol;showEndpoint(j.server.protocol);serverLoaded=true;}
 if(serverPending&&!j.pending){serverPending=false;serverSaving=false;if(j.error){serverDirty=true;$('server-notice').textContent=j.error}else{serverDirty=false;serverRevision=j.revision;$('server-notice').textContent='Server saved and applied';}}
 if(document.activeElement!==$('server-slot'))$('server-slot').value=j.serverSlot??0;activeProtocol=j.activeProtocol||j.server.protocol;kavtorAdapter=activeProtocol==='KAVTOR';$('studio-label').textContent=activeProtocol==='OBS'?'Studio mode':'Buses';$('scene-label').textContent=activeProtocol==='OBS'?'Available scenes':'Available inputs';$('protocol-link').href=kavtorAdapter?'#kavtor/mix':'#obs/inputs';$('protocol-link').textContent=kavtorAdapter?'Open kavtor →':'Configure OBS mappings →';$('protocol-note').textContent=kavtorAdapter?'Mixing, wipes and multiview are under Protocols / kavtor.':'OBS, kavtor, vMix and ATEM are available. Select a protocol and save to connect. MIDI is planned.';
 if(activeProtocol==='ATEM'){$('protocol-link').href='#atem/inputs';$('protocol-link').textContent='Configure ATEM inputs →';}
 if(activeProtocol==='VMIX'){$('protocol-link').href='#vmix/inputs';$('protocol-link').textContent='Configure vMix inputs →';}
 if(j.adapterPending)$('server-notice').textContent='Change pending · waiting for the mixer operation to finish';
 else if(!j.adapterEnabled)$('server-notice').textContent='The selected protocol does not have an adapter yet.';
 $('server-save').disabled=!serverLoaded||!serverDirty||serverSaving||j.pending;
 }catch(e){$('server-notice').textContent=e.message;$('panel-badge').textContent='Host · no response';$('panel-badge').className='badge warn';}
}
$('server-form').addEventListener('input',()=>{serverDirty=true;$('server-save').disabled=!serverLoaded||serverSaving;$('server-notice').textContent='Unsaved changes'});
$('server-reload').onclick=()=>{if(serverDirty&&!confirm('Discard server changes?'))return;serverDirty=false;systemStatus(true)};
$('server-form').onsubmit=async e=>{e.preventDefault();if(serverSaving)return;serverSaving=true;$('server-save').disabled=true;try{stashEndpoint();await api('/api/system',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({revision:serverRevision,server:{protocol:$('server-protocol').value,endpoints}})});serverPending=true;$('server-notice').textContent='Save requested · waiting for application';await systemStatus()}catch(e){serverSaving=false;$('server-save').disabled=false;$('server-notice').textContent=e.message}};

let revision=0,profile=null,patterns=[],wipeDefaults=[];
const fields=[],effects={},rates={},wipeFields=[];
const presets=[[23,'0 · Diamond iris'],[5,'1 · Top-left corner'],[21,'2 · Square iris'],[24,'3 · Circular iris'],[18,'4 · Horizontal center opening'],[9,'5 · Top-left diagonal'],[6,'6 · Top-right corner'],[1,'7 · Vertical wipe'],[3,'8 · Horizontal wipe'],[17,'9 · Vertical center opening']];
function patternOptions(select,value,empty=true){select.replaceChildren();if(empty){const o=document.createElement('option');o.value='';o.textContent='Unassigned';select.append(o)}for(const p of patterns){const o=document.createElement('option');o.value=p.file;o.textContent=p.label;select.append(o)}select.value=value}
function wipeField(code,label,pattern='',legacy=null){
 const box=document.createElement('div');box.className='wipe-entry';const l=document.createElement('label');l.textContent=label||'DIRECT';
 const c=document.createElement('input');c.type='number';c.min=1;c.max=999999;c.step=1;c.value=code;c.disabled=!!label;
 const f=document.createElement('select');f.setAttribute('aria-label','Pattern '+(label||'DIRECT '+code));patternOptions(f,pattern);
 l.append(c);box.append(l,f);document.getElementById('wipes').append(box);
 const row={c,f,legacy,originalPattern:pattern};wipeFields.push(row);
 if(!label){const remove=document.createElement('button');remove.type='button';remove.className='secondary';remove.textContent='Remove code';remove.onclick=()=>{wipeFields.splice(wipeFields.indexOf(row),1);box.remove();markDirty()};box.append(remove)}
}
function loadWipes(rows){wipeFields.length=0;document.getElementById('wipes').replaceChildren();for(const [code,label] of presets){const saved=rows.find(r=>r.code===code);wipeField(code,label,saved?.pattern??wipeDefaults.find(r=>r.code===code)?.pattern??'',saved)}for(const r of rows)if(!presets.some(p=>p[0]===r.code))wipeField(r.code,'',r.pattern||'',r)}
document.getElementById('add-wipe').onclick=()=>{if(wipeFields.length>=32)return;wipeField(1,'');markDirty()};
for(const [key,label] of [['mix','MIX · Fade'],['wipe','WIPE · Luma Wipe instance'],['dme','DME 1 · Move'],['dme_slide','DME 2 · Slide'],['dme_swipe','DME 3 · Swipe']]){const l=document.createElement('label');l.textContent=label;const f=document.createElement('input');f.setAttribute('list','transitions');f.maxLength=512;f.placeholder=key.endsWith('_reverse')?'No REV assignment':'Automatic by type';l.append(f);effects[key]=f;document.getElementById('effects').append(l)}
const stingerList=document.getElementById('stingers');
for(const slot of [1,2,3,4,5,6,7,8,9,0]) {
 const row=document.createElement('div');row.className='mapping-row';
 const keycap=document.createElement('span');keycap.className='keycap';keycap.textContent=slot;row.append(keycap);
 for(const reverse of [false,true]) {
  const key=(slot===1?'stinger':'stinger_'+slot)+(reverse?'_reverse':'');
  const l=document.createElement('label');l.textContent='USER WIPE '+slot+(reverse?' REV':' Normal');
  const f=document.createElement('input');f.setAttribute('list','stingers');f.maxLength=512;
  f.placeholder=slot===1&&!reverse?'Automatic if unique':'Unassigned';l.append(f);effects[key]=f;row.append(l);
 }
 document.getElementById('stinger-fields').append(row);
}
for(const [key,label] of [['auto_ms','AUTO TRANS'],['dsk_ms','DSK MIX'],['ftb_ms','FTB · reserved']]){const l=document.createElement('label');l.textContent=label;const f=document.createElement('input');f.type='number';f.min=50;f.max=20000;f.step=1;f.required=true;f.value=300;l.append(f);rates[key]=f;document.getElementById('rates').append(l)}
for(let i=0;i<24;i++){const l=document.createElement('label');l.textContent='Input '+(i%12+1)+(i>=12?' · SHIFT':'');const f=document.createElement('input');f.setAttribute('list','scenes');f.maxLength=512;f.placeholder='Unassigned';l.append(f);fields.push(f);document.getElementById(i<12?'normal':'shift').append(l)}
async function api(url,options){if(options?.method==='POST'){if(pageServer===null||profileChanged)throw Error('The server has changed; reload before sending commands');options={...options,headers:{...options.headers,'X-Server-Slot':String(pageServer)}};}const r=await fetch(url,options);const j=await r.json();if(!r.ok)throw Error(j.error||'Error HTTP '+r.status);return j}
async function load(){const [j,meta]=await Promise.all([api('/api/mappings'),api('/api/status')]);patterns=meta.lumaPatterns;wipeDefaults=meta.wipeDefaults;revision=j.revision;profile=j.profile;document.getElementById('dsk-name').value=profile.transitions?.dsk_name??'DSK 1';document.getElementById('dsk-scene').value=profile.transitions?.dsk_scene??'DSK1';document.getElementById('dsk2-name').value=profile.transitions?.dsk2_name??'';document.getElementById('dsk2-scene').value=profile.transitions?.dsk2_scene??'';loadWipes(profile.transitions?.wipe_codes||[]);patternOptions(document.getElementById('wipe-pattern'),profile.transitions?.wipe_pattern??'linear-h.png',false);document.getElementById('softness').value=profile.transitions?.softness??3;for(const [k,f] of Object.entries(effects))f.value=profile.transitions?.[k]??'';for(const [k,f] of Object.entries(rates))f.value=profile.transitions?.[k]??300;fields.forEach(f=>f.value='');for(const s of profile.sources)fields[s.slot].value=s.scene;setDirty(false);document.getElementById('notice').textContent='Configuration loaded'}
function fillLayout(j){$('layout-side').value=j.programLeft?'left':'right';$('layout-name-edge').value=j.nameEdge||'bottom';$('layout-name-align').value=j.nameAlign||'center';$('layout-clock-edge').value=j.clockEdge||'top';$('layout-clock-align').value=j.clockAlign||'center';$('layout-safe-preview').checked=!!j.safePreview;$('layout-safe-program').checked=!!j.safeProgram;$('layout-meters').checked=!!j.meters;$('layout-safe-preview-aspect').value=j.safePreviewAspect||'16:9';$('layout-safe-program-aspect').value=j.safeProgramAspect||'16:9';$('layout-safe-preset').value=j.safePreset||'ebu-r95'}
$('layout-form').addEventListener('input',()=>{layoutDirty=true;$('layout-notice').textContent='Unapplied changes'});
$('layout-form').onsubmit=async e=>{e.preventDefault();const button=$('layout-save');button.disabled=true;try{const j=await api('/api/kavtor/layout',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({programLeft:$('layout-side').value==='left',nameEdge:$('layout-name-edge').value,nameAlign:$('layout-name-align').value,clockEdge:$('layout-clock-edge').value,clockAlign:$('layout-clock-align').value,safePreview:$('layout-safe-preview').checked,safeProgram:$('layout-safe-program').checked,meters:$('layout-meters').checked,safePreviewAspect:$('layout-safe-preview-aspect').value,safeProgramAspect:$('layout-safe-program-aspect').value,safePreset:$('layout-safe-preset').value})});layoutDirty=false;fillLayout(j);$('layout-notice').textContent=j.connected?'Applied to multiview':'Sent · kavtor disconnected'}catch(e){$('layout-notice').textContent=e.message}finally{button.disabled=false}};
async function status(){if(activeProtocol==='ATEM'){await atemStatus();return}if(activeProtocol==='VMIX'){await vmixStatus();return}if(activeProtocol!=='OBS'&&activeProtocol!=='KAVTOR'){$('obs-badge').textContent=activeProtocol+' · pending';$('obs-badge').className='badge warn';$('status').textContent='Adapter not implemented';return}if(kavtorAdapter){try{const j=await api('/api/kavtor/status');paintkavtor(j);const layout=await api('/api/kavtor/layout');if(!layoutDirty)fillLayout(layout);$('layout-notice').textContent=layoutDirty?'Unapplied changes':(layout.connected?'Applied to multiview':'kavtor disconnected');$('kavtor-dot').classList.toggle('ok',j.connected);$('obs-badge').textContent=j.connected?'kavtor · connected':'kavtor · disconnected';$('obs-badge').className='badge '+(j.connected?'ok':'warn');$('status').textContent=j.connected?'kavtor connected':'kavtor disconnected';$('studio-state').textContent='PVW / PGM';$('scene-count').textContent=String(Array.isArray(j.assigned)?j.assigned.filter(Boolean).length:0);$('transition-count').textContent=Array.isArray(j.patterns)?String(j.patterns.length):'—'}catch(e){$('layout-notice').textContent=e.message;$('status').textContent=e.message}return}try{const j=await api('/api/status');updateObsStatus(j);document.getElementById('status').textContent=(j.connected?'OBS connected':'OBS disconnected')+' · '+j.message+(j.pending?' · Application pending':'');const list=document.getElementById('scenes');list.replaceChildren();for(const name of j.scenes){const o=document.createElement('option');o.value=name;list.append(o)}stingerList.replaceChildren();for(const t of j.transitions||[])if(t.transitionKind==='obs_stinger_transition'){const o=document.createElement('option');o.value=t.transitionName;stingerList.append(o)}const tl=document.getElementById('transitions');tl.replaceChildren();for(const t of j.transitions||[]){const o=document.createElement('option');o.value=t.transitionName;o.label=t.transitionKind;tl.append(o)}}catch(e){document.getElementById('status').textContent=e.message}}
document.getElementById('reload').onclick=()=>{if(dirty&&!confirm('Discard unsaved OBS changes?'))return;load().catch(e=>document.getElementById('notice').textContent=e.message)};
document.getElementById('form').onsubmit=async e=>{e.preventDefault();const b=document.getElementById('save');b.disabled=true;try{if(!profile)throw Error('Reload configuration');const sources=fields.flatMap((f,slot)=>f.value?[{slot,scene:f.value}]:[]);if(new Set(sources.map(s=>s.scene)).size!==sources.length)throw Error('Do not assign the same scene to two positions');const transitions={...profile.transitions,dsk2_name:document.getElementById('dsk2-name').value,dsk2_scene:document.getElementById('dsk2-scene').value,dsk_name:document.getElementById('dsk-name').value,dsk_scene:document.getElementById('dsk-scene').value};for(const [k,f] of Object.entries(effects))transitions[k]=f.value;for(const [k,f] of Object.entries(rates)){const n=Number(f.value);if(!Number.isInteger(n)||n<50||n>20000)throw Error('Duration out of range: 50–20000 ms');transitions[k]=n}transitions.wipe_pattern=document.getElementById('wipe-pattern').value;transitions.softness=Number(document.getElementById('softness').value);if(!Number.isInteger(transitions.softness)||transitions.softness<0||transitions.softness>100)throw Error('Softness out of range: 0–100');transitions.wipe_codes=wipeFields.map(r=>{const code=Number(r.c.value);if(!Number.isInteger(code)||code<1||code>999999)throw Error('Invalid WIPE code');return r.legacy&&!Object.hasOwn(r.legacy,'pattern')&&r.f.value===r.originalPattern?{...r.legacy,code}:{code,pattern:r.f.value}});if(transitions.wipe_codes.length>32)throw Error('Maximum 32 WIPE codes');if(new Set(transitions.wipe_codes.map(r=>r.code)).size!==transitions.wipe_codes.length)throw Error('Duplicate WIPE code');const j=await api('/api/mappings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({revision,profile:{...profile,sources,transitions}})});revision=j.revision;profile={...profile,sources,transitions};setDirty(false);document.getElementById('notice').textContent='Saved · application pending';await status()}catch(e){document.getElementById('notice').textContent=e.message}finally{b.disabled=!dirty||!profile}};


let wipeDirty=false,presetDirty=false,stingerDirty=false,rateDirty=false;
const presetHost=document.getElementById('wipe-presets');
for(let i=0;i<10;i++){const label=document.createElement('label');label.textContent='Key '+i;const input=document.createElement('input');input.type='number';input.min='0';input.max='999';input.step='1';input.dataset.preset=String(i);label.append(input);presetHost.append(label);}
let kavtorPreview=false;
const kavtorBus=$('kavtor-bus');
for(let i=0;i<24;i++){
 const row=document.createElement('div');row.className='bus-row';
 const name=document.createElement('span');name.id='kavtor-name-'+i;name.textContent='SRC '+(i+1);
 const pvw=document.createElement('button');pvw.type='button';pvw.className='secondary';pvw.id='kavtor-pvw-'+i;pvw.textContent='PST';
 const pgm=document.createElement('button');pgm.type='button';pgm.id='kavtor-pgm-'+i;pgm.textContent='PGM';
 pvw.onclick=()=>kavtorCmd({cmd:'pvw',source:i},$('kavtor-mix-notice'));
 pgm.onclick=()=>kavtorCmd({cmd:'pgm',source:i},$('kavtor-mix-notice'));
 row.append(name,pvw,pgm);$('kavtor-bank-'+(i<12?1:2)).append(row);
}
function sourceSelect(){const select=document.createElement('select');const empty=document.createElement('option');empty.value='-1';empty.textContent='—';select.append(empty);for(let s=0;s<24;s++){if(s===11||s===23)continue;const o=document.createElement('option');o.value=String(s);o.textContent='SRC '+(s+1);select.append(o)}return select}
const keyRows=[];
for(let i=0;i<4;i++){
 const row=document.createElement('div');row.className='bus-row';
 const name=document.createElement('span');name.textContent='KEY '+(i+1);
 const select=sourceSelect();select.onchange=()=>{const n=Number(select.value);if(n>=0)kavtorCmd({cmd:'key_source',slot:i,source:n},$('kavtor-mix-notice'))};
 const on=document.createElement('button');on.type='button';on.textContent='ON';on.onclick=()=>kavtorCmd({cmd:'key_on',slot:i},$('kavtor-mix-notice'));
 row.append(name,select,on);$('kavtor-keys').append(row);keyRows.push({select,on});
}
const dskRows=[];
for(let i=0;i<2;i++){
 const row=document.createElement('div');row.className='bus-row';
 const name=document.createElement('span');name.textContent='DSK '+(i+1);
 const select=sourceSelect();select.onchange=()=>{const n=Number(select.value);if(n>=0)kavtorCmd({cmd:'dsk_source',slot:i,source:n},$('kavtor-mix-notice'))};
 const on=document.createElement('button');on.type='button';on.textContent='ON';on.onclick=()=>kavtorCmd({cmd:'dsk',slot:i},$('kavtor-mix-notice'));
 row.append(name,select,on);$('kavtor-dsks').append(row);dskRows.push({select,on});
}
const stingerHost=$('kavtor-stingers');
for(let i=0;i<10;i++){
 const row=document.createElement('div');row.className='mapping-row stinger-row';
 const key=document.createElement('span');key.className='keycap';key.textContent=String(i);row.append(key);
 for(const [field,label] of [['media','Clip'],['reverse','REV'],['cut','Cue'],['length','Duration']]){
  const input=document.createElement('input');input.dataset.field=field;input.setAttribute('aria-label',label+' '+i);input.maxLength=field==='media'||field==='reverse'?200:4;
  if(field==='cut'||field==='length'){input.type='number';input.min='0';input.max='3000';input.value=field==='cut'?12:50}
  row.append(input);
 }
 stingerHost.append(row);
}
async function kavtorCmd(body,notice){try{await api('/api/kavtor/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});if(notice)notice.textContent='Sent';return true}catch(e){if(notice)notice.textContent=e.message;return false}}
function paintkavtor(j){
 $('kavtor-dot').classList.toggle('ok',!!j.connected);
 for(let i=0;i<24;i++){
  const off=Array.isArray(j.assigned)&&j.assigned[i]===false;
  $('kavtor-name-'+i).textContent=(i===11||i===23?((j.me||0)>=3?'NO NEXT M/E':'NEXT M/E '+((j.me||0)+2)):'SRC '+(i+1))+(off?' · NO SOURCE':'');
  const pvw=$('kavtor-pvw-'+i),pgm=$('kavtor-pgm-'+i);
  pvw.classList.toggle('is-preview',j.preview===i);pgm.classList.toggle('is-program',j.program===i);
  pvw.classList.toggle('is-program',!!j.bothSources&&j.preview===i);
  pvw.disabled=pgm.disabled=off||!j.connected||j.busy||j.transitioning;
 }
 const keys=Array.isArray(j.keys)?j.keys:[];
 keyRows.forEach((row,i)=>{const slot=keys[i]||{};if(document.activeElement!==row.select)row.select.value=String(slot.source??-1);row.on.classList.toggle('is-program',!!slot.on);row.select.disabled=row.on.disabled=!j.connected||j.busy||j.transitioning;for(const option of row.select.options)option.disabled=Number(option.value)>=0&&Array.isArray(j.assigned)&&j.assigned[Number(option.value)]===false});
 const dsks=Array.isArray(j.dsks)?j.dsks:[];
 dskRows.forEach((row,i)=>{const slot=dsks[i]||{};if(document.activeElement!==row.select)row.select.value=String(slot.source??-1);row.on.classList.toggle('is-program',!!slot.on);row.select.disabled=row.on.disabled=!j.connected||j.busy||j.transitioning;for(const option of row.select.options)option.disabled=Number(option.value)>=0&&Array.isArray(j.assigned)&&j.assigned[Number(option.value)]===false});
 const next=j.next||{};
 for(let i=0;i<4;i++) $('kavtor-me-'+i).classList.toggle('is-program',(j.me||0)===i);
 $('kavtor-next-bkgd').classList.toggle('is-program',next.background!==false);
 for(let i=0;i<4;i++)$('kavtor-next-k'+i).classList.toggle('is-program',!!(next.keys&&next.keys[i]));
 kavtorPreview=!!j.transitionPreview;
 const rehearsal=$('kavtor-preview');rehearsal.classList.toggle('is-preview',kavtorPreview);rehearsal.setAttribute('aria-pressed',String(kavtorPreview));rehearsal.disabled=!j.connected||!j.transitionPreviewSupported||j.busy||j.transitioning;
 for(const id of ['kavtor-cut','kavtor-auto','kavtor-wipe','kavtor-rate-apply','kavtor-next-bkgd',...Array.from({length:4},(_,i)=>'kavtor-next-k'+i),...Array.from({length:4},(_,i)=>'kavtor-me-'+i)])$(id).disabled=!j.connected||j.busy||j.transitioning;
 if(!rateDirty&&document.activeElement!==$('kavtor-rate'))$('kavtor-rate').value=j.autoFrames||25;
 const select=$('wipe-pattern-kavtor');
 if(Array.isArray(j.patterns)&&select.options.length!==j.patterns.length){select.replaceChildren();for(const p of j.patterns){const o=document.createElement('option');o.value=p.id;o.textContent=p.label||p.id;select.append(o)}}
 if(!wipeDirty&&!(document.activeElement&&document.activeElement.closest('#wipe-form'))){
  if(j.wipePattern)select.value=j.wipePattern;if(j.wipeDir)$('wipe-dir').value=j.wipeDir;if(j.wipeEdge)$('wipe-edge').value=j.wipeEdge;
  $('wipe-amount').value=j.wipeEdgeAmount??8;$('wipe-color').value=j.wipeBorderColor||'#ffffff';
  $('wipe-multi').value=String(j.wipeMulti??1);$('wipe-border').value=j.wipeBorder??0;$('wipe-shadow').value=j.wipeShadow??0;
  $('wipe-aspect').value=Math.round(100*(j.wipeAspectW||1)/(j.wipeAspectH||1));
  $('wipe-pos-x').value=j.wipePosX??500;$('wipe-pos-y').value=j.wipePosY??500;
 }
 if(!presetDirty&&!(document.activeElement&&document.activeElement.closest('#wipe-presets-form'))&&Array.isArray(j.wipePresets)){
  document.querySelectorAll('#wipe-presets input').forEach((input,i)=>{input.value=j.wipePresets[i]??0});
 }
 if(!stingerDirty&&!(document.activeElement&&document.activeElement.closest('#stinger-form'))){
  const rows=Array.isArray(j.stingers)?j.stingers:[];
  document.querySelectorAll('#kavtor-stingers .stinger-row').forEach((row,i)=>{const slot=rows[i]||{};row.querySelector('[data-field=media]').value=slot.media||'';row.querySelector('[data-field=reverse]').value=slot.reverse||'';row.querySelector('[data-field=cut]').value=slot.cutFrames??12;row.querySelector('[data-field=length]').value=slot.lengthFrames??50});
 }
}
paintkavtor({connected:false,assigned:Array(24).fill(false)});
for(let i=0;i<4;i++) $('kavtor-me-'+i).onclick=()=>kavtorCmd({cmd:'me',slot:i},$('kavtor-mix-notice'));
$('kavtor-cut').onclick=()=>kavtorCmd({cmd:'cut'},$('kavtor-mix-notice'));
$('kavtor-auto').onclick=()=>kavtorCmd({cmd:'mix'},$('kavtor-mix-notice'));
$('kavtor-wipe').onclick=()=>kavtorCmd({cmd:'wipe'},$('kavtor-mix-notice'));
$('kavtor-next-bkgd').onclick=()=>kavtorCmd({cmd:'next',target:'background'},$('kavtor-mix-notice'));
for(let i=0;i<4;i++)$('kavtor-next-k'+i).onclick=()=>kavtorCmd({cmd:'next',target:'key',slot:i},$('kavtor-mix-notice'));
$('kavtor-preview').onclick=()=>kavtorCmd({cmd:'trans_preview',on:!kavtorPreview},$('kavtor-mix-notice'));
$('kavtor-rate').addEventListener('input',()=>{rateDirty=true});
$('kavtor-rate-apply').onclick=async()=>{const n=Number($('kavtor-rate').value);if(!Number.isInteger(n)||n<1||n>1000){$('kavtor-mix-notice').textContent='Duration 1–1000 frames';return}if(await kavtorCmd({cmd:'rate',frames:n},$('kavtor-mix-notice')))rateDirty=false};
$('wipe-form').addEventListener('input',()=>{wipeDirty=true;$('wipe-notice').textContent='Unapplied changes'});
$('wipe-form').onsubmit=async e=>{
 e.preventDefault();
 const amount=Number($('wipe-amount').value),border=Number($('wipe-border').value),shadow=Number($('wipe-shadow').value);
 const posX=Number($('wipe-pos-x').value),posY=Number($('wipe-pos-y').value),notice=$('wipe-notice');
 if([amount,border,shadow].some(v=>!Number.isInteger(v)||v<0||v>40)){notice.textContent='Widths must be 0–40 units';return}
 if([posX,posY].some(v=>!Number.isInteger(v)||v<0||v>1000)){notice.textContent='Position 0–1000';return}
 const pattern=$('wipe-pattern-kavtor').value;if(!pattern){notice.textContent='Choose a pattern';return}
 const aspectW=Number($('wipe-aspect').value),aspectH=100;
 if(!Number.isInteger(aspectW)||aspectW<1||aspectW>1000){notice.textContent='Aspect must be 1–1000%';return}
 if(await kavtorCmd({cmd:'wipe_settings',pattern,direction:$('wipe-dir').value,edge:$('wipe-edge').value,amount,color:$('wipe-color').value,multi:Number($('wipe-multi').value),border,shadow,aspectW,aspectH,posX,posY},notice)){
  wipeDirty=false;notice.textContent='Wipe settings sent';
 }
};
$('wipe-presets-form').addEventListener('input',()=>{presetDirty=true;$('wipe-presets-notice').textContent='Unsaved changes'});
$('wipe-presets-form').onsubmit=async e=>{e.preventDefault();const presets=[...document.querySelectorAll('#wipe-presets input')].map(input=>Number(input.value));if(presets.some(code=>!Number.isInteger(code)||code<0||code>999)){$('wipe-presets-notice').textContent='Each preset is a code from 0 to 999';return}if(await kavtorCmd({cmd:'wipe_presets',presets},$('wipe-presets-notice'))){presetDirty=false;$('wipe-presets-notice').textContent='Presets saved'}};
$('stinger-form').addEventListener('input',()=>{stingerDirty=true;$('stinger-notice').textContent='Unsaved changes'});
$('stinger-form').onsubmit=async e=>{e.preventDefault();const slots=[...document.querySelectorAll('#kavtor-stingers .stinger-row')].map(row=>{const cut=Number(row.querySelector('[data-field=cut]').value),length=Number(row.querySelector('[data-field=length]').value);return{media:row.querySelector('[data-field=media]').value.trim(),reverse:row.querySelector('[data-field=reverse]').value.trim(),cutFrames:cut,lengthFrames:length}});for(const slot of slots){if(!Number.isInteger(slot.cutFrames)||slot.cutFrames<0||slot.cutFrames>2999||!Number.isInteger(slot.lengthFrames)||slot.lengthFrames<=slot.cutFrames||slot.lengthFrames>3000){$('stinger-notice').textContent='Cue and duration: duration must exceed the cue, up to 3000 frames';return}}if(await kavtorCmd({cmd:'stingers',slots},$('stinger-notice'))){stingerDirty=false;$('stinger-notice').textContent='Stingers saved'}};

load().catch(e=>$('notice').textContent=e.message);status();systemStatus();setInterval(()=>{status();systemStatus()},2000);

let vmixDirty=false,vmixRevision=0,vmixInputs=[],vmixSaving=false,vmixPaintKey="";
const vmixFields=[],vmixMix=[],vmixDme=[],vmixStingers=[],vmixWipes=new Map();
const vmixFunctions=['','Fade','Merge','AlphaFade','CrossZoom','Zoom','Slide','Fly','FlyRotate','Cube','CubeZoom','VerticalSlide','Wipe','VerticalWipe','WipeReverse','SlideReverse','VerticalWipeReverse','VerticalSlideReverse','BarnDoor','RollerDoor'];
function vmixEffectOptions(select,value){select.replaceChildren();for(const name of vmixFunctions){const o=document.createElement('option');o.value=name;o.textContent=name||'Unassigned';select.append(o)}select.value=value||'';}
function vmixChanged(){vmixDirty=true;$('vmix-save').disabled=false;$('vmix-notice').textContent='Unsaved changes';}
function vmixSelect(label,options=vmixFunctions){const l=document.createElement('label');l.textContent=label;const f=document.createElement('select');for(const name of options){const o=document.createElement('option');o.value=name;o.textContent=name||'Unassigned';f.append(o)}l.append(f);f.onchange=vmixChanged;return {l,f};}
for(let i=0;i<3;i++){const {l,f}=vmixSelect('MIX '+(i+1));$('vmix-effects').append(l);vmixMix.push(f);}
for(let i=0;i<8;i++){const row=document.createElement('div');row.className='vmix-binding-row';const fields={};for(const side of ['normal','reverse']){const {l,f}=vmixSelect('DME '+i+' '+(side==='normal'?'NORM':'REV'));row.append(l);fields[side]=f;} $('vmix-dme').append(row);vmixDme.push(fields);}
for(let i=0;i<8;i++){const row=document.createElement('div');row.className='vmix-binding-row';const {l,f}=vmixSelect('Key '+(i+1),['',...Array.from({length:8},(_,n)=>'Stinger'+(n+1))]);const label=document.createElement('label');label.textContent='Total length · ms';const length=document.createElement('input');length.type='number';length.min=50;length.max=60000;length.step=1;length.oninput=vmixChanged;label.append(length);row.append(l,label);$('vmix-stingers').append(row);vmixStingers.push({f,length});}
function vmixWipeRow(code,binding={}){const row=document.createElement('div');row.className='grid';const label=document.createElement('span');label.textContent='Sony '+code;row.append(label);const fields={};for(const side of ['normal','reverse']){const l=document.createElement('label');l.textContent=side==='normal'?'NORM':'REV';const f=document.createElement('select');vmixEffectOptions(f,binding[side]);l.append(f);row.append(l);fields[side]=f;f.onchange=vmixChanged;}$('vmix-wipes').append(row);vmixWipes.set(String(code),fields);}
$('vmix-add-wipe').onclick=()=>{const code=Number($('vmix-wipe-code').value);if(!Number.isInteger(code)||code<1||code>999){$('vmix-notice').textContent='Sony code must be 1–999';return;}if(vmixWipes.has(String(code))){$('vmix-notice').textContent='Code already listed';return;}if(vmixWipes.size>=32){$('vmix-notice').textContent='Maximum 32 wipe bindings';return;}vmixWipeRow(code);vmixChanged();};
for(let i=0;i<24;i++){const label=document.createElement('label');label.textContent=(i<12?'NORMAL ':'SHIFT ')+(i%12+1);const select=document.createElement('select');label.append(select);$('vmix-sources').append(label);vmixFields.push(select);select.onchange=()=>{vmixDirty=true;$('vmix-save').disabled=false;$('vmix-notice').textContent='Unsaved changes'};}
function vmixOptions(select,value){select.replaceChildren();const empty=document.createElement('option');empty.value='';empty.textContent='Unassigned';select.append(empty);for(const input of vmixInputs){const o=document.createElement('option');o.value=input.key;o.textContent=input.number+' · '+input.title;select.append(o);}if(value&&!vmixInputs.some(i=>i.key===value)){const missing=document.createElement('option');missing.value=value;missing.textContent='Unavailable · '+value;select.append(missing);}select.value=value;}
async function vmixStatus(){try{const j=await api('/api/vmix/status');vmixInputs=j.inputs||[];$('vmix-status').textContent=(j.connected?'Connected':'Disconnected')+' · '+j.message;$('vmix-dot').classList.toggle('ok',j.connected);if(activeProtocol==='VMIX'){$('obs-badge').textContent=j.connected?'vMix · connected':'vMix · disconnected';$('obs-badge').className='badge '+(j.connected?'ok':'warn');$('status').textContent=j.message;$('scene-count').textContent=vmixInputs.length;$('studio-state').textContent='PST / PGM';$('transition-count').textContent=Object.keys(j.profile.transitions.wipes).length+' WIPE mappings';}const paintKey=JSON.stringify([j.revision,j.inputs,j.profile]);if(!vmixDirty&&!vmixSaving&&paintKey!==vmixPaintKey){vmixPaintKey=paintKey;vmixRevision=j.revision;vmixFields.forEach((f,i)=>vmixOptions(f,j.profile.sources[i]||''));const t=j.profile.transitions;vmixMix.forEach((f,i)=>vmixEffectOptions(f,t.mix_types[i]));vmixDme.forEach((f,i)=>{for(const side of ["normal","reverse"])vmixEffectOptions(f[side],t.dme_types[i][side]);});vmixStingers.forEach((s,i)=>{s.f.value=t.stingers[i].function;s.length.value=t.stingers[i].total_ms;});vmixWipes.clear();$('vmix-wipes').replaceChildren();for(const code of [...new Set(['23','5','21','24','18','9','6','1','3','17',...Object.keys(t.wipes)])])vmixWipeRow(code,t.wipes[code]);}}catch(e){$('vmix-status').textContent=e.message;}}
$('vmix-fill').onclick=()=>{vmixFields.forEach((f,i)=>vmixOptions(f,vmixInputs[i]?.key||''));vmixDirty=true;$('vmix-save').disabled=false;};
$('vmix-reload').onclick=()=>{if(vmixDirty&&!confirm('Discard vMix changes?'))return;vmixDirty=false;vmixPaintKey='';$('vmix-save').disabled=true;vmixStatus();};
$('vmix-form').onsubmit=async e=>{e.preventDefault();if(vmixSaving)return;vmixSaving=true;const controls=[...$('vmix-form').querySelectorAll('input,select,button')];controls.forEach(f=>f.disabled=true);$('vmix-save').disabled=true;try{await api('/api/vmix/mappings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({revision:vmixRevision,profile:{version:1,sources:vmixFields.map(f=>f.value),transitions:{version:3,mix_types:vmixMix.map(f=>f.value),dme_types:vmixDme.map(f=>({normal:f.normal.value,reverse:f.reverse.value})),stingers:vmixStingers.map(s=>({function:s.f.value,total_ms:Number(s.length.value)})),wipes:Object.fromEntries([...vmixWipes].filter(([,f])=>f.normal.value||f.reverse.value).map(([k,f])=>[k,{normal:f.normal.value,reverse:f.reverse.value}]))}}})});vmixDirty=false;$('vmix-notice').textContent='Mappings saved';}catch(e){$('vmix-notice').textContent=e.message;}finally{vmixSaving=false;controls.forEach(f=>f.disabled=false);$('vmix-save').disabled=!vmixDirty;await vmixStatus();}};
$('obs-auth-form').onsubmit=async e=>{e.preventDefault();const button=e.currentTarget.querySelector('button');button.disabled=true;try{await api('/api/obs/auth',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({password:$('obs-password').value})});$('obs-password').value='';$('obs-auth-notice').textContent='Password saved. OBS will reconnect if it is the active protocol.';}catch(e){$('obs-auth-notice').textContent=e.message;}finally{button.disabled=false;}};

let atemDirty=false,atemRevision=0,atemInputs=[],atemSaving=false,atemProfile=null,atemMes=[];
const atemFields=[];
for(let i=0;i<24;i++){
 const label=document.createElement('label');label.textContent=(i<12?'NORMAL ':'SHIFT ')+(i%12+1);
 const select=document.createElement('select');label.append(select);$('atem-sources').append(label);atemFields.push(select);
 select.onchange=atemChanged;
}
function atemChanged(){atemDirty=true;$('atem-save').disabled=false;$('atem-notice').textContent='Unsaved changes';}
function atemAvailable(input){const me=Number($('atem-me').value);return me<8&&(input.meAvailability&(1<<me));}
function atemOptions(select,value,index){
 select.replaceChildren();const empty=document.createElement('option');empty.value='';empty.textContent='Unassigned';select.append(empty);
 if(index===11||index===23){const next=Number($('atem-me').value)+1;const source=10010+10*next;const available=atemMes.some(m=>m.index===next)&&atemInputs.some(i=>i.number===source&&i.kind===128&&atemAvailable(i));empty.textContent=available?'Program M/E '+(next+1)+' · reserved':'No next M/E available';select.disabled=true;return;}
 select.disabled=atemSaving;
 for(const input of atemInputs.filter(atemAvailable)){const o=document.createElement('option');o.value=input.key;o.textContent=input.shortName+' · '+input.title+' ['+input.number+']';select.append(o);}
 if(value&&!atemInputs.some(i=>i.key===value&&atemAvailable(i))){const missing=document.createElement('option');missing.value=value;missing.textContent='Unavailable on this M/E · '+value;select.append(missing);}
 select.value=value;
}
function atemFillProfile(){if(!atemProfile)return;const key=$('atem-me').value;const custom=atemProfile.meSources?.[key];$('atem-shared').checked=!custom;atemFields.forEach((f,i)=>atemOptions(f,(custom||atemProfile.sources)[i]||'',i));}
let atemEditingMe='0';
$('atem-me').onchange=()=>{if(atemDirty&&!confirm('Discard mapping changes?')){$('atem-me').value=atemEditingMe;return;}atemEditingMe=$('atem-me').value;atemDirty=false;$('atem-save').disabled=true;atemFillProfile();};
$('atem-shared').onchange=()=>{if($('atem-shared').checked)atemFields.forEach((f,i)=>atemOptions(f,atemProfile.sources[i]||'',i));atemChanged();};
async function atemStatus(force=false){try{
 const j=await api('/api/atem/status');atemInputs=j.inputs||[];atemMes=j.mixEffects||[];paintAtemDsk(j,force);paintAtemKey(j,force);paintAtemMultiview(j,force);
 if(atemOutputPending){const p=atemOutputPending;if(j.connected&&!j.busy&&(j.outputs||[]).some(o=>o.index===p.output&&o.source===p.source)){atemOutputPending=null;$('atem-output-notice').textContent='Route confirmed by ATEM';}else if(!j.connected||Date.now()>p.until){atemOutputPending=null;$('atem-output-notice').textContent='Change not confirmed. Check the active source before retrying.';force=true;}}
 const routes=$('atem-output-routes');if(force||atemOutputPending||!routes.contains(document.activeElement)){routes.replaceChildren();for(const output of j.outputs||[]){const label=document.createElement('label');label.textContent=output.name;const select=document.createElement('select');for(const source of output.choices||[]){const option=document.createElement('option');option.value=source.id;option.textContent=source.name;select.append(option);}select.value=output.source;select.disabled=!j.connected||j.busy||!!atemOutputPending||activeProtocol!=='ATEM';select.onchange=async()=>{if(atemOutputPending)return;atemOutputPending={output:output.index,source:Number(select.value),until:Date.now()+5000};try{await api('/api/atem/output',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({output:output.index,source:Number(select.value)})});$('atem-output-notice').textContent='Waiting for ATEM confirmation';}catch(e){atemOutputPending=null;$('atem-output-notice').textContent=e.message;}await atemStatus(true);};label.append(select);routes.append(label);}}
 $('atem-status').textContent=(j.connected?'Connected':'Disconnected')+' · '+(j.product||'ATEM')+' · Active M/E '+(j.selectedMe+1)+' · '+j.message;
 $('atem-dot').classList.toggle('ok',j.connected);
 const vf=$('atem-video-mode');if(!atemVideoDirty&&document.activeElement!==vf){vf.replaceChildren();for(const mode of j.videoFormats||[]){const o=document.createElement('option');o.value=mode.id;o.textContent=mode.name;vf.append(o);}vf.value=j.videoFormat;}vf.dataset.current=j.videoFormat;if(atemVideoPending){if(j.connected&&j.videoFormat===atemVideoPending.mode){$('atem-video-notice').textContent='Format confirmed by ATEM';atemVideoPending=null;}else if(Date.now()>atemVideoPending.until){$('atem-video-notice').textContent='Change not confirmed. Check the active format before retrying.';atemVideoPending=null;}}$('atem-video-apply').disabled=!j.connected||j.busy||activeProtocol!=='ATEM'||!(j.videoFormats||[]).length;
 const c=j.capabilities||{};
 $('atem-capabilities').textContent='Detected capabilities: '+(c.mixEffects??'—')+' M/E · '+(c.downstreamKeyers??'—')+' DSK · '+(c.auxiliaries??'—')+' AUX · '+(c.dves??'—')+' DVE. '+atemMes.map(me=>'M/E '+(me.index+1)+': '+(me.upstreamKeyers<0?'?':me.upstreamKeyers)+' USK').join(' | ');
 if(activeProtocol==='ATEM'){$('obs-badge').textContent=j.connected?'ATEM · connected':'ATEM · disconnected';$('obs-badge').className='badge '+(j.connected?'ok':'warn');$('status').textContent=j.message;$('scene-count').textContent=atemInputs.length;$('studio-state').textContent='PST / PGM · M/E '+(j.selectedMe+1);$('transition-count').textContent='Model dependent';}
 if(!atemDirty&&!atemSaving&&(force||!$('atem-form').contains(document.activeElement))){atemRevision=j.revision;atemProfile=j.profile;const select=$('atem-me');select.replaceChildren();const indices=atemMes.map(m=>m.index);if(!indices.length)indices.push(0);if(!indices.includes(Number(atemEditingMe)))atemEditingMe=String(indices[0]);for(const index of indices){const o=document.createElement('option');o.value=index;o.textContent='M/E '+(index+1);select.append(o);}select.value=atemEditingMe;atemFillProfile();}
}catch(e){$('atem-status').textContent=e.message;}}
let atemVideoDirty=false,atemVideoPending=null,atemOutputPending=null;
$('atem-video-mode').onchange=()=>{atemVideoDirty=true;};
$('atem-video-form').onsubmit=async e=>{e.preventDefault();if(!confirm('Changing the format interrupts output and may clear the media pool. Continue?'))return;const mode=Number($('atem-video-mode').value),previous=Number($('atem-video-mode').dataset.current);try{await api('/api/atem/video-format',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode,previous,confirm:true})});atemVideoPending={mode,until:Date.now()+10000};atemVideoDirty=false;$('atem-video-notice').textContent='Change requested; waiting for the active format';await atemStatus();}catch(e){$('atem-video-notice').textContent=e.message;}};
$('server-slot').onchange=async()=>{if(hasUnsavedChanges()&&!confirm('Discard unsaved changes and switch server?')){$('server-slot').value=pageServer;return;}try{await api('/api/server-slot',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({slot:Number($('server-slot').value)})});const wanted=Number($('server-slot').value);for(let i=0;i<120;i++){await new Promise(r=>setTimeout(r,500));const state=await api('/api/system');if(state.serverSlot===wanted&&state.profileSlot===wanted){dirty=serverDirty=vmixDirty=atemDirty=atemDskDirty=atemKeyDirty=layoutDirty=wipeDirty=presetDirty=stingerDirty=rateDirty=atemVideoDirty=false;location.reload();return;}}throw Error('Server change pending; check for an active transition');}catch(e){alert(e.message);}};
$('atem-fill').onclick=()=>{const inputs=atemInputs.filter(atemAvailable);let source=0;atemFields.forEach((f,i)=>atemOptions(f,i===11||i===23?'':inputs[source++]?.key||'',i));atemChanged();};
$('atem-reload').onclick=()=>{if(atemDirty&&!confirm('Discard ATEM changes?'))return;atemDirty=false;$('atem-save').disabled=true;atemStatus(true);};
$('atem-form').onsubmit=async e=>{e.preventDefault();if(atemSaving||!atemProfile)return;atemSaving=true;atemFields.forEach(f=>f.disabled=true);$('atem-me').disabled=true;$('atem-shared').disabled=true;$('atem-save').disabled=true;
 const profile=structuredClone(atemProfile);profile.version=2;profile.meSources??={};const row=atemFields.map((f,i)=>i===11||i===23?'':f.value);
 if($('atem-shared').checked){profile.sources=row;delete profile.meSources[atemEditingMe];}else profile.meSources[atemEditingMe]=row;
 try{await api('/api/atem/mappings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({revision:atemRevision,profile})});atemDirty=false;$('atem-notice').textContent='Mappings saved';}
 catch(e){$('atem-notice').textContent=e.message;}
 finally{atemSaving=false;atemFields.forEach((f,i)=>f.disabled=i===11||i===23);$('atem-me').disabled=false;$('atem-shared').disabled=false;$('atem-save').disabled=!atemDirty;await atemStatus(true);}};

let atemDskDirty=false,atemDskPending=null,atemDskRevision=0,atemDskSelected='0',atemDsks=[];
const atemDskNumbers=['fill','key','frames','clip','gain'],atemDskBools=['tie','premultiplied','invert'];
function paintAtemDsk(j,force=false){
 atemDsks=j.dsks||[];const select=$('atem-dsk-index');
 if(!atemDskDirty&&!atemDskPending){select.replaceChildren();for(const d of atemDsks){const o=document.createElement('option');o.value=d.index;o.textContent='DSK '+(d.index+1)+(d.index>1?' · web only':'');select.append(o);}select.value=atemDskSelected;}
 const d=atemDsks.find(d=>String(d.index)===atemDskSelected);
 if(atemDskPending){const p=atemDskPending;if(!j.connected||Date.now()>p.until){atemDskPending=null;atemDskDirty=true;$('atem-dsk-notice').textContent='Application not confirmed. Reload the state before retrying.';}else if(d&&[...atemDskNumbers,...atemDskBools].every(k=>d[k]===p.value[k])&&!j.busy){atemDskPending=null;atemDskDirty=false;$('atem-dsk-notice').textContent='Configuration confirmed by ATEM';}}
 $('atem-dsk-state').textContent=!d?.known?'State unavailable':d.transitioning?'Transitioning':d.onAir?'On air':'Off air';
 const blocked=!j.connected||!d?.known||d.onAir||d.transitioning||j.busy||!!atemDskPending||activeProtocol!=='ATEM';
 $('atem-dsk-fields').disabled=blocked;$('atem-dsk-save').disabled=blocked||!atemDskDirty;$('atem-dsk-index').disabled=!!atemDskPending;
 if(!d||atemDskDirty||atemDskPending||(!force&&$('atem-dsk-form').contains(document.activeElement)))return;
 atemDskRevision=d.revision;
 for(const k of ['fill','key']){const f=$('atem-dsk-'+k);f.replaceChildren();for(const input of atemInputs.filter(i=>k==='key'?(i.sourceAvailability&16):(i.meAvailability&1))){const o=document.createElement('option');o.value=input.number;o.textContent=input.title+' ['+input.number+']';f.append(o);}if(!Array.from(f.options).some(o=>Number(o.value)===d[k])){const o=document.createElement('option');o.value=d[k];o.textContent='Unavailable · '+d[k];f.append(o);}f.value=d[k];}
 for(const k of ['frames','clip','gain'])$('atem-dsk-'+k).value=d[k];for(const k of atemDskBools)$('atem-dsk-'+k).checked=d[k];
}
$('atem-dsk-fields').oninput=()=>{atemDskDirty=true;$('atem-dsk-save').disabled=false;$('atem-dsk-notice').textContent='Unapplied changes';};
$('atem-dsk-index').onchange=()=>{if(atemDskDirty&&!confirm('Discard DSK changes?')){$('atem-dsk-index').value=atemDskSelected;return;}atemDskSelected=$('atem-dsk-index').value;atemDskDirty=false;atemStatus(true);};
$('atem-dsk-reload').onclick=()=>{if(atemDskPending)return;if(atemDskDirty&&!confirm('Discard DSK changes?'))return;atemDskDirty=false;atemStatus(true);};
$('atem-dsk-form').onsubmit=async e=>{e.preventDefault();if(atemDskPending)return;const value={index:Number(atemDskSelected),revision:atemDskRevision};for(const k of atemDskNumbers)value[k]=Number($('atem-dsk-'+k).value);for(const k of atemDskBools)value[k]=$('atem-dsk-'+k).checked;atemDskPending={value,until:Date.now()+5000};$('atem-dsk-fields').disabled=true;$('atem-dsk-index').disabled=true;$('atem-dsk-save').disabled=true;try{await api('/api/atem/dsk',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(value)});$('atem-dsk-notice').textContent='Waiting for ATEM confirmation';}catch(e){atemDskPending=null;$('atem-dsk-notice').textContent=e.message;}await atemStatus();};

let atemKeyDirty=false,atemKeyPending=null,atemKeyRevision=0,atemKeySelected='0:0',atemKeys=[];
const atemKeyNumbers=['type','fill','key','clip','gain'],atemKeyBools=['premultiplied','invert'];
function atemKeyLuma(){const luma=Number($('atem-key-type').value)===0;$('atem-key-luma').hidden=!luma;$('atem-key-luma').disabled=!luma;}
function paintAtemKey(j,force=false){
 atemKeys=j.keys||[];const select=$('atem-key-index');
 if(!atemKeyDirty&&!atemKeyPending){select.replaceChildren();if(!atemKeys.some(k=>k.me+':'+k.index===atemKeySelected))atemKeySelected=atemKeys.length?atemKeys[0].me+':'+atemKeys[0].index:'0:0';for(const k of atemKeys){const o=document.createElement('option');o.value=k.me+':'+k.index;o.textContent='M/E '+(k.me+1)+' · KEY '+(k.index+1);select.append(o);}select.value=atemKeySelected;}
 const k=atemKeys.find(k=>k.me+':'+k.index===atemKeySelected);
 if(atemKeyPending){const p=atemKeyPending;if(!j.connected||Date.now()>p.until){atemKeyPending=null;atemKeyDirty=true;$('atem-key-notice').textContent='Application not confirmed. Reload before retrying.';}else if(k&&['type','fill','key',...(p.value.type===0?['clip','gain',...atemKeyBools]:[])].every(f=>k[f]===p.value[f])&&!j.busy){atemKeyPending=null;atemKeyDirty=false;$('atem-key-notice').textContent='Configuration confirmed by ATEM';}}
 $('atem-key-state').textContent=!k?.known?'State unavailable':k.transitioning?'Transitioning':k.onAir?'On air':'Off air';
 const blocked=!j.connected||!k?.known||k.onAir||k.transitioning||j.busy||!!atemKeyPending||activeProtocol!=='ATEM';
 $('atem-key-fields').disabled=blocked;$('atem-key-save').disabled=blocked||!atemKeyDirty;$('atem-key-index').disabled=!!atemKeyPending;
 for(const o of $('atem-key-type').options)o.disabled=Number(o.value)===3&&!j.capabilities?.dves||Number(o.value)===0&&!k?.lumaKnown;
 if(!k||atemKeyDirty||atemKeyPending||(!force&&$('atem-key-form').contains(document.activeElement)))return;
 atemKeyRevision=k.revision;
 for(const field of ['fill','key']){const f=$('atem-key-'+field);f.replaceChildren();for(const input of atemInputs.filter(i=>field==='key'?(i.sourceAvailability&16):k.me<8&&(i.meAvailability&(1<<k.me)))){const o=document.createElement('option');o.value=input.number;o.textContent=input.title+' ['+input.number+']';f.append(o);}if(!Array.from(f.options).some(o=>Number(o.value)===k[field])){const o=document.createElement('option');o.value=k[field];o.textContent='Unavailable · '+k[field];f.append(o);}f.value=k[field];}
 for(const field of ['type','clip','gain'])$('atem-key-'+field).value=k[field];for(const field of atemKeyBools)$('atem-key-'+field).checked=k[field];atemKeyLuma();
}
$('atem-key-fields').oninput=()=>{atemKeyDirty=true;$('atem-key-save').disabled=false;$('atem-key-notice').textContent='Unapplied changes';atemKeyLuma();};
$('atem-key-index').onchange=()=>{if(atemKeyDirty&&!confirm('Discard keyer changes?')){$('atem-key-index').value=atemKeySelected;return;}atemKeySelected=$('atem-key-index').value;atemKeyDirty=false;atemStatus(true);};
$('atem-key-reload').onclick=()=>{if(atemKeyPending)return;if(atemKeyDirty&&!confirm('Discard keyer changes?'))return;atemKeyDirty=false;atemStatus(true);};
$('atem-key-form').onsubmit=async e=>{e.preventDefault();if(atemKeyPending)return;const [me,index]=atemKeySelected.split(':').map(Number);const value={me,index,revision:atemKeyRevision};for(const f of atemKeyNumbers)value[f]=Number($('atem-key-'+f).value);for(const f of atemKeyBools)value[f]=$('atem-key-'+f).checked;atemKeyPending={value,until:Date.now()+5000};$('atem-key-fields').disabled=true;$('atem-key-index').disabled=true;$('atem-key-save').disabled=true;try{await api('/api/atem/keyers',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(value)});$('atem-key-notice').textContent='Waiting for ATEM confirmation';}catch(e){atemKeyPending=null;$('atem-key-notice').textContent=e.message;}await atemStatus();};

async function serverInfo(){try{const info=await api('/api/info');const list=$('server-info');list.replaceChildren();for(const field of info.fields||[]){const label=document.createElement('dt'),value=document.createElement('dd');label.textContent=field.label;value.textContent=field.value;list.append(label,value);}}catch(e){$('server-info').textContent=e.message;}}
setInterval(serverInfo,3000);serverInfo();

let multiviewPending=null;
const mvInput=w=>w.source>=0&&!(w.source>=10010&&w.source<=10081);
async function multiviewChange(viewer,windows,property,enabled,inputs=false){
 if(multiviewPending)return;
 multiviewPending={viewer,windows:windows.map(w=>w.index),property,enabled,until:Date.now()+5000};
 $('atem-multiview-notice').textContent='Waiting for ATEM confirmation';
 try{await api('/api/atem/multiview',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({viewer,property,enabled,...(inputs?{inputs:true}:{window:windows[0].index})})});}catch(e){multiviewPending=null;$('atem-multiview-notice').textContent=e.message;}
 await atemStatus(true);
}
function paintAtemMultiview(j,force=false){
 const viewers=j.multiviews||[];
 if(multiviewPending){const p=multiviewPending,v=viewers.find(v=>v.index===p.viewer);
  if(j.connected&&!j.busy&&v&&p.windows.every(index=>v.windows.find(w=>w.index===index)?.[p.property]===Number(p.enabled))){multiviewPending=null;$('atem-multiview-notice').textContent='Configuration confirmed by ATEM';}
  else if(!j.connected||Date.now()>p.until){multiviewPending=null;force=true;$('atem-multiview-notice').textContent='Change not confirmed. Check the current state before retrying.';}
 }
 const host=$('atem-multiview-controls');if(!force&&!multiviewPending&&host.contains(document.activeElement))return;host.replaceChildren();
 if(!viewers.length){host.textContent='This server has no multiview available.';return;}
 for(const v of viewers){const section=document.createElement('section'),title=document.createElement('h3');title.textContent='MULTIVIEW '+(v.index+1);section.append(title);
  for(const property of ['safe','meters']){const supported=property==='safe'?'safeSupported':'metersSupported',inputs=v.windows.filter(w=>mvInput(w)&&w[supported]&&w[property]>=0);if(!inputs.length)continue;
   const actions=document.createElement('div');actions.className='form-actions';for(const enabled of [true,false]){const button=document.createElement('button');button.type='button';button.className='secondary';button.textContent=(property==='safe'?'SAFE':'Audio meters')+' · entradas '+(enabled?'ON':'OFF');button.disabled=!j.connected||j.busy||!!multiviewPending||activeProtocol!=='ATEM';button.onclick=()=>multiviewChange(v.index,inputs,property,enabled,true);actions.append(button);}section.append(actions);
  }
  for(const w of v.windows){const row=document.createElement('div');row.className='multiview-row';const title=document.createElement('span');title.textContent='Window '+(w.index+1)+' · '+(atemInputs.find(i=>i.number===w.source)?.title||w.source);row.append(title);
   for(const property of ['safe','meters']){const label=document.createElement('label');label.className='inline-check';const checkbox=document.createElement('input');checkbox.type='checkbox';checkbox.checked=w[property]===1;checkbox.disabled=!j.connected||j.busy||!!multiviewPending||activeProtocol!=='ATEM'||!w[property==='safe'?'safeSupported':'metersSupported']||w[property]<0;checkbox.onchange=()=>multiviewChange(v.index,[w],property,checkbox.checked);label.append(checkbox,property==='safe'?'SAFE':'Audio meters');row.append(label);}section.append(row);
  }host.append(section);
 }
}

$('layout-safe-all').onclick=()=>{$('layout-safe-program-aspect').value=$('layout-safe-preview-aspect').value;$('layout-safe-preview').checked=$('layout-safe-program').checked=true;layoutDirty=true;};
