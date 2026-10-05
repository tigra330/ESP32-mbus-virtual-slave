#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "AppConfig.h"

class ConfigManager {
public:
  bool begin();
  AppConfig &config() { return cfg_; }
  const AppConfig &config() const { return cfg_; }
  bool save();
  bool load();
  void factoryDefaults();
  String toJson(bool includePassword = false) const;
  bool fromJson(const String &json, String &error);

  // Runtime meter values (REST/MQTT). index is 0-based.
  bool setMeterValue(size_t index, double value, String &error);
  void meterToJson(size_t index, JsonObject out) const;
  static double maxValue(const VirtualMeter &meter);
  static bool checkValue(const VirtualMeter &meter, double value, String &error);
  // Persists values changed via setMeterValue() delayed, to spare the flash.
  void loop();

private:
  Preferences prefs_;
  AppConfig cfg_;
  bool valuesDirty_ = false;
  uint32_t firstDirtyMs_ = 0;
  uint32_t lastChangeMs_ = 0;

  bool saveValues();
  void loadValues();
};
