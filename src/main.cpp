#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include "ConfigManager.h"
#include "MBusSlave.h"
#include "MqttBridge.h"
#include "Bme280Sensor.h"
#include "PulseCounter.h"
#include "WebUi.h"
#include "ApiDocs.h"

ConfigManager configManager;
WebServer server(80);
HardwareSerial MBusSerial(2);
MBusSlave mbus(MBusSerial);
MqttBridge mqtt;
Bme280Sensor bme280;
PulseCounter pulses;
String wifiModeText = "AP";

// POST /api/config bodies (~45 KB for 250 meters) are streamed here instead of into RAM:
// WebServer's "plain" argument needs two large contiguous heap blocks and fails above ~40 KB.
static const char *CONFIG_UPLOAD = "/config.upload";
File configUpload;
bool configUploadOk = false;

void startWifi() {
  AppConfig &cfg = configManager.config();

  WiFi.mode(WIFI_STA);
  if (!cfg.wifiSsid.isEmpty()) {
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
    Serial.printf("Connecting to WiFi '%s'", cfg.wifiSsid.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
      delay(250); Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      wifiModeText = "STA";
      Serial.print("IP: "); Serial.println(WiFi.localIP());
      return;
    }
  }

  WiFi.mode(WIFI_AP);
  uint64_t chip = ESP.getEfuseMac();
  String ap = "BAScloud-MBus-" + String(static_cast<uint16_t>(chip), HEX);
  ap.toUpperCase();
  WiFi.softAP(ap.c_str());
  wifiModeText = "AP";
  Serial.printf("Config AP: %s\n", ap.c_str());
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
}

String currentIp() {
  return wifiModeText == "STA" ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
}

// Buffers serializer output and passes it to the WebServer in small pieces.
class ChunkWriter : public Print {
public:
  size_t write(uint8_t c) override {
    buf_[len_++] = c;
    if (len_ == sizeof(buf_)) flush();
    return 1;
  }
  size_t write(const uint8_t *data, size_t n) override {
    for (size_t i = 0; i < n; ++i) write(data[i]);
    return n;
  }
  void flush() override {
    if (len_) server.sendContent(reinterpret_cast<const char *>(buf_), len_);
    len_ = 0;
  }

private:
  uint8_t buf_[1024];
  size_t len_ = 0;
};

// Streams the document, so large responses (all meters, config) never need one big String.
void sendJson(int code, JsonDocument &doc) {
  server.setContentLength(measureJson(doc));
  server.send(code, "application/json", "");
  ChunkWriter out;
  serializeJson(doc, out);
  out.flush();
}

void sendJsonError(int code, const String &message) {
  JsonDocument doc;
  doc["error"] = message;
  sendJson(code, doc);
}

// REST meter index from the URL: 1..meterCount -> 0-based, -1 if invalid.
int meterIndexFromPath() {
  String s = server.pathArg(0);
  long n = s.toInt();
  if (String(n) != s || n < 1 || n > static_cast<long>(configManager.config().meterCount)) return -1;
  return static_cast<int>(n - 1);
}

// Resolves a bulk item by "index" (1-based) or "primaryAddress". Returns 0-based index or -1.
int meterIndexFromItem(JsonObject item) {
  const AppConfig &cfg = configManager.config();
  if (item["index"].is<int>()) {
    int n = item["index"].as<int>();
    return (n >= 1 && n <= static_cast<int>(cfg.meterCount)) ? n - 1 : -1;
  }
  if (item["primaryAddress"].is<int>()) {
    int pa = item["primaryAddress"].as<int>();
    for (size_t i = 0; i < cfg.meterCount; ++i) {
      if (cfg.meters[i].primaryAddress == pa) return static_cast<int>(i);
    }
  }
  return -1;
}

void setupRestApi() {
  server.on("/api/docs", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", API_DOCS_HTML);
  });
  server.on("/api/openapi.json", HTTP_GET, []() {
    server.send_P(200, "application/json", OPENAPI_JSON);
  });

  // GET /api/values -> [v1, [v2, flow, flowTemp, returnTemp], [v3, 1.8.1, ..., 2.8.2], ...]
  // (compact, used by the web UI's auto refresh). Heat and bidirectional electricity meters are
  // an array, all others a plain number.
  server.on("/api/values", HTTP_GET, []() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    const AppConfig &cfg = configManager.config();
    for (size_t i = 0; i < cfg.meterCount; ++i) {
      const VirtualMeter &m = cfg.meters[i];
      if (isHeatMeter(m)) {
        JsonArray h = arr.add<JsonArray>();
        h.add(m.value);
        h.add(m.flow);
        h.add(m.flowTemp);
        h.add(m.returnTemp);
      } else if (isBidirectionalMeter(m)) {
        JsonArray e = arr.add<JsonArray>();
        e.add(m.value);
        for (size_t r = 0; r < ENERGY_REGS; ++r) e.add(m.energy[r]);
      } else {
        arr.add(m.value);
      }
    }
    sendJson(200, doc);
  });

  // GET /api/meters -> all meters
  server.on("/api/meters", HTTP_GET, []() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i = 0; i < configManager.config().meterCount; ++i) configManager.meterToJson(i, arr.add<JsonObject>());
    sendJson(200, doc);
  });

  // PUT/POST /api/meters with [{"index":1,"value":1.5},{"primaryAddress":7,"value":2,"flowTemp":70}]
  // or {"meters":[...]}. All items are validated first, then applied together.
  auto setMany = []() {
    JsonDocument body;
    if (deserializeJson(body, server.arg("plain"))) return sendJsonError(400, "Ungültiges JSON");
    JsonArray items = body.is<JsonArray>() ? body.as<JsonArray>() : body["meters"].as<JsonArray>();
    if (items.isNull() || items.size() == 0) return sendJsonError(400, "Liste von Zählern erwartet");

    std::vector<std::pair<int, MeterUpdate>> updates;
    for (size_t k = 0; k < items.size(); ++k) {
      JsonObject item = items[k].as<JsonObject>();
      int idx = meterIndexFromItem(item);
      if (idx < 0) return sendJsonError(400, "Eintrag " + String(k + 1) + ": unbekannter Zähler (index oder primaryAddress)");
      MeterUpdate u;
      String error;
      if (!ConfigManager::parseUpdate(configManager.config().meters[idx], item, u, error))
        return sendJsonError(400, "Zähler " + String(idx + 1) + ": " + error);
      updates.emplace_back(idx, u);
    }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (auto &u : updates) {
      configManager.applyUpdate(u.first, u.second);
      mqtt.publishMeter(u.first);
      configManager.meterToJson(u.first, arr.add<JsonObject>());
    }
    sendJson(200, doc);
  };
  server.on("/api/meters", HTTP_PUT, setMany);
  server.on("/api/meters", HTTP_POST, setMany);

  // GET /api/meters/<n> -> one meter
  server.on(UriBraces("/api/meters/{}"), HTTP_GET, []() {
    int idx = meterIndexFromPath();
    if (idx < 0) return sendJsonError(404, "Unbekannter Zähler");
    JsonDocument doc;
    configManager.meterToJson(idx, doc.to<JsonObject>());
    sendJson(200, doc);
  });

  // PUT/POST /api/meters/<n> with {"value":123.456}; heat meters also flow/flowTemp/returnTemp,
  // bidirectional electricity meters "1.8.0" ... "2.8.2"
  auto setOne = []() {
    int idx = meterIndexFromPath();
    if (idx < 0) return sendJsonError(404, "Unbekannter Zähler");
    JsonDocument body;
    if (deserializeJson(body, server.arg("plain"))) return sendJsonError(400, "Ungültiges JSON");
    MeterUpdate u;
    String error;
    if (!ConfigManager::parseUpdate(configManager.config().meters[idx], body.as<JsonObjectConst>(), u, error))
      return sendJsonError(400, error);
    configManager.applyUpdate(idx, u);
    mqtt.publishMeter(idx);
    JsonDocument doc;
    configManager.meterToJson(idx, doc.to<JsonObject>());
    sendJson(200, doc);
  };
  server.on(UriBraces("/api/meters/{}"), HTTP_PUT, setOne);
  server.on(UriBraces("/api/meters/{}"), HTTP_POST, setOne);
}

void setupWeb() {
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });

  server.on("/api/config", HTTP_GET, []() {
    JsonDocument doc;
    configManager.buildJson(doc, false);
    sendJson(200, doc);
  });

  auto receiveConfig = []() {
    HTTPRaw &raw = server.raw();
    switch (raw.status) {
      case RAW_START:
        configUpload = LittleFS.open(CONFIG_UPLOAD, "w");
        configUploadOk = static_cast<bool>(configUpload);
        break;
      case RAW_WRITE:
        if (configUploadOk && configUpload.write(raw.buf, raw.currentSize) != raw.currentSize) configUploadOk = false;
        break;
      case RAW_END:
      case RAW_ABORTED:
        if (configUpload) configUpload.close();
        if (raw.status == RAW_ABORTED) configUploadOk = false;
        break;
    }
  };

  server.on("/api/config", HTTP_POST, []() {
    bool received = configUploadOk;
    configUploadOk = false;
    File body = LittleFS.open(CONFIG_UPLOAD, "r");
    if (!received || !body || body.size() == 0) {
      if (body) body.close();
      LittleFS.remove(CONFIG_UPLOAD);
      server.send(400, "text/plain", "Leere Konfiguration oder Empfang fehlgeschlagen");
      return;
    }

    // Preserve existing passwords if the UI leaves them blank.
    String oldPass = configManager.config().wifiPassword;
    String oldMqttPass = configManager.config().mqttPassword;
    String error;
    bool parsed = configManager.fromJson(body, error);
    body.close();
    LittleFS.remove(CONFIG_UPLOAD);
    if (!parsed) {
      server.send(400, "text/plain", "Fehler: " + error);
      return;
    }
    if (configManager.config().wifiPassword.isEmpty()) configManager.config().wifiPassword = oldPass;
    if (configManager.config().mqttPassword.isEmpty()) configManager.config().mqttPassword = oldMqttPass;

    if (!configManager.save()) {
      server.send(500, "text/plain", "Speichern im Flash fehlgeschlagen");
      return;
    }
    mqtt.restart();
    pulses.apply();
    server.send(200, "text/plain", "Gespeichert. Änderungen an WLAN, UART und BME280 (Aktivieren, I²C-Pins) werden nach Neustart aktiv.");
  }, receiveConfig);

  server.on("/api/status", HTTP_GET, []() {
    JsonDocument doc;
    doc["ip"] = currentIp();
    doc["wifiMode"] = wifiModeText;
    doc["rxFrames"] = mbus.rxFrames();
    doc["txFrames"] = mbus.txFrames();
    doc["lastEvent"] = mbus.lastEvent();
    doc["lastRx"] = mbus.lastRxHex();
    doc["lastTx"] = mbus.lastTxHex();
    doc["mqtt"] = mqtt.statusText();
    doc["sensor"] = bme280.statusText();
    JsonArray pa = doc["pulses"].to<JsonArray>();
    const AppConfig &cfg = configManager.config();
    for (const PulseInput &p : cfg.pulses) {
      JsonObject o = pa.add<JsonObject>();
      o["enabled"] = p.enabled;
      if (!p.enabled) continue;
      o["meter"] = p.meter;
      o["startValue"] = p.startValue;
      o["count"] = p.count;
      o["value"] = cfg.meters[p.meter - 1].value;
      o["unit"] = cfg.meters[p.meter - 1].unit;
    }
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/restart", HTTP_POST, []() {
    server.send(200, "text/plain", "Restarting");
    delay(100);
    ESP.restart();
  });

  // OTA: firmware.bin as multipart upload, written straight into the inactive app slot.
  server.on("/api/update", HTTP_POST, []() {
    bool ok = !Update.hasError() && Update.isFinished();
    server.send(ok ? 200 : 500, "text/plain",
                ok ? "Update erfolgreich, Neustart..." : String("Update fehlgeschlagen: ") + Update.errorString());
    if (!ok) return;
    delay(300);
    ESP.restart();
  }, []() {
    HTTPUpload &up = server.upload();
    switch (up.status) {
      case UPLOAD_FILE_START:
        Serial.printf("OTA: %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
        break;
      case UPLOAD_FILE_WRITE:
        if (!Update.hasError() && Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
        break;
      case UPLOAD_FILE_END:
        if (!Update.end(true)) Update.printError(Serial);
        else Serial.printf("OTA: %u Bytes geschrieben\n", static_cast<unsigned>(up.totalSize));
        break;
      case UPLOAD_FILE_ABORTED:
        Update.abort();
        Serial.println("OTA abgebrochen");
        break;
    }
  });

  setupRestApi();
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nBAScloud M-Bus Virtual Meter v0.1");

  if (!configManager.begin()) {
    Serial.println("Config storage initialization failed");
  }

  startWifi();
  setupWeb();
  bme280.begin(&configManager.config().sensor);
  mbus.begin(&configManager.config(), &bme280);
  mqtt.begin(&configManager);
  pulses.begin(&configManager, &mqtt);

  Serial.printf("M-Bus UART: RX=%d TX=%d Baud=%lu 8E%u\n",
                configManager.config().mbusRxPin,
                configManager.config().mbusTxPin,
                static_cast<unsigned long>(configManager.config().mbusBaud),
                configManager.config().mbusStopBits);
}

void loop() {
  server.handleClient();
  mbus.loop();
  bme280.loop();
  pulses.loop();
  mqtt.loop();
  configManager.loop();
  delay(1);
}
