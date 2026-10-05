#include "ApiDocs.h"

const char API_DOCS_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>API-Dokumentation · BAScloud M-Bus Virtual Meter</title><style>
body{font-family:system-ui,sans-serif;background:#111827;color:#e5e7eb;margin:0}.wrap{max-width:1100px;margin:auto;padding:20px}
h1{margin:0 0 6px}h3{margin:0 0 8px}.muted{color:#9ca3af}a{color:#93c5fd}.card{background:#1f2937;border:1px solid #374151;border-radius:12px;padding:16px;margin:14px 0}
code,pre,textarea{font-family:ui-monospace,monospace;font-size:13px}code{background:#0b1220;padding:1px 5px;border-radius:4px}
pre{background:#0b1220;padding:10px;border-radius:8px;overflow-x:auto;white-space:pre-wrap;margin:8px 0}
table{width:100%;border-collapse:collapse;font-size:14px}th,td{text-align:left;padding:7px 8px;border-bottom:1px solid #374151;vertical-align:top}th{color:#cbd5e1}
.ep{border-top:1px solid #4b5563;padding-top:14px;margin-top:14px}.m{display:inline-block;min-width:44px;text-align:center;font-weight:700;font-size:12px;padding:3px 6px;border-radius:5px;margin-right:6px}
.get{background:#065f46;color:#d1fae5}.put{background:#92400e;color:#fef3c7}
input,textarea{box-sizing:border-box;padding:8px;border-radius:7px;border:1px solid #4b5563;background:#111827;color:#fff}textarea{width:100%;min-height:70px}
button{border:0;border-radius:8px;padding:8px 12px;font-weight:700;cursor:pointer;background:#3b82f6;color:#fff}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0}
.res{display:none}.ok{color:#86efac}.bad{color:#fca5a5}.small{font-size:13px}
</style></head><body><div class="wrap">
<h1>API-Dokumentation</h1><div class="muted">BAScloud M-Bus Virtual Meter · <a href="/">zur Konfiguration</a> · <a href="/api/openapi.json">OpenAPI-Spezifikation (JSON)</a></div>

<div class="card"><h2>Überblick</h2><div class="small">
Zählerstände können per <b>REST-API</b> oder <b>MQTT</b> gesetzt werden und sind sofort per M-Bus abrufbar.
Gespeichert im Flash werden sie nach 10 s ohne weitere Änderung, spätestens nach 60 s.<br><br>
<b>Zählernummer <code>n</code></b> = Nummer des Zählers in der Weboberfläche (1 … Anzahl Zähler), nicht die M-Bus-Primäradresse.<br>
<b>Wertebereich:</b> 0 … <code>maxValue</code>. Der Maximalwert hängt von der Auflösung des Zählers ab (32 Bit, z. B. 4.294.967,295 m³ bei 0,001). Der Wert wird auf die Auflösung gerundet.<br>
<b>Authentifizierung:</b> keine – jeder im Netz kann Werte setzen.
</div></div>

<div class="card"><h2>Zähler-Objekt</h2><div class="small">So wird ein Zähler in allen REST-Antworten und in der MQTT-<code>state</code>-Nachricht dargestellt:</div>
<table><tr><th>Feld</th><th>Typ</th><th>Beschreibung</th></tr>
<tr><td><code>index</code></td><td>int</td><td>Zählernummer n (1 …)</td></tr>
<tr><td><code>enabled</code></td><td>bool</td><td>Zähler antwortet auf M-Bus</td></tr>
<tr><td><code>name</code></td><td>string</td><td>Name aus der Konfiguration</td></tr>
<tr><td><code>primaryAddress</code></td><td>int</td><td>M-Bus-Primäradresse 1 … 250</td></tr>
<tr><td><code>secondaryAddress</code></td><td>int</td><td>8-stellige Sekundär-ID</td></tr>
<tr><td><code>medium</code></td><td>int</td><td>M-Bus-Medium (2 Strom, 3 Gas, 4 Wärme, 6 Warmwasser, 7 Wasser, 22 Kaltwasser)</td></tr>
<tr><td><code>value</code></td><td>number</td><td>aktueller Zählerstand</td></tr>
<tr><td><code>unit</code></td><td>string</td><td><code>m3</code> oder <code>kWh</code></td></tr>
<tr><td><code>resolution</code></td><td>number</td><td>Auflösung (0.001 … 10)</td></tr>
<tr><td><code>maxValue</code></td><td>number</td><td>größter zulässiger Wert bei dieser Auflösung</td></tr></table>
<pre>{"index":1,"enabled":true,"name":"Meter 1","primaryAddress":1,"secondaryAddress":10000001,
 "medium":7,"value":123.456,"unit":"m3","resolution":0.001,"maxValue":4294967.295}</pre></div>

<div class="card"><h2>REST-API</h2><div class="small">Alle Antworten sind JSON. <code>POST</code> funktioniert überall wie <code>PUT</code>.
Fehler: <code>400</code> (ungültige Anfrage/Wert) bzw. <code>404</code> (unbekannter Zähler) mit <code>{"error":"…"}</code>.
Mit den Knöpfen kannst du die Aufrufe direkt gegen dieses Gerät ausprobieren.</div>

<div class="ep"><h3><span class="m get">GET</span><code>/api/meters</code></h3><div class="small">Liefert alle Zähler als Array.</div>
<pre>curl http://<span class="host"></span>/api/meters</pre>
<div class="row"><button onclick="call('GET','/api/meters',null,'r1')">Ausprobieren</button></div><div id="r1" class="res"></div></div>

<div class="ep"><h3><span class="m get">GET</span><code>/api/meters/&lt;n&gt;</code></h3><div class="small">Liefert einen Zähler.</div>
<pre>curl http://<span class="host"></span>/api/meters/1</pre>
<div class="row">n <input id="n2" type="number" min="1" value="1" style="width:80px"><button onclick="call('GET','/api/meters/'+v('n2'),null,'r2')">Ausprobieren</button></div><div id="r2" class="res"></div></div>

<div class="ep"><h3><span class="m put">PUT</span><code>/api/meters/&lt;n&gt;</code></h3><div class="small">Setzt den Zählerstand eines Zählers. Antwort: der geänderte Zähler.</div>
<pre>curl -X PUT http://<span class="host"></span>/api/meters/1 -d '{"value":123.456}'</pre>
<div class="row">n <input id="n3" type="number" min="1" value="1" style="width:80px"></div>
<textarea id="b3">{"value":123.456}</textarea>
<div class="row"><button onclick="call('PUT','/api/meters/'+v('n3'),v('b3'),'r3')">Ausprobieren</button></div><div id="r3" class="res"></div></div>

<div class="ep"><h3><span class="m put">PUT</span><code>/api/meters</code></h3><div class="small">Setzt mehrere Zähler auf einmal. Jeder Eintrag wird über <code>index</code> (Zählernummer) oder <code>primaryAddress</code> angesprochen.
Body: Array oder <code>{"meters":[…]}</code>. Zuerst werden alle Einträge geprüft – ist einer ungültig, wird nichts übernommen. Antwort: die geänderten Zähler.</div>
<pre>curl -X PUT http://<span class="host"></span>/api/meters -d '[{"index":1,"value":10},{"primaryAddress":5,"value":20.5}]'</pre>
<textarea id="b4">[{"index":1,"value":10},{"index":2,"value":20.5}]</textarea>
<div class="row"><button onclick="call('PUT','/api/meters',v('b4'),'r4')">Ausprobieren</button></div><div id="r4" class="res"></div></div>

<div class="ep"><h3><span class="m get">GET</span><code>/api/status</code></h3><div class="small">Gerätestatus: IP, WLAN-Modus, M-Bus-Zähler und letzte Telegramme, MQTT-Status.</div>
<div class="row"><button onclick="call('GET','/api/status',null,'r5')">Ausprobieren</button></div><div id="r5" class="res"></div></div>
</div>

<div class="card"><h2>MQTT</h2><div class="small">Broker und Basis-Topic werden in der <a href="/">Weboberfläche</a> eingestellt.
Aktuelles Basis-Topic: <code class="bt"></code> · Status: <span id="mq">…</span></div>
<table><tr><th>Topic</th><th>Richtung</th><th>Inhalt</th></tr>
<tr><td><code><span class="bt"></span>/meter/&lt;n&gt;/set</code></td><td>an das Gerät</td><td>Zählerstand setzen: <code>123.456</code> oder <code>{"value":123.456}</code>, Dezimalkomma wird akzeptiert</td></tr>
<tr><td><code><span class="bt"></span>/meter/&lt;n&gt;/state</code></td><td>vom Gerät</td><td>Zähler-Objekt als JSON, retained. Wird nach jeder Änderung (MQTT oder REST) und beim Verbinden gesendet.</td></tr>
<tr><td><code><span class="bt"></span>/error</code></td><td>vom Gerät</td><td>abgelehnte Werte: <code>{"index":1,"payload":"abc","error":"…"}</code></td></tr>
<tr><td><code><span class="bt"></span>/status</code></td><td>vom Gerät</td><td><code>online</code> / <code>offline</code>, retained, Last Will</td></tr></table>
<pre>mosquitto_pub -h &lt;broker&gt; -t <span class="bt"></span>/meter/1/set -m 123.456
mosquitto_sub -h &lt;broker&gt; -t '<span class="bt"></span>/#' -v</pre>
<div class="small muted">Hinweis: Wird <code>set</code> mit Retain veröffentlicht, setzt der Broker diesen Wert nach jedem Neuverbinden erneut und überschreibt ggf. neuere Werte aus der REST-API.</div></div>
</div><script>
function v(id){return document.getElementById(id).value;}
async function call(method,path,body,out){let el=document.getElementById(out);el.style.display='block';el.innerHTML='<pre>…</pre>';
try{let r=await fetch(path,{method,headers:body?{'Content-Type':'application/json'}:{},body:body||undefined});let t=await r.text();
try{t=JSON.stringify(JSON.parse(t),null,2);}catch(e){}
el.innerHTML=`<div class="small ${r.ok?'ok':'bad'}">${method} ${esc(path)} → HTTP ${r.status}</div><pre>${esc(t)}</pre>`;}
catch(e){el.innerHTML=`<pre class="bad">${esc(String(e))}</pre>`;}}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
document.querySelectorAll('.host').forEach(e=>e.textContent=location.host);
fetch('/api/config').then(r=>r.json()).then(c=>document.querySelectorAll('.bt').forEach(e=>e.textContent=c.mqttBaseTopic||'bascloud/mbus')).catch(()=>{});
fetch('/api/status').then(r=>r.json()).then(s=>document.getElementById('mq').textContent=s.mqtt).catch(()=>{});
</script></body></html>
)HTML";

const char OPENAPI_JSON[] PROGMEM = R"JSON({
"openapi":"3.0.3",
"info":{"title":"BAScloud M-Bus Virtual Meter API","version":"0.1","description":"Zählerstände der virtuellen M-Bus-Zähler abfragen und setzen. n = Zählernummer aus der Weboberfläche (1 ... Anzahl Zähler)."},
"paths":{
"/api/meters":{
"get":{"summary":"Alle Zähler","responses":{"200":{"description":"Liste der Zähler","content":{"application/json":{"schema":{"type":"array","items":{"$ref":"#/components/schemas/Meter"}}}}}}},
"put":{"summary":"Mehrere Zählerstände setzen","description":"Alle Einträge werden zuerst geprüft; ist einer ungültig, wird nichts übernommen.",
"requestBody":{"required":true,"content":{"application/json":{"schema":{"oneOf":[{"type":"array","items":{"$ref":"#/components/schemas/BulkItem"}},{"type":"object","properties":{"meters":{"type":"array","items":{"$ref":"#/components/schemas/BulkItem"}}},"required":["meters"]}]},
"example":[{"index":1,"value":10},{"primaryAddress":5,"value":20.5}]}}},
"responses":{"200":{"description":"Geänderte Zähler","content":{"application/json":{"schema":{"type":"array","items":{"$ref":"#/components/schemas/Meter"}}}}},"400":{"$ref":"#/components/responses/Error"}}}},
"/api/meters/{n}":{
"parameters":[{"name":"n","in":"path","required":true,"description":"Zählernummer (1 ...)","schema":{"type":"integer","minimum":1,"maximum":250}}],
"get":{"summary":"Ein Zähler","responses":{"200":{"description":"Zähler","content":{"application/json":{"schema":{"$ref":"#/components/schemas/Meter"}}}},"404":{"$ref":"#/components/responses/Error"}}},
"put":{"summary":"Zählerstand setzen","requestBody":{"required":true,"content":{"application/json":{"schema":{"type":"object","properties":{"value":{"type":"number","minimum":0}},"required":["value"]},"example":{"value":123.456}}}},
"responses":{"200":{"description":"Geänderter Zähler","content":{"application/json":{"schema":{"$ref":"#/components/schemas/Meter"}}}},"400":{"$ref":"#/components/responses/Error"},"404":{"$ref":"#/components/responses/Error"}}}},
"/api/status":{
"get":{"summary":"Gerätestatus","responses":{"200":{"description":"Status","content":{"application/json":{"schema":{"type":"object","properties":{"ip":{"type":"string"},"wifiMode":{"type":"string"},"rxFrames":{"type":"integer"},"txFrames":{"type":"integer"},"lastEvent":{"type":"string"},"lastRx":{"type":"string"},"lastTx":{"type":"string"},"mqtt":{"type":"string"}}}}}}}}}
},
"components":{
"schemas":{
"Meter":{"type":"object","properties":{
"index":{"type":"integer","description":"Zählernummer"},
"enabled":{"type":"boolean"},
"name":{"type":"string"},
"primaryAddress":{"type":"integer","minimum":1,"maximum":250},
"secondaryAddress":{"type":"integer"},
"medium":{"type":"integer","description":"2 Strom, 3 Gas, 4 Wärme, 6 Warmwasser, 7 Wasser, 22 Kaltwasser"},
"value":{"type":"number"},
"unit":{"type":"string","enum":["m3","kWh"]},
"resolution":{"type":"number","description":"0.001 ... 10"},
"maxValue":{"type":"number","description":"Größter zulässiger Wert bei dieser Auflösung"}}},
"BulkItem":{"type":"object","description":"index oder primaryAddress angeben","properties":{"index":{"type":"integer","minimum":1},"primaryAddress":{"type":"integer","minimum":1,"maximum":250},"value":{"type":"number","minimum":0}},"required":["value"]},
"Error":{"type":"object","properties":{"error":{"type":"string"}}}},
"responses":{"Error":{"description":"Fehler","content":{"application/json":{"schema":{"$ref":"#/components/schemas/Error"}}}}}}
})JSON";
