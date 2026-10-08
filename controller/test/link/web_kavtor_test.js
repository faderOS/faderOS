const fs=require('fs'),vm=require('vm'),assert=require('assert');
const script=fs.readFileSync('web/app.js','utf8');
const html=fs.readFileSync('web/index.html','utf8');
const elements=new Map(),commands=[];
class Element {
 constructor(tag='div'){this.tag=tag;this.children=[];this.classes=new Set();this.attributes={};this.value='';this.classList={toggle:(name,on)=>on?this.classes.add(name):this.classes.delete(name)};}
 set id(value){this._id=value;elements.set(value,this);} get id(){return this._id;}
 append(...items){this.children.push(...items);} replaceChildren(...items){this.children=items;}
 get options(){return this.children.filter(item=>item.tag==='option');}
 setAttribute(name,value){this.attributes[name]=value;}
}
const lookup=id=>{if(!elements.has(id)){const e=new Element();e.id=id;}return elements.get(id);};
const context={$:lookup,document:{createElement:tag=>new Element(tag),querySelectorAll:()=>[],activeElement:null},kavtorCmd:body=>commands.push(body),rateDirty:false,wipeDirty:false,presetDirty:false,stingerDirty:false};
vm.createContext(context);
vm.runInContext(script.slice(script.indexOf('let kavtorPreview=false;'),script.indexOf('const stingerHost=')),context);
const start=script.indexOf('function paintkavtor(j)');
const end=script.indexOf("$('kavtor-rate').addEventListener",start);
vm.runInContext(script.slice(start,end),context);
assert.equal(lookup('kavtor-bank-1').children.length,12);assert.equal(lookup('kavtor-bank-2').children.length,12);
assert.equal(lookup('kavtor-keys').children.length,4);
const select=lookup('kavtor-keys').children[0].children[1];
assert(select.options.some(o=>o.value==='22'));assert(!select.options.some(o=>o.value==='11'||o.value==='23'));
const state={connected:true,assigned:Array(24).fill(true),me:1,preview:20,program:21,next:{background:true,keys:[false,false,true,true]},transitionPreview:true,transitionPreviewSupported:true,wipeBorderColor:'#123456',wipeBorder:19,wipeShadow:7};
state.assigned[3]=false;
context.paintkavtor(state);
assert(lookup('kavtor-pvw-20').classes.has('is-preview'));assert(lookup('kavtor-pgm-21').classes.has('is-program'));
assert(lookup('kavtor-pvw-3').disabled);assert(!lookup('kavtor-pvw-20').disabled);
assert(lookup('kavtor-next-k3').classes.has('is-program'));assert.equal(lookup('wipe-color').value,'#123456');assert.equal(lookup('wipe-border').value,19);assert.equal(lookup('wipe-shadow').value,7);
assert.equal(lookup('kavtor-preview').attributes['aria-pressed'],'true');assert(!lookup('kavtor-preview').disabled);
lookup('kavtor-preview').onclick();assert.deepEqual(commands.pop(),{cmd:'trans_preview',on:false});
lookup('kavtor-next-k3').onclick();assert.deepEqual(commands.pop(),{cmd:'next',target:'key',slot:3});
context.paintkavtor({...state,transitioning:true,bothSources:true});assert(lookup('kavtor-pvw-20').classes.has('is-program'));assert(lookup('kavtor-pvw-20').disabled);
context.paintkavtor({...state,transitionPreview:false,transitionPreviewSupported:false});assert(lookup('kavtor-preview').disabled);
context.paintkavtor({...state,connected:false});assert(lookup('kavtor-auto').disabled);assert(lookup('kavtor-pgm-21').disabled);assert(select.disabled);
for(const id of ['kavtor-preview','kavtor-next-k2','kavtor-next-k3','kavtor-bank-1','kavtor-bank-2'])assert(html.includes(`id="${id}"`));
console.log('kavtor web: banks, keyers, confirmed rehearsal, availability and WIPE colour PASS');

(async()=>{
 commands.length=0;context.kavtorCmd=async body=>{commands.push(body);return true};
 vm.runInContext(script.slice(script.indexOf("$('wipe-form').onsubmit="),script.indexOf("$('wipe-presets-form').addEventListener")),context);
 for(const [id,value] of Object.entries({'wipe-pattern-kavtor':'smil_irisWipe_diamond','wipe-dir':'fwd','wipe-edge':'soft','wipe-amount':'12','wipe-border':'8','wipe-shadow':'5','wipe-aspect':'175','wipe-multi':'2','wipe-pos-x':'500','wipe-pos-y':'500'}))lookup(id).value=value;
 await lookup('wipe-form').onsubmit({preventDefault(){}});
 assert.equal(commands.length,1);assert.equal(commands[0].cmd,'wipe_settings');assert.equal(commands[0].shadow,5);assert.equal(commands[0].aspectW,175);
 commands.length=0;lookup('wipe-shadow').value='41';await lookup('wipe-form').onsubmit({preventDefault(){}});assert.equal(commands.length,0);
 console.log('Atomic WIPE settings and shadow range: PASS');
})().catch(e=>{console.error(e);process.exitCode=1});
