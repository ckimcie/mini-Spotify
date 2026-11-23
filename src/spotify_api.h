#pragma once
#include <Arduino.h>

namespace sp_api {

struct NowPlaying {
  bool active = false, playing = false;
  char title[96] = {0}, artist[96] = {0}, device[40] = {0};
  uint32_t durationMs = 0, progressMs = 0, stamp = 0;  // stamp = millis() when progressMs was sampled
  int volume = -1;                                      // -1 = device doesn't support volume
};

struct ArtImage {          // RGB565, little-endian, owned by whoever takes it (free(px))
  uint16_t* px = nullptr;
  int w = 0, h = 0, stride = 0;  // stride in pixels
};

enum Cmd : uint8_t { CMD_TOGGLE, CMD_NEXT, CMD_PREV, CMD_VOLUME, CMD_SEEK };

void begin();                          // starts the network task (core 0)
bool snapshot(NowPlaying& out);        // returns out.active
void getStatus(char* buf, size_t n);   // short human-readable state
void send(Cmd c, int arg = 0);         // queued, non-blocking
bool takeArt(ArtImage& out);           // true once per new album image
}
