#include "MBusSlave.h"
#include <math.h>

void MBusSlave::begin(AppConfig *cfg) {
  cfg_ = cfg;
  serial_.begin(cfg_->mbusBaud, SERIAL_8E1, cfg_->mbusRxPin, cfg_->mbusTxPin);
  rxLen_ = 0;
  lastEvent_ = "M-Bus UART started at " + String(cfg_->mbusBaud) + " baud 8E1";
}

void MBusSlave::loop() {
  while (serial_.available()) {
    int b = serial_.read();
    if (b < 0) break;
    if (rxLen_ < sizeof(rxBuf_)) rxBuf_[rxLen_++] = static_cast<uint8_t>(b);
    else rxLen_ = 0;
    lastByteMs_ = millis();
  }

  // At 2400 baud a character is ~4.6 ms with parity. 15 ms idle reliably marks a short frame end.
  if (rxLen_ > 0 && (millis() - lastByteMs_) > 15) {
    processFrame(rxBuf_, rxLen_);
    rxLen_ = 0;
  }
}

VirtualMeter *MBusSlave::findMeter(uint8_t primaryAddress) {
  if (!cfg_) return nullptr;
  for (size_t i = 0; i < cfg_->meterCount && i < MAX_METERS; ++i) {
    VirtualMeter &m = cfg_->meters[i];
    if (m.enabled && m.primaryAddress == primaryAddress) return &m;
  }
  return nullptr;
}

void MBusSlave::processFrame(const uint8_t *data, size_t len) {
  lastRxHex_ = hexString(data, len);
  rxFrames_++;

  // M-Bus short frame: 10 C A CS 16
  if (len == 5 && data[0] == 0x10 && data[4] == 0x16) {
    uint8_t cs = static_cast<uint8_t>(data[1] + data[2]);
    if (cs != data[3]) {
      lastEvent_ = "RX short frame checksum error";
      return;
    }

    const uint8_t control = data[1];
    const uint8_t address = data[2];

    // Broadcasts are intentionally not answered; multiple virtual slaves replying at once would collide.
    if (address == 0xFE || address == 0xFF) {
      lastEvent_ = "Broadcast/test address received; no reply to avoid collision";
      return;
    }

    VirtualMeter *meter = findMeter(address);
    if (!meter) {
      lastEvent_ = "No virtual meter for primary address " + String(address);
      return;
    }

    uint8_t function = control & 0x0F;
    if (function == 0x00) { // SND_NKE = 0x40
      lastEvent_ = "SND_NKE for address " + String(address);
      delayMicroseconds((11000000UL + cfg_->mbusBaud - 1) / cfg_->mbusBaud); // >= 11 bit times
      sendAck();
      return;
    }

    if (function == 0x0B) { // REQ_UD2 = 0x5B / 0x7B
      lastEvent_ = "REQ_UD2 for address " + String(address) + " (" + meter->name + ")";
      delayMicroseconds((11000000UL + cfg_->mbusBaud - 1) / cfg_->mbusBaud); // >= 11 bit times
      sendRspUd(*meter);
      return;
    }

    lastEvent_ = "Unsupported short-frame control 0x" + String(control, HEX);
    return;
  }

  lastEvent_ = "RX frame not handled in v0.1";
}

void MBusSlave::sendAck() {
  uint8_t b = 0xE5;
  writeFrame(&b, 1);
}

uint16_t MBusSlave::encodeManufacturer(const String &input) {
  String s = input;
  s.toUpperCase();
  if (s.length() != 3) s = "BAS";
  uint16_t a = constrain(static_cast<int>(s[0] - '@'), 1, 26);
  uint16_t b = constrain(static_cast<int>(s[1] - '@'), 1, 26);
  uint16_t c = constrain(static_cast<int>(s[2] - '@'), 1, 26);
  return static_cast<uint16_t>((a << 10) | (b << 5) | c);
}

void MBusSlave::encodeBcd8(uint32_t number, uint8_t out[4]) {
  number %= 100000000UL;
  for (int i = 0; i < 4; ++i) {
    uint8_t lo = number % 10; number /= 10;
    uint8_t hi = number % 10; number /= 10;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
}

void MBusSlave::sendRspUd(VirtualMeter &meter) {
  uint8_t app[64]{};
  size_t p = 0;

  // CI 0x72: variable data structure, mode 1 (LSB first)
  uint8_t id[4];
  encodeBcd8(meter.secondaryAddress, id);
  memcpy(app + p, id, 4); p += 4;

  uint16_t man = encodeManufacturer(meter.manufacturer);
  app[p++] = static_cast<uint8_t>(man & 0xFF);
  app[p++] = static_cast<uint8_t>((man >> 8) & 0xFF);
  app[p++] = meter.version;
  app[p++] = meter.medium;

  app[p++] = meter.accessNumber++;
  app[p++] = 0x00; // status
  app[p++] = 0x00; // signature low
  app[p++] = 0x00; // signature high

  // One 32-bit integer data record, resolution 10^resolutionExp of the unit.
  // m3: VIF 0x10+n = 10^(n-6) m3. kWh: VIF 0x00+n = 10^(n-3) Wh = 10^(n-6) kWh.
  // So n = resolutionExp + 6 for both, e.g. m3/-3 -> 0x13, kWh/0 -> 0x06.
  app[p++] = 0x04; // DIF: 32-bit integer, instantaneous value, storage 0

  int resExp = meter.resolutionExp;
  if (resExp < -3) resExp = -3;
  if (resExp > 1) resExp = 1;
  uint8_t vifBase = meter.unit == "kWh" ? 0x00 : 0x10;
  app[p++] = static_cast<uint8_t>(vifBase + resExp + 6);

  double v = resExp <= 0 ? meter.value * pow(10.0, -resExp) : meter.value / pow(10.0, resExp);
  if (v < 0) v = 0;
  if (v > 4294967295.0) v = 4294967295.0;
  uint32_t raw = static_cast<uint32_t>(llround(v));

  app[p++] = static_cast<uint8_t>(raw & 0xFF);
  app[p++] = static_cast<uint8_t>((raw >> 8) & 0xFF);
  app[p++] = static_cast<uint8_t>((raw >> 16) & 0xFF);
  app[p++] = static_cast<uint8_t>((raw >> 24) & 0xFF);

  uint8_t frame[96]{};
  size_t f = 0;
  uint8_t L = static_cast<uint8_t>(3 + p); // C + A + CI + app payload
  frame[f++] = 0x68;
  frame[f++] = L;
  frame[f++] = L;
  frame[f++] = 0x68;
  frame[f++] = 0x08; // RSP_UD, ACD=0, DFC=0
  frame[f++] = meter.primaryAddress;
  frame[f++] = 0x72;
  memcpy(frame + f, app, p); f += p;

  uint8_t cs = 0;
  for (size_t i = 4; i < f; ++i) cs = static_cast<uint8_t>(cs + frame[i]);
  frame[f++] = cs;
  frame[f++] = 0x16;

  writeFrame(frame, f);
}

void MBusSlave::writeFrame(const uint8_t *data, size_t len) {
  serial_.write(data, len);
  serial_.flush();
  lastTxHex_ = hexString(data, len);
  txFrames_++;
}

String MBusSlave::hexString(const uint8_t *data, size_t len) {
  String out;
  for (size_t i = 0; i < len; ++i) {
    if (i) out += ' ';
    if (data[i] < 0x10) out += '0';
    out += String(data[i], HEX);
  }
  out.toUpperCase();
  return out;
}
