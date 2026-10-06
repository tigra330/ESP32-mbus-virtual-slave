#pragma once
#include <Arduino.h>
#include "AppConfig.h"
#include "Bme280Sensor.h"

class MBusSlave {
public:
  explicit MBusSlave(HardwareSerial &serial) : serial_(serial) {}
  void begin(AppConfig *cfg, Bme280Sensor *sensor = nullptr);
  void loop();
  String lastRxHex() const { return lastRxHex_; }
  String lastTxHex() const { return lastTxHex_; }
  String lastEvent() const { return lastEvent_; }
  uint32_t rxFrames() const { return rxFrames_; }
  uint32_t txFrames() const { return txFrames_; }

private:
  HardwareSerial &serial_;
  AppConfig *cfg_ = nullptr;
  Bme280Sensor *sensor_ = nullptr;
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
  void sendSensorRspUd();
  static size_t putHeader(uint8_t *app, uint32_t secondaryAddress, const String &manufacturer, uint8_t version,
                          uint8_t medium, uint8_t accessNumber, uint8_t status);
  void sendLongFrame(uint8_t primaryAddress, const uint8_t *app, size_t len);
  void writeFrame(const uint8_t *data, size_t len);
  static String hexString(const uint8_t *data, size_t len);
  static uint16_t encodeManufacturer(const String &id);
  static void encodeBcd8(uint32_t number, uint8_t out[4]);
};
