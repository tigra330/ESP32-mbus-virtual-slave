#include "ConfigManager.h"
#include <LittleFS.h>
#include <math.h>
#include <memory>
#include <vector>

// The configuration (up to ~45 KB JSON for 250 meters) lives in LittleFS; NVS strings are
// limited to 4000 bytes. Older firmware kept it in NVS key "config", which is migrated once.
static const char *CONFIG_FILE = "/config.json";
static const char *CONFIG_TMP = "/config.tmp";

// Values set via REST/MQTT are written as a small blob instead of the whole JSON config.
// Saved after VALUE_SAVE_QUIET_MS without changes, at the latest VALUE_SAVE_MAX_MS after the first change.
static constexpr uint32_t VALUE_SAVE_QUIET_MS = 10000;
static constexpr uint32_t VALUE_SAVE_MAX_MS = 60000;

bool ConfigManager::begin() {
  if (!prefs_.begin("mbusvirt", false)) return false;
  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  return load();
}

void ConfigManager::factoryDefaults() {
  // AppConfig is too large for the stack.
  std::unique_ptr<AppConfig> defaults(new AppConfig());
  cfg_ = *defaults;
  cfg_.meterCount = 10;
  for (size_t i = 0; i < MAX_METERS; ++i) {
    cfg_.meters[i].enabled = i < cfg_.meterCount;
    cfg_.meters[i].name = "Meter " + String(i + 1);
    cfg_.meters[i].primaryAddress = static_cast<uint8_t>(i + 1);
    cfg_.meters[i].secondaryAddress = 10000001UL + i;
    cfg_.meters[i].manufacturer = "BAS";
    cfg_.meters[i].version = 1;
    cfg_.meters[i].medium = 0x07;
    cfg_.meters[i].value = 0.0;
    cfg_.meters[i].unit = "m3";
    cfg_.meters[i].resolutionExp = -3;
  }
}

bool ConfigManager::load() {
  String error;
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (f) {
    bool ok = fromJson(f, error);
    f.close();
    if (ok) {
      loadValues();
      return true;
    }
    Serial.println("Config invalid, loading defaults: " + error);
    factoryDefaults();
    return save();
  }

  String legacy = prefs_.getString("config", "");
  if (!legacy.isEmpty()) {
    bool ok = fromJson(legacy, error);
    if (ok) loadValues();
    else {
      Serial.println("Config invalid, loading defaults: " + error);
      factoryDefaults();
    }
    if (!save()) return false;
    prefs_.remove("config");
    Serial.println("Config migrated from NVS to LittleFS");
    return true;
  }

  factoryDefaults();
  return save();
}

bool ConfigManager::save() {
  bool ok = false;
  {
    JsonDocument doc;
    buildJson(doc, true);
    File f = LittleFS.open(CONFIG_TMP, "w");
    if (f) {
      ok = serializeJson(doc, f) > 0;
      f.close();
    }
  }
  // Write to a temp file first so a power loss never leaves a truncated config behind.
  if (ok && !LittleFS.rename(CONFIG_TMP, CONFIG_FILE)) {
    LittleFS.remove(CONFIG_FILE);
    ok = LittleFS.rename(CONFIG_TMP, CONFIG_FILE);
  }
  return saveValues() && ok;
}

bool ConfigManager::saveValues() {
  std::vector<double> values(MAX_METERS);
  for (size_t i = 0; i < MAX_METERS; ++i) values[i] = cfg_.meters[i].value;
  valuesDirty_ = false;
  const size_t bytes = values.size() * sizeof(double);
  return prefs_.putBytes("values", values.data(), bytes) == bytes;
}

void ConfigManager::loadValues() {
  // Older firmware stored fewer meters; take whatever is there.
  size_t len = prefs_.getBytesLength("values");
  if (len == 0 || len % sizeof(double) != 0 || len > MAX_METERS * sizeof(double)) return;
  std::vector<double> values(len / sizeof(double));
  if (prefs_.getBytes("values", values.data(), len) != len) return;
  for (size_t i = 0; i < values.size(); ++i) {
    if (isfinite(values[i]) && values[i] >= 0) cfg_.meters[i].value = values[i];
  }
}

void ConfigManager::loop() {
  if (!valuesDirty_) return;
  uint32_t now = millis();
  if (now - lastChangeMs_ >= VALUE_SAVE_QUIET_MS || now - firstDirtyMs_ >= VALUE_SAVE_MAX_MS) {
    if (!saveValues()) Serial.println("Saving meter values failed");
  }
}

double ConfigManager::maxValue(const VirtualMeter &meter) {
  return 4294967295.0 * pow(10.0, meter.resolutionExp);
}

bool ConfigManager::checkValue(const VirtualMeter &meter, double value, String &error) {
  if (!isfinite(value) || value < 0) {
    error = "Wert muss eine Zahl >= 0 sein";
    return false;
  }
  int e = meter.resolutionExp;
  double scaled = e <= 0 ? value * pow(10.0, -e) : value / pow(10.0, e);
  if (scaled > 4294967295.5) {
    error = "Wert zu groß, max. " + String(maxValue(meter), e < 0 ? -e : 0) + " " + meter.unit +
            " bei aktueller Auflösung";
    return false;
  }
  return true;
}

bool ConfigManager::setMeterValue(size_t index, double value, String &error) {
  if (index >= cfg_.meterCount || index >= MAX_METERS) {
    error = "Unbekannter Zähler";
    return false;
  }
  VirtualMeter &m = cfg_.meters[index];
  if (!checkValue(m, value, error)) return false;
  if (m.value != value) {
    m.value = value;
    uint32_t now = millis();
    if (!valuesDirty_) firstDirtyMs_ = now;
    lastChangeMs_ = now;
    valuesDirty_ = true;
  }
  return true;
}

void ConfigManager::meterToJson(size_t index, JsonObject o) const {
  const VirtualMeter &m = cfg_.meters[index];
  o["index"] = index + 1;
  o["enabled"] = m.enabled;
  o["name"] = m.name;
  o["primaryAddress"] = m.primaryAddress;
  o["secondaryAddress"] = m.secondaryAddress;
  o["medium"] = m.medium;
  o["value"] = m.value;
  o["unit"] = m.unit;
  o["resolution"] = pow(10.0, m.resolutionExp);
  o["maxValue"] = maxValue(m);
}

void ConfigManager::buildJson(JsonDocument &doc, bool includePassword) const {
  doc["wifiSsid"] = cfg_.wifiSsid;
  doc["wifiPassword"] = includePassword ? cfg_.wifiPassword : "";
  doc["mbusBaud"] = cfg_.mbusBaud;
  doc["mbusStopBits"] = cfg_.mbusStopBits;
  doc["mbusByteGapMs"] = cfg_.mbusByteGapMs;
  doc["mbusRxPin"] = cfg_.mbusRxPin;
  doc["mbusTxPin"] = cfg_.mbusTxPin;
  doc["meterCount"] = cfg_.meterCount;
  doc["mqttEnabled"] = cfg_.mqttEnabled;
  doc["mqttHost"] = cfg_.mqttHost;
  doc["mqttPort"] = cfg_.mqttPort;
  doc["mqttUser"] = cfg_.mqttUser;
  doc["mqttPassword"] = includePassword ? cfg_.mqttPassword : "";
  doc["mqttBaseTopic"] = cfg_.mqttBaseTopic;

  JsonArray meters = doc["meters"].to<JsonArray>();
  for (size_t i = 0; i < cfg_.meterCount && i < MAX_METERS; ++i) {
    JsonObject m = meters.add<JsonObject>();
    m["enabled"] = cfg_.meters[i].enabled;
    m["name"] = cfg_.meters[i].name;
    m["primaryAddress"] = cfg_.meters[i].primaryAddress;
    m["secondaryAddress"] = cfg_.meters[i].secondaryAddress;
    m["manufacturer"] = cfg_.meters[i].manufacturer;
    m["version"] = cfg_.meters[i].version;
    m["medium"] = cfg_.meters[i].medium;
    m["value"] = cfg_.meters[i].value;
    m["unit"] = cfg_.meters[i].unit;
    m["resolutionExp"] = cfg_.meters[i].resolutionExp;
  }
}

bool ConfigManager::fromJson(const String &json, String &error) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) {
    error = e.c_str();
    return false;
  }
  return fromDoc(doc, error);
}

bool ConfigManager::fromJson(Stream &json, String &error) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) {
    error = e.c_str();
    return false;
  }
  return fromDoc(doc, error);
}

bool ConfigManager::fromDoc(JsonDocument &doc, String &error) {
  if (!doc["wifiSsid"].isNull()) cfg_.wifiSsid = doc["wifiSsid"].as<String>();
  if (!doc["wifiPassword"].isNull()) cfg_.wifiPassword = doc["wifiPassword"].as<String>();
  cfg_.mbusBaud = doc["mbusBaud"] | 2400;
  cfg_.mbusStopBits = (doc["mbusStopBits"] | 1) == 2 ? 2 : 1;
  cfg_.mbusByteGapMs = constrain(doc["mbusByteGapMs"] | 0, 0, 20);
  cfg_.mbusRxPin = doc["mbusRxPin"] | 16;
  cfg_.mbusTxPin = doc["mbusTxPin"] | 17;

  cfg_.mqttEnabled = doc["mqttEnabled"] | false;
  if (!doc["mqttHost"].isNull()) cfg_.mqttHost = doc["mqttHost"].as<String>();
  cfg_.mqttHost.trim();
  cfg_.mqttPort = doc["mqttPort"] | 1883;
  if (cfg_.mqttPort == 0) cfg_.mqttPort = 1883;
  if (!doc["mqttUser"].isNull()) cfg_.mqttUser = doc["mqttUser"].as<String>();
  if (!doc["mqttPassword"].isNull()) cfg_.mqttPassword = doc["mqttPassword"].as<String>();
  if (!doc["mqttBaseTopic"].isNull()) cfg_.mqttBaseTopic = doc["mqttBaseTopic"].as<String>();
  cfg_.mqttBaseTopic.trim();
  while (cfg_.mqttBaseTopic.endsWith("/")) cfg_.mqttBaseTopic.remove(cfg_.mqttBaseTopic.length() - 1);
  if (cfg_.mqttBaseTopic.isEmpty()) cfg_.mqttBaseTopic = "bascloud/mbus";

  size_t count = doc["meterCount"] | 10;
  if (count < 1) count = 1;
  if (count > MAX_METERS) count = MAX_METERS;
  cfg_.meterCount = count;

  JsonArray meters = doc["meters"].as<JsonArray>();
  for (size_t i = 0; i < MAX_METERS; ++i) {
    if (i >= cfg_.meterCount) {
      cfg_.meters[i].enabled = false;
      continue;
    }

    JsonObject m;
    if (!meters.isNull() && i < meters.size()) m = meters[i].as<JsonObject>();

    cfg_.meters[i].enabled = m.isNull() ? true : (m["enabled"] | true);
    cfg_.meters[i].name = m.isNull() ? ("Meter " + String(i + 1)) : m["name"].as<String>();
    if (cfg_.meters[i].name.isEmpty()) cfg_.meters[i].name = "Meter " + String(i + 1);

    int addr = m.isNull() ? static_cast<int>(i + 1) : (m["primaryAddress"] | static_cast<int>(i + 1));
    if (addr < 1) addr = 1;
    if (addr > 250) addr = 250;
    cfg_.meters[i].primaryAddress = static_cast<uint8_t>(addr);

    cfg_.meters[i].secondaryAddress = m.isNull() ? 10000001UL + i : (m["secondaryAddress"] | (10000001UL + i));
    cfg_.meters[i].manufacturer = m.isNull() ? "BAS" : m["manufacturer"].as<String>();
    cfg_.meters[i].manufacturer.toUpperCase();
    if (cfg_.meters[i].manufacturer.length() != 3) cfg_.meters[i].manufacturer = "BAS";
    cfg_.meters[i].version = m.isNull() ? 1 : (m["version"] | 1);
    cfg_.meters[i].medium = m.isNull() ? 0x07 : (m["medium"] | 0x07);
    cfg_.meters[i].value = m.isNull() ? 0.0 : (m["value"] | 0.0);
    cfg_.meters[i].unit = m.isNull() ? "m3" : m["unit"].as<String>();
    if (cfg_.meters[i].unit != "m3" && cfg_.meters[i].unit != "kWh") cfg_.meters[i].unit = "m3";

    // Older configs have no resolution: keep previous behaviour (m3 = 0.001, kWh = 1).
    int defExp = cfg_.meters[i].unit == "kWh" ? 0 : -3;
    int res = m.isNull() ? defExp : (m["resolutionExp"] | defExp);
    if (res < -3) res = -3;
    if (res > 1) res = 1;
    cfg_.meters[i].resolutionExp = static_cast<int8_t>(res);
  }

  // Reject duplicate active primary addresses.
  for (size_t i = 0; i < cfg_.meterCount; ++i) {
    if (!cfg_.meters[i].enabled) continue;
    for (size_t j = i + 1; j < cfg_.meterCount; ++j) {
      if (cfg_.meters[j].enabled && cfg_.meters[i].primaryAddress == cfg_.meters[j].primaryAddress) {
        error = "Duplicate primary M-Bus address: " + String(cfg_.meters[i].primaryAddress);
        return false;
      }
    }
  }
  return true;
}
