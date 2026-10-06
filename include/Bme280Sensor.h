#pragma once
#include <Arduino.h>
#include <Adafruit_BME280.h>
#include "AppConfig.h"

// BME280 on I2C. loop() reads it periodically and caches the values, so M-Bus requests are
// answered without waiting for the bus. A missing sensor is searched for again every few seconds.
class Bme280Sensor {
public:
  void begin(const SensorConfig *cfg);
  void loop();
  bool found() const { return found_; }
  bool valid() const { return valid_; }
  float temperature() const { return temperature_; } // °C
  float humidity() const { return humidity_; }       // % rel. humidity
  float pressure() const { return pressure_; }       // hPa
  String statusText() const;

private:
  const SensorConfig *cfg_ = nullptr;
  Adafruit_BME280 bme_;
  bool started_ = false;
  bool found_ = false;
  bool valid_ = false;
  float temperature_ = NAN, humidity_ = NAN, pressure_ = NAN;
  uint32_t lastReadMs_ = 0;
  uint32_t lastProbeMs_ = 0;

  bool probe();
  void read();
};
