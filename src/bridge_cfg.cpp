#include "bridge_cfg.h"
#include <Arduino.h>
#include <Preferences.h>
#include "secrets.h"

namespace bridge_cfg {
namespace {
char buf[48];
bool loaded = false;
void ensure() {
  if (loaded) return;
  Preferences p;
  p.begin("minibridge", true);
  String s = p.getString("host", "");
  p.end();
  strlcpy(buf, s.c_str(), sizeof buf);
  loaded = true;
}
}  // namespace

const char* host() {
  ensure();
  return buf[0] ? buf : BRIDGE_HOST;
}

bool save(const char* h) {
  String s(h);
  s.trim();
  if (s.length() > 47) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (!(isalnum((unsigned char)c) || c == '.' || c == '-')) return false;
  }
  Preferences p;
  p.begin("minibridge", false);
  if (s.length()) p.putString("host", s); else p.remove("host");
  p.end();
  strlcpy(buf, s.c_str(), sizeof buf);
  loaded = true;
  return true;
}
}  // namespace bridge_cfg
