#include <Arduino.h>
#include <WiFi.h>
#include "hal.h"
#include "ui.h"
#include "search_ui.h"
#include "spotify_api.h"
#include "secrets.h"
#include "settings.h"
#include "wifi_setup.h"
#include "ble_remote.h"

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // never block the UI waiting for a USB serial host

  if (!hal_display_begin()) {
    Serial.println("display init failed");
    for (;;) delay(1000);
  }

  settings::load();          // saved Wi-Fi + PC-bridge / Bluetooth mode
  ui::build();
  search_ui::begin();

  wifi_setup::begin();       // saved Wi-Fi, secrets.h fallback, or the setup portal
  sp_api::begin();

  if (settings::get().mode == settings::MODE_BLE) ble_remote::setEnabled(true);
}

static void diagNote(uint32_t ms) {
  static uint32_t worst = 0, last = 0, n = 0;
  if (ms > worst) worst = ms;
  n++;
  if (millis() - last >= 5000) {
    Serial.printf("[DIAG] loops=%u worst_hal_loop=%ums heap=%u psram=%u rssi=%d\n",
                  (unsigned)n, (unsigned)worst, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(), WiFi.RSSI());
    worst = 0; n = 0; last = millis();
  }
}

void loop() {
  static bool announced = false;
  if (!announced && WiFi.status() == WL_CONNECTED) {
    announced = true;
    Serial.printf("IP: %s -> bridge %s\n", WiFi.localIP().toString().c_str(), BRIDGE_HOST);
  }
  wifi_setup::loop();
  { uint32_t _t = millis(); hal_loop(); diagNote(millis() - _t); }
  delay(5);
}
