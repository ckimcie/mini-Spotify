#include "hal.h"
#include <Arduino.h>
#include <lvgl.h>
#include "display/DisplayManager.h"   // copied from PULSEON by setup.sh

static DisplayManager display;

bool hal_display_begin() {
  if (!display.begin()) return false;
  lv_tick_set_cb([]() -> uint32_t { return millis(); });  // harmless if begin() already set it
  display.setSleepEnabled(false);                         // now-playing screen stays on
  return true;
}

void hal_loop() {
  display.loop();
  lv_timer_handler();   // a second call is harmless if display.loop() already runs it
}
