#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <ArduinoJson.h>
#include "ConfigManager.h"
#include "MBusSlave.h"
#include "MqttBridge.h"
#include "WebUi.h"
#include "ApiDocs.h"

ConfigManager configManager;
WebServer server(80);
HardwareSerial MBusSerial(2);
MBusSlave mbus(MBusSerial);
MqttBridge mqtt;
String wifiModeText = "AP";

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

void sendJson(int code, JsonDocument &doc) {
  String out; serializeJson(doc, out);
  server.send(code, "application/json", out);
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

  // GET /api/meters -> all meters
  server.on("/api/meters", HTTP_GET, []() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i = 0; i < configManager.config().meterCount; ++i) configManager.meterToJson(i, arr.add<JsonObject>());
    sendJson(200, doc);
  });

  // PUT/POST /api/meters with [{"index":1,"value":1.5},{"primaryAddress":7,"value":2}]
  // or {"meters":[...]}. All items are validated first, then applied together.
  auto setMany = []() {
    JsonDocument body;
    if (deserializeJson(body, server.arg("plain"))) return sendJsonError(400, "Ungültiges JSON");
    JsonArray items = body.is<JsonArray>() ? body.as<JsonArray>() : body["meters"].as<JsonArray>();
    if (items.isNull() || items.size() == 0) return sendJsonError(400, "Liste von Zählern erwartet");

    std::vector<std::pair<int, double>> updates;
    for (size_t k = 0; k < items.size(); ++k) {
      JsonObject item = items[k].as<JsonObject>();
      int idx = meterIndexFromItem(item);
      if (idx < 0) return sendJsonError(400, "Eintrag " + String(k + 1) + ": unbekannter Zähler (index oder primaryAddress)");
      if (!item["value"].is<double>()) return sendJsonError(400, "Eintrag " + String(k + 1) + ": 'value' fehlt");
      double v = item["value"].as<double>();
      String error;
      if (!ConfigManager::checkValue(configManager.config().meters[idx], v, error))
        return sendJsonError(400, "Zähler " + String(idx + 1) + ": " + error);
      updates.emplace_back(idx, v);
    }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (auto &u : updates) {
      String error;
      configManager.setMeterValue(u.first, u.second, error);
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

  // PUT/POST /api/meters/<n> with {"value":123.456}
  auto setOne = []() {
    int idx = meterIndexFromPath();
    if (idx < 0) return sendJsonError(404, "Unbekannter Zähler");
    JsonDocument body;
    if (deserializeJson(body, server.arg("plain")) || !body["value"].is<double>())
      return sendJsonError(400, "JSON mit 'value' erwartet");
    String error;
    if (!configManager.setMeterValue(idx, body["value"].as<double>(), error)) return sendJsonError(400, error);
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
    server.send(200, "application/json", configManager.toJson(false));
  });

  server.on("/api/config", HTTP_POST, []() {
    String body = server.arg("plain");
    if (body.isEmpty()) {
      server.send(400, "text/plain", "Leere Konfiguration");
      return;
    }

    // Preserve existing passwords if the UI leaves them blank.
    String oldPass = configManager.config().wifiPassword;
    String oldMqttPass = configManager.config().mqttPassword;
    String error;
    if (!configManager.fromJson(body, error)) {
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
    server.send(200, "text/plain", "Gespeichert. Änderungen an WLAN/UART werden nach Neustart aktiv.");
  });

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
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/restart", HTTP_POST, []() {
    server.send(200, "text/plain", "Restarting");
    delay(100);
    ESP.restart();
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
    Serial.println("NVS initialization failed");
  }

  startWifi();
  setupWeb();
  mbus.begin(&configManager.config());
  mqtt.begin(&configManager);

  Serial.printf("M-Bus UART: RX=%d TX=%d Baud=%lu 8E1\n",
                configManager.config().mbusRxPin,
                configManager.config().mbusTxPin,
                static_cast<unsigned long>(configManager.config().mbusBaud));
}

void loop() {
  server.handleClient();
  mbus.loop();
  mqtt.loop();
  configManager.loop();
  delay(1);
}
