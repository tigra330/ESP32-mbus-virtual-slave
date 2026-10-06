#include "Bme280Sensor.h"
#include <Wire.h>

static constexpr uint32_t READ_INTERVAL_MS = 2000;
static constexpr uint32_t PROBE_INTERVAL_MS = 10000;

void Bme280Sensor::begin(const SensorConfig *cfg) {
  cfg_ = cfg;
  if (!cfg_->enabled) return;
  started_ = Wire.begin(cfg_->sdaPin, cfg_->sclPin);
  if (started_) probe();
}

bool Bme280Sensor::probe() {
  lastProbeMs_ = millis();
  found_ = bme_.begin(cfg_->i2cAddress, &Wire);
  if (!found_) return false;
  // Normal mode with 1 s standby: the sensor measures on its own, reads are plain register reads.
  bme_.setSampling(Adafruit_BME280::MODE_NORMAL, Adafruit_BME280::SAMPLING_X2, Adafruit_BME280::SAMPLING_X4,
                   Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::FILTER_X4, Adafruit_BME280::STANDBY_MS_1000);
  read();
  return true;
}

void Bme280Sensor::read() {
  lastReadMs_ = millis();
  float t = bme_.readTemperature();
  float h = bme_.readHumidity();
  float p = bme_.readPressure() / 100.0f;
  // The library returns NaN when the sensor does not answer (e.g. unplugged).
  valid_ = isfinite(t) && isfinite(h) && isfinite(p) && p > 0;
  if (!valid_) {
    found_ = false;
    return;
  }
  temperature_ = t;
  humidity_ = h;
  pressure_ = p;
}

void Bme280Sensor::loop() {
  if (!cfg_ || !cfg_->enabled || !started_) return;
  uint32_t now = millis();
  if (!found_) {
    if (now - lastProbeMs_ >= PROBE_INTERVAL_MS) probe();
    return;
  }
  if (now - lastReadMs_ >= READ_INTERVAL_MS) read();
}

String Bme280Sensor::statusText() const {
  if (!cfg_ || !cfg_->enabled) return "deaktiviert";
  if (!started_) return "I2C-Start fehlgeschlagen";
  char addr[8];
  snprintf(addr, sizeof(addr), "0x%02X", cfg_->i2cAddress);
  if (!found_) return "nicht gefunden (I2C " + String(addr) + ")";
  if (!valid_) return "keine gültigen Messwerte";
  return String(temperature_, 2) + " °C · " + String(humidity_, 1) + " % · " + String(pressure_, 1) + " hPa";
}
