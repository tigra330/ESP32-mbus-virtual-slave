#pragma once
#include <Arduino.h>

static constexpr size_t MAX_METERS = 32;

struct VirtualMeter {
  bool enabled = true;
  String name = "Meter";
  uint8_t primaryAddress = 1;
  uint32_t secondaryAddress = 10000001;
  String manufacturer = "BAS"; // exactly 3 A-Z chars for M-Bus manufacturer code
  uint8_t version = 1;
  uint8_t medium = 0x07;        // Water
  double value = 0.0;
  String unit = "m3";           // m3 or kWh in v0.1
  int8_t resolutionExp = -3;    // resolution as power of ten of unit (-3 = 0.001 ... 1 = 10)
  uint8_t accessNumber = 0;      // runtime RSP_UD access counter
};

struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  uint32_t mbusBaud = 2400;
  int mbusRxPin = 16;
  int mbusTxPin = 17;
  size_t meterCount = 10;
  bool mqttEnabled = false;
  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUser;
  String mqttPassword;
  String mqttBaseTopic = "bascloud/mbus";
  VirtualMeter meters[MAX_METERS];
};
