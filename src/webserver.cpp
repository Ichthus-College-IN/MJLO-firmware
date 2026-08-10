// Copyright (C) 2024-2026 StevenCellist (https://github.com/StevenCellist)
// Licensed under GPL-3.0

#include <Arduino.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "esp_eap_client.h"
#include "esp_wifi.h"
#include "config.h"
#include "webserver.h"

#define FS LittleFS

AsyncWebServer server(80);

wifi_mode_t wifiMode = WIFI_MODE_NULL;
IPAddress    IP;
volatile bool webAccelPending = false;

static AsyncEventSource gnssEvents("/api/gnss");
static AsyncEventSource accelEvents("/api/accel");

// OTA reboot flag — set after a successful upload, cleared on restart
static bool     _otaReboot   = false;
static uint32_t _otaRebootMs = 0;

// Auth verdict for the OTA upload currently in flight (uploads are serialised)
static bool     _otaUploadAuth = false;

// ============================================================
// Shared CSS (served at /style.css)
// ============================================================
static const char CSS[] PROGMEM =
  ":root{--bg:#f4f6fa;--card:#ffffff;--bd:#d8dde6;--acc:#1f6feb;--txt:#1a1d27;--mut:#5b6472;--ok:#1f9d57;--err:#d63a3a;--r:8px;color-scheme:light}"
  ":root[data-theme=dark]{--bg:#0f1117;--card:#1a1d27;--bd:#2a2d3a;--acc:#4e9af1;--txt:#e2e8f0;--mut:#7a849a;--ok:#52c87a;--err:#e05c5c;color-scheme:dark}"
  "*{box-sizing:border-box;margin:0;padding:0}"
  "body{background:var(--bg);color:var(--txt);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;font-size:14px;transition:background .15s,color .15s}"
  "nav{background:var(--card);border-bottom:1px solid var(--bd);display:flex;gap:4px;padding:0 16px;overflow-x:auto}"
  "nav a{color:var(--mut);text-decoration:none;padding:12px;border-bottom:2px solid transparent;white-space:nowrap;font-size:13px}"
  "nav a:hover,nav a.cur{color:var(--txt);border-color:var(--acc)}"
  "nav .tgl{margin-left:auto;align-self:center;background:none;border:1px solid var(--bd);color:var(--mut);border-radius:4px;padding:4px 9px;font-size:14px;line-height:1;cursor:pointer}"
  "nav .tgl:hover{color:var(--txt);border-color:var(--acc)}"
  ".tgl .s{display:none}"
  ":root[data-theme=dark] .tgl .m{display:none}"
  ":root[data-theme=dark] .tgl .s{display:inline}"
  "main{max-width:960px;margin:0 auto;padding:20px 16px}"
  "h2{font-size:18px;margin-bottom:16px}"
  ".card{background:var(--card);border:1px solid var(--bd);border-radius:var(--r);padding:16px;margin-bottom:12px}"
  ".card h3{font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:var(--mut);margin-bottom:12px;font-weight:600}"
  ".row{display:flex;align-items:center;gap:8px;margin-bottom:8px;flex-wrap:wrap}"
  ".lbl{width:130px;font-size:13px;color:var(--mut);flex-shrink:0}"
  ".val{font-size:13px}"
  "input[type=text],input[type=password],select{flex:1;min-width:120px;background:var(--bg);border:1px solid var(--bd);border-radius:4px;padding:6px 8px;color:var(--txt);font-size:13px}"
  "input:focus,select:focus,textarea:focus{outline:none;border-color:var(--acc)}"
  "textarea{width:100%;background:var(--bg);border:1px solid var(--bd);border-radius:4px;padding:6px 8px;color:var(--txt);font:12px/1.45 monospace;min-height:90px;resize:vertical}"
  ".fld{margin-bottom:14px}"
  ".fld>label{display:block;font-size:12px;color:var(--mut);margin-bottom:5px}"
  ".fld input[type=text]{width:100%}"
  "button{background:var(--acc);color:#fff;border:none;border-radius:4px;padding:6px 12px;font-size:13px;cursor:pointer}"
  "button:hover{filter:brightness(1.1)}"
  "button:disabled{opacity:.5;cursor:not-allowed}"
  "button.sec{background:var(--card);border:1px solid var(--bd);color:var(--txt)}"
  "button.dan{background:var(--err)}"
  ".badge{display:inline-block;padding:2px 6px;border-radius:3px;font-size:12px}"
  ".ok{background:rgba(82,200,122,.15);color:var(--ok)}"
  ".err{background:rgba(224,92,92,.15);color:var(--err)}"
  "table{width:100%;border-collapse:collapse}"
  "th,td{padding:8px 10px;text-align:left;border-bottom:1px solid var(--bd);font-size:13px}"
  "th{color:var(--mut);font-weight:500;font-size:12px}"
  ".mono{font-family:monospace;font-size:12px}"
  ".term{background:#080b0f;border-radius:var(--r);padding:12px;font-family:monospace;font-size:12px;"
        "line-height:1.6;height:260px;overflow-y:auto;color:#7cfc00;border:1px solid var(--bd);"
        "white-space:pre-wrap;word-break:break-all}"
  "#modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.75);z-index:100;overflow:auto}"
  ".mbox{background:var(--card);border:1px solid var(--bd);border-radius:var(--r);max-width:800px;margin:5vh auto;padding:20px}"
  ".mhdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}"
  ".dz{border:2px dashed var(--bd);border-radius:var(--r);padding:36px;text-align:center;"
      "color:var(--mut);cursor:pointer;transition:border-color .2s}"
  ".dz.over,.dz:hover{border-color:var(--acc);color:var(--txt)}"
  ".prog{height:6px;background:var(--bd);border-radius:3px;overflow:hidden;margin-top:12px}"
  ".progb{height:100%;background:var(--acc);width:0;transition:width .3s}";

// ============================================================
// HTML pages (PROGMEM raw string literals)
// ============================================================

// Theme toggle (light default / dark), shared by every page.
// Applied in <head> before <body> paints, so there is no flash of the wrong
// theme; the choice is remembered in localStorage.
#define THEME_HEAD R"html(<script>(function(){try{var t=localStorage.getItem('theme')||'light';
document.documentElement.setAttribute('data-theme',t);}catch(e){}
window.toggleTheme=function(){var d=document.documentElement,
n=d.getAttribute('data-theme')==='dark'?'light':'dark';
d.setAttribute('data-theme',n);try{localStorage.setItem('theme',n);}catch(e){}};
})();</script>)html"

// Nav button: shows the moon in light mode, the sun in dark mode (via CSS).
#define THEME_BTN R"html(  <button class=tgl type=button onclick=toggleTheme() title="Toggle theme" aria-label="Toggle theme"><span class=m>&#9790;</span><span class=s>&#9728;</span></button>
)html"

static const char PAGE_HOME[] PROGMEM = R"html(<!DOCTYPE html><html lang=en>
<head><meta charset=UTF-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>SensorBox &middot; Settings</title><link rel=stylesheet href=/style.css>)html" THEME_HEAD R"html(</head>
<body>
<nav>
  <a href=/ class=cur>Settings</a>
  <a href=/sandbox>Sandbox</a>
  <a href=/files>Files</a>
  <a href=/wifi>WiFi</a>
  <a href=/update>Update</a>
  <a href=/security>Security</a>
)html" THEME_BTN R"html(</nav>
<main>
<h2>Settings</h2>
<div id=cfg></div>
</main>
<script>
const GRP={
  'LoRaWAN':['version','method','relay','adr','dr','dbm','confirmed'],
  'Operation':['interval','sleep','operation','timeout'],
  'OTAA':['deveui','joineui','appkey','nwkkey'],
  'ABP':['devaddr','appskey','nwksenckey','fnwksintkey','snwksintkey'],
  'WiFi/BLE':['name','ssid','pass','user'],
  'Time':['timezone','dst']
};
const PWD=new Set(['pass','appkey','nwkkey','appskey','nwksenckey','fnwksintkey','snwksintkey']);
async function load(){
  const c=await(await fetch('/api/config')).json();
  let h='';
  for(const[g,ks]of Object.entries(GRP)){
    h+=`<div class=card><h3>${g}</h3>`;
    for(const k of ks){
      const v=(c[k]||'').replace(/&/g,'&amp;').replace(/"/g,'&quot;');
      h+=`<div class=row><span class=lbl>${k}</span>`+
         `<input type="${PWD.has(k)?'password':'text'}" id="f_${k}" value="${v}"`+
         ` onkeydown="if(event.key==='Enter')save('${k}')">`+
         `<button onclick="save('${k}')">Save</button>`+
         `<span id="s_${k}" class=badge></span></div>`;
    }
    h+='</div>';
  }
  document.getElementById('cfg').innerHTML=h;
}
async function save(k){
  const v=document.getElementById('f_'+k).value;
  const r=await fetch('/api/config?key='+encodeURIComponent(k)+'&value='+encodeURIComponent(v),{method:'POST'});
  const el=document.getElementById('s_'+k);
  if(r.ok){el.className='badge ok';el.textContent='OK';}
  else{el.className='badge err';el.textContent=await r.text();}
  setTimeout(()=>{el.textContent='';el.className='badge';},3000);
}
load();
</script></body></html>)html";

static const char PAGE_SANDBOX[] PROGMEM = R"html(<!DOCTYPE html><html lang=en>
<head><meta charset=UTF-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>SensorBox &middot; Sandbox</title><link rel=stylesheet href=/style.css>)html" THEME_HEAD R"html(</head>
<body>
<nav>
  <a href=/>Settings</a>
  <a href=/sandbox class=cur>Sandbox</a>
  <a href=/files>Files</a>
  <a href=/wifi>WiFi</a>
  <a href=/update>Update</a>
  <a href=/security>Security</a>
)html" THEME_BTN R"html(</nav>
<main>
<h2>Sandbox</h2>
<div class=card>
  <h3>Commands</h3>
  <div class=row>
    <button onclick="cmd('uplink')">Trigger Uplink</button>
    <button class=sec onclick="cmd('join')">Rejoin</button>
    <button class=sec onclick="cmd('devaddr')">DevAddr</button>
    <button class=sec onclick="cmd('id')">Chip ID</button>
    <button class=dan onclick="cmd('restart')">Restart</button>
  </div>
  <div id=cmdout style="margin-top:8px;font-family:monospace;font-size:12px;color:var(--mut);min-height:18px"></div>
</div>
<div class=card>
  <h3>GNSS Stream (raw NMEA)</h3>
  <div class=row>
    <button id=gbtn onclick="toggleGnss()">Start</button>
    <span id=gst style="color:var(--mut);font-size:12px">Idle</span>
    <button class=sec onclick="document.getElementById('gterm').textContent=''">Clear</button>
  </div>
  <div class=term id=gterm></div>
</div>
<div class=card>
  <h3>Accelerometer Triggers</h3>
  <div class=row>
    <button id=abtn onclick="toggleAccel()">Start</button>
    <span id=ast style="color:var(--mut);font-size:12px">Idle</span>
    <button class=sec onclick="document.getElementById('aterm').textContent=''">Clear</button>
  </div>
  <div class=term id=aterm style="height:160px"></div>
</div>
</main>
<script>
async function cmd(c){
  const r=await fetch('/api/command?cmd='+c,{method:'POST'});
  document.getElementById('cmdout').textContent=await r.text();
}
let gEs=null;
function toggleGnss(){
  const btn=document.getElementById('gbtn'),st=document.getElementById('gst'),t=document.getElementById('gterm');
  if(gEs){gEs.close();gEs=null;btn.textContent='Start';st.textContent='Idle';return;}
  gEs=new EventSource('/api/gnss');
  gEs.addEventListener('gnss',e=>{t.textContent+=e.data+'\n';t.scrollTop=t.scrollHeight;});
  gEs.onerror=()=>{st.textContent='Error';};
  btn.textContent='Stop';st.textContent='Streaming';
}
let aEs=null;
function toggleAccel(){
  const btn=document.getElementById('abtn'),st=document.getElementById('ast'),t=document.getElementById('aterm');
  if(aEs){aEs.close();aEs=null;btn.textContent='Start';st.textContent='Idle';return;}
  aEs=new EventSource('/api/accel');
  aEs.addEventListener('accel',e=>{t.textContent+=new Date().toLocaleTimeString([],{hour12:false})+' - '+e.data+'\n';t.scrollTop=t.scrollHeight;});
  aEs.onerror=()=>{st.textContent='Error';};
  btn.textContent='Stop';st.textContent='Listening';
}
</script></body></html>)html";

static const char PAGE_FILES[] PROGMEM = R"html(<!DOCTYPE html><html lang=en>
<head><meta charset=UTF-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>SensorBox &middot; Files</title><link rel=stylesheet href=/style.css>)html" THEME_HEAD R"html(</head>
<body>
<nav>
  <a href=/>Settings</a>
  <a href=/sandbox>Sandbox</a>
  <a href=/files class=cur>Files</a>
  <a href=/wifi>WiFi</a>
  <a href=/update>Update</a>
  <a href=/security>Security</a>
)html" THEME_BTN R"html(</nav>
<main>
<h2>Files</h2>
<div id=fsbar style="font-size:12px;color:var(--mut);margin-bottom:12px"></div>
<div class=row style="margin-bottom:12px">
  <button class=sec onclick="selAll()">Toggle All</button>
  <button class=dan onclick="delSel()">Delete Selected</button>
</div>
<div class=card>
  <table>
    <thead><tr><th style=width:32px></th><th>Name</th><th>Size</th><th>Lines</th><th>Actions</th></tr></thead>
    <tbody id=ftbl></tbody>
  </table>
</div>
</main>
<div id=modal>
  <div class=mbox>
    <div class=mhdr>
      <strong id=mname></strong>
      <button class=sec onclick="closeMod()">&#x2715;</button>
    </div>
    <div id=mcnt style="overflow:auto;max-height:65vh"></div>
  </div>
</div>
<script>
function esc(s){return s.replace(/&/g,'&amp;').replace(/"/g,'&quot;');}
function escH(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}
function fmt(b){return b<1024?b+'B':b<1048576?(b/1024).toFixed(1)+'KB':(b/1048576).toFixed(1)+'MB';}
async function load(){
  const[fr,sr]=await Promise.all([fetch('/api/files'),fetch('/api/fsinfo')]);
  const files=await fr.json(),fs=await sr.json();
  document.getElementById('fsbar').textContent=
    files.length+' file(s) · '+fmt(fs.used)+' used of '+fmt(fs.total);
  const tb=document.getElementById('ftbl');
  tb.innerHTML='';
  for(const f of files){
    const tr=document.createElement('tr');
    const en=esc(f.name);
    const ll=f.lines<0?'>64K':f.lines;
    tr.innerHTML=
      `<td><input type=checkbox data-n="${en}"></td>`+
      `<td class=mono>${f.name}</td>`+
      `<td>${fmt(f.size)}</td>`+
      `<td>${ll}</td>`+
      `<td style="display:flex;gap:4px;flex-wrap:wrap">`+
        `<button class=sec data-fn="${en}" onclick="prev(this)">View</button>`+
        `<a href="/api/files/download?name=${encodeURIComponent(f.name)}" download="${en}"><button class=sec>Download</button></a>`+
        `<button class=dan data-fn="${en}" onclick="del(this)">Delete</button>`+
      `</td>`;
    tb.appendChild(tr);
  }
}
function selAll(){document.querySelectorAll('#ftbl input[type=checkbox]').forEach(c=>c.checked=!c.checked);}
async function del(btn){
  const n=btn.dataset.fn;
  if(!confirm('Delete '+n+'?'))return;
  await fetch('/api/files?name='+encodeURIComponent(n),{method:'DELETE'});
  load();
}
async function delSel(){
  const ns=[...document.querySelectorAll('#ftbl input:checked')].map(c=>c.dataset.n);
  if(!ns.length)return;
  if(!confirm('Delete '+ns.length+' file(s)?'))return;
  const p=new URLSearchParams();
  ns.forEach(n=>p.append('names',n));
  await fetch('/api/files/delete',{method:'POST',body:p});
  load();
}

let _sc=-1,_sa=true;
function sortBy(col){
  const tbody=document.querySelector('#mcnt tbody');
  if(!tbody)return;
  _sa=(_sc===col)?!_sa:true;_sc=col;
  [...tbody.rows].sort((a,b)=>{
    const av=a.cells[col].textContent.trim(),bv=b.cells[col].textContent.trim();
    const an=parseFloat(av),bn=parseFloat(bv);
    const c=isNaN(an)||isNaN(bn)?av.localeCompare(bv):an-bn;
    return _sa?c:-c;
  }).forEach(r=>tbody.appendChild(r));
  document.querySelectorAll('#mcnt th').forEach((h,i)=>{
    h.dataset.raw=h.dataset.raw||h.textContent;
    h.textContent=h.dataset.raw+(i===col?(_sa?' ▲':' ▼'):'');
  });
}

async function prev(btn){
  const n=btn.dataset.fn;
  const r=await fetch('/api/files/content?name='+encodeURIComponent(n));
  const txt=await r.text();
  document.getElementById('mname').textContent=n;
  const el=document.getElementById('mcnt');
  if(n.toLowerCase().endsWith('.csv')){
    const rows=txt.trim().split('\n').filter(l=>l.trim()).map(l=>l.split(','));
    const hdr=rows[0]||[];
    let h='<table><thead><tr>';
    hdr.forEach((c,i)=>h+=`<th data-raw="${escH(c.trim())}" style=cursor:pointer onclick="sortBy(${i})">${escH(c.trim())}</th>`);
    h+='</tr></thead><tbody>';
    rows.slice(1).forEach(row=>{
      h+='<tr>';
      hdr.forEach((_,i)=>h+=`<td class=mono>${escH((row[i]||'').trim())}</td>`);
      h+='</tr>';
    });
    h+='</tbody></table>';
    el.innerHTML=h;
    _sc=-1;
  }else{
    el.innerHTML=`<pre style="font-size:12px;color:var(--txt);white-space:pre-wrap;word-break:break-all">${escH(txt)}</pre>`;
  }
  document.getElementById('modal').style.display='block';
}
function closeMod(){document.getElementById('modal').style.display='none';}
document.getElementById('modal').addEventListener('click',e=>{if(e.target.id==='modal')closeMod();});
load();
</script></body></html>)html";

static const char PAGE_WIFI[] PROGMEM = R"html(<!DOCTYPE html><html lang=en>
<head><meta charset=UTF-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>SensorBox &middot; WiFi</title><link rel=stylesheet href=/style.css>)html" THEME_HEAD R"html(</head>
<body>
<nav>
  <a href=/>Settings</a>
  <a href=/sandbox>Sandbox</a>
  <a href=/files>Files</a>
  <a href=/wifi class=cur>WiFi</a>
  <a href=/update>Update</a>
  <a href=/security>Security</a>
)html" THEME_BTN R"html(</nav>
<main>
<h2>WiFi</h2>
<div class=card>
  <h3>Current Connection</h3>
  <div id=winfo style="font-size:13px;color:var(--mut)">Loading...</div>
</div>
<div class=card>
  <h3>Scan Networks</h3>
  <div class=row>
    <button onclick="scan()">Scan</button>
    <span id=sst style="color:var(--mut);font-size:12px"></span>
  </div>
  <table style="margin-top:8px">
    <thead><tr><th>SSID</th><th>RSSI</th><th>Security</th><th></th></tr></thead>
    <tbody id=nets></tbody>
  </table>
</div>
<div class=card>
  <h3>Provision</h3>
  <div class=row><span class=lbl>SSID</span><input type=text id=pssid></div>
  <div class=row><span class=lbl>Password</span><input type=password id=ppass></div>
  <div class=row><span class=lbl>WPA2 User</span><input type=text id=puser placeholder="(leave empty for WPA2-PSK)"></div>
  <div class=row>
    <button onclick="provision()">Save Credentials</button>
    <span id=pst class=badge></span>
  </div>
</div>
</main>
<script>
async function loadInfo(){
  try{
    const d=await(await fetch('/api/wifi/info')).json();
    document.getElementById('winfo').innerHTML=
      'Mode: <b>'+d.mode+'</b> &nbsp; IP: <b>'+d.ip+'</b>'+(d.ssid?' &nbsp; SSID: <b>'+d.ssid+'</b>':'')+
      (d.rssi?' &nbsp; RSSI: <b>'+d.rssi+' dBm</b>':'');
  }catch(e){document.getElementById('winfo').textContent='Unavailable';}
}
async function scan(){
  const sst=document.getElementById('sst');
  const tb=document.getElementById('nets');
  sst.textContent='Scanning...';
  tb.innerHTML='';
  for(let i=0;i<20;i++){
    const r=await fetch('/api/wifi/scan');
    if(r.status===200){
      const ns=await r.json();
      sst.textContent=ns.length+' network(s) found';
      for(const n of ns){
        const tr=document.createElement('tr');
        tr.innerHTML=`<td>${n.ssid}</td><td>${n.rssi} dBm</td>`+
          `<td>${n.secure?'🔒':''}</td>`+
          `<td><button class=sec onclick="sel(${JSON.stringify(n.ssid)})">Select</button></td>`;
        tb.appendChild(tr);
      }
      return;
    }
    await new Promise(r=>setTimeout(r,1000));
  }
  sst.textContent='Scan timed out';
}
function sel(s){document.getElementById('pssid').value=s;document.getElementById('ppass').focus();}
async function provision(){
  const p=new URLSearchParams({
    ssid:document.getElementById('pssid').value,
    pass:document.getElementById('ppass').value,
    user:document.getElementById('puser').value
  });
  const r=await fetch('/api/wifi/provision',{method:'POST',body:p});
  const el=document.getElementById('pst');
  if(r.ok){el.className='badge ok';el.textContent='Saved';}
  else{el.className='badge err';el.textContent=await r.text();}
  setTimeout(()=>{el.textContent='';el.className='badge';},3000);
}
loadInfo();
</script></body></html>)html";

static const char PAGE_UPDATE[] PROGMEM = R"html(<!DOCTYPE html><html lang=en>
<head><meta charset=UTF-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>SensorBox &middot; Update</title><link rel=stylesheet href=/style.css>)html" THEME_HEAD R"html(</head>
<body>
<nav>
  <a href=/>Settings</a>
  <a href=/sandbox>Sandbox</a>
  <a href=/files>Files</a>
  <a href=/wifi>WiFi</a>
  <a href=/update class=cur>Update</a>
  <a href=/security>Security</a>
)html" THEME_BTN R"html(</nav>
<main>
<h2>Firmware / Filesystem Update</h2>
<div class=card>
  <h3>Select File</h3>
  <div class=dz id=dz onclick="document.getElementById('fi').click()">
    <div>Drop .bin file here or click to select</div>
    <div id=fn style="margin-top:8px;font-size:12px;color:var(--acc)"></div>
  </div>
  <input type=file id=fi accept=.bin style=display:none>
  <div class=prog id=pb style=display:none><div class=progb id=pbr></div></div>
  <div class=row style="margin-top:12px">
    <button id=fbtn onclick="flash('firmware')">Flash Firmware</button>
    <button id=sbtn class=sec onclick="flash('fs')">Flash Filesystem</button>
  </div>
  <div id=msg style="margin-top:8px;font-size:13px;color:var(--mut)"></div>
</div>
</main>
<script>
let selFile=null;
const dz=document.getElementById('dz');
dz.addEventListener('dragover',e=>{e.preventDefault();dz.classList.add('over');});
dz.addEventListener('dragleave',()=>dz.classList.remove('over'));
dz.addEventListener('drop',e=>{e.preventDefault();dz.classList.remove('over');setFile(e.dataTransfer.files[0]);});
document.getElementById('fi').addEventListener('change',e=>setFile(e.target.files[0]));
function setFile(f){
  if(!f)return;
  selFile=f;
  document.getElementById('fn').textContent=f.name+' ('+Math.round(f.size/1024)+' KB)';
}
async function flash(mode){
  if(!selFile){document.getElementById('msg').textContent='Select a file first.';return;}
  const fbtn=document.getElementById('fbtn'),sbtn=document.getElementById('sbtn');
  fbtn.disabled=sbtn.disabled=true;
  document.getElementById('msg').textContent='Initialising...';
  document.getElementById('pb').style.display='block';
  const pbr=document.getElementById('pbr');
  try{
    const s=await fetch('/ota/start?mode='+mode);
    if(!s.ok){document.getElementById('msg').textContent='Init failed: '+await s.text();fbtn.disabled=sbtn.disabled=false;return;}
    const xhr=new XMLHttpRequest();
    xhr.upload.onprogress=e=>{if(e.lengthComputable)pbr.style.width=(e.loaded/e.total*100)+'%';};
    xhr.onload=()=>{document.getElementById('msg').textContent=xhr.status===200?'Done! Device rebooting...':'Upload failed: '+xhr.responseText;fbtn.disabled=sbtn.disabled=false;};
    xhr.onerror=()=>{document.getElementById('msg').textContent='Connection error';fbtn.disabled=sbtn.disabled=false;};
    const fd=new FormData();
    fd.append('file',selFile,selFile.name);
    xhr.open('POST','/ota/upload');
    xhr.send(fd);
    document.getElementById('msg').textContent='Uploading...';
  }catch(e){document.getElementById('msg').textContent='Error: '+e.message;fbtn.disabled=sbtn.disabled=false;}
}
</script></body></html>)html";

// Login page (unauthenticated). Standalone, no nav. Submits credentials to
// /api/login via fetch; on success the server sets the session cookie and we
// navigate on. Can remember the credentials in localStorage ('mjlo_auth') and
// auto-login on the next visit.
static const char PAGE_LOGIN[] PROGMEM = R"html(<!DOCTYPE html><html lang=en>
<head><meta charset=UTF-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>SensorBox &middot; Sign in</title><link rel=stylesheet href=/style.css>)html" THEME_HEAD R"html(</head>
<body>
<main style="max-width:360px;margin-top:8vh">
<div class=card>
  <h2 style="margin-bottom:2px">Sign in</h2>
  <div style="font-size:12px;color:var(--mut);margin-bottom:16px">SensorBox</div>
  <div class=fld><label>Username (device ID)</label>
    <input type=text id=user autocomplete=username spellcheck=false placeholder="XX-XX-XX-XX-XX-XX"></div>
  <div class=fld><label>Password</label>
    <input type=password id=pass autocomplete=current-password></div>
  <label style="display:flex;align-items:center;gap:6px;font-size:13px;margin-bottom:14px">
    <input type=checkbox id=remember checked> Remember me on this device</label>
  <button id=go onclick=signIn() style="width:100%">Sign in</button>
  <div id=err class=val style="color:var(--err);margin-top:10px;min-height:16px"></div>
</div>
</main>
<script>
var qs=new URLSearchParams(location.search);
var next=qs.get('next')||'/';if(next.charAt(0)!=='/')next='/';
var KEY='mjlo_auth';
async function doLogin(u,p,r){
  var b=new URLSearchParams();b.append('user',u);b.append('pass',p);
  var res=await fetch('/api/login',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b.toString()});
  if(res.ok){
    try{if(r)localStorage.setItem(KEY,JSON.stringify({user:u,pass:p}));
      else localStorage.removeItem(KEY);}catch(e){}
    location.href=next;return true;
  }return false;
}
async function signIn(){
  var u=document.getElementById('user').value.trim();
  var p=document.getElementById('pass').value;
  var r=document.getElementById('remember').checked;
  var err=document.getElementById('err');err.textContent='';
  if(!u||!p){err.textContent='Enter your username and password.';return;}
  var btn=document.getElementById('go');btn.disabled=true;btn.textContent='Signing in...';
  var ok=await doLogin(u,p,r);
  if(!ok){err.textContent='Incorrect username or password.';
    btn.disabled=false;btn.textContent='Sign in';}
}
document.getElementById('pass').addEventListener('keydown',function(e){
  if(e.key==='Enter')signIn();});
(async function(){
  try{var s=JSON.parse(localStorage.getItem(KEY)||'null');
    if(s&&s.user&&s.pass){
      document.getElementById('user').value=s.user;
      var ok=await doLogin(s.user,s.pass,true);
      if(!ok){try{localStorage.removeItem(KEY);}catch(e){}}
    }
  }catch(e){}
})();
</script></body></html>)html";

// Security page (authenticated). Shows the username (device ID), whether a
// custom password is set, and lets the operator change it, reset it to the
// device-code default, or log out.
static const char PAGE_SECURITY[] PROGMEM = R"html(<!DOCTYPE html><html lang=en>
<head><meta charset=UTF-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>SensorBox &middot; Security</title><link rel=stylesheet href=/style.css>)html" THEME_HEAD R"html(</head>
<body>
<nav>
  <a href=/>Settings</a>
  <a href=/sandbox>Sandbox</a>
  <a href=/files>Files</a>
  <a href=/wifi>WiFi</a>
  <a href=/update>Update</a>
  <a href=/security class=cur>Security</a>
)html" THEME_BTN R"html(</nav>
<main>
<h2>Security</h2>
<div class=card>
  <h3>Account</h3>
  <div class=row><span class=lbl>Username</span><span class="val mono" id=user>...</span></div>
  <div class=row><span class=lbl>Password</span><span class=val id=pwstate>...</span></div>
</div>
<div class=card>
  <h3>Change password</h3>
  <div class=fld><label>New password</label>
    <input type=password id=np autocomplete=new-password></div>
  <div class=fld><label>Confirm new password</label>
    <input type=password id=np2 autocomplete=new-password></div>
  <div class=row>
    <button id=savebtn onclick=savePw()>Update password</button>
    <button class=sec onclick=resetPw()>Reset to default</button>
    <span id=st class=val style="margin-left:10px"></span>
  </div>
  <div style="font-size:12px;color:var(--mut);margin-top:10px">
    The default password is the device code, derived from the chip ID and
    printed by the <span class=mono>+id</span> serial command. Resetting clears
    the remembered login on this browser.</div>
</div>
<div class=card>
  <h3>Session</h3>
  <div class=row><button class=dan onclick=logout()>Log out</button></div>
</div>
</main>
<script>
var KEY='mjlo_auth';
async function load(){
  try{var d=await(await fetch('/api/security')).json();
    document.getElementById('user').textContent=d.eui||'-';
    document.getElementById('pwstate').textContent=
      d.custom?'Custom password set':'Default (device code)';
  }catch(e){}
}
async function savePw(){
  var a=document.getElementById('np').value;
  var b=document.getElementById('np2').value;
  var s=document.getElementById('st');
  if(!a){s.textContent='Enter a new password.';return;}
  if(a!==b){s.textContent='Passwords do not match.';return;}
  var btn=document.getElementById('savebtn');btn.disabled=true;
  var p=new URLSearchParams();p.append('pass',a);
  try{
    var r=await fetch('/api/security/password',{method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});
    if(r.ok){s.textContent='Password updated.';
      document.getElementById('np').value='';document.getElementById('np2').value='';
      try{var c=JSON.parse(localStorage.getItem(KEY)||'null');
        if(c&&c.user){c.pass=a;localStorage.setItem(KEY,JSON.stringify(c));}}catch(e){}
      load();
    }else{s.textContent='Update failed: '+await r.text();}
  }catch(e){s.textContent='Update failed.';}
  btn.disabled=false;
}
async function resetPw(){
  if(!confirm('Reset the web password back to the default device code?'))return;
  var s=document.getElementById('st');
  var p=new URLSearchParams();p.append('reset','1');
  try{
    var r=await fetch('/api/security/password',{method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});
    if(r.ok){s.textContent='Reset to default. Remembered login cleared.';
      try{localStorage.removeItem(KEY);}catch(e){}load();}
    else{s.textContent='Reset failed.';}
  }catch(e){s.textContent='Reset failed.';}
}
async function logout(){
  try{await fetch('/api/logout',{method:'POST'});}catch(e){}
  try{localStorage.removeItem(KEY);}catch(e){}
  location.href='/login';
}
load();
</script></body></html>)html";

// ============================================================
// WiFi management
// ============================================================

bool connectWiFi() {
  wifiMode = WIFI_MODE_STA;
  WiFi.mode(wifiMode);

  if (cfg.wl2g4.user == "") {
    WiFi.begin(cfg.wl2g4.ssid.c_str(), cfg.wl2g4.pass.c_str());
  } else {
    ESP_ERROR_CHECK(esp_eap_client_set_identity((uint8_t *)cfg.wl2g4.user.c_str(), strlen(cfg.wl2g4.user.c_str())));
    ESP_ERROR_CHECK(esp_eap_client_set_password((uint8_t *)cfg.wl2g4.pass.c_str(), strlen(cfg.wl2g4.pass.c_str())));
    ESP_ERROR_CHECK(esp_wifi_sta_enterprise_enable());
    WiFi.begin(cfg.wl2g4.ssid.c_str());
  }

  Serial.printf("[WiFi] Connecting to [%s]...\r\n", cfg.wl2g4.ssid.c_str());
  uint8_t status = WiFi.waitForConnectResult(20000);
  Serial.printf("[WiFi] Status: %d\r\n", status);

  switch (status) {
    case WL_CONNECTED:
      IP = WiFi.localIP();
      break;
    case WL_NO_SSID_AVAIL:
    default:
      WiFi.disconnect(true);
      wifiMode = WIFI_MODE_AP;
      WiFi.mode(wifiMode);
      WiFi.softAP(cfg.wl2g4.ssid.c_str(), cfg.wl2g4.pass.c_str());
      IP = WiFi.softAPIP();
      break;
  }

  Serial.printf("[WiFi] IP: %s\r\n", IP.toString().c_str());

  // Start an async WiFi scan for the WiFi page
  if (WiFi.scanComplete() == WIFI_SCAN_FAILED)
    WiFi.scanNetworks(true, false);

  if (mdns_init() == ESP_OK)
    mdns_hostname_set(cfg.wl2g4.name.c_str());

  return true;
}

void disconnectWiFi() {
  wifiMode = WIFI_MODE_NULL;
  WiFi.disconnect(true);
  WiFi.mode(wifiMode);
}

// ============================================================
// JSON helpers (write to AsyncResponseStream without String)
// ============================================================

static void jsonStr(AsyncResponseStream *res, const char *s) {
  while (*s) {
    if      (*s == '"')  res->print(F("\\\""));
    else if (*s == '\\') res->print(F("\\\\"));
    else if (*s == '\n') res->print(F("\\n"));
    else if (*s == '\r') res->print(F("\\r"));
    else                 res->write((uint8_t)*s);
    s++;
  }
}

// ============================================================
// Authentication
//
// The web UI is protected by a username/password:
//   • username = the device ID (the chip ID, as printed by the +id command)
//   • password = configurable on the Security page; defaults to the device
//     code, i.e. the last eight characters of the device ID
//
// A successful POST /api/login sets an HttpOnly session cookie ("sid"). The
// cookie value is a token derived from the device ID + the current password,
// so it needs no server-side session table and is invalidated automatically
// when the password changes. Protected page requests without a valid cookie
// are redirected to /login; protected API requests get 401. The browser can
// remember the credentials in localStorage for auto-login — handled entirely
// client-side on the /login page.
//
// NOTE: this runs over plain HTTP on the local network. The login is a real
// barrier against casual access but not against someone who can sniff the
// LAN — the credentials and the session cookie travel in the clear.
// ============================================================

#define SID_COOKIE  "sid"
// Cookie lifetime + attributes (no Secure flag: served over HTTP).
#define SID_ATTRS   "; Path=/; Max-Age=604800; HttpOnly; SameSite=Strict"
#define WEBPASS_KEY "webpass"

// Device ID — the login username. Six hex pairs of the eFuse MAC, in the same
// order the +id serial command prints them, so the two always agree.
static void computeDevId(char *out, size_t len) {
  uint64_t id = ESP.getEfuseMac();
  snprintf(out, len, "%02X-%02X-%02X-%02X-%02X-%02X",
           (uint8_t)(id >> 40), (uint8_t)(id >> 32), (uint8_t)(id >> 24),
           (uint8_t)(id >> 16), (uint8_t)(id >>  8), (uint8_t)(id));
}

// Default web password = the device code: the last eight characters of the
// device ID (its low 32 bits), unique per device and printable over serial.
void webDefaultPassword(char *out, size_t len) {
  snprintf(out, len, "%08X", (uint32_t)ESP.getEfuseMac());
}

// Drop separators and upper-case so "a1b2..", "A1-B2-.." and "a1:b2.." all
// compare equal when checking the submitted username against the device ID.
static String normalizeId(const String &in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '-' || c == ':' || c == ' ') continue;
    if (c >= 'a' && c <= 'z') c -= 32;
    out += c;
  }
  return out;
}

// The password lives in a raw NVS key outside the Settings[] table, so it is
// invisible to loadConfig(), printConfig() and /api/config — and unlike a file
// on LittleFS it cannot be read back through the Files page.
static bool webpassIsCustom() {
  Preferences p;
  p.begin("config", true);
  bool custom = p.getString(WEBPASS_KEY, "").length() > 0;
  p.end();
  return custom;
}

static String getPassword() {
  Preferences p;
  p.begin("config", true);
  String pw = p.getString(WEBPASS_KEY, "");
  p.end();
  if (pw.length()) return pw;
  char buf[16];
  webDefaultPassword(buf, sizeof(buf));
  return String(buf);
}

static bool setPassword(const String &pw) {
  Preferences p;
  if (!p.begin("config")) return false;
  bool ok = p.putString(WEBPASS_KEY, pw) > 0;
  p.end();
  return ok;
}

void webResetPassword() {
  Preferences p;
  if (!p.begin("config")) return;
  p.remove(WEBPASS_KEY);
  p.end();
}

// Session token = two 64-bit FNV-1a passes over device ID + current password,
// hex-encoded. Stateless: changing the password changes the token, which
// invalidates every outstanding session for free.
static uint32_t fnv1a(const char *s, uint32_t h) {
  while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
  return h;
}

static String sessionToken() {
  char id[24];
  computeDevId(id, sizeof(id));
  String pw = getPassword();
  uint32_t a = fnv1a(pw.c_str(), fnv1a("|", fnv1a(id, fnv1a("mjloA:", 2166136261u))));
  uint32_t b = fnv1a(pw.c_str(), fnv1a("#", fnv1a(id, fnv1a("mjloB:", 0xC6A4A793u))));
  char tok[17];
  snprintf(tok, sizeof(tok), "%08x%08x", a, b);
  return String(tok);
}

// Does the request carry a valid "sid" cookie? ESPAsyncWebServer has no cookie
// accessor, so walk the raw Cookie header ourselves.
static bool cookieOk(AsyncWebServerRequest *req) {
  if (!req->hasHeader("Cookie")) return false;
  const String &c = req->header("Cookie");
  int i = 0;
  while (i < (int)c.length()) {
    while (i < (int)c.length() && (c[i] == ' ' || c[i] == ';')) i++;
    int eq = c.indexOf('=', i);
    if (eq < 0) break;
    int end = c.indexOf(';', eq);
    if (end < 0) end = c.length();
    if (c.substring(i, eq) == SID_COOKIE)
      return c.substring(eq + 1, end) == sessionToken();
    i = end + 1;
  }
  return false;
}

// Send a response that (re)issues the session cookie for the current token.
static void sendWithCookie(AsyncWebServerRequest *req, int code,
                           const char *type, const String &body) {
  AsyncWebServerResponse *r = req->beginResponse(code, type, body);
  r->addHeader(F("Set-Cookie"), String(F(SID_COOKIE "=")) + sessionToken() + F(SID_ATTRS));
  req->send(r);
}

// ============================================================
// SSE: raw NMEA byte accumulator → fires one SSE per line
// ============================================================

void sendNmeaByte(char c) {
  if (gnssEvents.count() == 0) return;
  static char buf[128];
  static int  len = 0;
  if (c == '\n') {
    buf[len] = '\0';
    if (len > 0) gnssEvents.send(buf, "gnss", millis());
    len = 0;
  } else if (c != '\r' && len < 127) {
    buf[len++] = c;
  }
}

void sendAccelEvent() {
  if (accelEvents.count() == 0) return;
  accelEvents.send("Motion", "accel", millis());
}

// ============================================================
// Route handlers
// ============================================================

// GET /api/config  — all settings as JSON object
static void handleConfigGet(AsyncWebServerRequest *req) {
  AsyncResponseStream *res = req->beginResponseStream(F("application/json"));
  res->print('{');
  for (uint16_t i = 0; i < NUM_SETTINGS_METADATA; i++) {
    if (i) res->print(',');
    res->print('"');
    res->print(settingsMetadata[i].key);
    res->print(F("\":\""));
    // getByIndex() serves the cache and falls back to the metadata default,
    // so there is no need to open NVS here.
    jsonStr(res, configMgr.getByIndex(i).c_str());
    res->print('"');
  }
  res->print('}');
  req->send(res);
}

// POST /api/config?key=X&value=Y  — apply one setting
static void handleConfigPost(AsyncWebServerRequest *req) {
  if (!req->hasParam("key") || !req->hasParam("value")) {
    req->send(400, F("text/plain"), F("Missing key/value"));
    return;
  }
  String key = req->getParam("key")->value();
  String val = req->getParam("value")->value();
  int err = doSetting(key, val);
  if (err == noError) req->send(200, F("text/plain"), F("OK"));
  else                req->send(400, F("text/plain"), parseError(err).c_str());
}

// LittleFS is used as a flat root here — no handler ever builds a nested path.
// Reject anything that could escape it, and anything that would silently
// truncate into the 66-byte path buffers below.
static bool safeName(const String &n) {
  return n.length() > 0 && n.length() < 64 &&
         n.indexOf('/') < 0 && n.indexOf('\\') < 0 && n.indexOf("..") < 0;
}

// GET /api/files  — JSON array of {name, size, lines}
static void handleFilesGet(AsyncWebServerRequest *req) {
  AsyncResponseStream *res = req->beginResponseStream(F("application/json"));
  res->print('[');
  bool first = true;
  File root = FS.open("/");
  if (root) {
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        if (!first) res->print(',');
        first = false;
        const char *raw  = f.name();
        const char *name = (raw[0] == '/') ? raw + 1 : raw;
        size_t      sz   = f.size();

        // Count lines (cap at 64KB to avoid stalling)
        int lines = 0;
        char path[66];
        snprintf(path, sizeof(path), "/%s", name);
        File fc = FS.open(path, "r");
        if (fc) {
          const size_t CAP = 65536;
          size_t read = 0;
          while (fc.available() && read < CAP) {
            if (fc.read() == '\n') lines++;
            read++;
          }
          if (read >= CAP) lines = -(lines); // negative = truncated
          fc.close();
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "{\"name\":\"");
        res->print(buf);
        jsonStr(res, name);
        snprintf(buf, sizeof(buf), "\",\"size\":%d,\"lines\":%d}", (int)sz, lines);
        res->print(buf);
      }
      f = root.openNextFile();
    }
    root.close();
  }
  res->print(']');
  req->send(res);
}

// GET /api/fsinfo  — {total, used}
static void handleFsInfo(AsyncWebServerRequest *req) {
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"total\":%d,\"used\":%d}",
           (int)FS.totalBytes(), (int)FS.usedBytes());
  req->send(200, F("application/json"), buf);
}

// GET /api/files/content?name=X  — file preview (≤8 KB)
static void handleFilesContent(AsyncWebServerRequest *req) {
  if (!req->hasParam("name")) { req->send(400); return; }
  const String &name = req->getParam("name")->value();
  if (!safeName(name)) { req->send(400, F("text/plain"), F("Bad name")); return; }
  char path[66];
  snprintf(path, sizeof(path), "/%s", name.c_str());
  if (!FS.exists(path)) { req->send(404); return; }
  File f = FS.open(path, "r");
  if (!f) { req->send(500); return; }
  AsyncResponseStream *res = req->beginResponseStream(F("text/plain"));
  const size_t LIMIT = 8192;
  size_t remaining = min((size_t)f.size(), LIMIT);
  uint8_t buf[512];
  while (remaining > 0) {
    size_t chunk = min(remaining, (size_t)sizeof(buf));
    size_t rd    = f.read(buf, chunk);
    if (!rd) break;
    res->write(buf, rd);
    remaining -= rd;
  }
  if (f.size() > LIMIT) res->print(F("\n[Preview limited to 8 KB]"));
  f.close();
  req->send(res);
}

// GET /api/files/download?name=X  — file download
static void handleFilesDownload(AsyncWebServerRequest *req) {
  if (!req->hasParam("name")) { req->send(400); return; }
  const String &name = req->getParam("name")->value();
  if (!safeName(name)) { req->send(400, F("text/plain"), F("Bad name")); return; }
  char path[66];
  snprintf(path, sizeof(path), "/%s", name.c_str());
  if (!FS.exists(path)) { req->send(404); return; }
  AsyncWebServerResponse *r = req->beginResponse(FS, path, F("application/octet-stream"));
  String cd = "attachment; filename=\"" + name + "\"";
  r->addHeader(F("Content-Disposition"), cd.c_str());
  req->send(r);
}

// DELETE /api/files?name=X  — delete single file
static void handleFilesDelete(AsyncWebServerRequest *req) {
  if (!req->hasParam("name")) { req->send(400); return; }
  const String &name = req->getParam("name")->value();
  if (!safeName(name)) { req->send(400, F("text/plain"), F("Bad name")); return; }
  char path[66];
  snprintf(path, sizeof(path), "/%s", name.c_str());
  if (!FS.exists(path)) { req->send(404); return; }
  FS.remove(path);
  req->send(200, F("text/plain"), F("OK"));
}

// POST /api/files/delete  — batch delete (body: names=a&names=b…)
static void handleFilesDeleteBatch(AsyncWebServerRequest *req) {
  int count = req->params();
  for (int i = 0; i < count; i++) {
    const AsyncWebParameter *p = req->getParam(i);
    if (p->isPost() && p->name() == "names" && safeName(p->value())) {
      char path[66];
      snprintf(path, sizeof(path), "/%s", p->value().c_str());
      FS.remove(path);
    }
  }
  req->send(200, F("text/plain"), F("OK"));
}

// POST /api/command?cmd=X  — sandbox misc commands
static void handleCommand(AsyncWebServerRequest *req) {
  if (!req->hasParam("cmd")) { req->send(400); return; }
  const String &c = req->getParam("cmd")->value();

  if (c == "devaddr") {
    char buf[20];
    snprintf(buf, sizeof(buf), "DevAddr: %08X", webGetDevAddr());
    req->send(200, F("text/plain"), buf);
    return;
  }
  if (c == "id") {
    uint64_t id = ESP.getEfuseMac();
    char buf[24];
    snprintf(buf, sizeof(buf), "ChipID: %04X%08X", (uint16_t)(id >> 32), (uint32_t)id);
    req->send(200, F("text/plain"), buf);
    return;
  }

  String cmd = "+" + c;
  int err = execCommand(cmd);
  if (err == noError) req->send(200, F("text/plain"), F("OK"));
  else                req->send(400, F("text/plain"), parseError(err).c_str());
}

// GET /api/wifi/info
static void handleWifiInfo(AsyncWebServerRequest *req) {
  AsyncResponseStream *res = req->beginResponseStream(F("application/json"));
  const char *mode = (wifiMode == WIFI_MODE_STA) ? "STA" :
                     (wifiMode == WIFI_MODE_AP)  ? "AP"  : "NULL";
  res->print(F("{\"mode\":\""));
  res->print(mode);
  res->print(F("\",\"ip\":\""));
  res->print(IP.toString().c_str());
  res->print(F("\",\"ssid\":\""));
  if (wifiMode == WIFI_MODE_STA)
    jsonStr(res, WiFi.SSID().c_str());
  else if (wifiMode == WIFI_MODE_AP)
    jsonStr(res, WiFi.softAPSSID().c_str());
  char buf[32];
  snprintf(buf, sizeof(buf), "\",\"rssi\":%d}", (wifiMode == WIFI_MODE_STA) ? WiFi.RSSI() : 0);
  res->print(buf);
  req->send(res);
}

// GET /api/wifi/scan  — 202 while scanning, 200+JSON when done
static void handleWifiScan(AsyncWebServerRequest *req) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    req->send(202, F("application/json"), F("[]"));
    return;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true, false);
    req->send(202, F("application/json"), F("[]"));
    return;
  }
  AsyncResponseStream *res = req->beginResponseStream(F("application/json"));
  res->print('[');
  for (int i = 0; i < n; i++) {
    if (i) res->print(',');
    res->print(F("{\"ssid\":\""));
    jsonStr(res, WiFi.SSID(i).c_str());
    char buf[32];
    snprintf(buf, sizeof(buf), "\",\"rssi\":%d,\"secure\":%s}",
             WiFi.RSSI(i),
             WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
    res->print(buf);
  }
  res->print(']');
  req->send(res);
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false); // start next scan for subsequent requests
}

// POST /api/wifi/provision  — save SSID/pass/user
static void handleWifiProvision(AsyncWebServerRequest *req) {
  if (!req->hasParam("ssid", true) || !req->hasParam("pass", true)) {
    req->send(400, F("text/plain"), F("Missing ssid/pass"));
    return;
  }
  String ssid = req->getParam("ssid", true)->value();
  String pass = req->getParam("pass", true)->value();
  String user = req->hasParam("user", true) ? req->getParam("user", true)->value() : String("");

  String ks = "ssid", kp = "pass", ku = "user";
  int e1 = doSetting(ks, ssid);
  int e2 = doSetting(kp, pass);
  int e3 = doSetting(ku, user);

  if (e1) { req->send(400, F("text/plain"), parseError(e1).c_str()); return; }
  if (e2) { req->send(400, F("text/plain"), parseError(e2).c_str()); return; }
  if (e3) { req->send(400, F("text/plain"), parseError(e3).c_str()); return; }
  req->send(200, F("text/plain"), F("OK"));
}

// ============================================================
// Auth API
//   POST /api/login             — validate device ID + password, set cookie
//   POST /api/logout            — clear the session cookie
//   GET  /api/security          — username + whether a custom password is set
//   POST /api/security/password — change password, or reset=1 for the default
// ============================================================

static void handleLogin(AsyncWebServerRequest *req) {
  String user = req->hasParam("user", true) ? req->getParam("user", true)->value() : String("");
  String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : String("");

  char id[24];
  computeDevId(id, sizeof(id));
  String nu = normalizeId(user);
  String ni = normalizeId(String(id));

  if (nu.length() == 0 || nu != ni || pass != getPassword()) {
    delay(200);  // throttle brute-force attempts
    req->send(401, F("application/json"), F("{\"ok\":false}"));
    return;
  }

  Serial.println("[Web] Login OK");
  sendWithCookie(req, 200, "application/json", F("{\"ok\":true}"));
}

static void handleLogout(AsyncWebServerRequest *req) {
  AsyncWebServerResponse *r = req->beginResponse(200, F("text/plain"), F("OK"));
  r->addHeader(F("Set-Cookie"), F(SID_COOKIE "=" SID_ATTRS));
  req->send(r);
}

static void handleSecurityGet(AsyncWebServerRequest *req) {
  char id[24];
  computeDevId(id, sizeof(id));
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"eui\":\"%s\",\"custom\":%s}",
           id, webpassIsCustom() ? "true" : "false");
  req->send(200, F("application/json"), buf);
}

static void handleSecurityPassword(AsyncWebServerRequest *req) {
  if (req->hasParam("reset", true) && req->getParam("reset", true)->value() == "1") {
    webResetPassword();
    Serial.println("[Web] Password reset to default");
    // Re-issue the cookie so the operator stays logged in under the default.
    sendWithCookie(req, 200, "text/plain", F("OK"));
    return;
  }

  String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : String("");
  if (pass.length() == 0) {
    req->send(400, F("text/plain"), F("Invalid password"));
    return;
  }
  if (!setPassword(pass)) {
    req->send(500, F("text/plain"), F("Cannot save"));
    return;
  }
  Serial.println("[Web] Password changed");
  sendWithCookie(req, 200, "text/plain", F("OK"));
}

// ============================================================
// OTA helpers
// ============================================================

void otaLoop() {
  // Reboot 500 ms after a successful OTA upload so the HTTP response
  // has time to reach the browser before the device disappears.
  if (_otaReboot && millis() - _otaRebootMs > 500) {
    ESP.restart();
  }
}

// ============================================================
// Server lifecycle
// ============================================================

void start_file_browser() {
  // Auth gate. The server-level middleware chain runs before every handler,
  // including the SSE event sources, so this one registration protects every
  // route below without touching the individual handlers.
  server.addMiddleware([](AsyncWebServerRequest *req, ArMiddlewareNext next) {
    const String &u = req->url();
    bool open = (u == "/login") || (u == "/style.css") ||
                (u == "/api/login") || (u == "/api/logout");
    if (open || cookieOk(req)) { next(); return; }

    if (req->method() == HTTP_GET && !u.startsWith("/api/") && !u.startsWith("/ota/"))
      req->redirect("/login?next=" + u);
    else
      req->send(401, F("application/json"), F("{\"error\":\"auth\"}"));
  });

  // CSS — now fetched by every page including /login, so let the browser cache it
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *r = req->beginResponse(200, "text/css", CSS);
    r->addHeader(F("Cache-Control"), F("max-age=86400"));
    req->send(r);
  });

  // Pages
  server.on("/",         HTTP_GET, [](AsyncWebServerRequest *req) { req->send(200, "text/html", PAGE_HOME);    });
  server.on("/sandbox",  HTTP_GET, [](AsyncWebServerRequest *req) { req->send(200, "text/html", PAGE_SANDBOX); });
  server.on("/files",    HTTP_GET, [](AsyncWebServerRequest *req) { req->send(200, "text/html", PAGE_FILES);   });
  server.on("/wifi",     HTTP_GET, [](AsyncWebServerRequest *req) { req->send(200, "text/html", PAGE_WIFI);    });
  server.on("/login",    HTTP_GET, [](AsyncWebServerRequest *req) { req->send(200, "text/html", PAGE_LOGIN);   });
  server.on("/security", HTTP_GET, [](AsyncWebServerRequest *req) { req->send(200, "text/html", PAGE_SECURITY);});

  server.on("/update",  HTTP_GET, [](AsyncWebServerRequest *req) { req->send(200, "text/html", PAGE_UPDATE); });

  // API — auth (the /password sub-path must precede its parent, see below)
  server.on("/api/security/password", HTTP_POST, handleSecurityPassword);
  server.on("/api/security",          HTTP_GET,  handleSecurityGet);
  server.on("/api/login",             HTTP_POST, handleLogin);
  server.on("/api/logout",            HTTP_POST, handleLogout);

  // API — config
  server.on("/api/config", HTTP_GET,  handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);

  // API — files (sub-paths must be registered before the parent /api/files,
  // because this ESPAsyncWebServer version matches any URL that starts with
  // a registered URI prefix + '/', so /api/files would otherwise shadow them)
  server.on("/api/files/content",  HTTP_GET,    handleFilesContent);
  server.on("/api/files/download", HTTP_GET,    handleFilesDownload);
  server.on("/api/files/delete",   HTTP_POST,   handleFilesDeleteBatch);
  server.on("/api/files",          HTTP_GET,    handleFilesGet);
  server.on("/api/files",          HTTP_DELETE, handleFilesDelete);
  server.on("/api/fsinfo",         HTTP_GET,    handleFsInfo);

  // API — sandbox
  server.on("/api/command", HTTP_POST, handleCommand);

  // API — wifi
  server.on("/api/wifi/info",      HTTP_GET,  handleWifiInfo);
  server.on("/api/wifi/scan",      HTTP_GET,  handleWifiScan);
  server.on("/api/wifi/provision", HTTP_POST, handleWifiProvision);

  // SSE event sources
  gnssEvents.onConnect([](AsyncEventSourceClient *c) {
    c->send("connected", "status", millis());
  });
  accelEvents.onConnect([](AsyncEventSourceClient *c) {
    c->send("connected", "status", millis());
  });
  server.addHandler(&gnssEvents);
  server.addHandler(&accelEvents);

  // OTA — initialise the Update subsystem, optionally unmount FS for fs flash
  server.on("/ota/start", HTTP_GET, [](AsyncWebServerRequest *req) {
    int mode = U_FLASH;
    if (req->hasParam("mode") && req->getParam("mode")->value() == "fs") {
      mode = U_SPIFFS;
      FS.end(); // unmount LittleFS before overwriting its partition
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, mode)) {
      String err;
      Update.printError(Serial);
      req->send(500, F("text/plain"), Update.errorString());
      return;
    }
    req->send(200, F("text/plain"), F("OK"));
  });

  // OTA — receive firmware/filesystem binary in chunks
  server.on("/ota/upload", HTTP_POST,
    // onRequest: send final response once upload is done
    [](AsyncWebServerRequest *req) {
      if (!_otaUploadAuth) { req->send(401, F("application/json"), F("{\"error\":\"auth\"}")); return; }
      bool ok = !Update.hasError();
      AsyncWebServerResponse *r = req->beginResponse(
        ok ? 200 : 500, F("text/plain"),
        ok ? F("OK") : F("Upload failed"));
      r->addHeader(F("Connection"), F("close"));
      req->send(r);
      if (ok) {
        _otaReboot   = true;
        _otaRebootMs = millis();
      }
    },
    // onUpload: write each multipart chunk to flash
    [](AsyncWebServerRequest *req, const String &filename,
       size_t index, uint8_t *data, size_t len, bool final) {
      // The middleware chain only runs once the body has been parsed, i.e.
      // after this callback has already seen the whole upload — so the auth
      // check has to happen here too, before anything reaches the flash.
      if (index == 0) _otaUploadAuth = cookieOk(req);
      if (!_otaUploadAuth) return;

      if (len && Update.write(data, len) != len) {
        Update.printError(Serial);
      }
      if (final) {
        if (!Update.end(true)) {
          Update.printError(Serial);
        } else {
          Serial.printf("[OTA] Flashed %s (%u bytes)\r\n",
                        filename.c_str(), index + len);
        }
      }
    });

  server.onNotFound([](AsyncWebServerRequest *req) {
    req->send(404, F("text/plain"), F("Not found"));
  });

  server.begin();
  Serial.println("[Web] Server started");
}

void end_file_browser() {
  server.end();
}
