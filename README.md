# ESP32 M-Bus Virtual Slave / Virtual Meter

Version 0.1 – ESP32 + TSS721

Ein ESP32 emuliert mehrere virtuelle M-Bus-Zähler hinter einem einzelnen TSS721. Jeder virtuelle Zähler besitzt eine eigene Primäradresse und antwortet auf `SND_NKE` und `REQ_UD2`.

## Funktionen

- bis zu 32 virtuelle M-Bus-Zähler
- Primäradresse 1–250 pro Zähler
- eigene 8-stellige Sekundär-ID pro Zähler
- Manufacturer-ID mit 3 Buchstaben, Standard `BAS`
- Medium pro Zähler (u. a. Wasser, Gas, Strom, Wärme)
- manueller Zählerwert
- Einheit m³ oder kWh
- einstellbare Auflösung pro Zähler (0,001 … 10)
- Zählerstände per MQTT setzen
- REST-API zum Abfragen und Setzen der Zählerstände
- Weboberfläche zur Konfiguration
- Konfiguration persistent in ESP32 NVS/Flash
- WLAN-Client; bei fehlender/fehlerhafter WLAN-Konfiguration startet ein Access Point
- M-Bus-Monitor im Browser mit letztem RX-/TX-Telegramm
- M-Bus UART standardmäßig 2400 Baud, 8E1

## Wichtiger Hinweis

Der TSS721 ist nur der physikalische M-Bus-Transceiver. Diese Firmware implementiert die Slave-Logik im ESP32.

Version 0.1 implementiert absichtlich nur die minimale Kommunikation:

- `SND_NKE -> E5`
- `REQ_UD2 -> RSP_UD`

Sekundäradress-Selektion (`SND_UD`, CI 0x52/0x56), Schreiben von Primäradressen, FCB-Zwischenspeicherung und weitere M-Bus-Datensätze sind noch nicht enthalten.

## Anschluss ESP32 <-> TSS721

Die exakten Pin-Namen hängen von deiner TSS721-Platine ab. Logisch gilt:

- TSS721 UART TX -> ESP32 RX (Standard GPIO 16)
- ESP32 TX (Standard GPIO 17) -> TSS721 UART RX
- GND gemeinsam

**Vor dem Anschluss die Logikpegel deiner konkreten TSS721-Platine prüfen.** Der nackte TSS721 und fertige Module können sich in Beschaltung und Pegeln unterscheiden.

Die UART-Pins können in der Weboberfläche geändert werden.

## PlatformIO

Standard-Board:

```ini
board = esp32dev
```

Falls du ein anderes ESP32-Board nutzt, ändere `board` in `platformio.ini`.

Kompilieren:

```bash
pio run
```

Flashen:

```bash
pio run -t upload
```

Serieller Monitor:

```bash
pio device monitor
```

## Erster Start

Wenn keine funktionierende WLAN-Konfiguration vorhanden ist, erzeugt der ESP32 einen Access Point:

```text
BAScloud-MBus-XXXX
```

Danach im Browser öffnen:

```text
http://192.168.4.1
```

SSID/Passwort und die virtuellen Zähler konfigurieren, speichern und den ESP32 neu starten.

## M-Bus Beispiel

Virtueller Zähler mit Primäradresse 5.

Master sendet `REQ_UD2`:

```text
10 5B 05 60 16
```

Der ESP32 erkennt Adresse 5 und antwortet über den TSS721 mit einem Long Frame:

```text
68 L L 68 08 05 72 ... CS 16
```

Für `SND_NKE` an eine konfigurierte Primäradresse antwortet der ESP32 mit:

```text
E5
```

## Datenkodierung

Der RSP_UD verwendet `CI=0x72` (variable data structure, Mode 1). Jeder virtuelle Zähler sendet genau einen 32-Bit-Integer-Datensatz (DIF `0x04`).

Der Zählerstand wird als vorzeichenlose 32-Bit-Ganzzahl übertragen (0 … 4.294.967.295 Schritte). Die **Auflösung** legt fest, welchem Wert ein Schritt entspricht. Je feiner die Auflösung, desto kleiner der maximale Zählerstand:

| Auflösung | VIF m³ | VIF kWh | Max. Zählerstand |
|---|---|---|---|
| 0,001 | `0x13` | `0x03` | 4.294.967,295 |
| 0,01 | `0x14` | `0x04` | 42.949.672,95 |
| 0,1 | `0x15` | `0x05` | 429.496.729,5 |
| 1 | `0x16` | `0x06` | 4.294.967.295 |
| 10 | `0x17` | `0x07` | 42.949.672.950 |

Der Wert wird auf die gewählte Auflösung gerundet. Zu große oder negative Werte lehnen Weboberfläche, REST-API und MQTT ab.
Standard ist 0,001 für m³ und 1 für kWh. Bestehende Konfigurationen behalten dieses Verhalten.

> Nach EN 13757-3 ist DIF `0x04` ein vorzeichenbehafteter Integer. Manche Master zeigen RAW-Werte über 2.147.483.647 deshalb negativ an.

## MQTT

MQTT wird in der Weboberfläche in der Karte **MQTT** eingerichtet:

| Feld | Standard | Beschreibung |
|---|---|---|
| MQTT aktiv | Nein | MQTT ein-/ausschalten |
| Broker | – | Hostname oder IP des Brokers |
| Port | 1883 | |
| Benutzer / Passwort | – | optional; leeres Passwort = unverändert |
| Basis-Topic | `bascloud/mbus` | Präfix für alle Topics |

Nach dem Speichern verbindet sich der ESP32 sofort neu, ein Neustart ist nicht nötig. Der Verbindungsstatus steht im M-Bus-Monitor. MQTT läuft nur, wenn der ESP32 mit einem WLAN verbunden ist, nicht im Access-Point-Modus.

### Topics

`<base>` ist das Basis-Topic, `<n>` die Zählernummer 1…32 wie in der Weboberfläche.

| Topic | Richtung | Inhalt |
|---|---|---|
| `<base>/meter/<n>/set` | an den ESP32 | `123.456` oder `{"value":123.456}` (Dezimalkomma wird akzeptiert) |
| `<base>/meter/<n>/state` | vom ESP32 | Zähler als JSON, retained |
| `<base>/error` | vom ESP32 | abgelehnte Werte mit Fehlermeldung |
| `<base>/status` | vom ESP32 | `online` / `offline` (retained, Last Will) |

Beispiel mit Mosquitto:

```bash
mosquitto_pub -h 192.168.1.10 -t bascloud/mbus/meter/1/set -m 123.456
mosquitto_sub -h 192.168.1.10 -t 'bascloud/mbus/#' -v
```

Beispiel für eine `state`-Nachricht:

```json
{"index":1,"enabled":true,"name":"Meter 1","primaryAddress":1,"secondaryAddress":10000001,
 "medium":7,"value":123.456,"unit":"m3","resolution":0.001,"maxValue":4294967.295}
```

Der MQTT-Client (esp-mqtt aus dem ESP-IDF) läuft in einer eigenen Task. Ist der Broker nicht erreichbar, beantwortet der ESP32 M-Bus-Abfragen trotzdem ohne Verzögerung.

**Hinweis zu Retain:** Wird `set` mit Retain veröffentlicht, setzt der Broker diesen Wert nach jedem Neuverbinden erneut und überschreibt damit z. B. einen neueren Wert aus der REST-API.

## REST-API

Eine ausführliche API-Dokumentation liefert das Gerät selbst unter `http://<ip>/api/docs`. Dort lassen sich alle Aufrufe auch direkt im Browser ausprobieren. Die OpenAPI-3-Spezifikation (z. B. für Postman oder Swagger) gibt es unter `http://<ip>/api/openapi.json`.

Alle Antworten sind JSON. Ein Zähler wird genauso dargestellt wie in der MQTT-`state`-Nachricht. POST funktioniert überall wie PUT.

| Methode | Pfad | Body | Beschreibung |
|---|---|---|---|
| GET | `/api/meters` | – | alle Zähler |
| GET | `/api/meters/<n>` | – | ein Zähler |
| PUT | `/api/meters/<n>` | `{"value":123.456}` | Zählerstand setzen |
| PUT | `/api/meters` | `[{"index":1,"value":1.5},{"primaryAddress":7,"value":2}]` oder `{"meters":[...]}` | mehrere Zähler setzen |

Beim Setzen mehrerer Zähler kann jeder Eintrag über `index` (Zählernummer) oder `primaryAddress` angesprochen werden. Zuerst werden alle Einträge geprüft. Ist einer ungültig, wird nichts übernommen.

Fehler werden als `400` bzw. `404` mit `{"error":"..."}` beantwortet.

Beispiele:

```bash
curl http://<ip>/api/meters
curl http://<ip>/api/meters/1
curl -X PUT http://<ip>/api/meters/1 -d '{"value":42949671}'
curl -X PUT http://<ip>/api/meters -d '[{"index":1,"value":10},{"primaryAddress":5,"value":20.5}]'
```

Jede Änderung per REST wird zusätzlich als MQTT-`state` veröffentlicht.

**Sicherheit:** Die REST-API hat wie die Weboberfläche keine Authentifizierung. Jeder im Netz kann Werte setzen.

## Speicherung der Zählerstände

Werte, die per MQTT oder REST gesetzt werden, sind sofort per M-Bus abrufbar. Im Flash gespeichert werden sie verzögert: nach 10 s ohne weitere Änderung, spätestens 60 s nach der ersten Änderung. Die Werte liegen in einem eigenen kleinen NVS-Block, getrennt von der Konfiguration. Das schont den Flash auch bei häufigen Updates.

Die Weboberfläche lädt die Zählerstände automatisch neu, sodass Änderungen per MQTT oder REST direkt sichtbar sind. Ein Feld, das gerade bearbeitet wird, wird dabei nicht überschrieben und bleibt gelb markiert, bis gespeichert wird. Das Intervall wird oben auf der Seite unter der Überschrift eingestellt (Aus, 1 … 60 s, Standard 2 s) und im Browser gespeichert.

**Hinweis:** „Speichern“ in der Weboberfläche überträgt alle angezeigten Zählerstände. Ein gelb markiertes (bearbeitetes) Feld überschreibt dabei einen Wert, der zwischenzeitlich per MQTT oder REST gesetzt wurde.

## Broadcast

Auf M-Bus Test-/Broadcast-Adressen `0xFE` und `0xFF` antwortet v0.1 absichtlich nicht. Da ein physischer ESP32 mehrere virtuelle Slaves repräsentiert, würde eine einzige Antwort das Verhalten mehrerer Teilnehmer nicht korrekt abbilden und könnte bei echten weiteren Slaves zu Kollisionen führen.

## Geplante Erweiterungen

- Modbus TCP/RTU
- mehrere Datenpunkte pro Zähler
- weitere Einheiten und VIFs
- Authentifizierung für Weboberfläche und REST-API
- Secondary Address Selection
- Änderung der Primäradresse über M-Bus
- Import/Export der Konfiguration als JSON
