#pragma once
#include <Arduino.h>
#include "AppConfig.h"

class MBusSlave {
public:
  explicit MBusSlave(HardwareSerial &serial) : serial_(serial) {}
  void begin(AppConfig *cfg);
  void loop();
  String lastRxHex() const { return lastRxHex_; }
  String lastTxHex() const { return lastTxHex_; }
  String lastEvent() const { return lastEvent_; }
  uint32_t rxFrames() const { return rxFrames_; }
  uint32_t txFrames() const { return txFrames_; }

private:
  HardwareSerial &serial_;
  AppConfig *cfg_ = nullptr;
  uint8_t rxBuf_[300]{};
  size_t rxLen_ = 0;
  uint32_t lastByteMs_ = 0;
  String lastRxHex_;
  String lastTxHex_;
  String lastEvent_ = "Waiting for M-Bus traffic";
  uint32_t rxFrames_ = 0;
  uint32_t txFrames_ = 0;

  void processFrame(const uint8_t *data, size_t len);
  VirtualMeter *findMeter(uint8_t primaryAddress);
  void sendAck();
  void sendRspUd(VirtualMeter &meter);
  void writeFrame(const uint8_t *data, size_t len);
  static String hexString(const uint8_t *data, size_t len);
  static uint16_t encodeManufacturer(const String &id);
  static void encodeBcd8(uint32_t number, uint8_t out[4]);
};
