#pragma once
#include <Arduino.h>
#include <esp_timer.h>
#include "ConfigManager.h"
#include "MqttBridge.h"

// Counts pulses on up to PULSE_INPUTS GPIOs. A 1 ms esp_timer samples the inputs and debounces
// them, independent of loop(), which can block for a while (M-Bus byte gap, WiFi). A pulse is
// counted when the input has been stable in the active state for debounceMs.
class PulseCounter {
public:
  void begin(ConfigManager *config, MqttBridge *mqtt);
  // (Re)configures the GPIOs from the current configuration, e.g. after saving it.
  void apply();
  // Hands counted pulses to the ConfigManager.
  void loop();

private:
  struct Channel {
    int pin = -1;               // -1 = disabled
    bool activeLow = true;
    uint16_t debounceMs = 20;
    bool stable = false;        // debounced state, true = active
    uint16_t stableMs = 0;      // how long the raw level has differed from stable
    uint32_t pulses = 0;        // counted, not yet handed over
  };

  ConfigManager *config_ = nullptr;
  MqttBridge *mqtt_ = nullptr;
  esp_timer_handle_t timer_ = nullptr;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  Channel ch_[PULSE_INPUTS];

  static void onTick(void *arg);
};
