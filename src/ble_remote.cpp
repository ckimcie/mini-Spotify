#include "ble_remote.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

namespace ble_remote {
namespace {

// HID report map: one Consumer Control report (ID 1), 8 one-bit usages.
const uint8_t kReportMap[] = {
  0x05, 0x0C,        // Usage Page (Consumer)
  0x09, 0x01,        // Usage (Consumer Control)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID 1
  0x15, 0x00,        //   Logical Min 0
  0x25, 0x01,        //   Logical Max 1
  0x75, 0x01,        //   Report Size 1
  0x95, 0x08,        //   Report Count 8
  0x09, 0xCD,        //   Play/Pause      bit0
  0x09, 0xB5,        //   Next Track      bit1
  0x09, 0xB6,        //   Previous Track  bit2
  0x09, 0xE9,        //   Volume Up       bit3
  0x09, 0xEA,        //   Volume Down     bit4
  0x09, 0xE2,        //   Mute            bit5
  0x09, 0xB7,        //   Stop            bit6
  0x09, 0xB3,        //   Fast Forward    bit7
  0x81, 0x02,        //   Input (Data, Var, Abs)
  0xC0               // End Collection
};
const uint8_t kHidInfo[] = {0x11, 0x01, 0x00, 0x02};  // HID 1.11, no country, normally connectable
const uint8_t kReportRef[] = {0x01, 0x01};            // report ID 1, input report

const uint8_t kBit[] = {1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5};

NimBLEServer* server = nullptr;
NimBLECharacteristic* input = nullptr;
bool inited = false;
bool on = false;
volatile bool isConnected = false;

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override { isConnected = true; }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    isConnected = false;
    if (on) NimBLEDevice::getAdvertising()->start();
  }
};
ServerCb serverCb;

void init() {
  if (inited) return;
  NimBLEDevice::init("MiniSpotify");
  NimBLEDevice::setSecurityAuth(true, false, true);       // bonding, no MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);  // "just works" pairing

  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCb, false);

  NimBLEService* hid = server->createService(NimBLEUUID((uint16_t)0x1812));

  NimBLECharacteristic* info = hid->createCharacteristic(NimBLEUUID((uint16_t)0x2A4A), NIMBLE_PROPERTY::READ);
  info->setValue(kHidInfo, sizeof kHidInfo);

  NimBLECharacteristic* map = hid->createCharacteristic(NimBLEUUID((uint16_t)0x2A4B), NIMBLE_PROPERTY::READ);
  map->setValue(kReportMap, sizeof kReportMap);

  hid->createCharacteristic(NimBLEUUID((uint16_t)0x2A4C), NIMBLE_PROPERTY::WRITE_NR);  // control point

  NimBLECharacteristic* proto = hid->createCharacteristic(
      NimBLEUUID((uint16_t)0x2A4E), NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR);
  const uint8_t reportMode = 1;
  proto->setValue(&reportMode, 1);

  input = hid->createCharacteristic(
      NimBLEUUID((uint16_t)0x2A4D),
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
  NimBLEDescriptor* ref = input->createDescriptor(
      NimBLEUUID((uint16_t)0x2908), NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC, 2);
  ref->setValue(kReportRef, sizeof kReportRef);
  const uint8_t zero = 0;
  input->setValue(&zero, 1);

  hid->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setName("MiniSpotify");
  adv->setAppearance(0x03C0);                 // generic HID
  adv->addServiceUUID(NimBLEUUID((uint16_t)0x1812));
  inited = true;
}

}  // namespace

void setEnabled(bool enable) {
  if (enable) {
    init();
    on = true;
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv->isAdvertising() && !isConnected) adv->start();
  } else {
    on = false;
    if (!inited) return;
    NimBLEDevice::getAdvertising()->stop();
    for (auto h : server->getPeerDevices()) server->disconnect(h);
    isConnected = false;
  }
}

bool enabled() { return on; }
bool connected() { return on && isConnected; }

void key(Key k) {
  if (!connected() || !input || k > MUTE) return;
  uint8_t v = kBit[k];
  input->setValue(&v, 1);
  input->notify();
  delay(25);
  v = 0;
  input->setValue(&v, 1);
  input->notify();
}

const char* statusText() {
  if (!on) return "Bluetooth off";
  return isConnected ? "Bluetooth: connected" : "Pair: MiniSpotify";
}

}  // namespace ble_remote
