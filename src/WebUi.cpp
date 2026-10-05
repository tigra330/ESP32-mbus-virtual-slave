#include "WebUi.h"

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BAScloud M-Bus Virtual Meter</title><style>
body{font-family:system-ui,sans-serif;background:#111827;color:#e5e7eb;margin:0}.wrap{max-width:1100px;margin:auto;padding:20px}
h1{margin:0 0 6px}.muted{color:#9ca3af}.card{background:#1f2937;border:1px solid #374151;border-radius:12px;padding:16px;margin:14px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:10px}label{display:block;font-size:13px;color:#cbd5e1}
input,select{width:100%;box-sizing:border-box;margin-top:4px;padding:9px;border-radius:7px;border:1px solid #4b5563;background:#111827;color:#fff}
button{border:0;border-radius:8px;padding:10px 14px;font-weight:700;cursor:pointer}.primary{background:#3b82f6;color:white}.danger{background:#991b1b;color:white}
.meter{border-top:1px solid #4b5563;padding-top:14px;margin-top:14px}.status{font-family:monospace;white-space:pre-wrap;background:#0b1220;padding:10px;border-radius:8px}
.small{font-size:12px}input.live{border-color:#3b82f6;transition:border-color .3s}input.edited{border-color:#f59e0b}.row{display:flex;gap:8px;flex-wrap:wrap}.ok{color:#86efac}.bad{color:#fca5a5}
</style></head><body><div class="wrap"><h1>BAScloud M-Bus Virtual Meter</h1><div class="muted">ESP32 + TSS721 · v0.1 · <a href="/api/docs" style="color:#93c5fd">API-Dokumentation</a></div>
<div class="row small" style="align-items:center;margin-top:10px"><label for="refresh" style="display:inline">Zählerstände automatisch aktualisieren:</label>
<select id="refresh" onchange="setRefresh(+this.value)" style="width:auto;margin:0;padding:5px 8px"><option value="0">Aus</option><option value="1">alle 1 s</option><option value="2">alle 2 s</option><option value="5">alle 5 s</option><option value="10">alle 10 s</option><option value="30">alle 30 s</option><option value="60">alle 60 s</option></select></div>
<div class="card"><h2>Gerät</h2><div class="grid">
<label>WLAN SSID<input id="wifiSsid"></label><label>WLAN Passwort<input id="wifiPassword" type="password" placeholder="leer = unverändert"></label>
<label>M-Bus Baudrate<select id="mbusBaud"><option>300</option><option>2400</option><option>9600</option></select></label>
<label>M-Bus Stoppbits<select id="mbusStopBits"><option value="1">1 (8E1, Standard)</option><option value="2">2 (8E2, Workaround)</option></select></label>
<label>Pause nach jedem Byte (ms, Standard 10, 0 = normgerecht)<input id="mbusByteGapMs" type="number" min="0" max="20"></label>
<label>UART RX GPIO<input id="mbusRxPin" type="number"></label><label>UART TX GPIO<input id="mbusTxPin" type="number"></label>
<label>Anzahl virtueller Zähler<input id="meterCount" type="number" min="1" max="250"></label></div>
<div class="row" style="margin-top:12px"><button class="primary" onclick="renderMeters()">Anzahl übernehmen</button><button class="primary" onclick="save()">Speichern</button><button onclick="restart()">ESP32 neu starten</button></div><div id="msg" class="small"></div></div>
<div class="card"><h2>MQTT</h2><div class="grid">
<label>MQTT aktiv<select id="mqttEnabled"><option value="1">Ja</option><option value="0">Nein</option></select></label>
<label>Broker (Host/IP)<input id="mqttHost" placeholder="192.168.1.10"></label><label>Port<input id="mqttPort" type="number" min="1" max="65535"></label>
<label>Benutzer<input id="mqttUser" placeholder="optional"></label><label>Passwort<input id="mqttPassword" type="password" placeholder="leer = unverändert"></label>
<label>Basis-Topic<input id="mqttBaseTopic" placeholder="bascloud/mbus"></label></div>
<div class="muted small" style="margin-top:10px">Zählerstand setzen: <code><span class="bt"></span>/meter/&lt;n&gt;/set</code> mit <code>123.456</code> oder <code>{"value":123.456}</code> (n = Zählernummer 1…250).<br>
Der Zähler meldet seinen Stand zurück auf <code><span class="bt"></span>/meter/&lt;n&gt;/state</code> (retained JSON), abgelehnte Werte auf <code><span class="bt"></span>/error</code>, Verbindungsstatus auf <code><span class="bt"></span>/status</code>.</div></div>
<div class="card"><h2>Virtuelle M-Bus-Zähler</h2><div class="muted small">Der Zählerstand wird als 32-Bit-Ganzzahl übertragen (0 … 4.294.967.295 Schritte). Die <b>Auflösung</b> legt fest, welchem Wert ein Schritt entspricht – je feiner die Auflösung, desto kleiner der maximale Zählerstand.<br>
Beispiel m³: 0,001 → max. 4.294.967,295 m³ · 0,01 → max. 42.949.672,95 m³ · 1 → max. 4.294.967.295 m³. Der Wert wird auf die gewählte Auflösung gerundet.<br>
Die Zählerstände werden im oben eingestellten Intervall automatisch aktualisiert (z. B. bei Änderungen per MQTT oder REST). Felder, die du gerade bearbeitest, werden dabei nicht überschrieben und sind gelb markiert, bis du speicherst.</div><div id="meters"></div></div>
<div class="card"><h2>REST-API</h2><div class="muted small">
<code>GET /api/meters</code> – alle Zähler abfragen · <code>GET /api/meters/&lt;n&gt;</code> – einen Zähler abfragen<br>
<code>PUT /api/meters/&lt;n&gt;</code> mit <code>{"value":123.456}</code> – Zählerstand setzen<br>
<code>PUT /api/meters</code> mit <code>[{"index":1,"value":1.5},{"primaryAddress":7,"value":2}]</code> – mehrere Zähler auf einmal setzen<br>
Werte, die per REST oder MQTT gesetzt werden, sind sofort per M-Bus abrufbar und werden nach spätestens 60 s dauerhaft gespeichert.<br>
Ausführliche Doku zum Ausprobieren: <a href="/api/docs" style="color:#93c5fd">/api/docs</a></div></div>
<div class="card"><h2>M-Bus Monitor</h2><div id="status" class="status">lade...</div></div>
</div><script>
let cfg={meters:[]};
const media=[['0','Other'],['2','Strom'],['3','Gas'],['4','Wärme'],['6','Warmwasser'],['7','Wasser'],['22','Kaltwasser']];
async function load(){cfg=await (await fetch('/api/config')).json();for(const k of ['wifiSsid','mbusBaud','mbusStopBits','mbusByteGapMs','mbusRxPin','mbusTxPin','meterCount','mqttHost','mqttPort','mqttUser','mqttBaseTopic']) document.getElementById(k).value=cfg[k]??'';document.getElementById('mqttEnabled').value=cfg.mqttEnabled?'1':'0';document.querySelectorAll('.bt').forEach(e=>e.textContent=cfg.mqttBaseTopic||'bascloud/mbus');renderMeters();}
function renderMeters(){let n=Math.max(1,Math.min(250,+document.getElementById('meterCount').value||1));document.getElementById('meterCount').value=n;
while(cfg.meters.length<n){let i=cfg.meters.length;cfg.meters.push({enabled:true,name:`Meter ${i+1}`,primaryAddress:i+1,secondaryAddress:10000001+i,manufacturer:'BAS',version:1,medium:7,value:0,unit:'m3',resolutionExp:-3});}cfg.meters=cfg.meters.slice(0,n);
let h='';cfg.meters.forEach((m,i)=>{h+=`<div class="meter"><h3>Zähler ${i+1}</h3><div class="grid">
<label>Aktiv<select id="en${i}"><option value="1" ${m.enabled?'selected':''}>Ja</option><option value="0" ${!m.enabled?'selected':''}>Nein</option></select></label>
<label>Name<input id="name${i}" value="${esc(m.name)}"></label><label>Primäradresse<input id="pa${i}" type="number" min="1" max="250" value="${m.primaryAddress}"></label>
<label>Sekundäradresse<input id="sa${i}" type="number" min="0" max="99999999" value="${m.secondaryAddress}"></label><label>Manufacturer (3 Zeichen)<input id="man${i}" maxlength="3" value="${esc(m.manufacturer)}"></label>
<label>Version<input id="ver${i}" type="number" min="0" max="255" value="${m.version}"></label><label>Medium<select id="med${i}">${media.map(x=>`<option value="${x[0]}" ${+m.medium===+x[0]?'selected':''}>${x[1]}</option>`).join('')}</select></label>
<label>Zählerstand<input id="val${i}" type="number" min="0" value="${m.value}" oninput="this.dataset.edited=1;this.classList.add('edited')"><span id="hint${i}" class="muted small"></span></label><label>Einheit<select id="unit${i}" onchange="upd(${i})"><option value="m3" ${m.unit==='m3'?'selected':''}>m³</option><option value="kWh" ${m.unit==='kWh'?'selected':''}>kWh</option></select></label>
<label>Auflösung<select id="res${i}" onchange="upd(${i})">${RES.map(e=>`<option value="${e}" ${e===resOf(m)?'selected':''}>${fmt(Math.pow(10,e),e)}</option>`).join('')}</select></label>
</div></div>`});document.getElementById('meters').innerHTML=h;cfg.meters.forEach((m,i)=>upd(i));}
const RES=[-3,-2,-1,0,1],RAW_MAX=4294967295;
function resOf(m){return m.resolutionExp??(m.unit==='kWh'?0:-3);}
function fmt(x,e){let d=Math.max(0,-e);return x.toLocaleString('de-DE',{minimumFractionDigits:d,maximumFractionDigits:d});}
function unitName(u){return u==='m3'?'m³':u;}
function maxOf(e){return e<=0?RAW_MAX/Math.pow(10,-e):RAW_MAX*Math.pow(10,e);}
function rawOf(v,e){return Math.round(e<=0?v*Math.pow(10,-e):v/Math.pow(10,e));}
function upd(i){let e=+document.getElementById('res'+i).value,u=unitName(document.getElementById('unit'+i).value),inp=document.getElementById('val'+i);
inp.step=e<0?Math.pow(10,e).toFixed(-e):'any';inp.max=maxOf(e);
document.getElementById('hint'+i).textContent=`max. ${fmt(maxOf(e),e)} ${u}`;
for(const o of document.getElementById('res'+i).options)o.textContent=`${fmt(Math.pow(10,+o.value),+o.value)} ${u}`;}
function esc(s){return String(s||'').replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
function gather(){let n=+document.getElementById('meterCount').value;let meters=[];for(let i=0;i<n;i++)meters.push({enabled:document.getElementById('en'+i).value==='1',name:document.getElementById('name'+i).value,primaryAddress:+document.getElementById('pa'+i).value,secondaryAddress:+document.getElementById('sa'+i).value,manufacturer:document.getElementById('man'+i).value.toUpperCase(),version:+document.getElementById('ver'+i).value,medium:+document.getElementById('med'+i).value,value:+document.getElementById('val'+i).value,unit:document.getElementById('unit'+i).value,resolutionExp:+document.getElementById('res'+i).value});return {wifiSsid:document.getElementById('wifiSsid').value,wifiPassword:document.getElementById('wifiPassword').value,mbusBaud:+document.getElementById('mbusBaud').value,mbusStopBits:+document.getElementById('mbusStopBits').value,mbusByteGapMs:document.getElementById('mbusByteGapMs').value===''?10:+document.getElementById('mbusByteGapMs').value,mbusRxPin:+document.getElementById('mbusRxPin').value,mbusTxPin:+document.getElementById('mbusTxPin').value,meterCount:n,meters,mqttEnabled:document.getElementById('mqttEnabled').value==='1',mqttHost:document.getElementById('mqttHost').value,mqttPort:+document.getElementById('mqttPort').value||1883,mqttUser:document.getElementById('mqttUser').value,mqttPassword:document.getElementById('mqttPassword').value,mqttBaseTopic:document.getElementById('mqttBaseTopic').value};}
function check(c){for(let i=0;i<c.meters.length;i++){let m=c.meters[i],r=rawOf(m.value,m.resolutionExp);if(!(m.value>=0)||r>RAW_MAX)return `Zähler ${i+1}: Zählerstand muss zwischen 0 und ${fmt(maxOf(m.resolutionExp),m.resolutionExp)} ${unitName(m.unit)} liegen (Auflösung erhöhen für größere Werte).`;}return '';}
async function save(){let c=gather(),err=check(c),msg=document.getElementById('msg');if(err){msg.className='small bad';msg.textContent=err;return;}let r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(c)});let t=await r.text();document.getElementById('msg').className=r.ok?'small ok':'small bad';document.getElementById('msg').textContent=t;if(r.ok)setTimeout(load,400);}
async function restart(){await fetch('/api/restart',{method:'POST'});document.getElementById('msg').textContent='Neustart ausgelöst...';}
async function stat(){try{let s=await (await fetch('/api/status')).json();document.getElementById('status').textContent=`IP: ${s.ip}\nModus: ${s.wifiMode}\nRX Frames: ${s.rxFrames}\nTX Frames: ${s.txFrames}\nLetztes Ereignis: ${s.lastEvent}\nRX: ${s.lastRx}\nTX: ${s.lastTx}\nMQTT: ${s.mqtt}`;}catch(e){}}
async function refreshValues(){try{let list=await (await fetch('/api/values')).json();
for(let i=0;i<list.length;i++){let v=list[i],inp=document.getElementById('val'+i);if(!inp||inp.dataset.edited||document.activeElement===inp)continue;
if(cfg.meters[i])cfg.meters[i].value=v;if(+inp.value!==v){inp.value=v;inp.classList.add('live');setTimeout(()=>inp.classList.remove('live'),800);}}}catch(e){}}
let refreshTimer=null;
function setRefresh(sec){clearInterval(refreshTimer);refreshTimer=sec>0?setInterval(refreshValues,sec*1000):null;try{localStorage.setItem('refreshSec',sec);}catch(e){}}
function initRefresh(){let sec=2;try{let s=localStorage.getItem('refreshSec');if(s!==null&&[...document.getElementById('refresh').options].some(o=>o.value===s))sec=+s;}catch(e){}
document.getElementById('refresh').value=sec;setRefresh(sec);}
load();stat();setInterval(stat,1500);initRefresh();
</script></body></html>
)HTML";
