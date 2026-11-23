#pragma once
#include <Arduino.h>

// Small persistent settings store (ESP32 NVS via Preferences).
namespace settings {

enum Mode : uint8_t { MODE_BRIDGE = 0, MODE_BLE = 1 };

struct Data {
  char ssid[33];
  char pass[65];
  uint8_t mode;   // MODE_BRIDGE: buttons go to the PC bridge over Wi-Fi; MODE_BLE: buttons go over Bluetooth
};

void load();                                       // call once at boot
const Data& get();
bool hasWifi();                                    // true when credentials were saved from the setup page
void saveWifi(const char* ssid, const char* pass);
void clearWifi();
void saveMode(uint8_t mode);

}  // namespace settings
