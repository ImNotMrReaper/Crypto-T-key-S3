#pragma once
// Setup page served by WebPortal. Self-contained (the setup AP has no internet).
// Data comes from /api/state; nothing secret is ever rendered back into it.
// Preview locally with tools/preview_portal.py.

static const char PORTAL_PAGE[] PROGMEM = R"PAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark">
<title>T-Key setup</title>
<style>
:root{--void:#000;--line:#221f2c;--field:#0d0c13;--text:#edebf4;--muted:#8d89a1;--reaper:#7764d8;--reaper-soft:rgba(119,100,216,.18);--ok:#34c77b;--warn:#f0a93b;--bad:#ff5a52;--home:#7764d8;
--round:ui-rounded,"SF Pro Rounded","Nunito","Varela Round",system-ui,sans-serif;--body:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
*{box-sizing:border-box}
html{background:var(--void)}
body{margin:0;color:var(--text);font:16px/1.5 var(--body);font-variant-numeric:tabular-nums;padding:0 20px 120px;-webkit-text-size-adjust:100%}
main{max-width:520px;margin:0 auto}
h1,h2{font-family:var(--round);font-weight:700;letter-spacing:-.01em;margin:0}
h1{font-size:28px;line-height:1.15}
h2{font-size:20px;margin-bottom:4px}
p{margin:0}
.lede{color:var(--muted);margin-top:8px;max-width:38ch}
section{padding:28px 0;border-top:1px solid var(--line)}
section>.why{color:var(--muted);font-size:14px;margin-bottom:14px;max-width:44ch}
label.f{display:block;font-size:14px;color:var(--muted);margin:16px 0 6px}
input[type=text],input[type=password],input[type=search],select{width:100%;background:var(--field);color:var(--text);border:1px solid var(--line);border-radius:12px;padding:12px 14px;font:inherit}
input::placeholder{color:#5d5970}
input:focus-visible,select:focus-visible,button:focus-visible,.seg button:focus-visible,.sw:focus-within{outline:2px solid var(--reaper);outline-offset:2px}
.pair{display:grid;gap:8px}
.note{font-size:13px;color:var(--muted);margin-top:6px}
.caution{font-size:13px;color:var(--warn);margin-top:6px}
button{font:600 16px/1 var(--body);border:0;border-radius:12px;padding:14px 16px;color:#fff;background:var(--reaper);cursor:pointer}
button.quiet{background:transparent;color:var(--text);border:1px solid var(--line)}
button.link{background:none;padding:6px 0;color:var(--reaper);font-weight:600;font-size:14px}
button:disabled{opacity:.5}

/* Hero: the key itself */
.hero{padding:28px 0 24px}
.key{position:relative;margin:4px 0 22px;display:flex;align-items:center;max-width:340px}
.plug{width:46px;height:34px;background:linear-gradient(#cfd0d6,#8e9099);border-radius:3px 0 0 3px;position:relative}
.plug::before,.plug::after{content:"";position:absolute;left:12px;width:10px;height:6px;background:#3a3b41;border-radius:1px}
.plug::before{top:8px}.plug::after{bottom:8px}
.body{flex:1;background:#141319;border:1px solid #2a2733;border-radius:6px 18px 18px 6px;padding:10px 12px 10px 14px;display:flex;align-items:center;gap:12px}
.screen{aspect-ratio:2/1;flex:1;background:#000;border-radius:3px;border:1px solid #2c2936;padding:7% 8%;display:flex;flex-direction:column;justify-content:space-between;overflow:hidden}
.screen .top{display:flex;justify-content:space-between;font:700 11px/1 var(--round);color:var(--home)}
.screen .big{font:800 clamp(18px,6vw,24px)/1 var(--round);color:#fff}
.screen .chips{display:flex;gap:3px;flex-wrap:nowrap;overflow:hidden;height:7px}
.screen .chips i{width:7px;height:7px;border-radius:50%;flex:0 0 7px}
.led{width:14px;height:14px;border-radius:50%;background:var(--home);box-shadow:0 0 14px 3px var(--home);flex:0 0 14px}
.led.breathe{animation:br 3.2s ease-in-out infinite}
.led.rainbow{animation:rb 6s linear infinite}
@keyframes br{0%,100%{opacity:.3;box-shadow:0 0 4px 0 var(--home)}50%{opacity:1;box-shadow:0 0 16px 4px var(--home)}}
@keyframes rb{0%{background:#ff3b30;box-shadow:0 0 14px 3px #ff3b30}20%{background:#ffcc00;box-shadow:0 0 14px 3px #ffcc00}40%{background:#34c759;box-shadow:0 0 14px 3px #34c759}60%{background:#00c7ff;box-shadow:0 0 14px 3px #00c7ff}80%{background:#7764d8;box-shadow:0 0 14px 3px #7764d8}100%{background:#ff3b30;box-shadow:0 0 14px 3px #ff3b30}}
@media (prefers-reduced-motion:reduce){.led.breathe,.led.rainbow{animation:none}}

/* Coins */
.tools{display:flex;gap:6px;flex-wrap:wrap;margin:12px 0 4px}
.seg{display:flex;background:var(--field);border:1px solid var(--line);border-radius:12px;padding:3px;gap:2px;flex-wrap:wrap}
.seg button{flex:1;background:none;color:var(--muted);font-size:14px;padding:9px 10px;border-radius:9px;white-space:nowrap}
.seg button[aria-pressed=true]{background:var(--reaper-soft);color:var(--text)}
.tally{font-size:14px;color:var(--muted);margin:12px 0 2px}
.tally b{color:var(--text)}
.group{margin-top:18px}
.group>p{font-size:13px;color:var(--muted);margin-bottom:4px}
.coin{display:flex;align-items:center;gap:12px;padding:8px 0;border-bottom:1px solid #15131c;cursor:pointer}
.chip{width:14px;height:14px;border-radius:50%;flex:0 0 14px;border:2px solid var(--c);background:transparent;transition:background .15s}
.coin.on .chip{background:var(--c);box-shadow:0 0 8px var(--c)}
.coin .who{flex:1;min-width:0;display:flex;align-items:baseline;gap:8px;overflow:hidden}
.coin .who b{font:700 16px/1.2 var(--round)}
.coin .who span{color:var(--muted);font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.coin .net{color:var(--muted);font-size:12px;white-space:nowrap}
.coin .kind{font-size:12px;white-space:nowrap}
.kind.stable{color:var(--ok)}.kind.meme{color:var(--warn)}
.sw{position:relative;width:44px;height:26px;flex:0 0 44px}
.sw input{position:absolute;inset:0;opacity:0;margin:0;cursor:pointer}
.sw span{position:absolute;inset:0;background:#2a2733;border-radius:13px;transition:background .15s}
.sw span::after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#fff;transition:transform .15s}
.sw input:checked+span{background:var(--reaper)}
.sw input:checked+span::after{transform:translateX(18px)}

/* Home look */
.swatches{display:flex;gap:10px;flex-wrap:wrap;margin:10px 0 16px}
.swatch{width:36px;height:36px;border-radius:50%;border:2px solid transparent;padding:0;background:var(--s)}
.swatch[aria-pressed=true]{border-color:#fff}
.swatch.custom{background:conic-gradient(#ff3b30,#ffcc00,#34c759,#00c7ff,#7764d8,#ff3b30);position:relative;overflow:hidden}
.swatch.custom input{position:absolute;inset:0;opacity:0;width:100%;height:100%;cursor:pointer}

/* Wi-Fi */
.net-row{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid #15131c}
.net-row.gone span{text-decoration:line-through;color:var(--muted)}
.net-row small{color:var(--reaper);margin-left:8px}
.inline{display:flex;gap:8px}.inline input{flex:1}

/* Footer */
.bar{position:fixed;left:0;right:0;bottom:0;background:rgba(0,0,0,.94);backdrop-filter:blur(8px);border-top:1px solid var(--line);padding:12px 20px calc(12px + env(safe-area-inset-bottom))}
.bar .in{max-width:520px;margin:0 auto;display:flex;gap:10px}.bar .grow{flex:1}
#msg{font-size:14px;min-height:20px;margin-bottom:10px;max-width:520px;margin-left:auto;margin-right:auto}
#msg.bad{color:var(--bad)}#msg.ok{color:var(--ok)}
.hide{display:none!important}
</style></head><body><main>

<header class="hero">
  <div class="key" aria-hidden="true">
    <div class="plug"></div>
    <div class="body">
      <div class="screen"><div class="top"><span>T-KEY</span><span id="scrCount"></span></div><div class="big" id="scrBig">READY</div><div class="chips" id="scrChips"></div></div>
      <div class="led" id="led"></div>
    </div>
  </div>
  <h1 id="title">Set up your T-Key</h1>
  <p class="lede" id="lede">Choose a PIN, pick your coins and give the key its look. Nothing is saved until you tap Save to key.</p>
</header>

<section id="login" class="hide">
  <h2>Enter the setup password</h2>
  <p class="why">This key is already set up. The setup password you chose protects these settings.</p>
  <label class="f" for="lp">Setup password</label>
  <input type="password" id="lp" autocomplete="current-password">
  <p class="caution hide" id="lock"></p>
  <div style="height:14px"></div>
  <button id="lb">Unlock settings</button>
</section>

<div id="app" class="hide">
<section>
  <h2>Protect the key</h2>
  <p class="why">You type the PIN on the key itself to open the wallet. The setup password guards this page.</p>
  <label class="f" for="pin">PIN, 4 to 8 digits</label>
  <div class="pair"><input type="password" id="pin" inputmode="numeric" autocomplete="new-password" maxlength="8" placeholder="New PIN"><input type="password" id="pin2" inputmode="numeric" autocomplete="new-password" maxlength="8" placeholder="Repeat PIN"></div>
  <p class="note" id="pinHint"></p>
  <label class="f" for="du">Duress PIN, optional</label>
  <input type="password" id="du" inputmode="numeric" autocomplete="new-password" maxlength="8" placeholder="Leave empty for none">
  <p class="caution">Typing the duress PIN on the key erases everything on it. Set one only if your recovery words are written down.</p>
  <label class="note hide" id="duClearRow" style="display:flex;gap:8px;align-items:center;margin-top:10px"><input type="checkbox" id="duClear" style="width:18px;height:18px;accent-color:var(--reaper)"> Remove the duress PIN that is set now</label>
  <label class="f" for="sp">Setup password, at least 8 characters</label>
  <div class="pair"><input type="password" id="sp" autocomplete="new-password" placeholder="New setup password"><input type="password" id="sp2" autocomplete="new-password" placeholder="Repeat setup password"></div>
  <p class="note" id="spHint"></p>
  <p class="note" style="margin-top:18px">Your 12 recovery words are made and shown only on the key's own screen, never on this page.</p>
</section>
<section>
  <h2>Pick the home look</h2>
  <p class="why">This colour lights the home screen and the LED while the key sits idle. Login and signing prompts keep their fixed colours, so a real prompt always looks the same.</p>
  <div class="swatches" id="sw"></div>
  <div class="seg" id="fx" role="group" aria-label="LED effect"><button data-fx="0">Steady</button><button data-fx="1">Breathe</button><button data-fx="2">Rainbow</button></div>
</section>
<section>
  <h2>Wi-Fi for wall power</h2>
  <p class="why">On a phone charger the key briefly joins Wi-Fi to refresh prices. Plugged into a computer, its radio stays off.</p>
  <div id="nets"></div>
  <label class="f" for="ws">Add a network</label>
  <div class="inline"><input type="text" id="ws" placeholder="Network name" autocapitalize="off" autocorrect="off"><button class="quiet" id="scan">Find</button></div>
  <select id="scanList" class="hide" style="margin-top:8px"></select>
  <input type="password" id="wp" placeholder="Wi-Fi password" style="margin-top:8px">
  <button class="link" id="addNet">Add network</button>
</section>
<section>
  <h2>Choose your coins</h2>
  <p class="why">Each coin you turn on gets a receive address, a live price, its own screen and its own LED rhythm. Coins on the same network share one address.</p>
  <input type="search" id="q" placeholder="Search by name, symbol or network" autocomplete="off">
  <div class="tools"><div class="seg" id="tabs" role="group" aria-label="Filter coins"></div></div>
  <p class="tally" id="tally"></p>
  <div id="list"></div>
  <div style="display:flex;gap:16px;margin-top:10px"><button class="link" id="selVis">Turn on all shown</button><button class="link" id="clrVis">Turn off all shown</button></div>
</section>
</div>
</main>

<div class="bar hide" id="bar"><p id="msg" role="status"></p><div class="in"><button class="quiet hide" id="exit">Leave without saving</button><button class="grow" id="save">Save to key</button></div></div>

<script>
const $=id=>document.getElementById(id);
let S=null,tab='all',fx=1,col='7764d8',newNets=[],delNets=new Set();
const PRESETS=['7764d8','00e5ff','30d158','ffb300','ff375f','ffffff'];
function say(t,kind){const m=$('msg');m.textContent=t;m.className=kind||''}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
async function api(p,body){
  const o=body?{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-TKey-CSRF':S&&S.csrf||''},body:new URLSearchParams(body)}:{};
  const r=await fetch(p,o);const j=await r.json().catch(()=>({error:'The key sent an unreadable reply. Reload the page.'}));
  if(!r.ok)throw new Error(j.error||('Error '+r.status));return j}
async function load(){
  try{S=await api('/api/state')}catch(e){$('bar').classList.remove('hide');say('Can\'t reach the key. Check that your phone is still on its Wi-Fi.','bad');return}
  if(!S.auth){$('login').classList.remove('hide');$('app').classList.add('hide');$('bar').classList.add('hide');
    $('title').textContent='Your T-Key';$('lede').textContent='Unlock to change its settings.';
    if(S.lockedFor>0){$('lock').textContent='Too many wrong passwords. Try again in '+Math.ceil(S.lockedFor/60)+' min.';$('lock').classList.remove('hide')}
    mirror();return}
  $('login').classList.add('hide');$('app').classList.remove('hide');$('bar').classList.remove('hide');
  if(S.provisioned){$('title').textContent='Your T-Key';$('lede').textContent='Change what you need. Nothing is saved until you tap Save to key.'}
  $('pinHint').textContent=S.hasPin?'Leave both empty to keep your current PIN.':'Needed to open the wallet on the key.';
  $('spHint').textContent=S.hasPassword?'Leave both empty to keep your current setup password.':'Needed to change these settings later.';
  $('duClearRow').classList.toggle('hide',!S.hasDuress);
  $('exit').classList.toggle('hide',!S.provisioned);
  col=S.theme.rgb;fx=S.theme.fx;
  renderTabs();renderList();renderLook();renderNets()}
const sel=()=>S?S.coins.filter(c=>c[6]):[];
function mirror(){const s=sel();document.documentElement.style.setProperty('--home','#'+col);
  $('led').className='led'+(fx==1?' breathe':fx==2?' rainbow':'');
  $('scrCount').textContent=s.length?s.length+' COINS':'';
  $('scrChips').innerHTML=s.slice(0,18).map(c=>`<i style="background:#${vis(c[5])}"></i>`).join('')}
function tally(){const s=sel(),n=new Set(s.map(c=>c[3])).size;
  $('tally').innerHTML=s.length?`<b>${s.length}</b> ${s.length==1?'coin':'coins'} on <b>${n}</b> ${n==1?'address':'addresses'}`:'No coins on yet. Turn on at least one.';mirror()}
function shown(){const q=$('q').value.trim().toLowerCase();
  return S.coins.filter(c=>(tab==='all'||(tab==='sel'?c[6]:c[4]==tab))&&(!q||(c[0]+' '+c[1]+' '+c[2]).toLowerCase().includes(q)))}
function renderTabs(){const t=[['all','All'],[0,'Crypto'],[1,'Stablecoins'],[2,'Meme'],['sel','On']];
  $('tabs').innerHTML=t.map(([k,l])=>`<button data-t="${k}" aria-pressed="${k===tab}">${l}</button>`).join('');
  $('tabs').querySelectorAll('button').forEach(e=>e.onclick=()=>{const v=e.dataset.t;tab=isNaN(v)?v:+v;renderTabs();renderList()})}
function groupLine(f){const n=S.families[f];return f==1?'These share your Ethereum address, on each coin\'s own network':f==2?'These share your Solana address':f==5?'These share your TRON address':'Uses your '+n+' address'}
function vis(h){const n=parseInt(h,16),r=n>>16,g=n>>8&255,b=n&255,l=(.2126*r+.7152*g+.0722*b)/255;if(l>=.28)return h;const k=(.28-l)/(1-l);
  return[r,g,b].map(v=>Math.round(v+(255-v)*k).toString(16).padStart(2,'0')).join('')}
function renderList(){const by={};shown().forEach(c=>(by[c[3]]=by[c[3]]||[]).push(c));
  const html=Object.keys(by).sort((a,b)=>a-b).map(f=>`<div class="group"><p>${esc(groupLine(+f))}</p>`+by[f].map(c=>
   `<label class="coin${c[6]?' on':''}" style="--c:#${vis(c[5])}"><span class="chip"></span><span class="who"><b>${esc(c[0])}</b><span>${esc(c[1])}</span></span>`+
   `<span class="${c[4]==1?'kind stable':c[4]==2?'kind meme':'net'}">${c[4]==1?'Stablecoin':c[4]==2?'Meme':''}</span><span class="net">${c[2]===c[1]?'':esc(c[2])}</span>`+
   `<span class="sw"><input type="checkbox" data-s="${esc(c[0])}" aria-label="${esc(c[1])}"${c[6]?' checked':''}><span></span></span></label>`).join('')+'</div>').join('');
  $('list').innerHTML=html||'<p class="note" style="margin-top:14px">No coin matches that search. Try a symbol like BTC or a network like Solana.</p>';
  $('list').querySelectorAll('input').forEach(e=>e.onchange=()=>{S.coins.find(c=>c[0]===e.dataset.s)[6]=e.checked?1:0;e.closest('.coin').classList.toggle('on',e.checked);tally()});tally()}
$('q').oninput=()=>renderList();
$('selVis').onclick=()=>{shown().forEach(c=>c[6]=1);renderList()};
$('clrVis').onclick=()=>{shown().forEach(c=>c[6]=0);renderList()};
function renderLook(){const custom=!PRESETS.includes(col);
  $('sw').innerHTML=PRESETS.map(p=>`<button class="swatch" style="--s:#${p}" data-c="${p}" aria-label="#${p}" aria-pressed="${p===col}"></button>`).join('')+
   `<label class="swatch custom" aria-pressed="${custom}" title="Any colour"><input type="color" id="colPick" value="#${col}" aria-label="Any colour"></label>`;
  $('sw').querySelectorAll('[data-c]').forEach(b=>b.onclick=()=>{col=b.dataset.c;renderLook()});
  $('colPick').oninput=e=>{col=e.target.value.slice(1);mirror()};$('colPick').onchange=()=>renderLook();
  $('fx').querySelectorAll('button').forEach(b=>{b.setAttribute('aria-pressed',+b.dataset.fx===fx);b.onclick=()=>{fx=+b.dataset.fx;renderLook()}});mirror()}
function renderNets(){$('nets').innerHTML=S.wifi.map(s=>`<div class="net-row${delNets.has(s)?' gone':''}"><span>${esc(s)}</span><button class="link" data-d="${esc(s)}">${delNets.has(s)?'Keep':'Remove'}</button></div>`).join('')+
  newNets.map((n,i)=>`<div class="net-row"><span>${esc(n.s)}<small>new</small></span><button class="link" data-n="${i}">Undo</button></div>`).join('')||'<p class="note">No networks saved yet.</p>';
  $('nets').querySelectorAll('[data-d]').forEach(b=>b.onclick=()=>{const s=b.dataset.d;delNets.has(s)?delNets.delete(s):delNets.add(s);renderNets()});
  $('nets').querySelectorAll('[data-n]').forEach(b=>b.onclick=()=>{newNets.splice(+b.dataset.n,1);renderNets()})}
$('addNet').onclick=()=>{const s=$('ws').value.trim();if(!s){say('Type a network name first.','bad');return}
  if(newNets.length>=5){say('Add up to 5 networks at a time, then save.','bad');return}
  newNets.push({s,p:$('wp').value});$('ws').value='';$('wp').value='';say('');renderNets()};
$('scan').onclick=async()=>{$('scan').textContent='Finding…';try{const n=await api('/api/scan');const l=$('scanList');
  l.innerHTML='<option value="">Networks nearby</option>'+n.map(x=>`<option value="${esc(x.ssid)}">${esc(x.ssid)} (${x.rssi} dBm${x.secure?'':', open'})</option>`).join('');
  l.classList.remove('hide');l.onchange=()=>{if(l.value)$('ws').value=l.value}}catch(e){say(e.message,'bad')}$('scan').textContent='Find'};
$('lb').onclick=async()=>{try{await api('/api/login',{password:$('lp').value});$('lp').value='';load()}catch(e){$('lock').textContent=e.message;$('lock').classList.remove('hide');load()}};
$('lp').onkeydown=e=>{if(e.key==='Enter')$('lb').click()};
$('exit').onclick=async()=>{try{await api('/api/exit',{});say('Left without saving. You can close this page.','ok');$('exit').classList.add('hide');$('save').classList.add('hide')}catch(e){say(e.message,'bad')}};
$('save').onclick=async()=>{
  const pin=$('pin').value,sp=$('sp').value;
  if(pin!==$('pin2').value){say('The two PINs don\'t match.','bad');return}
  if(sp!==$('sp2').value){say('The two setup passwords don\'t match.','bad');return}
  if(!sel().length){say('Turn on at least one coin.','bad');return}
  const b={pin,duress:$('du').value,duress_clear:$('duClear').checked?'1':'',setup_pass:sp,
    coins:sel().map(c=>c[0]).join(','),theme_rgb:col,theme_fx:String(fx)};
  newNets.forEach((n,i)=>{b['ws'+i]=n.s;b['wp'+i]=n.p});[...delNets].forEach((s,i)=>b['wd'+i]=s);
  $('save').disabled=true;say('Saving to the key…');
  try{await api('/api/save',b);['pin','pin2','du','sp','sp2'].forEach(i=>$(i).value='');
    say('Saved to the key. It turns its Wi-Fi off now, so you can close this page.','ok');$('exit').classList.add('hide');$('save').classList.add('hide')}
  catch(e){say(e.message,'bad');$('save').disabled=false}};
load();
</script></body></html>)PAGE";
