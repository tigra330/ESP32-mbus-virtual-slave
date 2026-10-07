#include "PulseCounter.h"
#include <driver/gpio.h>

static constexpr uint64_t TICK_US = 1000;

void PulseCounter::begin(ConfigManager *config, MqttBridge *mqtt) {
  config_ = config;
  mqtt_ = mqtt;
  esp_timer_create_args_t args{};
  args.callback = &PulseCounter::onTick;
  args.arg = this;
  args.name = "pulses";
  if (esp_timer_create(&args, &timer_) != ESP_OK) {
    timer_ = nullptr;
    Serial.println("Pulse timer creation failed");
    return;
  }
  apply();
}

void PulseCounter::apply() {
  if (!timer_) return;
  esp_timer_stop(timer_); // fails harmlessly if not running
  loop();                 // pulses counted with the old settings

  const AppConfig &cfg = config_->config();
  bool any = false;
  for (size_t i = 0; i < PULSE_INPUTS; ++i) {
    const PulseInput &p = cfg.pulses[i];
    Channel &c = ch_[i];
    c = Channel{};
    if (!p.enabled) continue;
    pinMode(p.pin, p.pullup ? INPUT_PULLUP : INPUT);
    c.pin = p.pin;
    c.activeLow = p.activeLow;
    c.debounceMs = p.debounceMs;
    // Start from the current level, so an input that is active right now is no pulse.
    c.stable = (digitalRead(p.pin) == LOW) == c.activeLow;
    any = true;
    Serial.printf("Pulse input %u: GPIO %d -> meter %u, factor %g, debounce %u ms\n", static_cast<unsigned>(i + 1),
                  p.pin, p.meter, p.factor, p.debounceMs);
  }
  if (any) esp_timer_start_periodic(timer_, TICK_US);
}

void PulseCounter::onTick(void *arg) {
  PulseCounter *self = static_cast<PulseCounter *>(arg);
  for (Channel &c : self->ch_) {
    if (c.pin < 0) continue;
    const bool active = (gpio_get_level(static_cast<gpio_num_t>(c.pin)) == 0) == c.activeLow;
    if (active == c.stable) {
      c.stableMs = 0;
      continue;
    }
    if (++c.stableMs < c.debounceMs) continue;
    c.stable = active;
    c.stableMs = 0;
    if (active) {
      portENTER_CRITICAL(&self->mux_);
      c.pulses++;
      portEXIT_CRITICAL(&self->mux_);
    }
  }
}

void PulseCounter::loop() {
  if (!config_) return;
  for (size_t i = 0; i < PULSE_INPUTS; ++i) {
    portENTER_CRITICAL(&mux_);
    uint32_t n = ch_[i].pulses;
    ch_[i].pulses = 0;
    portEXIT_CRITICAL(&mux_);
    if (n == 0) continue;
    config_->addPulses(i, n);
    int idx = config_->config().pulses[i].meter - 1;
    if (mqtt_ && config_->pulseInputOf(idx) == static_cast<int>(i)) mqtt_->publishMeter(idx);
  }
}
