// ═══════════════════════════════════════════════════════════════════════════════
//  hotspot_manager.cpp — AP Mode for MP3 file upload (Hotspot Mode)
// ═══════════════════════════════════════════════════════════════════════════════
//  Simplified version of SecureVault's AP mode. No encryption, no pairing code,
//  no 6-layer security stack. Just a simple SoftAP + DNS hijack + captive
//  portal with a file upload page for adding MP3 files to the /MP3/ folder.
//
//  Lifecycle:
//    1. User selects "HOTSPOT" in the mode menu.
//    2. start() brings up SoftAP + DNS hijack + web server.
//    3. UI shows the HOTSPOT info screen (SSID, QR code, BACK).
//    4. User joins the AP, opens the captive portal, uploads MP3 files.
//    5. User taps BACK, or 5-min idle timeout → stop().
// ═══════════════════════════════════════════════════════════════════════════════
#include "hotspot_manager.h"
#include "board_config.h"
#include "mp3_manager.h"
#include "sd_manager.h"   // for NVS-backed hotspot credentials
#include <SD.h>
#include <cstring>

// ═══════════════════════════════════════════════════════════════════════════════
//  Embedded HTML for the captive portal upload page
// ═══════════════════════════════════════════════════════════════════════════════
static const char PROGMEM HTML_PORTAL[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<meta http-equiv="Cache-Control" content="no-store,no-cache,must-revalidate">
<title>ShadowTune</title>
<style>
:root{--bg:#0f0f1a;--card:#1a1a2e;--ac:#8b5cf6;--ac2:#a78bfa;--tx:#eaeaea;--mu:#8888a0;--bd:#2a2a3e;--ok:#4ecdc4;--sh:rgba(0,0,0,.4)}
.L{--bg:#f5f5f7;--card:#fff;--ac:#7c3aed;--ac2:#6d28d9;--tx:#1a1a2e;--mu:#636e80;--bd:#e0e0e8;--ok:#00b894;--sh:rgba(0,0,0,.08)}
*{margin:0;padding:0;box-sizing:border-box}body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--tx);height:100vh;overflow:hidden;transition:background .3s,color .3s}
button{font-family:inherit;cursor:pointer;border:none;background:none;color:inherit;font-size:inherit}
input,select{font-family:inherit}
::-webkit-scrollbar{width:5px}::-webkit-scrollbar-thumb{background:var(--bd);border-radius:3px}
.app{display:flex;height:100vh;width:100vw}
.sb{width:216px;background:var(--card);border-right:1px solid var(--bd);display:flex;flex-direction:column;padding:14px 10px;overflow-y:auto;flex-shrink:0;transition:transform .3s}
.mn{flex:1;display:flex;flex-direction:column;overflow:hidden;min-width:0}
.tb{display:flex;align-items:center;gap:6px;padding:10px 14px;border-bottom:1px solid var(--bd);background:var(--card);flex-shrink:0}
.ct{flex:1;overflow-y:auto;padding:14px;padding-bottom:76px;-webkit-overflow-scrolling:touch;overscroll-behavior-y:contain}
.pl{height:60px;background:var(--card);border-top:1px solid var(--bd);display:none;align-items:center;padding:0 14px;gap:10px;flex-shrink:0}
.sbl{font-size:1.15rem;font-weight:700;color:var(--ac);display:flex;align-items:center;gap:5px}
.sbs{font-size:.68rem;color:var(--mu);margin:2px 0 16px}
.sn{display:flex;flex-direction:column;gap:1px;margin-bottom:14px}
.sb button,.sn button{display:flex;align-items:center;gap:7px;padding:7px 9px;border-radius:7px;font-size:.82rem;transition:background .15s;width:100%;text-align:left}
.sb button:hover,.sn button.on{background:var(--bd)}.sn button.on{color:var(--ac)}
.ss{font-size:.68rem;color:var(--mu);text-transform:uppercase;letter-spacing:.5px;margin:10px 0 5px;padding-left:3px}
.sd{height:1px;background:var(--bd);margin:10px 0}
.sw{margin-top:auto;padding-top:10px}
.stb{height:5px;background:var(--bd);border-radius:3px;overflow:hidden}.stf{height:100%;background:var(--ok);border-radius:3px;transition:width .3s}
.stx{font-size:.68rem;color:var(--mu);display:flex;justify-content:space-between;margin-top:3px}
.sn2{font-size:.62rem;color:var(--mu);margin-top:7px;line-height:1.3}
.sf{font-size:.58rem;color:var(--mu);margin-top:10px;text-align:center}
.si{width:100%;padding:7px 10px;border-radius:7px;border:1px solid var(--bd);background:var(--bg);color:var(--tx);font-size:.82rem}
.si:focus{outline:none;border-color:var(--ac)}
.bb{width:32px;height:32px;display:flex;align-items:center;justify-content:center;border-radius:7px;color:var(--mu);transition:background .15s,color .15s;flex-shrink:0}
.bb:hover{background:var(--bd);color:var(--tx)}.bb.on{color:var(--ac)}
.pab{padding:5px 12px;border-radius:7px;background:var(--ac);color:#fff;font-size:.78rem;font-weight:600;white-space:nowrap;display:flex;align-items:center;gap:3px}.pab:hover{background:var(--ac2)}
.sos{padding:5px 7px;border-radius:7px;border:1px solid var(--bd);background:var(--card);color:var(--tx);font-size:.78rem}
.gr{display:grid;grid-template-columns:repeat(auto-fill,minmax(148px,1fr));gap:10px}
.cd{background:var(--card);border-radius:10px;overflow:hidden;cursor:pointer;transition:transform .15s,box-shadow .15s;position:relative;border:2px solid transparent}
.cd:hover{transform:translateY(-2px);box-shadow:0 4px 16px var(--sh)}.cd.py{border-color:var(--ac)}
.ca{aspect-ratio:1;display:flex;align-items:center;justify-content:center;position:relative;overflow:hidden}
.ca .po{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;background:rgba(0,0,0,.4);opacity:0;transition:opacity .2s}.cd:hover .po{opacity:1}
.ci{padding:8px}.cn{font-size:.82rem;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.cs{font-size:.62rem;color:var(--mu);margin-top:1px}
.cf{position:absolute;top:5px;right:5px;z-index:2;font-size:1rem;opacity:.6;transition:opacity .15s}.cf:hover{opacity:1}.cf.fv{opacity:1}
.lt{width:100%;border-collapse:collapse}.lt th{text-align:left;font-size:.68rem;color:var(--mu);text-transform:uppercase;letter-spacing:.5px;padding:7px 5px;border-bottom:1px solid var(--bd)}
.lt td{padding:7px 5px;border-bottom:1px solid var(--bd);font-size:.82rem;vertical-align:middle}.lt tr{cursor:pointer;transition:background .1s}.lt tr:hover{background:var(--bd)}.lt tr.py td{color:var(--ac)}
.td{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;max-width:180px}
.ab{width:26px;height:26px;display:inline-flex;align-items:center;justify-content:center;border-radius:5px;color:var(--mu);transition:background .15s,color .15s;font-size:.82rem}
.ab:hover{background:var(--bd);color:var(--tx)}.ab.fv{color:var(--ac)}
.pl .pa{width:38px;height:38px;border-radius:5px;flex-shrink:0}
.pl .pi{min-width:0;flex-shrink:1}.pl .pt{font-size:.82rem;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.pl .pr{font-size:.68rem;color:var(--mu);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.pc{display:flex;align-items:center;gap:3px;flex-shrink:0}
.pb{width:34px;height:34px;display:flex;align-items:center;justify-content:center;border-radius:50%;color:var(--tx);transition:background .15s}.pb:hover{background:var(--bd)}
.pm{background:var(--ac);color:#fff;width:38px;height:38px;border-radius:50%;display:flex;align-items:center;justify-content:center;transition:background .15s,transform .1s}.pm:hover{background:var(--ac2);transform:scale(1.05)}
.pp{flex:1;display:flex;align-items:center;gap:6px;min-width:0}
.pbr{flex:1;height:4px;background:var(--bd);border-radius:2px;cursor:pointer;overflow:hidden}
.pbf{height:100%;background:var(--ac);border-radius:2px;pointer-events:none;transition:width .1s linear}
.ptm{font-size:.68rem;color:var(--mu);white-space:nowrap;min-width:32px}
.pv{display:flex;align-items:center;gap:3px;flex-shrink:0}
.pv input[type=range]{width:70px;height:3px;-webkit-appearance:none;appearance:none;background:var(--bd);border-radius:2px;outline:none}
.pv input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:10px;height:10px;border-radius:50%;background:var(--ac);cursor:pointer}
.pfv{font-size:1rem;flex-shrink:0;cursor:pointer}
.fb{position:fixed;bottom:76px;right:18px;width:48px;height:48px;border-radius:50%;background:var(--ac);color:#fff;font-size:1.4rem;display:flex;align-items:center;justify-content:center;box-shadow:0 4px 12px var(--sh);z-index:50;transition:transform .2s}
.fb:hover{transform:scale(1.1);background:var(--ac2)}.fb.ul{pointer-events:none;opacity:.5}
.bn{display:none;height:50px;background:var(--card);border-top:1px solid var(--bd);flex-shrink:0}
.bni{display:flex;height:100%}
.bt{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1px;font-size:.58rem;color:var(--mu);transition:color .15s}.bt.on{color:var(--ac)}
.tw{position:fixed;top:14px;right:14px;z-index:200;display:flex;flex-direction:column;gap:6px;pointer-events:none}
.to{padding:8px 14px;border-radius:7px;font-size:.82rem;pointer-events:auto;animation:ti .3s;box-shadow:0 4px 12px var(--sh)}
.to.ok{background:var(--ok);color:#fff}.to.er{background:var(--ac);color:#fff}.to.in{background:var(--card);color:var(--tx);border:1px solid var(--bd)}
@keyframes ti{from{opacity:0;transform:translateY(-8px)}to{opacity:1;transform:translateY(0)}}
.ut{position:fixed;bottom:76px;left:50%;transform:translateX(-50%);background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:10px 16px;z-index:100;display:none;min-width:240px;box-shadow:0 4px 16px var(--sh)}
.ut .ub{height:4px;background:var(--bd);border-radius:2px;overflow:hidden;margin-top:6px}.ut .uf{height:100%;background:var(--ac);transition:width .2s}
.ut .ux{font-size:.78rem;color:var(--tx)}
.mb{position:fixed;inset:0;background:rgba(0,0,0,.5);z-index:150;display:none;align-items:center;justify-content:center}.mb.sh{display:flex}
.md{background:var(--card);border-radius:10px;padding:16px;width:90%;max-width:340px;box-shadow:0 8px 24px var(--sh)}
.md h3{font-size:.95rem;margin-bottom:10px}.md input{width:100%;padding:7px 9px;border-radius:7px;border:1px solid var(--bd);background:var(--bg);color:var(--tx);font-size:.85rem}.md input:focus{outline:none;border-color:var(--ac)}
.mdb{display:flex;gap:6px;margin-top:10px;justify-content:flex-end}
.mdb button{padding:7px 14px;border-radius:7px;font-size:.82rem;font-weight:600}
.mdb .mc{background:var(--bd);color:var(--tx)}.mdb .mk{background:var(--ac);color:#fff}
.shg{position:fixed;inset:0;background:rgba(0,0,0,.4);z-index:80;display:none}.shg.sh{display:block}
.sht{position:fixed;left:0;top:0;bottom:0;width:250px;background:var(--card);z-index:81;transform:translateX(-100%);transition:transform .3s;overflow-y:auto;padding:14px 10px}.shg.sh .sht{transform:translateX(0)}
.g0{background:linear-gradient(135deg,#667eea,#764ba2)}.g1{background:linear-gradient(135deg,#f093fb,#f5576c)}.g2{background:linear-gradient(135deg,#4facfe,#00f2fe)}.g3{background:linear-gradient(135deg,#43e97b,#38f9d7)}.g4{background:linear-gradient(135deg,#fa709a,#fee140)}.g5{background:linear-gradient(135deg,#a18cd1,#fbc2eb)}.g6{background:linear-gradient(135deg,#ff9a9e,#fecfef)}.g7{background:linear-gradient(135deg,#ffecd2,#fcb69f)}
.emp{text-align:center;padding:36px 16px;color:var(--mu)}.emp p{font-size:.85rem}
.ti2{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:16px;text-align:center}.ti2 h2{font-size:1rem;margin-bottom:6px}.ti2 p{font-size:.82rem;color:var(--mu);line-height:1.4}
.plc{background:var(--card);border:1px solid var(--bd);border-radius:8px;padding:10px;margin-bottom:6px;cursor:pointer;transition:border-color .15s}.plc:hover{border-color:var(--ac)}
.pln{font-size:.85rem;font-weight:600}.plc2{font-size:.68rem;color:var(--mu)}
.pdl{float:right;color:var(--mu);font-size:.78rem;padding:2px 5px;border-radius:4px}.pdl:hover{background:var(--ac);color:#fff}
.npb{width:100%;padding:7px;border:1px dashed var(--bd);border-radius:7px;color:var(--mu);font-size:.78rem;margin-top:3px}.npb:hover{border-color:var(--ac);color:var(--ac)}
.fld{background:var(--card);border:1px solid var(--bd);border-radius:8px;padding:10px;margin-bottom:6px;cursor:pointer;transition:border-color .15s;display:flex;align-items:center;gap:8px}.fld:hover{border-color:var(--ac)}
.fld-ic{font-size:1.2rem;flex-shrink:0}.fld-nm{font-size:.85rem;font-weight:600;flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.fld-ct{font-size:.68rem;color:var(--mu)}
@media(max-width:767px){.sb{display:none}.dk{display:none}.bn{display:block}.pl{height:52px;padding:0 8px;gap:6px}.pv{display:none}.ptm{display:none}.fb{bottom:110px;right:14px;width:44px;height:44px;font-size:1.2rem}.ct{padding:10px;padding-bottom:110px;-webkit-overflow-scrolling:touch;overscroll-behavior-y:contain}.gr{grid-template-columns:repeat(auto-fill,minmax(128px,1fr));gap:8px}.ci{padding:6px}}
@media(min-width:768px){.mo{display:none}.shg,.sht{display:none!important}}
</style>
</head>
<body>
<div class="app">
<aside class="sb" id="SB">
<div class="sbl">&#9835; ShadowTune</div>
<div class="sbs">ShadowTune MP3 Player</div>
<nav class="sn" id="SBN"></nav>
<div class="sd"></div>
<div class="ss">Playlists</div>
<div id="SBP"></div>
<button class="npb" onclick="cpl()">+ New Playlist</button>
<div class="sw"><div class="stb"><div class="stf" id="SF" style="width:0%"></div></div><div class="stx"><span id="SU">-- MB</span><span id="SP">--%</span></div><div class="sn2">Delete moves files to .trash. Permanent delete only via USB.</div></div>
<div class="sf" id="FV"></div>
</aside>
<div class="mn">
<div class="tb">
<button class="bb mo" onclick="osh()" aria-label="Menu">&#9776;</button>
<div style="flex:1;position:relative"><input type="text" class="si" id="SQ" placeholder="Search tracks..." oninput="onS()" style="width:100%;padding-right:52px"><span style="position:absolute;right:28px;top:50%;transform:translateY(-50%);color:var(--mu);font-size:.82rem;pointer-events:none">&#128269;</span><button id="SC" onclick="clrS()" style="position:absolute;right:6px;top:50%;transform:translateY(-50%);color:var(--mu);font-size:.9rem;background:none;border:none;cursor:pointer;display:none;padding:2px 4px">&times;</button></div><span id="SRC" style="font-size:.68rem;color:var(--mu);white-space:nowrap;margin-left:4px"></span>
<button class="bb" id="BG" onclick="sV('grid')" title="Grid">&#9638;</button>
<button class="bb" id="BL" onclick="sV('list')" title="List">&#9776;</button>
<select class="sos dk" id="SS" onchange="onSo()"><option value="name">Name</option><option value="size">Size</option></select>
<button class="pab dk" onclick="pAll()">&#9654; Play All</button>
<button class="pab dk" onclick="sAll()">&#128256; Shuffle</button>
<button class="bb" onclick="tTh()" id="TB" title="Theme">&#9790;</button>
</div>
<div class="ct" id="CT"></div>
<div class="pl" id="PL">
<div class="pa" id="PA"></div>
<div class="pi"><div class="pt" id="PT">--</div><div class="pr" id="PR">--</div></div>
<div class="pc"><button class="pb" onclick="pT()">&#9198;</button><button class="pb pm" onclick="tP()" id="PBtn">&#9654;</button><button class="pb" onclick="nT()">&#9197;</button></div>
<div class="pp"><span class="ptm" id="PC">0:00</span><div class="pbr" id="PB" onclick="sk(event)"><div class="pbf" id="PF" style="width:0%"></div></div><span class="ptm" id="PD">0:00</span></div>
<div class="pv"><svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/></svg><input type="range" min="0" max="100" value="80" id="VS" oninput="sVo()"></div>
<span class="pfv" id="PFv" onclick="tCF()">&#9825;</span>
</div>
<nav class="bn"><div class="bni">
<button class="bt on" onclick="sT('all')" id="M0">&#127968; Library</button>
<button class="bt" onclick="fS()" id="M1">&#128269; Search</button>
<button class="bt" onclick="sT('playlists')" id="M2">&#127925; Playlists</button>
<button class="bt" onclick="sT('trash')" id="M3">&#128465; Trash</button>
</div></nav>
</div>
</div>
<button class="fb" id="FAB" onclick="document.getElementById('FI').click()">+</button>
<input type="file" id="FI" accept=".mp3,.wav,.aac,.ogg,.flac,.wma,.m4a,.opus,audio/*" multiple style="display:none" onchange="oFS(this.files)">
<div class="shg" id="SHG" onclick="cSh()"><aside class="sht" onclick="event.stopPropagation()" id="SHT"></aside></div>
<div class="mb" id="RM"><div class="md"><h3>Rename Track</h3><input type="text" id="RI" placeholder="New name"><div class="mdb"><button class="mc" onclick="cR()">Cancel</button><button class="mk" onclick="cRK()">Rename</button></div></div></div>
<div class="ut" id="UT"><div class="ux" id="UTX">Uploading...</div><div class="ub"><div class="uf" id="UTF" style="width:0%"></div></div></div>
<div class="tw" id="TW"></div>
<audio id="AU" preload="auto"></audio>
<script>
var aF=[],fv=[],pl={},cT=null,iP=false,vM='grid',sB='name',sQ='',aTab='all',aPl=null,rT=null,q=[],qI=-1,fld=[];
var au=document.getElementById('AU'),G=['g0','g1','g2','g3','g4','g5','g6','g7'];
function eH(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;')}
function eJ(s){return s.replace(/\\/g,'\\\\').replace(/'/g,"\\'")}
function fT(s){if(!s||!isFinite(s))return'0:00';var m=Math.floor(s/60),sc=Math.floor(s%60);return m+':'+(sc<10?'0':'')+sc}
function gC(i){return G[i%G.length]}
function gFld(n){var s=n.indexOf('/');return s>0?n.substring(0,s):''}
function stm(n){var d=n.lastIndexOf('/');var b=d>0?n.substring(d+1):n;var e=b.lastIndexOf('.');return e>0?b.substring(0,e):b}
function iF(n){return fv.indexOf(n)>=0}
function pSM(s){var m=s.match(/([\d.]+)\s*(MB|KB|GB|B)/i);if(!m)return 0;var v=parseFloat(m[1]),u=m[2].toUpperCase();if(u==='B')return v/1048576;if(u==='KB')return v/1024;if(u==='GB')return v*1024;return v}
function lP(){try{fv=JSON.parse(localStorage.getItem('sv_f')||'[]');pl=JSON.parse(localStorage.getItem('sv_p')||'{}');vM=localStorage.getItem('sv_v')||'grid';sB=localStorage.getItem('sv_s')||'name';if(localStorage.getItem('sv_t')==='L')document.body.classList.add('L')}catch(e){}}
function sFv(){try{localStorage.setItem('sv_f',JSON.stringify(fv))}catch(e){}}
function sPl(){try{localStorage.setItem('sv_p',JSON.stringify(pl))}catch(e){}}
function sVm(){try{localStorage.setItem('sv_v',vM)}catch(e){}}
function sSb(){try{localStorage.setItem('sv_s',sB)}catch(e){}}
function tTh(){document.body.classList.toggle('L');var l=document.body.classList.contains('L');try{localStorage.setItem('sv_t',l?'L':'D')}catch(e){}document.getElementById('TB').textContent=l?'\u2600':'\u263E'}
function sT(t){aTab=t;if(t!=='playlists')aPl=null;rSB();rMB();r()}
function rSB(){var t=aTab,n=document.getElementById('SBN');var items=[['all','\u266A All Tracks'],['favorites','\u2665 Favorites'],['playlists','\u2669 Playlists'],['trash','\u2670 Trash']];var h='';for(var i=0;i<items.length;i++){var on=items[i][0]===t;h+='<button class="'+(on?'on':'')+'" onclick="sT(\''+items[i][0]+'\');cSh()">'+items[i][1]+'</button>'}n.innerHTML=h;rSP()}
function rMB(){document.getElementById('M0').className='bt'+(aTab==='all'?' on':'');document.getElementById('M2').className='bt'+(aTab==='playlists'?' on':'');document.getElementById('M3').className='bt'+(aTab==='trash'?' on':'')}
function fS(){if(!(aTab==='playlists'&&aPl)){sT('all')}document.getElementById('SQ').focus();document.getElementById('M1').className='bt on'}
function osh(){document.getElementById('SHG').classList.add('sh');rSH()}
function cSh(){document.getElementById('SHG').classList.remove('sh')}
function rSH(){var d=document.getElementById('SHT');var items=[['all','\u266A All Tracks'],['favorites','\u2665 Favorites'],['playlists','\u2669 Playlists'],['trash','\u2670 Trash']];var h='<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:14px"><span class="sbl">\u266A ShadowTune</span><button onclick="cSh()" style="color:var(--mu);font-size:1.1rem">&times;</button></div><nav class="sn">';for(var i=0;i<items.length;i++){var on=items[i][0]===aTab;h+='<button class="'+(on?'on':'')+'" onclick="sT(\''+items[i][0]+'\');cSh()">'+items[i][1]+'</button>'}h+='</nav><div class="sd"></div><div class="ss">Playlists</div><div id="SHP"></div><button class="npb" onclick="cpl()">+ New Playlist</button><div class="sd"></div><div style="margin-top:0"><div class="stb"><div class="stf" id="SF2" style="width:0%"></div></div><div class="stx"><span id="SU2">-- MB</span><span id="SP2">--%</span></div></div>';d.innerHTML=h;rSP2();lSt()}
function sV(v){vM=v;sVm();uVB();r()}
function uVB(){document.getElementById('BG').className='bb'+(vM==='grid'?' on':'');document.getElementById('BL').className='bb'+(vM==='list'?' on':'')}
function onSo(){sB=document.getElementById('SS').value;sSb();r()}
function onS(){sQ=document.getElementById('SQ').value.trim().toLowerCase();document.getElementById('SC').style.display=sQ?'block':'none';r();uSRC()}
function clrS(){document.getElementById('SQ').value='';sQ='';document.getElementById('SC').style.display='none';document.getElementById('SRC').textContent='';r()}
function uSRC(){var e=document.getElementById('SRC');if(!sQ){e.textContent='';return}var f=gFl();var scope=aPl?(aPl.indexOf('__fld__')===0?'in '+aPl.substring(7):'in '+aPl):'all tracks';e.textContent=f.length+' result'+(f.length!==1?'s':'')+' \u2022 '+scope}
function lFl(){fetch('/list').then(function(r){return r.json()}).then(function(d){aF=d;bFld();r()}).catch(function(){aF=[];fld=[];r()})}
function bFld(){var m={};for(var i=0;i<aF.length;i++){var f=gFld(aF[i].name);if(f&&!m[f])m[f]=0;if(f)m[f]++}fld=Object.keys(m).sort();fld.forEach(function(k){if(!pl['__fld__'+k])pl['__fld__'+k]=true})}
function lSt(){fetch('/storage').then(function(r){return r.json()}).then(function(d){if(!d.total)return;var p=Math.min(100,Math.round(d.used/d.total*100)),f=p>90?'var(--ac)':'var(--ok)';var e1=[document.getElementById('SF'),document.getElementById('SU'),document.getElementById('SP')];var e2=[document.getElementById('SF2'),document.getElementById('SU2'),document.getElementById('SP2')];var es=[e1,e2];for(var i=0;i<es.length;i++){if(es[i][0]){es[i][0].style.width=p+'%';es[i][0].style.background=f}if(es[i][1])es[i][1].textContent=d.used+'/'+d.total+' MB';if(es[i][2])es[i][2].textContent=p+'%'}}).catch(function(){})}
function lVr(){fetch('/version').then(function(r){return r.text()}).then(function(t){document.getElementById('FV').textContent=t}).catch(function(){})}
function gFl(){var f=aF;if(aTab==='favorites')f=f.filter(function(x){return iF(x.name)});if(aPl){if(aPl.indexOf('__fld__')===0){var fn=aPl.substring(7);f=f.filter(function(x){return gFld(x.name)===fn})}else{var p=pl[aPl];if(p)f=f.filter(function(x){return p.indexOf(x.name)>=0})}}if(sQ)f=f.filter(function(x){var nm=x.name.toLowerCase();var tl=stm(x.name).toLowerCase();return nm.indexOf(sQ)>=0||tl.indexOf(sQ)>=0});return f.slice().sort(function(a,b){if(sB==='size')return pSM(b.size)-pSM(a.size);return a.name.toLowerCase().localeCompare(b.name.toLowerCase())})}
function r(){var c=document.getElementById('CT');if(aTab==='trash'){rTr(c);return}if(aTab==='playlists'&&!aPl){rPL(c);return}var f=gFl();if(f.length===0){var msg=aF.length===0?'No MP3 files yet. Tap + to upload!':aTab==='favorites'?'No favorites yet. Tap the heart!':'No tracks match your search.';c.innerHTML='<div class="emp"><p>'+msg+'</p></div>';return}if(vM==='grid')rG(c,f);else rL(c,f);rSP()}
function rG(c,f){var h='<div class="gr">';for(var i=0;i<f.length;i++){var t=f[i],g=gC(i),py=cT&&cT.name===t.name,fav=iF(t.name);h+='<div class="cd'+(py?' py':'')+'" onclick="pA('+i+')"><div class="ca '+g+'"><button class="cf'+(fav?' fv':'')+'" onclick="event.stopPropagation();tFv(\''+eJ(t.name)+'\')">'+(fav?'\u2764':'\u2661')+'</button><div class="po"><span style="font-size:1.8rem;color:#fff">\u25B6</span></div></div><div class="ci"><div class="cn">'+eH(stm(t.name))+'</div><div class="cs">'+eH(t.size)+'</div></div></div>'}h+='</div>';c.innerHTML=h}
function rL(c,f){var h='<table class="lt"><thead><tr><th>#</th><th>Title</th><th>Size</th><th></th></tr></thead><tbody>';for(var i=0;i<f.length;i++){var t=f[i],py=cT&&cT.name===t.name,fav=iF(t.name),inPl=aPl&&aPl.indexOf('__fld__')!==0&&pl[aPl]&&pl[aPl].indexOf(t.name)>=0;h+='<tr class="'+(py?'py':'')+'" onclick="pA('+i+')"><td style="color:var(--mu);width:24px">'+(i+1)+'</td><td class="td">'+eH(stm(t.name))+'</td><td style="color:var(--mu);white-space:nowrap">'+eH(t.size)+'</td><td style="white-space:nowrap"><button class="ab'+(fav?' fv':'')+'" onclick="event.stopPropagation();tFv(\''+eJ(t.name)+'\')">'+(fav?'\u2764':'\u2661')+'</button>'+(inPl?'<button class="ab" onclick="event.stopPropagation();rPl2(\''+eJ(t.name)+'\')" title="Remove from playlist">\u2212</button>':'<button class="ab" onclick="event.stopPropagation();aPl2(\''+eJ(t.name)+'\')" title="Add to playlist">+</button>')+'<button class="ab" onclick="event.stopPropagation();dF(\''+eJ(t.name)+'\')" title="Download">\u21E9</button><button class="ab" onclick="event.stopPropagation();oR(\''+eJ(t.name)+'\')" title="Rename">\u270E</button><button class="ab" onclick="event.stopPropagation();delF(\''+eJ(t.name)+'\')" title="Delete">\u2670</button></td></tr>'}h+='</tbody></table>';c.innerHTML=h}
function rTr(c){c.innerHTML='<div class="ti2"><h2>\u2670 Trash Folder</h2><p>Deleting a track moves it to the <strong>.trash</strong> folder on the SD card. This is a soft delete &mdash; the file still uses storage.</p><p style="margin-top:10px">To permanently delete, connect via <strong>USB mass storage</strong> and empty the <strong>.trash</strong> folder.</p></div>'}
function rPL(c){var h='';if(fld.length>0){h+='<div class="ss" style="margin-top:0">Folders</div>';for(var i=0;i<fld.length;i++){var fn=fld[i],fk='__fld__'+fn,cnt=0;for(var j=0;j<aF.length;j++){if(gFld(aF[j].name)===fn)cnt++}h+='<div class="fld" onclick="oPV(\''+eJ(fk)+'\')"><span class="fld-ic">\uD83D\uDCC1</span><span class="fld-nm">'+eH(fn)+'</span><span class="fld-ct">'+cnt+' tracks</span></div>'}}var k=Object.keys(pl).filter(function(x){return x.indexOf('__fld__')!==0});if(k.length>0){h+='<div class="ss">Custom</div>';for(var i=0;i<k.length;i++){h+='<div class="plc" onclick="oPV(\''+eJ(k[i])+'\')"><button class="pdl" onclick="event.stopPropagation();dPl(\''+eJ(k[i])+'\')">&times;</button><div class="pln">'+eH(k[i])+'</div><div class="plc2">'+pl[k[i]].length+' tracks</div></div>'}}if(fld.length===0&&k.length===0)h='<div class="emp"><p>No playlists yet. Create one or add folders to your SD card!</p></div>';h+='<button class="npb" onclick="cpl()">+ New Playlist</button>';c.innerHTML=h}
function rSP(){var h='';for(var i=0;i<fld.length;i++){var fn=fld[i],fk='__fld__'+fn,on=aPl===fk;h+='<button class="'+(on?'on':'')+'" onclick="oPV(\''+eJ(fk)+'\');cSh()" style="font-size:.78rem;padding:5px 8px">\uD83D\uDCC1 '+eH(fn)+'</button>'}var k=Object.keys(pl).filter(function(x){return x.indexOf('__fld__')!==0});for(var i=0;i<k.length;i++){var on=aPl===k[i];h+='<button class="'+(on?'on':'')+'" onclick="oPV(\''+eJ(k[i])+'\');cSh()" style="font-size:.78rem;padding:5px 8px">\u2669 '+eH(k[i])+'</button>'}var sb=document.getElementById('SBP');if(sb)sb.innerHTML=h}
function rSP2(){var h='';for(var i=0;i<fld.length;i++){var fn=fld[i],fk='__fld__'+fn;h+='<button onclick="oPV(\''+eJ(fk)+'\');cSh()" style="display:flex;align-items:center;gap:6px;padding:5px 8px;border-radius:6px;font-size:.78rem;width:100%;text-align:left">\uD83D\uDCC1 '+eH(fn)+'</button>'}var k=Object.keys(pl).filter(function(x){return x.indexOf('__fld__')!==0});for(var i=0;i<k.length;i++){h+='<button onclick="oPV(\''+eJ(k[i])+'\');cSh()" style="display:flex;align-items:center;gap:6px;padding:5px 8px;border-radius:6px;font-size:.78rem;width:100%;text-align:left">\u2669 '+eH(k[i])+'</button>'}var sh=document.getElementById('SHP');if(sh)sh.innerHTML=h}
function cpl(){var n=prompt('Playlist name:');if(!n||!n.trim())return;n=n.trim();if(pl[n]){tk('Already exists','er');return}pl[n]=[];sPl();r();rSP();tk('Playlist "'+n+'" created','ok')}
function dPl(n){if(!confirm('Delete playlist "'+n+'"?'))return;delete pl[n];if(aPl===n){aPl=null;aTab='playlists'}sPl();r();rSP();tk('Playlist deleted','in')}
function oPV(n){aPl=n;aTab='playlists';r()}
function aPl2(n){var k=Object.keys(pl).filter(function(x){return x.indexOf('__fld__')!==0});if(!k.length){tk('Create a playlist first','in');return}var s='';for(var i=0;i<k.length;i++)s+=(i+1)+'. '+k[i]+'\n';var c=prompt('Add "'+n+'" to playlist. Enter number:\n'+s);if(!c)return;var idx=parseInt(c)-1;if(idx<0||idx>=k.length){tk('Invalid number','er');return}var pn=k[idx];if(!pl[pn])pl[pn]=[];if(pl[pn].indexOf(n)>=0){tk('Already in playlist','in');return}pl[pn].push(n);sPl();rSP();tk('Added to "'+pn+'"','ok')}
function rPl2(n){if(!aPl||!pl[aPl])return;var i=pl[aPl].indexOf(n);if(i>=0){pl[aPl].splice(i,1);sPl();r();rSP();tk('Removed from playlist','ok')}}
function tFv(n){var i=fv.indexOf(n);if(i>=0)fv.splice(i,1);else fv.push(n);sFv();r();uPF()}
function tCF(){if(!cT)return;tFv(cT.name)}
function pA(i){var f=gFl();if(i<0||i>=f.length)return;q=f.slice();qI=i;pQ()}
function pAll(){var f=gFl();if(!f.length)return;q=f.slice();qI=0;pQ()}
function sAll(){var f=gFl();if(!f.length)return;for(var i=f.length-1;i>0;i--){var j=Math.floor(Math.random()*(i+1)),t=f[i];f[i]=f[j];f[j]=t}q=f;qI=0;pQ()}
function pQ(){if(qI<0||qI>=q.length)return;cT=q[qI];au.src='/stream?file='+encodeURIComponent(cT.name);au.play().catch(function(){});iP=true;document.getElementById('PL').style.display='flex';uPU();r()}
function tP(){if(!cT)return;if(iP){au.pause();iP=false}else{au.play().catch(function(){});iP=true}uPB()}
function pT(){if(!q.length)return;qI=(qI-1+q.length)%q.length;pQ()}
function nT(){if(!q.length)return;qI=(qI+1)%q.length;pQ()}
function uPB(){var b=document.getElementById('PBtn');if(iP){b.innerHTML='\u275A\u275A';b.style.background='var(--ac)';b.style.color='#fff'}else{b.innerHTML='\u25B6';b.style.background='var(--ac)';b.style.color='#fff'}}
function uPU(){if(!cT)return;document.getElementById('PT').textContent=stm(cT.name);document.getElementById('PR').textContent=cT.size;var g=gC(aF.indexOf(cT)>=0?aF.indexOf(cT):0);document.getElementById('PA').className='pa '+g;uPB();uPF()}
function uPF(){if(!cT)return;var e=document.getElementById('PFv');e.innerHTML=iF(cT.name)?'\u2764':'\u2661';e.style.color=iF(cT.name)?'var(--ac)':'var(--mu)'}
function sk(e){if(!au.duration)return;var b=document.getElementById('PB'),r=b.getBoundingClientRect();au.currentTime=Math.max(0,Math.min(1,(e.clientX-r.left)/r.width))*au.duration}
function sVo(){au.volume=document.getElementById('VS').value/100}
au.addEventListener('timeupdate',function(){if(!au.duration)return;document.getElementById('PF').style.width=(au.currentTime/au.duration*100)+'%';document.getElementById('PC').textContent=fT(au.currentTime);document.getElementById('PD').textContent=fT(au.duration)});
au.addEventListener('ended',function(){nT()});
au.addEventListener('play',function(){iP=true;uPB()});
au.addEventListener('pause',function(){iP=false;uPB()});
function dF(n){window.location.href='/download?file='+encodeURIComponent(n)}
function oR(n){rT=n;document.getElementById('RI').value=stm(n);document.getElementById('RM').classList.add('sh');document.getElementById('RI').focus()}
function cR(){document.getElementById('RM').classList.remove('sh');rT=null}
function cRK(){if(!rT)return;var ns=document.getElementById('RI').value.trim();if(!ns){tk('Name cannot be empty','er');return}if(ns===stm(rT)){cR();return}fetch('/rename',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'from='+encodeURIComponent(rT)+'&to='+encodeURIComponent(ns)}).then(function(r){if(r.ok){tk('Renamed to '+ns+'.mp3','ok');lFl()}else return r.text().then(function(t){tk('Error: '+t,'er')})}).catch(function(){tk('Network error','er')});cR()}
function delF(n){if(!confirm('Move "'+n+'" to trash?'))return;fetch('/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'file='+encodeURIComponent(n)}).then(function(r){if(r.ok){tk('Moved to trash','ok');var i=fv.indexOf(n);if(i>=0){fv.splice(i,1);sFv()}var pk=Object.keys(pl);for(var j=0;j<pk.length;j++){var pi=pl[pk[j]].indexOf(n);if(pi>=0)pl[pk[j]].splice(pi,1)}sPl();if(cT&&cT.name===n){au.pause();au.src='';cT=null;iP=false;document.getElementById('PL').style.display='none'}lFl();lSt()}else return r.text().then(function(t){tk('Error: '+t,'er')})}).catch(function(){tk('Network error','er')})}
function isMus(n){var e=n.toLowerCase(),x=e.lastIndexOf('.');if(x<0)return false;var s=e.substring(x);return s==='.mp3'||s==='.wav'||s==='.aac'||s==='.ogg'||s==='.flac'||s==='.wma'||s==='.m4a'||s==='.opus'||s==='.mid'||s==='.midi'}
function oFS(files){var m=[];for(var i=0;i<files.length;i++){if(isMus(files[i].name)||files[i].type.startsWith('audio/'))m.push(files[i])}if(!m.length){tk('Please select music files','er');return}uFl(m);document.getElementById('FI').value=''}
function uFl(files){var ut=document.getElementById('UT'),tx=document.getElementById('UTX'),fl=document.getElementById('UTF');ut.style.display='block';document.getElementById('FAB').classList.add('ul');var idx=0;function nx(){if(idx>=files.length){ut.style.display='none';document.getElementById('FAB').classList.remove('ul');tk('Uploaded '+files.length+' file(s)','ok');lFl();lSt();return}uOne(files[idx],idx,files.length,tx,fl,function(){idx++;lFl();nx()},function(e){tk('Failed: '+e,'er');idx++;nx()})}nx()}
function uOne(file,i,tot,txE,flE,ok,fail){var x=new XMLHttpRequest();x.open('POST','/upload');x.timeout=300000;x.upload.addEventListener('progress',function(e){if(!e.lengthComputable)return;var p=Math.round(e.loaded/e.total*100);flE.style.width=p+'%';txE.textContent='Uploading '+(i+1)+'/'+tot+': '+file.name+' ('+p+'%)'});x.onload=function(){if(x.status>=200&&x.status<300)ok();else fail(x.responseText||'HTTP '+x.status)};x.onerror=function(){fail('Network error')};x.ontimeout=function(){fail('Timeout')};var fd=new FormData();fd.append('file',file);x.send(fd)}
document.addEventListener('dragover',function(e){e.preventDefault()});
document.addEventListener('drop',function(e){e.preventDefault();var f=[];for(var i=0;i<e.dataTransfer.files.length;i++){if(isMus(e.dataTransfer.files[i].name))f.push(e.dataTransfer.files[i])}if(f.length)uFl(f)});
document.body.addEventListener('touchmove',function(e){if(e.touches.length>1)e.preventDefault()},{passive:false});
function tk(m,t){var w=document.getElementById('TW'),e=document.createElement('div');e.className='to '+(t==='ok'?'ok':t==='er'?'er':'in');e.textContent=m;w.appendChild(e);setTimeout(function(){if(e.parentNode)e.parentNode.removeChild(e)},3000)}
lP();uVB();document.getElementById('SS').value=sB;
if(document.body.classList.contains('L'))document.getElementById('TB').textContent='\u2600';
au.volume=0.8;rSB();lFl();lSt();lVr();
</script>
</body>
</html>

)rawliteral";

// ═══════════════════════════════════════════════════════════════════════════════
//  Singleton
// ═══════════════════════════════════════════════════════════════════════════════
HotspotManager& HotspotManager::getInstance() {
  static HotspotManager instance;
  return instance;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  begin() — one-time init (called during setup)
// ═══════════════════════════════════════════════════════════════════════════════
bool HotspotManager::begin() {
  if (_initialized) return true;
  // Pre-load credentials from NVS so ssid()/password() work even before
  // start() is called (e.g. for the settings screen preview).
  mp3Mgr.getHotspotSSID(_ssid, sizeof(_ssid));
  mp3Mgr.getHotspotPassword(_password, sizeof(_password));
  Serial.printf("[Hotspot] begin() — SSID: %s, password loaded (%zu chars)\n",
                _ssid, strlen(_password));
  _initialized = true;
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  start() — bring up SoftAP + DNS hijack + web server
// ═══════════════════════════════════════════════════════════════════════════════
bool HotspotManager::start() {
  if (_active) return true;

  // Reload credentials from NVS at every start() — the user may have
  // changed them in Settings since the last activation.
  mp3Mgr.getHotspotSSID(_ssid, sizeof(_ssid));
  mp3Mgr.getHotspotPassword(_password, sizeof(_password));

  // Sanity: WPA2 requires password length 8..63.
  if (strlen(_password) < 8) {
    Serial.println("[Hotspot] ERROR: WPA2 password too short — refusing to start open AP");
    return false;
  }

  Serial.printf("[Hotspot] Starting AP — SSID: %s (WPA2, %zu-char password)\n",
                _ssid, strlen(_password));

  // 1. Bring up the SoftAP with WPA2.
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(_ssid, _password, 1, 0, AP_MAX_CLIENTS)) {
    Serial.println("[Hotspot] ERROR: softAP() failed");
    WiFi.mode(WIFI_OFF);
    return false;
  }

  // Cap TX power to 8.5 dBm — phone is inches away, default 19.5 dBm
  // is unnecessary and causes current-spike resets on battery.
  WiFi.setTxPower(AP_TX_POWER_DBM);

  Serial.printf("[Hotspot] AP started — IP: %s\n", WiFi.softAPIP().toString().c_str());

  // 2. Ensure the /MP3/ folder exists on the SD card.
  if (!ensureMP3Folder()) {
    Serial.println("[Hotspot] ERROR: could not create /MP3/ folder on SD");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  // 3. DNS hijack — every hostname resolves to the AP IP (192.168.4.1).
  //    This makes the phone's captive-portal sheet pop up automatically.
  _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  _dnsServer.start(53, "*", WiFi.softAPIP());
  _dnsRunning = true;

  // 4. Web server on port 80.
  _webServer = new WebServer(80);
  if (!_webServer) {
    Serial.println("[Hotspot] ERROR: failed to allocate WebServer");
    _dnsServer.stop();
    _dnsRunning = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  // ── Captive portal detection routes ──────────────────────────────────
  // Android / Chrome OS
  _webServer->on("/generate_204",       HTTP_GET, std::bind(&HotspotManager::handleCaptivePortal, this));
  // Apple iOS / macOS
  _webServer->on("/hotspot-detect.html", HTTP_GET, std::bind(&HotspotManager::handleCaptivePortal, this));
  // Windows
  _webServer->on("/ncsi.txt",           HTTP_GET, std::bind(&HotspotManager::handleCaptivePortal, this));
  // Windows (alternative)
  _webServer->on("/fwlink",             HTTP_GET, std::bind(&HotspotManager::handleCaptivePortal, this));

  // ── Application routes ───────────────────────────────────────────────
  _webServer->on("/",        HTTP_GET,  std::bind(&HotspotManager::handleRoot,  this));
  _webServer->on("/version", HTTP_GET,  std::bind(&HotspotManager::handleVersion, this));
  _webServer->on("/upload",  HTTP_POST,
    std::bind(&HotspotManager::handleUploadPost, this),  // upload handler
    std::bind(&HotspotManager::handleUpload, this)       // upload callback
  );
  _webServer->on("/list",    HTTP_GET,  std::bind(&HotspotManager::handleList,   this));
  _webServer->on("/delete",  HTTP_POST, std::bind(&HotspotManager::handleDelete, this));
  _webServer->on("/rename",  HTTP_POST, std::bind(&HotspotManager::handleRename, this));
  _webServer->on("/download", HTTP_GET, std::bind(&HotspotManager::handleDownload, this));
  _webServer->on("/stream",  HTTP_GET,  std::bind(&HotspotManager::handleStream, this));
  _webServer->on("/storage", HTTP_GET,  std::bind(&HotspotManager::handleStorageInfo, this));

  // ── 404 → 302 redirect to captive portal ─────────────────────────────
  _webServer->onNotFound(std::bind(&HotspotManager::handleNotFound, this));

  _webServer->begin();

  _lastActivity = millis();
  _uploadedCount = 0;
  _active = true;

  Serial.println("[Hotspot] Ready — captive portal active on port 80");
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  stop() — tear everything down in reverse order
// ═══════════════════════════════════════════════════════════════════════════════
void HotspotManager::stop() {
  if (!_active) return;

  Serial.println("[Hotspot] Stopping AP...");

  // 1. Tear down the web server.
  if (_webServer) {
    _webServer->stop();
    delete _webServer;
    _webServer = nullptr;
  }

  // 2. Tear down the DNS hijack.
  if (_dnsRunning) {
    _dnsServer.stop();
    _dnsRunning = false;
  }

  // 3. Disconnect the SoftAP.
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  _active = false;
  Serial.println("[Hotspot] Stopped");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  tick() — called from main loop
// ═══════════════════════════════════════════════════════════════════════════════
void HotspotManager::tick() {
  if (!_active) return;

  // 1. Process DNS queries (captive portal pop-up).
  if (_dnsRunning) {
    _dnsServer.processNextRequest();
  }

  // 2. Process web server requests.
  if (_webServer) {
    _webServer->handleClient();
  }

  // 3. Idle auto-off — if no activity for AP_IDLE_TIMEOUT_MS, stop.
  if (millis() - _lastActivity >= AP_IDLE_TIMEOUT_MS) {
    Serial.println("[Hotspot] Idle timeout — auto-stopping");
    stop();
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  noteActivity() — reset the idle timer
// ═══════════════════════════════════════════════════════════════════════════════
void HotspotManager::noteActivity() {
  _lastActivity = millis();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  connectedClients()
// ═══════════════════════════════════════════════════════════════════════════════
int HotspotManager::connectedClients() const {
  if (!_active) return 0;
  return WiFi.softAPgetStationNum();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Web server route handlers
// ═══════════════════════════════════════════════════════════════════════════════

// ── GET / — the file upload page (captive portal) ───────────────────────────
void HotspotManager::handleRoot() {
  noteActivity();
  // Captive-portal browsers (phones especially) are notorious for caching
  // this page hard across reconnects to the same SSID, which makes a
  // freshly-flashed UI look like nothing changed. Force no caching.
  _webServer->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  _webServer->sendHeader("Pragma", "no-cache");
  _webServer->send(200, "text/html", HTML_PORTAL);
}

// ── GET /version — plain-text build stamp, shown in the page footer ─────────
//    Lets you confirm at a glance (no guessing, no cache ambiguity) which
//    firmware build is actually running: __DATE__/__TIME__ are baked in by
//    the compiler for whatever build produced the .bin currently on the
//    device, and change every time this file is recompiled.
void HotspotManager::handleVersion() {
  noteActivity();
  _webServer->sendHeader("Cache-Control", "no-store");
  _webServer->send(200, "text/plain", "Build " __DATE__ " " __TIME__);
}

// ── Captive portal detection routes → 302 redirect to / ─────────────────────
void HotspotManager::handleCaptivePortal() {
  noteActivity();
  Serial.println("[Hotspot] Captive portal detection — redirecting to /");
  _webServer->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
  _webServer->send(302, "text/plain", "");
}

// ── 404 handler → 302 redirect to captive portal ────────────────────────────
void HotspotManager::handleNotFound() {
  noteActivity();
  _webServer->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
  _webServer->send(302, "text/plain", "");
}

// ── POST /upload — multipart file upload handler (callback) ─────────────────
//    This is the "upload" callback that fires for each chunk of the
//    multipart body. The SD file is opened ONCE at UPLOAD_FILE_START and
//    kept open (via Mp3Manager's upload-file API) across every chunk,
//    closing only at UPLOAD_FILE_END/ABORTED.
//
//    Previously this reopened the file with SD.open(path, FILE_APPEND) on
//    EVERY chunk. FILE_APPEND has to walk/seek to the current end of the
//    file on each open, which is O(file size so far) — so total cost over
//    a whole upload was O(n^2) in the file size. For a several-MB MP3
//    (hundreds of ~1.4KB TCP chunks) that blocked the main loop task for
//    long enough to trip the task watchdog and reset the board — usually
//    right at the end, i.e. *after* all the data had already been
//    correctly appended, which is why the file looked "properly
//    uploaded" despite the crash.
//
//    Power-loss / corruption safety: we write to a hidden staging name
//    (UPLOAD_STAGING_PREFIX + final name) and only rename it to the real
//    filename after the upload finishes AND the byte count checks out.
//    If the board loses power, crashes, or the connection drops mid-
//    upload, the real filename is never touched — the library never shows
//    a half-written, corrupt-sounding track. Mp3Manager::cleanupStaleUploads()
//    sweeps up the orphaned staging file on next boot. Mp3Manager also
//    flushes the file to the SD card every few chunks (not every chunk —
//    that would reintroduce per-chunk overhead — and not only at the end,
//    which would leave the whole file unflushed if power drops early).
// ═══════════════════════════════════════════════════════════════════════════════
void HotspotManager::handleUpload() {
  noteActivity();

  HTTPUpload& upload = _webServer->upload();

  if (upload.status == UPLOAD_FILE_START) {
    // ── Validate the filename ───────────────────────────────────────────
    String filename = upload.filename;
    if (filename.length() == 0) {
      Serial.println("[Hotspot] Upload rejected — empty filename");
      _uploadRelName = "";
      return;
    }

    // Only accept music files (mp3, wav, aac, ogg, flac, wma, m4a, opus, mid, midi)
    String lowerName = filename;
    lowerName.toLowerCase();
    bool isMusic = lowerName.endsWith(".mp3")  || lowerName.endsWith(".wav")  ||
                   lowerName.endsWith(".aac")  || lowerName.endsWith(".ogg")  ||
                   lowerName.endsWith(".flac") || lowerName.endsWith(".wma")  ||
                   lowerName.endsWith(".m4a")  || lowerName.endsWith(".opus") ||
                   lowerName.endsWith(".mid")  || lowerName.endsWith(".midi");
    if (!isMusic) {
      Serial.printf("[Hotspot] Upload rejected — not a music file: %s\n", filename.c_str());
      _uploadRelName = "";
      return;
    }

    // Sanitize: strip any path components, keep only the base name.
    // Some clients send the full path (e.g. "C:\Users\...\song.mp3").
    int lastSlash = filename.lastIndexOf('/');
    int lastBackslash = filename.lastIndexOf('\\');
    int lastSep = (lastSlash > lastBackslash) ? lastSlash : lastBackslash;
    if (lastSep >= 0) {
      filename = filename.substring(lastSep + 1);
    }

    // Truncate to MAX_FILENAME_LEN if needed, leaving room for the
    // staging prefix too. Preserve the original extension.
    size_t maxBase = MAX_FILENAME_LEN - strlen(UPLOAD_STAGING_PREFIX) - 5;
    if (filename.length() > maxBase) {
      // Extract extension, truncate base, re-append extension
      int dotPos = filename.lastIndexOf('.');
      String ext = (dotPos >= 0) ? filename.substring(dotPos) : ".mp3";
      filename = filename.substring(0, maxBase - ext.length()) + ext;
    }

    lowerName = filename;
    lowerName.toLowerCase();
    if (isReservedName(lowerName)) {
      Serial.printf("[Hotspot] Upload rejected — reserved name: %s\n", filename.c_str());
      _uploadRelName = "";
      return;
    }

    String stagingName = String(UPLOAD_STAGING_PREFIX) + filename;
    Serial.printf("[Hotspot] Upload start: %s (staging as %s)\n",
                  filename.c_str(), stagingName.c_str());

    if (!mp3Mgr.openUploadFile(stagingName.c_str())) {
      Serial.printf("[Hotspot] ERROR: could not open %s for writing\n", stagingName.c_str());
      _uploadRelName = "";
      return;
    }
    _uploadRelName = filename;       // final name, used for rename target
    _uploadStagingName = stagingName;

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // ── Append a chunk to the already-open staging file ──────────────────
    if (_uploadRelName.length() == 0) return;  // START was rejected — drop the rest

    size_t written = mp3Mgr.writeUploadChunk(upload.buf, upload.currentSize);
    if (written != upload.currentSize) {
      Serial.printf("[Hotspot] WARNING: partial write — %u of %u bytes, aborting\n",
                    (unsigned)written, (unsigned)upload.currentSize);
      mp3Mgr.abortUploadFile(_uploadStagingName.c_str());
      _uploadRelName = "";
      _uploadStagingName = "";
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    // ── Upload complete — verify size, then atomically publish ───────────
    if (_uploadRelName.length() == 0) return;

    mp3Mgr.closeUploadFile();

    if (upload.totalSize == 0) {
      Serial.println("[Hotspot] Upload rejected — empty file");
      mp3Mgr.abortUploadFile(_uploadStagingName.c_str());
      _uploadRelName = "";
      _uploadStagingName = "";
      return;
    }

    // Publish: rename the staging file to its real name. Only now does
    // the track become visible in the library.
    if (mp3Mgr.renameEntry(_uploadStagingName.c_str(), _uploadRelName.c_str())) {
      Serial.printf("[Hotspot] Upload complete: %s (%u bytes)\n",
                    _uploadRelName.c_str(), (unsigned)upload.totalSize);
      _uploadedCount++;
    } else {
      Serial.printf("[Hotspot] ERROR: could not publish %s -> %s\n",
                    _uploadStagingName.c_str(), _uploadRelName.c_str());
      mp3Mgr.abortUploadFile(_uploadStagingName.c_str());
    }
    _uploadRelName = "";
    _uploadStagingName = "";

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Serial.println("[Hotspot] Upload aborted by client");
    if (_uploadStagingName.length() > 0) {
      mp3Mgr.abortUploadFile(_uploadStagingName.c_str());
      _uploadRelName = "";
      _uploadStagingName = "";
    }
  }
}

// ── POST /upload — final response after upload completes ─────────────────────
void HotspotManager::handleUploadPost() {
  noteActivity();

  // Check if the upload was rejected (non-MP3).
  // The WebServer doesn't give us a clean way to signal rejection from the
  // upload callback, so we check the Content-Type of the last uploaded file.
  // Instead, we rely on the fact that the upload callback silently drops
  // non-MP3 files. We always return 200 here — the client-side JS will
  // refresh the file list to show the result.

  // If there were no uploaded files at all (e.g. the upload callback rejected
  // every file), we still return 200 — the client will see no new files.
  _webServer->send(200, "text/plain", "OK");
}

// ── GET /list — returns JSON list of MP3 files with sizes ────────────────────
void HotspotManager::handleList() {
  sd_switch_to_mp3();
  noteActivity();

  // SECURITY: Only list files in /MP3/ — never access vault.db or anything else.
  File dir = SD.open(MP3_FOLDER);
  if (!dir || !dir.isDirectory()) {
    _webServer->send(500, "application/json", "[]");
    return;
  }

  String json = "[";
  int count = 0;

  File entry = dir.openNextFile();
  while (entry && count < MAX_MP3_FILES) {
    if (entry.isDirectory()) {
      // Skip the trash folder (and any other folder) — the captive
      // portal only manages flat top-level MP3 files.
      entry.close();
      entry = dir.openNextFile();
      continue;
    }

    String name = String(entry.name());
    // Strip path prefix — the entry.name() may include "/MP3/"
    int slash = name.lastIndexOf('/');
    if (slash >= 0) {
      name = name.substring(slash + 1);
    }

    // Only list music files (mp3, wav, aac, ogg, flac, wma, m4a, opus, mid, midi)
    String lowerName = name;
    lowerName.toLowerCase();
    bool isMusic = lowerName.endsWith(".mp3")  || lowerName.endsWith(".wav")  ||
                   lowerName.endsWith(".aac")  || lowerName.endsWith(".ogg")  ||
                   lowerName.endsWith(".flac") || lowerName.endsWith(".wma")  ||
                   lowerName.endsWith(".m4a")  || lowerName.endsWith(".opus") ||
                   lowerName.endsWith(".mid")  || lowerName.endsWith(".midi");
    if (!isMusic || isReservedName(lowerName)) {
      entry.close();
      entry = dir.openNextFile();
      continue;
    }

    if (count > 0) json += ",";
    json += "{\"name\":\"";
    // Escape JSON string
    for (unsigned int i = 0; i < name.length(); i++) {
      char c = name.charAt(i);
      if (c == '"') json += "\\\"";
      else if (c == '\\') json += "\\\\";
      else json += c;
    }
    json += "\",\"size\":\"";
    json += formatFileSize(entry.size());
    json += "\"}";

    count++;
    entry.close();
    entry = dir.openNextFile();
  }

  if (entry) entry.close();
  dir.close();

  json += "]";
  _webServer->send(200, "application/json", json);
}

// ── Shared filename validation ────────────────────────────────────────────
//    lowerName must already be lowercased by the caller.
bool HotspotManager::isReservedName(const String& lowerName) {
  return lowerName == "vault.db" ||
         lowerName == TRASH_FOLDER_NAME ||
         lowerName.startsWith(".");
}

// ── Shared filename sanitizer/validator for /delete, /rename, /download ──
//    Rejects path traversal, separators, empty names, and anything not a
//    music filename. Returns "" (invalid) or the clean name.
static String sanitizeMusicFilename(const String& raw) {
  if (raw.length() == 0) return "";
  if (raw.indexOf('/') >= 0 || raw.indexOf('\\') >= 0 || raw.indexOf("..") >= 0) return "";
  String lower = raw;
  lower.toLowerCase();
  bool isMusic = lower.endsWith(".mp3")  || lower.endsWith(".wav")  ||
                 lower.endsWith(".aac")  || lower.endsWith(".ogg")  ||
                 lower.endsWith(".flac") || lower.endsWith(".wma")  ||
                 lower.endsWith(".m4a")  || lower.endsWith(".opus") ||
                 lower.endsWith(".mid")  || lower.endsWith(".midi");
  if (!isMusic) return "";
  if (HotspotManager::isReservedName(lower)) return "";
  return raw;
}

// ── POST /delete — moves a file from /MP3/ into the trash folder ────────────
//    Files are never permanently erased here — they're moved into
//    TRASH_FOLDER_NAME. The user empties the trash themselves later by
//    plugging in over USB (Drive mode) and deleting from the .trash folder
//    on the host PC.
void HotspotManager::handleDelete() {
  noteActivity();

  if (!_webServer->hasArg("file")) {
    _webServer->send(400, "text/plain", "Missing 'file' parameter");
    return;
  }

  String filename = sanitizeMusicFilename(_webServer->arg("file"));
  if (filename.length() == 0) {
    Serial.printf("[Hotspot] Delete rejected — invalid filename: %s\n",
                  _webServer->arg("file").c_str());
    _webServer->send(400, "text/plain", "Invalid filename");
    return;
  }

  sd_switch_to_mp3();
  String path = String(MP3_FOLDER) + "/" + filename;
  if (!SD.exists(path)) {
    _webServer->send(404, "text/plain", "File not found");
    return;
  }

  if (mp3Mgr.moveToTrash(filename.c_str())) {
    Serial.printf("[Hotspot] Trashed: %s\n", filename.c_str());
    _webServer->send(200, "text/plain", "OK");
  } else {
    Serial.printf("[Hotspot] ERROR: failed to trash %s\n", filename.c_str());
    _webServer->send(500, "text/plain", "Failed to delete file");
  }
}

// ── POST /rename — renames a file within /MP3/ ───────────────────────────────
void HotspotManager::handleRename() {
  noteActivity();

  if (!_webServer->hasArg("from") || !_webServer->hasArg("to")) {
    _webServer->send(400, "text/plain", "Missing 'from'/'to' parameter");
    return;
  }

  String fromName = sanitizeMusicFilename(_webServer->arg("from"));
  String toRaw = _webServer->arg("to");
  toRaw.trim();
  // Let the caller omit the extension on the new name — preserve the original extension.
  String toLower = toRaw;
  toLower.toLowerCase();
  bool hasMusicExt = toLower.endsWith(".mp3")  || toLower.endsWith(".wav")  ||
                     toLower.endsWith(".aac")  || toLower.endsWith(".ogg")  ||
                     toLower.endsWith(".flac") || toLower.endsWith(".wma")  ||
                     toLower.endsWith(".m4a")  || toLower.endsWith(".opus") ||
                     toLower.endsWith(".mid")  || toLower.endsWith(".midi");
  if (!hasMusicExt) {
    // No extension provided — preserve the original file's extension
    int dotPos = fromName.lastIndexOf('.');
    String origExt = (dotPos >= 0) ? fromName.substring(dotPos) : ".mp3";
    toRaw += origExt;
  }
  String toName = sanitizeMusicFilename(toRaw);

  if (fromName.length() == 0 || toName.length() == 0) {
    Serial.println("[Hotspot] Rename rejected — invalid filename(s)");
    _webServer->send(400, "text/plain", "Invalid filename");
    return;
  }

  sd_switch_to_mp3();
  String fromPath = String(MP3_FOLDER) + "/" + fromName;
  String toPath = String(MP3_FOLDER) + "/" + toName;

  if (!SD.exists(fromPath)) {
    _webServer->send(404, "text/plain", "File not found");
    return;
  }
  if (SD.exists(toPath)) {
    _webServer->send(409, "text/plain", "A file with that name already exists");
    return;
  }

  if (mp3Mgr.renameEntry(fromName.c_str(), toName.c_str())) {
    Serial.printf("[Hotspot] Renamed: %s -> %s\n", fromName.c_str(), toName.c_str());
    _webServer->send(200, "text/plain", "OK");
  } else {
    Serial.printf("[Hotspot] ERROR: failed to rename %s -> %s\n",
                  fromName.c_str(), toName.c_str());
    _webServer->send(500, "text/plain", "Failed to rename file");
  }
}

// ── GET /download?file=<name> — streams a file from /MP3/ back to the client ─
void HotspotManager::handleDownload() {
  noteActivity();

  if (!_webServer->hasArg("file")) {
    _webServer->send(400, "text/plain", "Missing 'file' parameter");
    return;
  }

  String filename = sanitizeMusicFilename(_webServer->arg("file"));
  if (filename.length() == 0) {
    _webServer->send(400, "text/plain", "Invalid filename");
    return;
  }

  sd_switch_to_mp3();
  String path = String(MP3_FOLDER) + "/" + filename;
  File file = SD.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    _webServer->send(404, "text/plain", "File not found");
    return;
  }

  _webServer->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  _webServer->streamFile(file, "audio/mpeg");
  file.close();
}

// ── GET /stream?file=<name> — serves a file inline for <audio> preview ───────
//    Unlike /download, no Content-Disposition: attachment header, so the
//    browser plays it in place instead of saving it. WebServer::streamFile()
//    handles Range requests itself, so seeking works.
void HotspotManager::handleStream() {
  noteActivity();

  if (!_webServer->hasArg("file")) {
    _webServer->send(400, "text/plain", "Missing 'file' parameter");
    return;
  }

  String filename = sanitizeMusicFilename(_webServer->arg("file"));
  if (filename.length() == 0) {
    _webServer->send(400, "text/plain", "Invalid filename");
    return;
  }

  sd_switch_to_mp3();
  String path = String(MP3_FOLDER) + "/" + filename;
  File file = SD.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    _webServer->send(404, "text/plain", "File not found");
    return;
  }

  _webServer->streamFile(file, "audio/mpeg");
  file.close();
}

// ── GET /storage — used/total SD space for the library card header ──────────
void HotspotManager::handleStorageInfo() {
  noteActivity();
  sd_switch_to_mp3();
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  String json = "{\"used\":" + String((uint32_t)(used / (1024 * 1024))) +
                ",\"total\":" + String((uint32_t)(total / (1024 * 1024))) + "}";
  _webServer->send(200, "application/json", json);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  File management helpers
// ═══════════════════════════════════════════════════════════════════════════════

// ── ensureMP3Folder() — create /MP3/ on SD if it doesn't exist ───────────────
bool HotspotManager::ensureMP3Folder() {
  sd_switch_to_mp3();
  if (!SD.exists(MP3_FOLDER)) {
    if (!SD.mkdir(MP3_FOLDER)) {
      Serial.println("[Hotspot] ERROR: could not create " MP3_FOLDER " on SD");
      return false;
    }
    Serial.println("[Hotspot] Created " MP3_FOLDER " folder on SD");
  }
  return true;
}

// ── formatFileSize() — human-readable file size ─────────────────────────────
String HotspotManager::formatFileSize(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + " B";
  } else if (bytes < 1024UL * 1024) {
    return String(bytes / 1024.0, 1) + " KB";
  } else {
    return String(bytes / (1024.0 * 1024.0), 1) + " MB";
  }
}
