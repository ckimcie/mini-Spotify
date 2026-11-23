#pragma once
// PC bridge address: saved from the setup page, falls back to BRIDGE_HOST in secrets.h.
namespace bridge_cfg {
const char* host();
bool save(const char* host);   // empty string clears the saved value; false if the address is invalid
}
