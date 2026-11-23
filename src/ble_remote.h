#pragma once

// BLE HID media remote: the board pairs like a small keyboard and sends media keys
// to whatever is playing on the phone or PC. One-way only (no title/cover over Bluetooth).
namespace ble_remote {

enum Key : unsigned char { PLAY_PAUSE, NEXT, PREV, VOL_UP, VOL_DOWN, MUTE };

void setEnabled(bool on);        // starts advertising (initialises the BLE stack on first use) / stops and disconnects
bool enabled();
bool connected();
void key(Key k);                 // press + release; ignored when not connected
const char* statusText();        // short text for the screen

}  // namespace ble_remote
