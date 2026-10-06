#include "ConfigManager.h"
#include <LittleFS.h>
#include <math.h>
#include <memory>
#include <vector>

// The configuration (up to ~45 KB JSON for 250 meters) lives in LittleFS; NVS strings are
// limited to 4000 bytes. Older firmware kept it in NVS key "config", which is migrated once.
static const char *CONFIG_FILE = "/config.json";
static const char *CONFIG_TMP = "/config.tmp";

// Ranges of the additional heat meter records (see MBusSlave::sendRspUd).
static constexpr double FLOW_MAX = 4294967.295;   // uint32 at 0.001 m3/h
static constexpr double TEMP_MIN = -3276.8;       // int16 at 0.1 °C
static constexpr double TEMP_MAX = 3276.7;

// Values set via REST/MQTT are written to their own file instead of the whole JSON config.
// Saved after VALUE_SAVE_QUIET_MS without changes, at the latest VALUE_SAVE_MAX_MS after the first change.
// Format: magic, meter count, values per meter, then per meter: value, flow, flowTemp, returnTemp,
// energy[0..4] as double. Older firmware kept value/heat in NVS ("values"/"heat"), migrated once.
static const char *VALUES_FILE = "/values.bin";
static const char *VALUES_TMP = "/values.tmp";
static constexpr uint32_t VALUES_MAGIC = 0x3156424D; // "MBV1"
static constexpr uint32_t VALUES_PER_METER = 4 + ENERGY_REGS;
static constexpr uint32_t VALUE_SAVE_QUIET_MS = 10000;
static constexpr uint32_t VALUE_SAVE_MAX_MS = 60000;

// Replaces dst by tmp, so a power loss never leaves a truncated file behind.
static bool replaceFile(const char *tmp, const char *dst) {
  if (LittleFS.rename(tmp, dst)) return true;
  LittleFS.remove(dst);
  return LittleFS.rename(tmp, dst);
}

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
  if (ok) ok = replaceFile(CONFIG_TMP, CONFIG_FILE);
  return saveValues() && ok;
}

bool ConfigManager::saveValues() {
  File f = LittleFS.open(VALUES_TMP, "w");
  if (!f) return false;
  const uint32_t header[3] = {VALUES_MAGIC, MAX_METERS, VALUES_PER_METER};
  bool ok = f.write(reinterpret_cast<const uint8_t *>(header), sizeof(header)) == sizeof(header);
  for (size_t i = 0; ok && i < MAX_METERS; ++i) {
    const VirtualMeter &m = cfg_.meters[i];
    double row[VALUES_PER_METER] = {m.value, m.flow, m.flowTemp, m.returnTemp};
    for (size_t r = 0; r < ENERGY_REGS; ++r) row[4 + r] = m.energy[r];
    ok = f.write(reinterpret_cast<const uint8_t *>(row), sizeof(row)) == sizeof(row);
  }
  f.close();
  if (ok) ok = replaceFile(VALUES_TMP, VALUES_FILE);
  // Not retried on failure: loop() would otherwise rewrite the flash on every pass.
  valuesDirty_ = false;
  return ok;
}

static bool validCount(double v) { return isfinite(v) && v >= 0; }
static bool validFlow(double v) { return isfinite(v) && v >= 0 && v <= FLOW_MAX; }
static bool validTemp(double v) { return isfinite(v) && v >= TEMP_MIN && v <= TEMP_MAX; }

void ConfigManager::loadValues() {
  File f = LittleFS.open(VALUES_FILE, "r");
  if (!f) {
    if (loadLegacyValues() && saveValues()) {
      prefs_.remove("values");
      prefs_.remove("heat");
      Serial.println("Meter values migrated from NVS to LittleFS");
    }
    return;
  }
  uint32_t header[3];
  if (f.read(reinterpret_cast<uint8_t *>(header), sizeof(header)) != sizeof(header) ||
      header[0] != VALUES_MAGIC || header[2] != VALUES_PER_METER) {
    f.close();
    return;
  }
  for (size_t i = 0; i < header[1] && i < MAX_METERS; ++i) {
    double row[VALUES_PER_METER];
    if (f.read(reinterpret_cast<uint8_t *>(row), sizeof(row)) != sizeof(row)) break;
    VirtualMeter &m = cfg_.meters[i];
    if (validCount(row[0])) m.value = row[0];
    if (validFlow(row[1])) m.flow = row[1];
    if (validTemp(row[2])) m.flowTemp = row[2];
    if (validTemp(row[3])) m.returnTemp = row[3];
    for (size_t r = 0; r < ENERGY_REGS; ++r) {
      if (validCount(row[4 + r])) m.energy[r] = row[4 + r];
    }
  }
  f.close();
}

bool ConfigManager::loadLegacyValues() {
  // Older firmware stored fewer meters; take whatever is there.
  size_t len = prefs_.getBytesLength("values");
  if (len == 0 || len % sizeof(double) != 0 || len > MAX_METERS * sizeof(double)) return false;
  std::vector<double> values(len / sizeof(double));
  if (prefs_.getBytes("values", values.data(), len) != len) return false;
  for (size_t i = 0; i < values.size(); ++i) {
    if (validCount(values[i])) cfg_.meters[i].value = values[i];
  }

  len = prefs_.getBytesLength("heat");
  if (len == 0 || len % (3 * sizeof(float)) != 0 || len > MAX_METERS * 3 * sizeof(float)) return true;
  std::vector<float> heat(len / sizeof(float));
  if (prefs_.getBytes("heat", heat.data(), len) != len) return true;
  for (size_t i = 0; i < heat.size() / 3; ++i) {
    VirtualMeter &m = cfg_.meters[i];
    if (validFlow(heat[i * 3])) m.flow = heat[i * 3];
    if (validTemp(heat[i * 3 + 1])) m.flowTemp = heat[i * 3 + 1];
    if (validTemp(heat[i * 3 + 2])) m.returnTemp = heat[i * 3 + 2];
  }
  return true;
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
  if (!checkValue(cfg_.meters[index], value, error)) return false;
  MeterUpdate u;
  u.hasValue = true;
  u.value = value;
  applyUpdate(index, u);
  return true;
}

static bool readNumber(JsonObjectConst in, const char *key, bool &has, double &out, String &error) {
  JsonVariantConst v = in[key];
  if (v.isNull()) return true;
  if (!v.is<double>()) {
    error = "'" + String(key) + "' muss eine Zahl sein";
    return false;
  }
  has = true;
  out = v.as<double>();
  return true;
}

bool ConfigManager::parseUpdate(const VirtualMeter &meter, JsonObjectConst in, MeterUpdate &out, String &error) {
  out = MeterUpdate{};
  if (in.isNull()) {
    error = "JSON-Objekt erwartet";
    return false;
  }
  if (!readNumber(in, "value", out.hasValue, out.value, error) ||
      !readNumber(in, "flow", out.hasFlow, out.flow, error) ||
      !readNumber(in, "flowTemp", out.hasFlowTemp, out.flowTemp, error) ||
      !readNumber(in, "returnTemp", out.hasReturnTemp, out.returnTemp, error)) {
    return false;
  }
  // "1.8.0" is an alias for value.
  bool has180 = false;
  double v180 = 0;
  if (!readNumber(in, "1.8.0", has180, v180, error)) return false;
  if (has180) {
    if (out.hasValue && v180 != out.value) {
      error = "'value' und '1.8.0' widersprechen sich";
      return false;
    }
    out.hasValue = true;
    out.value = v180;
  }
  bool anyEnergy = false;
  for (size_t r = 0; r < ENERGY_REGS; ++r) {
    if (!readNumber(in, ENERGY_REG_NAMES[r], out.hasEnergy[r], out.energy[r], error)) return false;
    anyEnergy |= out.hasEnergy[r];
  }
  if (out.empty()) {
    error = "'value', 'flow', 'flowTemp', 'returnTemp' oder OBIS-Register ('1.8.0' ... '2.8.2') erwartet";
    return false;
  }
  if (out.hasValue && !checkValue(meter, out.value, error)) return false;
  if (anyEnergy && !isBidirectionalMeter(meter)) {
    error = "Register 1.8.1 ... 2.8.2 gibt es nur bei Zählern vom Typ Strom 2-Richtung";
    return false;
  }
  for (size_t r = 0; r < ENERGY_REGS; ++r) {
    if (out.hasEnergy[r] && !checkValue(meter, out.energy[r], error)) {
      error = String(ENERGY_REG_NAMES[r]) + ": " + error;
      return false;
    }
  }
  if ((out.hasFlow || out.hasFlowTemp || out.hasReturnTemp) && !isHeatMeter(meter)) {
    error = "'flow', 'flowTemp' und 'returnTemp' gibt es nur bei Wärmezählern (Medium 4)";
    return false;
  }
  if (out.hasFlow && (!isfinite(out.flow) || out.flow < 0 || out.flow > FLOW_MAX)) {
    error = "Durchfluss muss zwischen 0 und 4294967.295 m³/h liegen";
    return false;
  }
  if ((out.hasFlowTemp && (!isfinite(out.flowTemp) || out.flowTemp < TEMP_MIN || out.flowTemp > TEMP_MAX)) ||
      (out.hasReturnTemp && (!isfinite(out.returnTemp) || out.returnTemp < TEMP_MIN || out.returnTemp > TEMP_MAX))) {
    error = "Temperatur muss zwischen -3276.8 und 3276.7 °C liegen";
    return false;
  }
  return true;
}

void ConfigManager::applyUpdate(size_t index, const MeterUpdate &u) {
  if (index >= cfg_.meterCount || index >= MAX_METERS) return;
  VirtualMeter &m = cfg_.meters[index];
  bool changed = false;
  auto set = [&changed](bool has, double v, double &target) {
    if (has && target != v) {
      target = v;
      changed = true;
    }
  };
  set(u.hasValue, u.value, m.value);
  set(u.hasFlow, u.flow, m.flow);
  set(u.hasFlowTemp, u.flowTemp, m.flowTemp);
  set(u.hasReturnTemp, u.returnTemp, m.returnTemp);
  for (size_t r = 0; r < ENERGY_REGS; ++r) set(u.hasEnergy[r], u.energy[r], m.energy[r]);
  if (changed) {
    uint32_t now = millis();
    if (!valuesDirty_) firstDirtyMs_ = now;
    lastChangeMs_ = now;
    valuesDirty_ = true;
  }
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
  if (isHeatMeter(m)) {
    o["flow"] = m.flow;
    o["flowTemp"] = m.flowTemp;
    o["returnTemp"] = m.returnTemp;
  }
  if (isBidirectionalMeter(m)) {
    o["bidirectional"] = true;
    o["1.8.0"] = m.value;
    for (size_t r = 0; r < ENERGY_REGS; ++r) o[ENERGY_REG_NAMES[r]] = m.energy[r];
  }
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

  JsonObject s = doc["sensor"].to<JsonObject>();
  s["enabled"] = cfg_.sensor.enabled;
  s["name"] = cfg_.sensor.name;
  s["primaryAddress"] = cfg_.sensor.primaryAddress;
  s["secondaryAddress"] = cfg_.sensor.secondaryAddress;
  s["manufacturer"] = cfg_.sensor.manufacturer;
  s["version"] = cfg_.sensor.version;
  s["sdaPin"] = cfg_.sensor.sdaPin;
  s["sclPin"] = cfg_.sensor.sclPin;
  s["i2cAddress"] = cfg_.sensor.i2cAddress;

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
    if (isHeatMeter(cfg_.meters[i])) {
      m["flow"] = cfg_.meters[i].flow;
      m["flowTemp"] = cfg_.meters[i].flowTemp;
      m["returnTemp"] = cfg_.meters[i].returnTemp;
    }
    if (isBidirectionalMeter(cfg_.meters[i])) {
      m["bidirectional"] = true;
      for (size_t r = 0; r < ENERGY_REGS; ++r) m[ENERGY_REG_NAMES[r]] = cfg_.meters[i].energy[r];
    }
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
  cfg_.mbusByteGapMs = constrain(doc["mbusByteGapMs"] | 10, 0, 20);
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

    // Bidirectional electricity meter: always medium electricity and kWh.
    cfg_.meters[i].bidirectional = m.isNull() ? false : (m["bidirectional"] | false);
    if (cfg_.meters[i].bidirectional) {
      cfg_.meters[i].medium = 0x02;
      cfg_.meters[i].unit = "kWh";
    }
    for (size_t r = 0; r < ENERGY_REGS; ++r) {
      double e = m.isNull() ? 0.0 : (m[ENERGY_REG_NAMES[r]] | 0.0);
      cfg_.meters[i].energy[r] = validCount(e) ? e : 0.0;
    }

    // Older configs have no resolution: keep previous behaviour (m3 = 0.001, kWh = 1).
    int defExp = cfg_.meters[i].unit == "kWh" ? 0 : -3;
    int res = m.isNull() ? defExp : (m["resolutionExp"] | defExp);
    if (res < -3) res = -3;
    if (res > 1) res = 1;
    cfg_.meters[i].resolutionExp = static_cast<int8_t>(res);

    double flow = m.isNull() ? 0.0 : (m["flow"] | 0.0);
    double flowTemp = m.isNull() ? 0.0 : (m["flowTemp"] | 0.0);
    double returnTemp = m.isNull() ? 0.0 : (m["returnTemp"] | 0.0);
    cfg_.meters[i].flow = isfinite(flow) ? constrain(flow, 0.0, FLOW_MAX) : 0.0;
    cfg_.meters[i].flowTemp = isfinite(flowTemp) ? constrain(flowTemp, TEMP_MIN, TEMP_MAX) : 0.0;
    cfg_.meters[i].returnTemp = isfinite(returnTemp) ? constrain(returnTemp, TEMP_MIN, TEMP_MAX) : 0.0;
  }

  JsonObject s = doc["sensor"].as<JsonObject>();
  SensorConfig &sc = cfg_.sensor;
  sc.enabled = s["enabled"] | false;
  sc.name = s["name"] | "BME280";
  if (sc.name.isEmpty()) sc.name = "BME280";
  sc.primaryAddress = static_cast<uint8_t>(constrain(s["primaryAddress"] | 250, 1, 250));
  sc.secondaryAddress = s["secondaryAddress"] | 20000001UL;
  sc.manufacturer = s["manufacturer"] | "BAS";
  sc.manufacturer.toUpperCase();
  if (sc.manufacturer.length() != 3) sc.manufacturer = "BAS";
  sc.version = s["version"] | 1;
  sc.sdaPin = s["sdaPin"] | 21;
  sc.sclPin = s["sclPin"] | 22;
  sc.i2cAddress = (s["i2cAddress"] | 0x76) == 0x77 ? 0x77 : 0x76;

  // Reject duplicate active primary addresses.
  if (sc.enabled) {
    for (size_t i = 0; i < cfg_.meterCount; ++i) {
      if (cfg_.meters[i].enabled && cfg_.meters[i].primaryAddress == sc.primaryAddress) {
        error = "Primäradresse " + String(sc.primaryAddress) + " des BME280-Sensors ist schon von Zähler " +
                String(i + 1) + " belegt";
        return false;
      }
    }
  }
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
