#pragma once

// Wi-Fi connection + first-run setup page.
//  - Uses credentials saved from the setup page; falls back to WIFI_SSID / WIFI_PASS from secrets.h
//    so an existing build keeps working.
//  - If nothing is configured, or the first connection fails for ~25 s, the board opens an access point
//    "MiniSpotify-Setup" with a captive page at http://192.168.4.1 where you pick a network.
namespace wifi_setup {

void begin();          // call once in setup(), after settings::load()
void loop();           // call every loop(); services the DNS + web server while the portal is open
void startPortal();    // open the setup access point now (e.g. from the settings overlay)
void stopPortal();     // close it and go back to the saved network
bool portalActive();

}  // namespace wifi_setup
