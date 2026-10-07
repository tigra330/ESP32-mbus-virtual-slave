#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "AppConfig.h"

// Runtime values to set on one meter (REST/MQTT). Only fields with has* = true are applied.
struct MeterUpdate {
  bool hasValue = false, hasFlow = false, hasFlowTemp = false, hasReturnTemp = false;
  double value = 0, flow = 0, flowTemp = 0, returnTemp = 0;
  bool hasEnergy[ENERGY_REGS] = {};
  double energy[ENERGY_REGS] = {};
  bool empty() const {
    for (bool b : hasEnergy) if (b) return false;
    return !hasValue && !hasFlow && !hasFlowTemp && !hasReturnTemp;
  }
};

class ConfigManager {
public:
  bool begin();
  AppConfig &config() { return cfg_; }
  const AppConfig &config() const { return cfg_; }
  bool save();
  bool load();
  void factoryDefaults();
  void buildJson(JsonDocument &doc, bool includePassword = false) const;
  bool fromJson(const String &json, String &error);
  bool fromJson(Stream &json, String &error);

  // Runtime meter values (REST/MQTT). index is 0-based.
  bool setMeterValue(size_t index, double value, String &error);
  // Reads value/flow/flowTemp/returnTemp and the OBIS registers ("1.8.0" ... "2.8.2") from a
  // JSON object and validates them for this meter.
  static bool parseUpdate(const VirtualMeter &meter, JsonObjectConst in, MeterUpdate &out, String &error);
  // Applies an update validated with parseUpdate().
  void applyUpdate(size_t index, const MeterUpdate &update);
  void meterToJson(size_t index, JsonObject out) const;
  static double maxValue(const VirtualMeter &meter);
  static bool checkValue(const VirtualMeter &meter, double value, String &error);
  // Adds pulses of an enabled pulse input to its meter.
  void addPulses(size_t input, uint32_t pulses);
  // Pulse input (0-based) counting this meter, -1 if none.
  int pulseInputOf(size_t index) const;
  // Persists values changed via setMeterValue()/applyUpdate() delayed, to spare the flash.
  void loop();

private:
  Preferences prefs_;
  AppConfig cfg_;
  bool valuesDirty_ = false;
  uint32_t firstDirtyMs_ = 0;
  uint32_t lastChangeMs_ = 0;

  void markValuesDirty();
  // Restarts a pulse input from its meter's value when that no longer matches
  // startValue + count * factor (value set from outside, factor or meter changed).
  void syncPulses();
  bool saveValues();
  void loadValues();
  void loadPulses();
  bool loadLegacyValues();
  bool fromDoc(JsonDocument &doc, String &error);
};
