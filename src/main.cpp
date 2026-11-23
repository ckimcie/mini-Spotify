#include <Arduino.h>
#include <WiFi.h>
#include "hal.h"
#include "ui.h"
#include "spotify_api.h"
#include "secrets.h"

void setup() {
  Serial.begin(115200);

  if (!hal_display_begin()) {
    Serial.println("display init failed");
    for (;;) delay(1000);
  }
  ui::build();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  sp_api::begin();
}

void loop() {
  static bool announced = false;
  if (!announced && WiFi.status() == WL_CONNECTED) {
    announced = true;
    Serial.printf("IP: %s -> bridge %s\n", WiFi.localIP().toString().c_str(), BRIDGE_HOST);
  }
  hal_loop();
  delay(5);
}
