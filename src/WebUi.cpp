#include "WebUi.h"

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BAScloud M-Bus Virtual Meter</title><style>
body{font-family:system-ui,sans-serif;background:#111827;color:#e5e7eb;margin:0}.wrap{max-width:1100px;margin:auto;padding:0 16px 20px}
h1{margin:0;font-size:18px}h2{margin-top:0}.muted{color:#9ca3af}.card{background:#1f2937;border:1px solid #374151;border-radius:12px;padding:16px;margin:14px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:10px}label{display:block;font-size:13px;color:#cbd5e1}
input,select{width:100%;box-sizing:border-box;margin-top:4px;padding:9px;border-radius:7px;border:1px solid #4b5563;background:#111827;color:#fff}
button{border:0;border-radius:8px;padding:10px 14px;font-weight:700;cursor:pointer}.primary{background:#3b82f6;color:white}.danger{background:#991b1b;color:white}
.meter{border-top:1px solid #4b5563;padding-top:14px;margin-top:14px}.status{font-family:monospace;white-space:pre-wrap;word-break:break-all;background:#0b1220;padding:10px;border-radius:8px}
.small{font-size:12px}input.live{border-color:#3b82f6;transition:border-color .3s}input.edited{border-color:#f59e0b}.row{display:flex;gap:8px;flex-wrap:wrap}.ok{color:#86efac}.bad{color:#fca5a5}
header{position:sticky;top:0;z-index:10;background:#0b1220;border-bottom:1px solid #374151}
.bar{max-width:1100px;margin:auto;padding:10px 16px 0;display:flex;align-items:center;gap:12px;flex-wrap:wrap}.bar .sp{flex:1}
nav{max-width:1100px;margin:auto;padding:0 16px;display:flex;gap:2px;overflow-x:auto}
nav a{padding:10px 14px;color:#9ca3af;text-decoration:none;font-weight:600;font-size:14px;border-bottom:3px solid transparent;white-space:nowrap}
nav a:hover{color:#e5e7eb}nav a.act{color:#fff;border-bottom-color:#3b82f6}
#msg{max-width:1100px;margin:auto;padding:4px 16px 8px}#msg:empty{display:none}
.page{display:none}.page.act{display:block}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px}
.tile{background:#111827;border:1px solid #374151;border-radius:10px;padding:12px}.tile b{display:block;font-size:12px;color:#9ca3af;font-weight:600;margin-bottom:4px}.tile span{font-size:15px;word-break:break-word}
a{color:#93c5fd}
#login{position:fixed;inset:0;z-index:20;background:rgba(0,0,0,.6);display:none;align-items:center;justify-content:center;padding:16px}#login.show{display:flex}#login .card{width:100%;max-width:360px}#login label{margin-bottom:10px}
.ghost{background:#374151;color:#e5e7eb}#target{cursor:pointer}
input:disabled{opacity:.45;cursor:not-allowed}.tag{display:inline-block;font-size:12px;font-weight:600;padding:3px 8px;border-radius:999px;margin-left:8px;vertical-align:middle;background:#374151;color:#cbd5e1}.tag.bad{background:#7f1d1d;color:#fecaca}
.meter.used h3{color:#9ca3af}input.conflict{border-color:#ef4444}
</style></head><body>
<header><div class="bar"><h1>BAScloud M-Bus Virtual Meter</h1><span class="muted small">ESP32 + TSS721 · v0.1</span><span class="sp"></span><span id="target" class="muted small" onclick="showLogin(true)" title="ESP-Adresse ändern"></span><button id="logout" class="ghost" style="display:none" onclick="logout()">Abmelden</button><button class="primary" onclick="save()">Speichern</button></div>
<nav><a href="#status">Status</a><a href="#zaehler">Zähler</a><a href="#impulse">Impulse</a><a href="#sensor">BME280</a><a href="#mqtt">MQTT</a><a href="#system">System</a></nav>
<div id="msg" class="small"></div></header>
<div id="login"><div class="card"><h2>Anmelden</h2>
<label id="hostRow">ESP-Adresse<input id="lHost" placeholder="192.168.1.50"></label>
<label>Benutzer<input id="lUser" autocomplete="username" value="admin"></label><label>Passwort<input id="lPass" type="password" autocomplete="current-password" onkeydown="if(event.key==='Enter')login()"></label>
<div class="row"><button class="primary" onclick="login()">Anmelden</button><button class="ghost" onclick="hideLogin();location.hash='status'">Abbrechen</button></div><div id="lMsg" class="small"></div></div></div><div class="wrap">
<section class="page" id="p-status"><div class="card"><h2>Übersicht</h2><div class="tiles">
<div class="tile"><b>IP-Adresse</b><span id="tIp">…</span></div><div class="tile"><b>WLAN-Modus</b><span id="tMode">…</span></div>
<div class="tile"><b>M-Bus RX / TX</b><span id="tFrames">…</span></div><div class="tile"><b>MQTT</b><span id="tMqtt">…</span></div>
<div class="tile"><b>BME280</b><span id="tSensor">…</span></div></div></div>
<div class="card"><h2>M-Bus Monitor</h2><div id="status" class="status">lade...</div></div></section>
<section class="page" id="p-zaehler"><div class="card"><h2>Virtuelle M-Bus-Zähler</h2><div class="row small" style="align-items:center;margin-bottom:4px"><label for="refresh" style="display:inline">Zählerstände automatisch aktualisieren:</label>
<select id="refresh" onchange="setRefresh(+this.value)" style="width:auto;margin:0;padding:5px 8px"><option value="0">Aus</option><option value="1">alle 1 s</option><option value="2">alle 2 s</option><option value="5">alle 5 s</option><option value="10">alle 10 s</option><option value="30">alle 30 s</option><option value="60">alle 60 s</option></select></div>
<div class="muted small">Der Zählerstand wird als 32-Bit-Ganzzahl übertragen (0 … 4.294.967.295 Schritte). Die <b>Auflösung</b> legt fest, welchem Wert ein Schritt entspricht – je feiner die Auflösung, desto kleiner der maximale Zählerstand.<br>
Beispiel m³: 0,001 → max. 4.294.967,295 m³ · 0,01 → max. 42.949.672,95 m³ · 1 → max. 4.294.967.295 m³. Der Wert wird auf die gewählte Auflösung gerundet.<br>
Die Zählerstände werden im hier eingestellten Intervall automatisch aktualisiert (z. B. bei Änderungen per MQTT oder REST). Felder, die du gerade bearbeitest, werden dabei nicht überschrieben und sind gelb markiert, bis du speicherst.</div><div class="grid" style="margin-top:12px;align-items:end"><label>Anzahl virtueller Zähler<input id="meterCount" type="number" min="1" max="250"></label><div><button onclick="renderMeters()">Anzahl übernehmen</button></div></div><div id="meters"></div></div>
</section>
<section class="page" id="p-impulse"><div class="card"><h2>Impulseingänge</h2><div class="muted small">Zählt Impulse (Reedkontakt, S0-Ausgang) auf einem virtuellen Zähler: <b>Zählerstand = Startwert + Impulse × Faktor</b>. Der Faktor ist der Wert pro Impuls in der Einheit des Zählers, z. B. 0,001 m³ (1 l/Impuls) oder 0,001 kWh (1000 Imp./kWh).<br>
Ein Impuls zählt, wenn der Eingang mindestens die Entprellzeit lang aktiv ist. Wird der Zählerstand gesetzt (hier, per REST oder MQTT), zählt der Eingang von diesem Wert aus weiter. Startwert und Impulse werden spätestens nach 60 s gespeichert.<br>
Kontakt zwischen GPIO und GND mit internem Pull-up. GPIO 34–39 haben keinen Pull-up. Einstellungen gelten sofort nach dem Speichern.</div><div id="pulses"></div></div>
</section>
<section class="page" id="p-sensor"><div class="card"><h2>BME280-Sensor</h2><div class="muted small">Temperatur, Luftfeuchte und Luftdruck über I²C. Der Sensor antwortet auf dem M-Bus als eigener Slave (Medium Raumsensor) unter seiner Primäradresse. Diese darf nicht von einem aktiven Zähler belegt sein.</div>
<div class="grid" style="margin-top:10px">
<label>Aktiv<select id="sEn"><option value="1">Ja</option><option value="0">Nein</option></select></label>
<label>Name<input id="sName"></label><label>Primäradresse<input id="sPa" type="number" min="1" max="250"></label>
<label>Sekundäradresse<input id="sSa" type="number" min="0" max="99999999"></label><label>Manufacturer (3 Zeichen)<input id="sMan" maxlength="3"></label>
<label>Version<input id="sVer" type="number" min="0" max="255"></label>
<label>I²C SDA GPIO<input id="sSda" type="number"></label><label>I²C SCL GPIO<input id="sScl" type="number"></label>
<label>I²C-Adresse<select id="sI2c"><option value="118">0x76 (SDO an GND)</option><option value="119">0x77 (SDO an VCC)</option></select></label></div>
<div class="small" style="margin-top:10px">Messwerte: <span id="sensorStatus">…</span></div>
<div class="muted small">Aktivieren und Änderungen an den I²C-Pins werden nach einem Neustart wirksam.</div></div>
</section>
<section class="page" id="p-mqtt"><div class="card"><h2>MQTT</h2><div class="grid">
<label>MQTT aktiv<select id="mqttEnabled"><option value="1">Ja</option><option value="0">Nein</option></select></label>
<label>Broker (Host/IP)<input id="mqttHost" placeholder="192.168.1.10"></label><label>Port<input id="mqttPort" type="number" min="1" max="65535"></label>
<label>Benutzer<input id="mqttUser" placeholder="optional"></label><label>Passwort<input id="mqttPassword" type="password" placeholder="leer = unverändert"></label>
<label>Basis-Topic<input id="mqttBaseTopic" placeholder="bascloud/mbus"></label></div>
<div class="muted small" style="margin-top:10px">Zählerstand setzen: <code><span class="bt"></span>/meter/&lt;n&gt;/set</code> mit <code>123.456</code> oder <code>{"value":123.456}</code> (n = Zählernummer 1…250).<br>
Der Zähler meldet seinen Stand zurück auf <code><span class="bt"></span>/meter/&lt;n&gt;/state</code> (retained JSON), abgelehnte Werte auf <code><span class="bt"></span>/error</code>, Verbindungsstatus auf <code><span class="bt"></span>/status</code>.</div></div>
</section>
<section class="page" id="p-system"><div class="card"><h2>WLAN</h2><div class="grid">
<label>WLAN SSID<input id="wifiSsid"></label><label>WLAN Passwort<input id="wifiPassword" type="password" placeholder="leer = unverändert"></label></div>
<div class="muted small" style="margin-top:10px">Ohne Verbindung startet der ESP32 einen eigenen Access Point (BAScloud-MBus-…). Änderungen werden nach Neustart aktiv.</div></div>
<div class="card"><h2>M-Bus Schnittstelle</h2><div class="grid">
<label>Baudrate<select id="mbusBaud"><option>300</option><option>2400</option><option>9600</option></select></label>
<label>Stoppbits<select id="mbusStopBits"><option value="1">1 (8E1, Standard)</option><option value="2">2 (8E2, Workaround)</option></select></label>
<label>Byte-Pause (ms)<input id="mbusByteGapMs" type="number" min="0" max="20"></label>
<label>UART RX GPIO<input id="mbusRxPin" type="number"></label><label>UART TX GPIO<input id="mbusTxPin" type="number"></label></div>
<div class="muted small" style="margin-top:10px">Byte-Pause: Pause nach jedem gesendeten Byte, Standard 10 ms, 0 = normgerecht. Änderungen an der UART werden nach Neustart aktiv.</div></div>
<div class="card"><h2>Firmware-Update</h2><div class="muted small">firmware.bin aus .pio/build/esp32dev/ hochladen. Konfiguration und Zählerstände bleiben erhalten.</div><div class="row" style="margin-top:10px;flex-wrap:nowrap"><input id="fw" type="file" accept=".bin"><button class="primary" onclick="ota()">Hochladen</button></div><div id="otaMsg" class="small"></div></div>
<div class="card"><h2>Anmeldung</h2><div class="grid">
<label>Anmeldung aktiv<select id="authEnabled"><option value="1">Ja</option><option value="0">Nein</option></select></label>
<label>Benutzer<input id="authUser" placeholder="admin"></label><label>Passwort<input id="authPassword" type="password" autocomplete="new-password" placeholder="leer = unverändert"></label></div>
<div class="muted small" style="margin-top:10px">Schützt alle Seiten außer Status, die REST-API und das Firmware-Update (HTTP Basic-Auth). Passwort vergessen: BOOT-Taste am ESP32 im Betrieb 5 s gedrückt halten, dann ist die Anmeldung aus.</div></div>
<div class="card"><h2>Weboberfläche extern nutzen</h2><div class="muted small">Die Weboberfläche kann auch ohne den ESP32 als Webserver laufen, z. B. als lokale Datei oder von einem anderen Server im LAN. Sie fragt dann nach der ESP-Adresse, alternativ <code>?esp=192.168.1.50</code> an die URL hängen. Nur über http, nicht https.</div>
<div class="grid" style="margin-top:10px;align-items:end"><label>Lokale Weboberfläche auf dem ESP32<select id="webUiEnabled"><option value="1">Ein</option><option value="0">Aus (nur REST-API)</option></select></label>
<div><a id="dl" href="/" download="mbus-weboberflaeche.html"><button class="ghost">Weboberfläche herunterladen</button></a></div></div>
<div class="muted small" style="margin-top:10px">Aus: Der ESP32 liefert diese Seite und die API-Doku nicht mehr aus, REST-API und MQTT laufen weiter. Vorher die Weboberfläche herunterladen. Wieder einschalten: hier in der externen Weboberfläche, per REST <code>PUT /api/webui</code> mit <code>{"enabled":true}</code>, per MQTT <code><span class="bt"></span>/webui/set</code> = <code>on</code> oder BOOT-Taste 5 s halten.</div></div>
<div class="card"><h2>Neustart</h2><div class="row"><button onclick="restart()">ESP32 neu starten</button></div></div>
<div class="card"><h2>REST-API</h2><div class="muted small">
<code>GET /api/meters</code> – alle Zähler abfragen · <code>GET /api/meters/&lt;n&gt;</code> – einen Zähler abfragen<br>
<code>PUT /api/meters/&lt;n&gt;</code> mit <code>{"value":123.456}</code> – Zählerstand setzen<br>
<code>PUT /api/meters</code> mit <code>[{"index":1,"value":1.5},{"primaryAddress":7,"value":2}]</code> – mehrere Zähler auf einmal setzen<br>
Werte, die per REST oder MQTT gesetzt werden, sind sofort per M-Bus abrufbar und werden nach spätestens 60 s dauerhaft gespeichert.<br>
Ausführliche Doku zum Ausprobieren: <a href="/api/docs" style="color:#93c5fd">/api/docs</a></div></div>
</section>
</div><script>
let cfg={meters:[]};
// '2b' = electricity, bidirectional (medium 2 + bidirectional flag)
const media=[['0','Other'],['2','Strom'],['2b','Strom 2-Richtung'],['3','Gas'],['4','Wärme'],['6','Warmwasser'],['7','Wasser'],['22','Kaltwasser']];
const REGS=[['1.8.1','Bezug Tarif 1'],['1.8.2','Bezug Tarif 2'],['2.8.0','Einspeisung gesamt'],['2.8.1','Einspeisung Tarif 1'],['2.8.2','Einspeisung Tarif 2']];
// Requests go to API (empty = the ESP serving this page). Outside the ESP (local file, ?esp=, other
// server) the address is asked for and remembered. Login: Basic auth header, kept for the session.
const Q=new URLSearchParams(location.search),EXT=location.protocol==='file:'||Q.has('esp');
let API='',AUTH='',authOn=false,authed=false;
function host(h){h=(h||'').trim().replace(/\/+$/,'');return h&&!/^https?:\/\//.test(h)?'http://'+h:h;}
try{API=host(Q.get('esp')||(EXT?localStorage.getItem('espHost'):''));AUTH=sessionStorage.getItem('auth')||'';}catch(e){}
function basic(u,p){return 'Basic '+btoa(unescape(encodeURIComponent(u+':'+p)));}
async function api(path,opt={}){if(AUTH)opt.headers=Object.assign({},opt.headers,{Authorization:AUTH});
let r=await fetch(API+path,opt);if(r.status===401){authed=false;showLogin();throw new Error('Anmeldung erforderlich');}return r;}
function isOpen(){return (location.hash.slice(1)||'status')==='status';}
function showLogin(force){if(!force&&isOpen())return;document.getElementById('hostRow').style.display=EXT?'':'none';document.getElementById('lHost').value=API.replace(/^http:\/\//,'');
document.getElementById('lMsg').textContent='';document.getElementById('login').classList.add('show');(EXT&&!API?document.getElementById('lHost'):document.getElementById('lPass')).focus();}
function hideLogin(){document.getElementById('login').classList.remove('show');}
async function login(){let m=document.getElementById('lMsg');if(EXT){API=host(document.getElementById('lHost').value);try{localStorage.setItem('espHost',API);}catch(e){}}
m.className='small';m.textContent='Verbinde...';
try{authOn=!!(await (await fetch(API+'/api/status')).json()).auth;AUTH=authOn?basic(document.getElementById('lUser').value,document.getElementById('lPass').value):'';
let r=await fetch(API+'/api/config',{headers:AUTH?{Authorization:AUTH}:{}});if(r.status===401){m.className='small bad';m.textContent='Benutzer oder Passwort falsch';return;}
if(!r.ok)throw 0;try{sessionStorage.setItem('auth',AUTH);}catch(e){}authed=true;hideLogin();showTarget();await load();stat();}catch(e){m.className='small bad';m.textContent='ESP nicht erreichbar'+(EXT?' (Adresse prüfen)':'');}}
function logout(){AUTH='';authed=false;try{sessionStorage.removeItem('auth');}catch(e){}location.hash='status';showTarget();}
function showTarget(){document.getElementById('target').textContent=EXT?'Gerät: '+(API.replace(/^http:\/\//,'')||'–'):'';document.getElementById('logout').style.display=authOn&&authed?'':'none';document.getElementById('dl').href=API+'/';}
// Loads the configuration once the device is known and the login (if any) is there.
async function start(){showTarget();if(EXT&&!API){showLogin(true);return;}
try{let s=await (await fetch(API+'/api/status')).json();authOn=!!s.auth;}catch(e){if(EXT)showLogin(true);return;}
if(authOn&&!AUTH){route();return;}try{await load();authed=true;showTarget();}catch(e){}}
async function load(){cfg=await (await api('/api/config')).json();for(const k of ['wifiSsid','authUser','mbusBaud','mbusStopBits','mbusByteGapMs','mbusRxPin','mbusTxPin','meterCount','mqttHost','mqttPort','mqttUser','mqttBaseTopic']) document.getElementById(k).value=cfg[k]??'';document.getElementById('mqttEnabled').value=cfg.mqttEnabled?'1':'0';document.getElementById('authEnabled').value=cfg.authEnabled?'1':'0';document.getElementById('webUiEnabled').value=cfg.webUiEnabled===false?'0':'1';
let s=cfg.sensor||{};document.getElementById('sEn').value=s.enabled?'1':'0';for(const [id,k] of SENSOR_FIELDS)document.getElementById(id).value=s[k]??'';document.querySelectorAll('.bt').forEach(e=>e.textContent=cfg.mqttBaseTopic||'bascloud/mbus');renderPulses();renderMeters();}
function renderMeters(){let n=Math.max(1,Math.min(250,+document.getElementById('meterCount').value||1));document.getElementById('meterCount').value=n;
while(cfg.meters.length<n){let i=cfg.meters.length;cfg.meters.push({enabled:true,name:`Meter ${i+1}`,primaryAddress:i+1,secondaryAddress:10000001+i,manufacturer:'BAS',version:1,medium:7,value:0,unit:'m3',resolutionExp:-3});}cfg.meters=cfg.meters.slice(0,n);
let h='';cfg.meters.forEach((m,i)=>{h+=`<div class="meter" id="m${i}"><h3>Zähler ${i+1}<span id="use${i}"></span></h3><div class="grid">
<label>Aktiv<select id="en${i}"><option value="1" ${m.enabled?'selected':''}>Ja</option><option value="0" ${!m.enabled?'selected':''}>Nein</option></select></label>
<label>Name<input id="name${i}" value="${esc(m.name)}"></label><label>Primäradresse<input id="pa${i}" type="number" min="1" max="250" value="${m.primaryAddress}"></label>
<label>Sekundäradresse<input id="sa${i}" type="number" min="0" max="99999999" value="${m.secondaryAddress}"></label><label>Manufacturer (3 Zeichen)<input id="man${i}" maxlength="3" value="${esc(m.manufacturer)}"></label>
<label>Version<input id="ver${i}" type="number" min="0" max="255" value="${m.version}"></label><label>Medium<select id="med${i}" onchange="typeVis(${i})">${media.map(x=>`<option value="${x[0]}" ${(x[0]==='2b'?!!m.bidirectional:!m.bidirectional&&+m.medium===+x[0])?'selected':''}>${x[1]}</option>`).join('')}</select></label>
<label><span id="vl${i}">Zählerstand</span><input id="val${i}" type="number" min="0" value="${m.value}" oninput="this.dataset.edited=1;this.classList.add('edited')"><span id="hint${i}" class="muted small"></span><span id="pu${i}" class="muted small"></span></label><label>Einheit<select id="unit${i}" onchange="upd(${i})"><option value="m3" ${m.unit==='m3'?'selected':''}>m³</option><option value="kWh" ${m.unit==='kWh'?'selected':''}>kWh</option></select></label>
<label>Auflösung<select id="res${i}" onchange="upd(${i})">${RES.map(e=>`<option value="${e}" ${e===resOf(m)?'selected':''}>${fmt(Math.pow(10,e),e)}</option>`).join('')}</select></label>
</div><div class="grid" id="heat${i}" style="margin-top:10px${isHeat(m.medium)?'':';display:none'}">
<label>Durchfluss (m³/h)<input id="fl${i}" type="number" min="0" max="${FLOW_MAX}" step="0.001" value="${m.flow??0}" oninput="this.dataset.edited=1;this.classList.add('edited')"><span class="muted small">Auflösung 0,001 m³/h (1 l/h)</span></label>
<label>Vorlauftemperatur (°C)<input id="ft${i}" type="number" min="${TEMP_MIN}" max="${TEMP_MAX}" step="0.1" value="${m.flowTemp??0}" oninput="this.dataset.edited=1;this.classList.add('edited')"><span class="muted small">Auflösung 0,1 °C</span></label>
<label>Rücklauftemperatur (°C)<input id="rt${i}" type="number" min="${TEMP_MIN}" max="${TEMP_MAX}" step="0.1" value="${m.returnTemp??0}" oninput="this.dataset.edited=1;this.classList.add('edited')"><span class="muted small">Auflösung 0,1 °C</span></label>
</div><div class="grid" id="bidir${i}" style="margin-top:10px${m.bidirectional?'':';display:none'}">
${REGS.map((r,k)=>`<label>${r[0]} ${r[1]} (kWh)<input id="reg${k}_${i}" type="number" min="0" value="${m[r[0]]??0}" oninput="this.dataset.edited=1;this.classList.add('edited')"></label>`).join('')}
</div></div>`});document.getElementById('meters').innerHTML=h;cfg.meters.forEach((m,i)=>typeVis(i));markUsage();}
const PULSE_PINS=[32,33,25,26,27,14];
const PULSE_FIELDS=[['pPin','pin'],['pDeb','debounceMs'],['pMeter','meter'],['pFac','factor']];
function renderPulses(){let h='';(cfg.pulses||PULSE_PINS.map(()=>({}))).forEach((p,i)=>{h+=`<div class="meter"><h3>Impulseingang ${i+1}</h3><div class="grid">
<label>Aktiv<select id="pEn${i}"><option value="1" ${p.enabled?'selected':''}>Ja</option><option value="0" ${!p.enabled?'selected':''}>Nein</option></select></label>
<label>GPIO<input id="pPin${i}" type="number" min="0" max="39" value="${p.pin??PULSE_PINS[i]}"></label>
<label>Pull-up intern<select id="pPull${i}"><option value="1" ${p.pullup!==false?'selected':''}>Ja</option><option value="0" ${p.pullup===false?'selected':''}>Nein</option></select></label>
<label>Impuls aktiv bei<select id="pLow${i}"><option value="1" ${p.activeLow!==false?'selected':''}>Low (Kontakt nach GND)</option><option value="0" ${p.activeLow===false?'selected':''}>High</option></select></label>
<label>Entprellzeit (ms)<input id="pDeb${i}" type="number" min="1" max="1000" value="${p.debounceMs??20}"></label>
<label>Zähler-Nr.<input id="pMeter${i}" type="number" min="1" max="250" value="${p.meter??(i+1)}"></label>
<label>Faktor (Einheit pro Impuls)<input id="pFac${i}" type="number" min="0" step="any" value="${p.factor??0.001}"></label>
<label>Startwert<span class="row" style="flex-wrap:nowrap"><input id="pStart${i}" type="number" min="0" step="any" placeholder="neuer Zählerstand"><button class="primary" style="margin-top:4px" onclick="setStart(${i})">Setzen</button></span></label>
</div><div class="small" style="margin-top:8px" id="pSt${i}">…</div></div>`;});document.getElementById('pulses').innerHTML=h;}
function gatherPulses(){return [...document.querySelectorAll('[id^=pEn]')].map((_,i)=>{let p={enabled:document.getElementById('pEn'+i).value==='1',pullup:document.getElementById('pPull'+i).value==='1',activeLow:document.getElementById('pLow'+i).value==='1'};for(const [id,k] of PULSE_FIELDS)p[k]=+document.getElementById(id+i).value;return p;});}
// Marks meters used by another function, based on the current (possibly unsaved) form values:
// pulse inputs count a meter (value field locked), the BME280 must not share a primary address.
function markUsage(){if(!document.getElementById('pEn0'))return;let ps=gatherPulses(),s=gatherSensor();
cfg.meters.forEach((m,i)=>{let tags=[],k=ps.findIndex(p=>p.enabled&&p.meter===i+1),val=document.getElementById('val'+i),pa=document.getElementById('pa'+i);if(!val)return;
if(k>=0)tags.push(`<span class="tag">Impulseingang ${k+1} · GPIO ${ps[k].pin}</span>`);
let clash=s.enabled&&document.getElementById('en'+i).value==='1'&&+pa.value===s.primaryAddress;
if(clash)tags.push(`<span class="tag bad">Primäradresse ${s.primaryAddress} belegt durch BME280</span>`);
pa.classList.toggle('conflict',clash);val.disabled=k>=0;document.getElementById('pu'+i).innerHTML=k>=0?'<br>wird vom Impulseingang gezählt':'';
document.getElementById('use'+i).innerHTML=tags.join('');document.getElementById('m'+i).classList.toggle('used',k>=0);});}
document.addEventListener('change',e=>{if(/^(pEn|pPin|pMeter|sEn|sPa|pa|en)\d*$/.test(e.target.id))markUsage();});
document.addEventListener('input',e=>{if(/^(pPin|pMeter|sPa|pa)\d*$/.test(e.target.id))markUsage();});
async function setStart(i){let p=(cfg.pulses||[])[i],v=document.getElementById('pStart'+i).value,msg=document.getElementById('pSt'+i);msg._until=Date.now()+4000;
if(!p||!p.enabled){msg.className='small bad';msg.textContent='Impulseingang erst aktivieren und speichern.';return;}
if(v===''||!(+v>=0)){msg.className='small bad';msg.textContent='Startwert muss eine Zahl >= 0 sein.';return;}
let r=await api('/api/meters/'+p.meter,{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({value:+v})});
if(r.ok){document.getElementById('pStart'+i).value='';msg.className='small ok';msg.textContent='Startwert gesetzt.';}else{msg.className='small bad';try{msg.textContent=(await r.json()).error;}catch(e){msg.textContent='Fehler '+r.status;}}}
const SENSOR_FIELDS=[['sName','name'],['sPa','primaryAddress'],['sSa','secondaryAddress'],['sMan','manufacturer'],['sVer','version'],['sSda','sdaPin'],['sScl','sclPin'],['sI2c','i2cAddress']];
function gatherSensor(){let s={enabled:document.getElementById('sEn').value==='1'};for(const [id,k] of SENSOR_FIELDS){let v=document.getElementById(id).value;s[k]=k==='name'?v:k==='manufacturer'?v.toUpperCase():+v;}return s;}
const RES=[-3,-2,-1,0,1],RAW_MAX=4294967295,FLOW_MAX=4294967.295,TEMP_MIN=-3276.8,TEMP_MAX=3276.7;
function isHeat(med){return +med===4||+med===12;}
// Shows the extra fields of the selected meter type; bidirectional electricity is always kWh.
function typeVis(i){let med=document.getElementById('med'+i).value,bi=med==='2b',unit=document.getElementById('unit'+i);
document.getElementById('heat'+i).style.display=isHeat(med)?'':'none';document.getElementById('bidir'+i).style.display=bi?'':'none';
document.getElementById('vl'+i).textContent=bi?'1.8.0 Bezug gesamt':'Zählerstand';if(bi)unit.value='kWh';unit.disabled=bi;upd(i);}
function resOf(m){return m.resolutionExp??(m.unit==='kWh'?0:-3);}
function fmt(x,e){let d=Math.max(0,-e);return x.toLocaleString('de-DE',{minimumFractionDigits:d,maximumFractionDigits:d});}
function unitName(u){return u==='m3'?'m³':u;}
function maxOf(e){return e<=0?RAW_MAX/Math.pow(10,-e):RAW_MAX*Math.pow(10,e);}
function rawOf(v,e){return Math.round(e<=0?v*Math.pow(10,-e):v/Math.pow(10,e));}
function upd(i){let e=+document.getElementById('res'+i).value,u=unitName(document.getElementById('unit'+i).value),inp=document.getElementById('val'+i);
for(const x of [inp,...REGS.map((r,k)=>document.getElementById(`reg${k}_${i}`))]){x.step=e<0?Math.pow(10,e).toFixed(-e):'any';x.max=maxOf(e);}
document.getElementById('hint'+i).textContent=`max. ${fmt(maxOf(e),e)} ${u}`;
for(const o of document.getElementById('res'+i).options)o.textContent=`${fmt(Math.pow(10,+o.value),+o.value)} ${u}`;}
function esc(s){return String(s||'').replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
function gather(){let n=+document.getElementById('meterCount').value;let meters=[];for(let i=0;i<n;i++)meters.push({enabled:document.getElementById('en'+i).value==='1',name:document.getElementById('name'+i).value,primaryAddress:+document.getElementById('pa'+i).value,secondaryAddress:+document.getElementById('sa'+i).value,manufacturer:document.getElementById('man'+i).value.toUpperCase(),version:+document.getElementById('ver'+i).value,medium:document.getElementById('med'+i).value==='2b'?2:+document.getElementById('med'+i).value,bidirectional:document.getElementById('med'+i).value==='2b',value:+document.getElementById('val'+i).value,unit:document.getElementById('unit'+i).value,resolutionExp:+document.getElementById('res'+i).value,flow:+document.getElementById('fl'+i).value,flowTemp:+document.getElementById('ft'+i).value,returnTemp:+document.getElementById('rt'+i).value,...Object.fromEntries(REGS.map((r,k)=>[r[0],+document.getElementById(`reg${k}_${i}`).value]))});
// Pulse-counted meters: the device keeps its own value.
meters.forEach((m,i)=>{if(document.getElementById('val'+i).disabled)delete m.value;});return {wifiSsid:document.getElementById('wifiSsid').value,wifiPassword:document.getElementById('wifiPassword').value,authEnabled:document.getElementById('authEnabled').value==='1',authUser:document.getElementById('authUser').value.trim()||'admin',authPassword:document.getElementById('authPassword').value,webUiEnabled:document.getElementById('webUiEnabled').value==='1',mbusBaud:+document.getElementById('mbusBaud').value,mbusStopBits:+document.getElementById('mbusStopBits').value,mbusByteGapMs:document.getElementById('mbusByteGapMs').value===''?10:+document.getElementById('mbusByteGapMs').value,mbusRxPin:+document.getElementById('mbusRxPin').value,mbusTxPin:+document.getElementById('mbusTxPin').value,meterCount:n,meters,mqttEnabled:document.getElementById('mqttEnabled').value==='1',mqttHost:document.getElementById('mqttHost').value,mqttPort:+document.getElementById('mqttPort').value||1883,mqttUser:document.getElementById('mqttUser').value,mqttPassword:document.getElementById('mqttPassword').value,mqttBaseTopic:document.getElementById('mqttBaseTopic').value,sensor:gatherSensor(),pulses:gatherPulses()};}
function check(c){if(c.authEnabled&&!c.authPassword&&!cfg.authEnabled)return 'Anmeldung: Passwort festlegen.';if(c.sensor.enabled){if(!(c.sensor.primaryAddress>=1&&c.sensor.primaryAddress<=250))return 'BME280: Primäradresse muss zwischen 1 und 250 liegen.';
let k=c.meters.findIndex(m=>m.enabled&&m.primaryAddress===c.sensor.primaryAddress);if(k>=0)return `BME280: Primäradresse ${c.sensor.primaryAddress} ist schon von Zähler ${k+1} belegt.`;}
for(let k=0;k<c.pulses.length;k++){let p=c.pulses[k];if(!p.enabled)continue;
if(!(p.meter>=1&&p.meter<=c.meters.length))return `Impulseingang ${k+1}: Zähler-Nr. muss zwischen 1 und ${c.meters.length} liegen.`;
if(!(p.factor>0))return `Impulseingang ${k+1}: Faktor muss größer 0 sein.`;
if(!(p.debounceMs>=1&&p.debounceMs<=1000))return `Impulseingang ${k+1}: Entprellzeit muss zwischen 1 und 1000 ms liegen.`;}
for(let i=0;i<c.meters.length;i++){let m=c.meters[i],r=m.value===undefined?0:rawOf(m.value,m.resolutionExp);
if(m.bidirectional)for(const [reg,name] of REGS){let v=m[reg];if(!(v>=0)||rawOf(v,m.resolutionExp)>RAW_MAX)return `Zähler ${i+1}: ${reg} ${name} muss zwischen 0 und ${fmt(maxOf(m.resolutionExp),m.resolutionExp)} kWh liegen.`;}
if(isHeat(m.medium)){if(!(m.flow>=0&&m.flow<=FLOW_MAX))return `Zähler ${i+1}: Durchfluss muss zwischen 0 und ${fmt(FLOW_MAX,-3)} m³/h liegen.`;
for(const [t,name] of [[m.flowTemp,'Vorlauftemperatur'],[m.returnTemp,'Rücklauftemperatur']])if(!(t>=TEMP_MIN&&t<=TEMP_MAX))return `Zähler ${i+1}: ${name} muss zwischen ${fmt(TEMP_MIN,-1)} und ${fmt(TEMP_MAX,-1)} °C liegen.`;}if(m.value!==undefined&&(!(m.value>=0)||r>RAW_MAX))return `Zähler ${i+1}: Zählerstand muss zwischen 0 und ${fmt(maxOf(m.resolutionExp),m.resolutionExp)} ${unitName(m.unit)} liegen (Auflösung erhöhen für größere Werte).`;}return '';}
async function save(){if(!authed){showLogin(true);return;}let c=gather(),err=check(c),msg=document.getElementById('msg');if(err){msg.className='small bad';msg.textContent=err;return;}
if(!EXT&&cfg.webUiEnabled!==false&&!c.webUiEnabled&&!confirm('Lokale Weboberfläche abschalten? Diese Seite ist danach auf dem ESP32 nicht mehr erreichbar, nur noch extern (vorher herunterladen).'))return;
let r;try{r=await api('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(c)});}catch(e){return;}let t=await r.text();msg.className=r.ok?'small ok':'small bad';msg.textContent=t;
if(!r.ok)return;
// Keep the session logged in with the new login data.
if(c.authEnabled){let old=AUTH?decodeURIComponent(escape(atob(AUTH.slice(6)))):'',pw=c.authPassword||old.slice(old.indexOf(':')+1);AUTH=basic(c.authUser,pw);}else AUTH='';
try{sessionStorage.setItem('auth',AUTH);}catch(e){}authOn=c.authEnabled;document.getElementById('authPassword').value='';showTarget();setTimeout(load,400);}
async function ota(){let f=document.getElementById('fw').files[0],m=document.getElementById('otaMsg');if(!f){m.className='small bad';m.textContent='Keine Datei gewählt';return;}m.className='small';m.textContent='Upload läuft...';let d=new FormData();d.append('firmware',f);try{let r=await api('/api/update',{method:'POST',body:d});m.className=r.ok?'small ok':'small bad';m.textContent=await r.text();}catch(e){m.className='small bad';m.textContent='Verbindung abgebrochen';}}
async function restart(){try{await api('/api/restart',{method:'POST'});}catch(e){return;}document.getElementById('msg').textContent='Neustart ausgelöst...';}
async function stat(){try{let s=await (await fetch(API+'/api/status')).json();authOn=!!s.auth;document.getElementById('status').textContent=`IP: ${s.ip}\nModus: ${s.wifiMode}\nRX Frames: ${s.rxFrames}\nTX Frames: ${s.txFrames}\nLetztes Ereignis: ${s.lastEvent}\nRX: ${s.lastRx}\nTX: ${s.lastTx}\nMQTT: ${s.mqtt}\nBME280: ${s.sensor}`;document.getElementById('sensorStatus').textContent=s.sensor;
for(const [id,v] of [['tIp',s.ip],['tMode',s.wifiMode],['tFrames',s.rxFrames+' / '+s.txFrames],['tMqtt',s.mqtt],['tSensor',s.sensor]])document.getElementById(id).textContent=v;
(s.pulses||[]).forEach((p,i)=>{let el=document.getElementById('pSt'+i);if(!el||Date.now()<(el._until||0))return;el.className='small';
el.textContent=p.enabled?`Zähler ${p.meter}: Startwert ${+p.startValue.toFixed(6)} + ${p.count} Impulse → Zählerstand ${+p.value.toFixed(6)} ${unitName(p.unit)}`:'deaktiviert';});}catch(e){}}
async function refreshValues(){if(!authed)return;try{let list=await (await api('/api/values')).json();
// Entry per meter: number, or [value, flow, flowTemp, returnTemp] for heat meters.
// Entry for bidirectional electricity meters: [1.8.0, 1.8.1, 1.8.2, 2.8.0, 2.8.1, 2.8.2].
const LIVE_HEAT=[['val','value'],['fl','flow'],['ft','flowTemp'],['rt','returnTemp']];
for(let i=0;i<list.length;i++){let vals=Array.isArray(list[i])?list[i]:[list[i]];
const LIVE=cfg.meters[i]&&cfg.meters[i].bidirectional?[['val','value'],...REGS.map((r,k)=>[`reg${k}_`,r[0]])]:LIVE_HEAT;
vals.forEach((v,k)=>{if(!LIVE[k])return;let inp=document.getElementById(LIVE[k][0]+i);if(!inp||inp.dataset.edited||document.activeElement===inp)return;
if(cfg.meters[i])cfg.meters[i][LIVE[k][1]]=v;if(+inp.value!==v){inp.value=v;inp.classList.add('live');setTimeout(()=>inp.classList.remove('live'),800);}});}}catch(e){}}
let refreshTimer=null;
function setRefresh(sec){clearInterval(refreshTimer);refreshTimer=sec>0?setInterval(refreshValues,sec*1000):null;try{localStorage.setItem('refreshSec',sec);}catch(e){}}
function initRefresh(){let sec=2;try{let s=localStorage.getItem('refreshSec');if(s!==null&&[...document.getElementById('refresh').options].some(o=>o.value===s))sec=+s;}catch(e){}
document.getElementById('refresh').value=sec;setRefresh(sec);}
function route(){let id=location.hash.slice(1);if(!document.getElementById('p-'+id))id='status';
document.querySelectorAll('.page').forEach(p=>p.classList.toggle('act',p.id==='p-'+id));document.querySelectorAll('nav a').forEach(a=>a.classList.toggle('act',a.getAttribute('href')==='#'+id));
if(id==='status')hideLogin();else if(!authed&&authOn)showLogin();}
window.addEventListener('hashchange',route);route();
start();setInterval(()=>{if(API||!EXT)stat();},1500);initRefresh();
</script></body></html>
)HTML";
