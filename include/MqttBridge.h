#pragma once
#include <Arduino.h>
#include <mqtt_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "ConfigManager.h"

// MQTT connection based on the ESP-IDF esp-mqtt client. It runs in its own task and
// never blocks loop(), so M-Bus requests are answered even if the broker is unreachable.
//
// Topics (<base> = configured base topic, <n> = meter index 1..32):
//   <base>/status          "online" / "offline" (retained, last will)
//   <base>/meter/<n>/set   subscribe: "123.456" or {"value":123.456}
//   <base>/meter/<n>/state publish (retained): meter as JSON
//   <base>/error           publish: rejected set messages
class MqttBridge {
public:
  void begin(ConfigManager *config);
  void loop();
  // Stops the client and starts it again with the current configuration.
  void restart();
  void publishMeter(size_t index);
  void publishAll();
  bool connected() const { return connected_; }
  String statusText() const;

private:
  struct SetMessage {
    uint8_t index; // 0-based
    char payload[48];
  };

  ConfigManager *config_ = nullptr;
  esp_mqtt_client_handle_t client_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  String uri_, clientId_, statusTopic_, setPrefix_;
  volatile bool connected_ = false;
  volatile bool needPublishAll_ = false;
  String lastEvent_ = "deaktiviert";

  void start();
  void stop();
  void handleSet(const SetMessage &msg);
  void publish(const String &topic, const String &payload, bool retain);
  static esp_err_t onEvent(esp_mqtt_event_handle_t event);
};
