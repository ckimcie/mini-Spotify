#include "wifi_setup.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "settings.h"
#include "bridge_cfg.h"
#include "bridge_cfg.h"
#include "secrets.h"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

namespace wifi_setup {
namespace {

const char* AP_NAME = "MiniSpotify-Setup";
const uint32_t FIRST_CONNECT_TIMEOUT_MS = 25000;

WebServer server(80);
DNSServer dns;
bool portal = false;
bool autoPortal = false;      // opened by the timeout (closes itself if Wi-Fi comes up after all)
bool routesSet = false;
bool everConnected = false;
uint32_t beganAt = 0;

void beginSta() {
  const char* ssid = settings::hasWifi() ? settings::get().ssid : WIFI_SSID;
  const char* pass = settings::hasWifi() ? settings::get().pass : WIFI_PASS;
  if (!ssid[0]) return;
  WiFi.begin(ssid, pass);
  WiFi.setSleep(false);   // modem sleep adds 100-300 ms latency spikes to every request
  beganAt = millis();
}

String esc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

void handleRoot() {
  int n = WiFi.scanNetworks(false, false);
  String h;
  h.reserve(2048);
  h += F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>Mini Spotify setup</title><style>"
         "body{font-family:sans-serif;background:#0A0F0E;color:#E8F1EE;max-width:420px;margin:24px auto;padding:0 16px}"
         "input,button{width:100%;padding:12px;margin:6px 0 14px;border-radius:8px;border:1px solid #2A3835;background:#16201E;color:#E8F1EE;font-size:16px;box-sizing:border-box}"
         "button{background:#1ED760;color:#0A0F0E;font-weight:bold;border:0}h2{margin-top:0}</style></head><body>"
         "<h2>Mini Spotify Wi-Fi</h2><form method=POST action=/save>"
         "<label>Network</label><input name=s list=nets autocomplete=off placeholder='choose or type the name' required>"
         "<datalist id=nets>");
  for (int i = 0; i < n; i++) {
    h += "<option value=\"" + esc(WiFi.SSID(i)) + "\">";
  }
  h += F("</datalist><label>Password</label><input name=p type=password placeholder='leave empty for an open network'>"
         "<label>PC bridge address</label><input name=b placeholder='PC IP, e.g. 192.168.1.20' value=\"");
  h += esc(bridge_cfg::host());
  h += F("\"><button type=submit>Save and restart</button></form></body></html>");
  WiFi.scanDelete();
  server.send(200, "text/html", h);
}

void handleSave() {
  String s = server.arg("s");
  String p = server.arg("p");
  if (s.length() == 0 || s.length() > 32 || p.length() > 64) {
    server.send(400, "text/plain", "Network name missing or too long");
    return;
  }
  if (!bridge_cfg::save(server.arg("b").c_str())) {
    server.send(400, "text/plain", "Bridge address invalid");
    return;
  }
  if (!bridge_cfg::save(server.arg("b").c_str())) {
    server.send(400, "text/plain", "Bridge address invalid");
    return;
  }
  settings::saveWifi(s.c_str(), p.c_str());
  server.send(200, "text/html",
              "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
              "<body style='font-family:sans-serif;background:#0A0F0E;color:#E8F1EE;padding:24px'>"
              "<h2>Saved</h2><p>The board is restarting and will join the network now.</p></body>");
  delay(800);
  ESP.restart();
}

void handleRedirect() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

}  // namespace

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (!settings::hasWifi() && !WIFI_SSID[0]) {
    startPortal();
    return;
  }
  beginSta();
}

void startPortal() {
  if (portal) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NAME);
  delay(100);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  if (!routesSet) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleRedirect);
    routesSet = true;
  }
  server.begin();
  portal = true;
}

void stopPortal() {
  if (!portal) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portal = false;
  autoPortal = false;
  beginSta();
}

bool portalActive() { return portal; }

void loop() {
  if (portal) {
    dns.processNextRequest();
    server.handleClient();
    if (autoPortal && WiFi.status() == WL_CONNECTED) stopPortal();
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    everConnected = true;
    return;
  }
  // only the very first connection after boot can trigger the portal, so a router reboot never pops it up
  if (!everConnected && beganAt && millis() - beganAt > FIRST_CONNECT_TIMEOUT_MS) {
    autoPortal = true;
    startPortal();
  }
}

}  // namespace wifi_setup
