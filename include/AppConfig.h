#pragma once
#include <Arduino.h>

static constexpr size_t MAX_METERS = 250; // one per M-Bus primary address 1..250

// Additional registers of a bidirectional electricity meter; 1.8.0 is VirtualMeter::value.
static constexpr size_t ENERGY_REGS = 5;
static constexpr const char *ENERGY_REG_NAMES[ENERGY_REGS] = {"1.8.1", "1.8.2", "2.8.0", "2.8.1", "2.8.2"};

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

  // Heat meters only: additional data records in RSP_UD.
  double flow = 0.0;             // volume flow in m3/h, sent with 0.001 m3/h (1 l/h) resolution
  double flowTemp = 0.0;         // flow temperature in °C, sent with 0.1 °C resolution
  double returnTemp = 0.0;       // return temperature in °C, sent with 0.1 °C resolution

  // Bidirectional electricity meter (medium 0x02, unit kWh): registers 1.8.1 ... 2.8.2,
  // same resolution as value (= 1.8.0).
  bool bidirectional = false;
  double energy[ENERGY_REGS] = {};
};

// Medium 0x04 = heat (outlet), 0x0C = heat (inlet).
inline bool isHeatMeter(const VirtualMeter &m) { return m.medium == 0x04 || m.medium == 0x0C; }
inline bool isBidirectionalMeter(const VirtualMeter &m) { return m.bidirectional && m.medium == 0x02; }

struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  uint32_t mbusBaud = 2400;
  uint8_t mbusStopBits = 1;     // 2 = workaround for slaves whose bus-side supply sags on long space runs
  uint8_t mbusByteGapMs = 10;   // idle time after each sent byte, same workaround (0 = standard-compliant, max 20)
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
