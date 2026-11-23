#include "settings.h"
#include <Preferences.h>

namespace settings {
namespace {
Data d = {"", "", MODE_BRIDGE};
Preferences prefs;
const char* NS = "minisp";
}  // namespace

void load() {
  prefs.begin(NS, true);
  String s = prefs.getString("ssid", "");
  String p = prefs.getString("pass", "");
  d.mode = prefs.getUChar("mode", MODE_BRIDGE);
  prefs.end();
  strlcpy(d.ssid, s.c_str(), sizeof d.ssid);
  strlcpy(d.pass, p.c_str(), sizeof d.pass);
  if (d.mode > MODE_BLE) d.mode = MODE_BRIDGE;
}

const Data& get() { return d; }

bool hasWifi() { return d.ssid[0] != 0; }

void saveWifi(const char* ssid, const char* pass) {
  strlcpy(d.ssid, ssid ? ssid : "", sizeof d.ssid);
  strlcpy(d.pass, pass ? pass : "", sizeof d.pass);
  prefs.begin(NS, false);
  prefs.putString("ssid", d.ssid);
  prefs.putString("pass", d.pass);
  prefs.end();
}

void clearWifi() {
  d.ssid[0] = 0;
  d.pass[0] = 0;
  prefs.begin(NS, false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
}

void saveMode(uint8_t mode) {
  d.mode = mode > MODE_BLE ? MODE_BRIDGE : mode;
  prefs.begin(NS, false);
  prefs.putUChar("mode", d.mode);
  prefs.end();
}

}  // namespace settings
