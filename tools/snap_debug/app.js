'use strict';
const $ = id => document.getElementById(id);
const key = document.querySelector('meta[name="snap-key"]').content;
const pageNames = {home:'首页',loud:'强响指采集',soft_ready:'等待轻响指',soft:'轻响指采集',invalid:'校准失败',practice:'练习',playing:'奔跑',paused:'游戏暂停',over:'游戏结束'};
const reasons = {mic:'麦克风未就绪',guard:'页面切换／采集间隔保护',clipped:'声音削顶',noisy:'环境噪声过高',range:'采样幅度无效',captured:'已采集校准样本',jump:'起跳成功',air:'空中忽略',idle:'当前页面不接受跳跃',stale:'输入过期',warmup:'估计环境噪声',quiet:'安静，等待声音',threshold:'未达起音门限',cooldown:'检测冷却中',wait_quiet:'等待安静后重新触发',collecting:'分析瞬态窗口',accepted:'符合瞬态特征',decay:'衰减不够快',spectrum:'高频特征不足',key:'按键',freeze:'切换时间冻结',step:'时间步进',reset:'已重置'};
let mode = 'sim', latest = null, demo = false, busy = false;
const screen = $('screen').getContext('2d');
const music = $('music');
let musicRun = null;
let musicEpoch = null, controlsPending = 0, controlVersion = 0;
$('musicEnable').onclick = async () => {
  try {
    const enabled = !latest?.musicMaster;
    if (enabled) {
      // Unlock playback from this explicit user gesture, then await a run.
      const volume = music.volume;
      music.volume = 0;
      try { await music.play(); music.pause(); }
      finally { music.volume = volume; }
    } else music.pause();
    await command({op:'music',value:enabled});
  } catch(e) { error('音乐播放未启用：'+e.message); }
};
async function syncMusic(s) {
  const version = controlVersion;
  $('musicEnable').textContent = s.musicMaster ? '关闭音乐同步' : '启用音乐同步';
  const active = s.musicMaster && !s.frozen && s.page==='playing';
  if (!active) { music.pause(); return; }
  if (musicRun !== s.seed || musicEpoch !== s.musicEpoch || music.paused) {
    musicRun = s.seed;
    musicEpoch = s.musicEpoch;
    music.currentTime = s.songMs / 1000;
    try { if (music.paused) await music.play(); }
    catch(e) {
      if (version===controlVersion) {
        await post('/api/command',{op:'pause'});
        error('浏览器未能播放音乐，请点击确定继续；也可关闭音乐同步后试玩。');
      }
    }
  }
  music.volume = Math.min(1,s.volume/50);
}

function error(e) { $('error').textContent = e; $('error').hidden = !e; }
async function post(path, data) {
  const r = await fetch(path, {method:'POST',headers:{'Content-Type':'application/json','X-Snap-Key':key},body:JSON.stringify(data)});
  const d = await r.json(); if(!r.ok) throw Error(d.error || r.status); return d;
}
async function command(data) {
  const version = ++controlVersion;
  controlsPending++;
  try {
    const resume = data.op==='key' && data.key===2 && latest?.musicMaster && !latest.frozen &&
      ['home','practice','over','paused'].includes(latest.page);
    let playback;
    if (resume) {
      music.currentTime = latest.page==='paused' ? latest.songMs/1000 : 0;
      music.volume = Math.min(1,latest.volume/50);
      // Must be invoked synchronously from click/keydown, before any HTTP await.
      playback = music.play().then(()=>null,e=>e);
      musicEpoch = null;
    } else if (['reset','freeze','pause','music'].includes(data.op) || (data.op==='key' && data.key===2)) {
      music.pause(); musicEpoch = null;
    }
    await post('/api/command',data);
    const failure = playback && await playback;
    if (failure && version===controlVersion) {
      await post('/api/command',{op:'pause'});
      error('音乐播放失败，请点击确定重试；也可关闭音乐同步后试玩。');
    } else if (version===controlVersion) error('');
  } catch(e) {error(e.message);}
  finally { controlsPending--; }
}
function tabs(value) {
  if(value!=='sim' && latest?.musicMaster && latest.page==='playing') command({op:'pause'});
  if(value!=='sim') music.pause();
  mode=value; $('simPanel').hidden=value!=='sim'; $('devicePanel').hidden=value!=='device';
  for(const v of ['sim','device']) {$(v+'Tab').classList.toggle('active',v===value); $(v+'Tab').setAttribute('aria-selected',String(v===value));}
}
$('simTab').onclick=()=>tabs('sim'); $('deviceTab').onclick=()=>tabs('device');
for(const b of document.querySelectorAll('[data-key]')) b.onclick=()=>command({op:'key',key:Number(b.dataset.key)});
$('level').oninput=()=>$('levelLabel').textContent=$('level').value;
function snap(level=Number($('level').value)) {command({op:'level',level});}
$('snap').onclick=()=>snap();
$('sound').onclick=()=>command({op:'sound',peak:Number($('peak').value),clipped:$('clipped').checked,noisy:$('noisy').checked});
$('freeze').onclick=()=>latest && command({op:'freeze',value:!latest.frozen});
$('step').onclick=()=>command({op:'step',steps:1}); $('step100').onclick=()=>command({op:'step',steps:10});
$('reset').onclick=()=>command({op:'reset',seed:Number($('seed').value),calibrated:$('calibrated').checked});
$('demo').onclick=async()=>{try{await post('/api/demo',{value:!demo});error('');}catch(e){error(e.message+'（连接真实串口时不可启用演示）');}};
document.addEventListener('keydown', e=>{
  if(mode!=='sim'||e.repeat||e.altKey||e.ctrlKey||e.metaKey||/INPUT|TEXTAREA|SELECT|SUMMARY/.test(e.target.tagName))return;
  const keys={ArrowUp:0,ArrowDown:1,Enter:2};
  if(e.code in keys){e.preventDefault();command({op:'key',key:keys[e.code]});}
  else if(e.code==='Space'){e.preventDefault();snap();}
  else if(/^[0-9]$/.test(e.key)){e.preventDefault();const level=Number(e.key)||10;$('level').value=level;$('levelLabel').textContent=level;snap(level);}
});
function metrics(target, s) {
  const items=s ? [['页面',pageNames[s.page]||s.page],['力度',s.level+' / 10'],['距离',s.score+' 米'],['生命',s.lives+' / 3'],['跳高',s.y.toFixed(1)+' px'],['速度',s.speed.toFixed(1)+' px/s'],['校准 Min / Max',Math.round(s.min)+' / '+Math.round(s.max)],['音乐进度',((s.songMs||0)/1000).toFixed(2)+' s'],['下次重拍',s.cueInMs<10000?(Math.max(0,s.cueInMs)/1000).toFixed(2)+' s':'—'],['模拟／设备时间',(s.ms/1000).toFixed(2)+' s']] : [['状态','等待游戏数据']];
  target.replaceChildren(...items.map(([title,value])=>{const d=document.createElement('div');d.className='metric';const a=document.createElement('span'),b=document.createElement('strong');a.textContent=title;b.textContent=value;d.append(a,b);return d;}));
}
function events(target, data) {
  if(!data.length){target.textContent='暂无事件。';return;}
  target.replaceChildren(...data.map(e=>{const d=document.createElement('div');d.className='event';const t=document.createElement('time'),b=document.createElement('b'),v=document.createElement('span');t.textContent=(e.ms/1000).toFixed(2)+' s';b.textContent=e.type==='detect'?'声音判定':e.type==='input'?'游戏输入':e.action==='level'?'模拟响指':'模拟操作';v.textContent=(reasons[e.reason]||e.reason)+(e.peak!==undefined?' · 峰值 '+Math.round(e.peak):e.level?' · '+e.level+' 档':'');d.append(t,b,v);return d;}));
}
function frame(s) {
  const raw=atob(s.frame), image=screen.createImageData(240,320);
  for(let i=0;i<240*320;i++){const v=raw.charCodeAt(i*2)|(raw.charCodeAt(i*2+1)<<8);const j=i*4;image.data[j]=((v>>11)&31)*255/31;image.data[j+1]=((v>>5)&63)*255/63;image.data[j+2]=(v&31)*255/31;image.data[j+3]=255;}
  screen.putImageData(image,0,0);
  if(!$('overlay').checked||!['playing','practice','paused','over'].includes(s.page))return;
  screen.lineWidth=1;screen.strokeStyle='#faf59d';screen.setLineDash([2,2]);
  screen.strokeRect(51,210-s.y-22,14,22);
  // Rocks collide by horizontal footprint and feet height. Pits use foot centre.
  screen.fillStyle='#faf59d';screen.fillRect(57,209-s.y,3,3);
  if(!s.practice)for(const h of s.hazards){if(h.hit)continue;screen.strokeStyle=h.kind?'#f49989':'#91dfbc';screen.strokeRect(h.x,h.kind?210:210-h.h,h.w,h.kind?5:h.h);}
  screen.setLineDash([]);
}
function chart(samples) {
  const c=$('chart'),ctx=c.getContext('2d'),w=c.width,h=c.height;
  ctx.clearRect(0,0,w,h);const max=Math.max(1000,...samples.map(s=>s.peak));
  ctx.font='12px sans-serif';ctx.textAlign='left';
  for(let i=0;i<5;i++){const y=15+i*45;ctx.strokeStyle='#304251';ctx.beginPath();ctx.moveTo(52,y);ctx.lineTo(w-10,y);ctx.stroke();ctx.fillStyle='#9fadb9';ctx.fillText(Math.round(max*(1-i/4)),2,y+4);}
  if(!samples.length){ctx.fillText('等待声音诊断数据',80,100);return;}
  const end=samples[samples.length-1].ms,start=Math.max(0,end-30000),span=Math.max(1000,end-start);
  for(const [k,color] of [['peak','#91dfbc'],['rms','#edca86'],['noise','#f49989']]){ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();samples.forEach((s,i)=>{const x=52+(s.ms-start)/span*(w-65),y=195-Math.min(max,s[k])/max*180;if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});ctx.stroke();}
  ctx.fillStyle='#9fadb9';ctx.fillText((start/1000).toFixed(1)+' s',52,222);ctx.textAlign='right';ctx.fillText((end/1000).toFixed(1)+' s',w-12,222);
}
async function poll(){
  if(busy)return;busy=true;
  try{
    if(controlsPending)return;
    const version=controlVersion;
    if(mode==='sim' && latest?.musicMaster && latest.page==='playing' && !latest.frozen && !music.seeking && musicEpoch===latest.musicEpoch && (!music.paused || music.ended)) await post('/api/command',{op:'music_clock',song_ms:Math.round(music.currentTime*1000),run:latest.seed,epoch:latest.musicEpoch});
    const response=await fetch(mode==='sim'?'/api/sim':'/api/device');if(!response.ok)throw Error('本地工具连接失败');const s=await response.json();$('service').textContent='● 本地工具已连接';
    if(version!==controlVersion || controlsPending)return;
    if(mode==='sim'&&s.frame){latest=s;await syncMusic(s);frame(s);metrics($('simMetrics'),s);events($('simEvents'),s.events);$('freeze').textContent=s.frozen?'恢复模拟':'冻结模拟';$('step').disabled=$('step100').disabled=!s.frozen;const hs=s.hazards.filter(h=>!h.hit).sort((a,b)=>a.x-b.x);const gaps=hs.slice(1).map((h,i)=>(h.x-hs[i].x-hs[i].w).toFixed(1));$('gapInfo').textContent='相邻障碍边缘间距：'+(gaps.join(' / ')||'—')+' px。黄框为角色范围，黄点为坑判定的足底中心。';}
    else if(mode==='device'&&s.connection){demo=s.demo;$('demoNotice').hidden=!demo;$('demo').textContent=demo?'停止演示':'查看演示数据';$('connection').textContent=s.connection;const alive=s.age!==null&&s.age<2;$('freshness').textContent=alive?'正在接收'+(demo?'演示':'真机')+'数据':s.age===null?'尚未收到有效数据。旧固件无诊断流；串口占用或设备休眠时也可能没有输出。':'数据已停更 '+s.age.toFixed(1)+' 秒。请检查连接；休眠期间也会停更。';metrics($('deviceMetrics'),s.state);chart(s.samples);events($('deviceEvents'),s.events);const dropped=Math.max(0,...s.samples.map(x=>x.dropped),...s.events.map(x=>x.dropped),s.state?.dropped||0);$('quality').textContent=`已收 ${s.received} 条 · 格式异常 ${s.invalid} 条 · 固件诊断队列累计丢弃 ${dropped} 条。停更时保留最后数据，不能视作实时值。`;}
  }catch(e){$('service').textContent='本地工具已断开';error(e.message);}finally{busy=false;setTimeout(poll,mode==='sim'?50:200);}
}
poll();
