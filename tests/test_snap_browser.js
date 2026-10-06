// Execute the shipped controller with media/HTTP boundaries under our control.
// This tests event ordering, not browser decoding or audible timing.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const {test} = require('node:test');
function harness() {
  const elements = new Map(), requests = [], plays = [];
  let gesture = false;
  const element = id => {
    if (!elements.has(id)) elements.set(id, {value:7,textContent:'',hidden:false,
      getContext:()=>({}),classList:{toggle(){}},setAttribute(){},replaceChildren(){}});
    return elements.get(id);
  };
  const music = element('music');
  Object.assign(music,{paused:true,seeking:false,ended:false,currentTime:1.1,volume:.6,
    play(){plays.push(gesture);if(!gesture)return Promise.reject(new Error('NotAllowedError'));
      this.paused=false;return Promise.resolve();},pause(){this.paused=true;}});
  const context=vm.createContext({console,setTimeout(){},document:{
    getElementById:element,querySelector:()=>({content:'local-test-key'}),
    querySelectorAll:()=>[],addEventListener(){}},fetch:async(path,options)=>{
      if(!options)return new Promise(()=>{}); // Do not run the background poll in unit tests.
      requests.push(JSON.parse(options.body));return {ok:true,json:async()=>({ok:true})};
    }});
  vm.runInContext(fs.readFileSync('tools/snap_debug/app.js','utf8'),context);
  return {music,requests,plays,element,run:code=>vm.runInContext(code,context),
    gesture:code=>{gesture=true;const value=vm.runInContext(code,context);gesture=false;return value;}};
}
test('Continue starts media during the user gesture, before asynchronous HTTP',async()=>{
  const h=harness();
  h.run("latest={page:'paused',musicMaster:true,songMs:1100,seed:43,musicEpoch:3,volume:30};");
  await h.gesture("command({op:'key',key:2})");
  assert.deepEqual(h.plays,[true]);
  assert.equal(h.music.paused,false);
  assert.equal(h.music.currentTime,1.1);
  assert.equal(h.requests[0].op,'key');
});
test('Autoplay rejection requests an idempotent pause, never toggles back to play',async()=>{
  const h=harness();
  await h.run("syncMusic({page:'playing',musicMaster:true,frozen:false,songMs:1100,seed:43,musicEpoch:3,volume:30})");
  assert.equal(h.requests.at(-1).op,'pause');
  assert.match(h.element('error').textContent,/播放/);
});
test('A cancelled old playback promise cannot pause a later control action',async()=>{
  const h=harness();
  let reject;
  h.music.play=()=>new Promise((_,fail)=>{reject=fail;});
  h.run("latest={page:'playing',musicMaster:true,songMs:1100,seed:43,musicEpoch:3,volume:30};");
  const pending=h.run("syncMusic({...latest,frozen:false})");
  await h.gesture("command({op:'key',key:2})");
  reject(new Error('AbortError'));
  await pending;
  assert.equal(h.requests.length,1);
  assert.equal(h.requests[0].op,'key');
});
