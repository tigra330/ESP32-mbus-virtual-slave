#include "MqttBridge.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// Pending state messages are only handed to esp-mqtt while its outbox stays below this size.
static constexpr int OUTBOX_LIMIT_BYTES = 8192;

void MqttBridge::begin(ConfigManager *config) {
  config_ = config;
  if (!queue_) queue_ = xQueueCreate(40, sizeof(SetMessage));
  start();
}

void MqttBridge::start() {
  const AppConfig &cfg = config_->config();
  if (!cfg.mqttEnabled) {
    lastEvent_ = "deaktiviert";
    return;
  }
  if (cfg.mqttHost.isEmpty()) {
    lastEvent_ = "kein Broker eingetragen";
    return;
  }
  if (WiFi.getMode() != WIFI_STA) {
    lastEvent_ = "kein WLAN (AP-Modus)";
    return;
  }

  uri_ = "mqtt://" + cfg.mqttHost + ":" + String(cfg.mqttPort);
  uint64_t chip = ESP.getEfuseMac();
  clientId_ = "bascloud-mbus-" + String(static_cast<uint32_t>(chip & 0xFFFFFF), HEX);
  statusTopic_ = cfg.mqttBaseTopic + "/status";
  setPrefix_ = cfg.mqttBaseTopic + "/meter/";

  esp_mqtt_client_config_t mc{};
#if ESP_IDF_VERSION_MAJOR >= 5
  mc.broker.address.uri = uri_.c_str();
  mc.credentials.client_id = clientId_.c_str();
  if (!cfg.mqttUser.isEmpty()) {
    mc.credentials.username = cfg.mqttUser.c_str();
    mc.credentials.authentication.password = cfg.mqttPassword.c_str();
  }
  mc.session.last_will.topic = statusTopic_.c_str();
  mc.session.last_will.msg = "offline";
  mc.session.last_will.qos = 1;
  mc.session.last_will.retain = 1;
  mc.session.keepalive = 30;
#else
  mc.uri = uri_.c_str();
  mc.client_id = clientId_.c_str();
  if (!cfg.mqttUser.isEmpty()) {
    mc.username = cfg.mqttUser.c_str();
    mc.password = cfg.mqttPassword.c_str();
  }
  mc.lwt_topic = statusTopic_.c_str();
  mc.lwt_msg = "offline";
  mc.lwt_qos = 1;
  mc.lwt_retain = 1;
  mc.keepalive = 30;
  mc.event_handle = &MqttBridge::onEvent;
  mc.user_context = this;
#endif

  client_ = esp_mqtt_client_init(&mc);
#if ESP_IDF_VERSION_MAJOR >= 5
  if (client_) esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MqttBridge::onEventIdf5, this);
#endif
  if (!client_ || esp_mqtt_client_start(client_) != ESP_OK) {
    lastEvent_ = "Start fehlgeschlagen";
    stop();
    return;
  }
}

void MqttBridge::stop() {
  if (client_) {
    if (connected_) esp_mqtt_client_publish(client_, statusTopic_.c_str(), "offline", 0, 1, 1);
    esp_mqtt_client_stop(client_);
    esp_mqtt_client_destroy(client_);
    client_ = nullptr;
  }
  connected_ = false;
  needPublishAll_ = false;
  if (queue_) xQueueReset(queue_);
}

void MqttBridge::restart() {
  stop();
  start();
}

#if ESP_IDF_VERSION_MAJOR >= 5
void MqttBridge::onEventIdf5(void *arg, esp_event_base_t, int32_t, void *data) {
  handleEvent(static_cast<MqttBridge *>(arg), static_cast<esp_mqtt_event_handle_t>(data));
}
#else
esp_err_t MqttBridge::onEvent(esp_mqtt_event_handle_t event) {
  handleEvent(static_cast<MqttBridge *>(event->user_context), event);
  return ESP_OK;
}
#endif

// Runs in the esp-mqtt task: only touches the queue and flags, never the configuration.
void MqttBridge::handleEvent(MqttBridge *self, esp_mqtt_event_handle_t event) {
  switch (event->event_id) {
    case MQTT_EVENT_CONNECTED: {
      self->connected_ = true;
      self->needPublishAll_ = true;
      String sub = self->setPrefix_ + "+/set";
      esp_mqtt_client_subscribe(event->client, sub.c_str(), 1);
      esp_mqtt_client_publish(event->client, self->statusTopic_.c_str(), "online", 0, 1, 1);
      break;
    }
    case MQTT_EVENT_DISCONNECTED:
      self->connected_ = false;
      break;
    case MQTT_EVENT_DATA: {
      // Ignore fragmented (oversized) messages; set payloads are tiny.
      if (event->data_len != event->total_data_len || event->current_data_offset != 0) break;
      String topic(event->topic, event->topic_len);
      if (!topic.startsWith(self->setPrefix_) || !topic.endsWith("/set")) break;
      String num = topic.substring(self->setPrefix_.length(), topic.length() - 4);
      long n = num.toInt();
      if (n < 1 || n > static_cast<long>(MAX_METERS) || String(n) != num) break;

      SetMessage msg{};
      msg.index = static_cast<uint8_t>(n - 1);
      size_t len = min(static_cast<size_t>(event->data_len), sizeof(msg.payload) - 1);
      memcpy(msg.payload, event->data, len);
      xQueueSend(self->queue_, &msg, 0);
      break;
    }
    default:
      break;
  }
}

void MqttBridge::loop() {
  if (!client_) return;

  SetMessage msg;
  while (xQueueReceive(queue_, &msg, 0) == pdTRUE) handleSet(msg);

  if (connected_ && needPublishAll_) {
    needPublishAll_ = false;
    publishAll();
  }
  if (connected_) sendPending();
}

void MqttBridge::sendPending() {
  const size_t count = config_->config().meterCount;
  for (size_t i = 0; i < count && i < MAX_METERS; ++i) {
    if (!pending_[i]) continue;
    if (esp_mqtt_client_get_outbox_size(client_) >= OUTBOX_LIMIT_BYTES) return;
    pending_[i] = false;
    JsonDocument doc;
    config_->meterToJson(i, doc.to<JsonObject>());
    String out; serializeJson(doc, out);
    publish(setPrefix_ + String(i + 1) + "/state", out, true);
  }
}

void MqttBridge::handleSet(const SetMessage &msg) {
  String payload(msg.payload);
  payload.trim();

  String error;
  if (msg.index >= config_->config().meterCount) {
    error = "Unbekannter Zähler";
  } else if (payload.startsWith("{")) {
    JsonDocument doc;
    MeterUpdate u;
    if (deserializeJson(doc, payload)) {
      error = "Ungültiges JSON";
    } else if (ConfigManager::parseUpdate(config_->config().meters[msg.index], doc.as<JsonObjectConst>(), u, error)) {
      config_->applyUpdate(msg.index, u);
      publishMeter(msg.index);
      return;
    }
  } else {
    payload.replace(',', '.');
    char *end = nullptr;
    double value = strtod(payload.c_str(), &end);
    if (end == payload.c_str() || *end != '\0') {
      error = "Ungültiger Wert '" + String(msg.payload) + "'";
    } else if (config_->setMeterValue(msg.index, value, error)) {
      publishMeter(msg.index);
      return;
    }
  }

  JsonDocument doc;
  doc["index"] = msg.index + 1;
  doc["payload"] = msg.payload;
  doc["error"] = error;
  String out; serializeJson(doc, out);
  publish(config_->config().mqttBaseTopic + "/error", out, false);
}

void MqttBridge::publish(const String &topic, const String &payload, bool retain) {
  if (!client_ || !connected_) return;
  // enqueue() does not block; the esp-mqtt task sends it.
  esp_mqtt_client_enqueue(client_, topic.c_str(), payload.c_str(), payload.length(), 0, retain, true);
}

void MqttBridge::publishMeter(size_t index) {
  if (!client_ || !connected_ || index >= config_->config().meterCount || index >= MAX_METERS) return;
  pending_[index] = true;
}

void MqttBridge::publishAll() {
  for (size_t i = 0; i < config_->config().meterCount; ++i) publishMeter(i);
}

String MqttBridge::statusText() const {
  if (client_) return (connected_ ? "verbunden mit " : "verbinde mit ") + uri_;
  return lastEvent_;
}
