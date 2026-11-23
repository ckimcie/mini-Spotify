// Now-playing client for the PC bridge (bridge/mini_player_bridge.py) over plain HTTP on the LAN.
#include "spotify_api.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <JPEGDEC.h>
#include "secrets.h"
#include "config.h"

namespace sp_api {
namespace {

struct CmdMsg { Cmd c; int arg; };

SemaphoreHandle_t mtx;
QueueHandle_t cmdQ;
NowPlaying np;
ArtImage pendingArt;
bool artReady = false;
char statusBuf[72] = "Starting...";
char shownArtId[16] = "";
WiFiClient client;
HTTPClient http;
uint32_t nextPollAt = 0;

struct Lock {
  Lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mtx); }
};

void setStatus(const char* s) {
  Lock l;
  strlcpy(statusBuf, s, sizeof statusBuf);
}

bool open(const String& path) {
  if (!http.begin(client, BRIDGE_HOST, BRIDGE_PORT, path)) return false;
  if (BRIDGE_TOKEN[0]) http.addHeader("X-Token", BRIDGE_TOKEN);
  return true;
}

uint32_t currentPos(const NowPlaying& n) {
  uint32_t p = n.progressMs + (n.playing ? millis() - n.stamp : 0);
  return n.durationMs && p > n.durationMs ? n.durationMs : p;
}

// ---------- album art ----------
uint16_t* gOut;
int gStride;

int jpgDraw(JPEGDRAW* p) {
  for (int r = 0; r < p->iHeight; r++)
    memcpy(gOut + (size_t)(p->y + r) * gStride + p->x, p->pPixels + (size_t)r * p->iWidth,
           (size_t)p->iWidth * 2);
  return 1;
}

bool fetchArt() {
  if (!open("/art")) return false;
  int code = http.GET();
  int len = http.getSize();
  if (code != 200 || len <= 0 || len > MAX_ART_BYTES) { http.end(); return false; }

  uint8_t* buf = (uint8_t*)ps_malloc(len);
  if (!buf) { http.end(); return false; }
  WiFiClient* s = http.getStreamPtr();
  int got = 0;
  uint32_t t0 = millis();
  while (got < len && millis() - t0 < 8000) {
    int a = s->available();
    if (a > 0) got += s->readBytes(buf + got, min(a, len - got));
    else delay(5);
  }
  http.end();
  if (got < len) { free(buf); return false; }

  JPEGDEC* jpg = new JPEGDEC();
  bool ok = false;
  uint16_t* out = nullptr;
  if (jpg->openRAM(buf, len, jpgDraw)) {
    int w = jpg->getWidth(), hgt = jpg->getHeight();
    int scale = 0, div = 1;
    if (w >= 560) { scale = JPEG_SCALE_QUARTER; div = 4; }
    else if (w >= 280) { scale = JPEG_SCALE_HALF; div = 2; }
    int dw = w / div, dh = hgt / div;
    int stride = (dw + 15) & ~15, rows = (dh + 15) & ~15;
    out = (uint16_t*)ps_malloc((size_t)stride * rows * 2);
    if (out) {
      memset(out, 0, (size_t)stride * rows * 2);
      gOut = out; gStride = stride;
      jpg->setPixelType(RGB565_LITTLE_ENDIAN);
      if (jpg->decode(0, 0, scale)) {
        Lock l;
        if (pendingArt.px) free(pendingArt.px);
        pendingArt = {out, dw, dh, stride};
        artReady = true;
        ok = true;
      }
    }
    jpg->close();
  }
  delete jpg;
  free(buf);
  if (!ok && out) free(out);
  return ok;
}

// ---------- bridge ----------
void doCommand(const CmdMsg& m) {
  const char* c = "";
  int v = 0;
  switch (m.c) {
    case CMD_TOGGLE:
      c = "toggle";
      { Lock l; np.progressMs = currentPos(np); np.stamp = millis(); np.playing = !np.playing; }
      break;
    case CMD_NEXT: c = "next"; break;
    case CMD_PREV: c = "prev"; break;
    case CMD_VOLUME:
      c = "volume"; v = constrain(m.arg, 0, 100);
      { Lock l; np.volume = v; }
      break;
    case CMD_SEEK:
      c = "seek"; v = m.arg;
      { Lock l; np.progressMs = m.arg; np.stamp = millis(); }
      break;
  }
  if (open(String("/cmd?c=") + c + "&v=" + v)) {
    int code = http.POST("");
    http.end();
    if (code < 0) setStatus("PC bridge unreachable");
  }
  nextPollAt = millis() + 250;
}

void doPoll() {
  if (!open("/state")) { setStatus("Bad bridge address"); return; }
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    http.end();
    JsonDocument d;
    if (deserializeJson(d, payload)) { setStatus("Bad response from bridge"); return; }

    if (!(d["active"] | false)) {
      Lock l;
      np.active = false; np.playing = false;
      strlcpy(statusBuf, d["error"] | "Nothing playing - play something on your PC", sizeof statusBuf);
      return;
    }
    NowPlaying t;
    t.active = true;
    t.playing = d["playing"] | false;
    t.progressMs = d["progressMs"] | 0;
    t.durationMs = d["durationMs"] | 0;
    t.volume = d["volume"] | -1;
    t.stamp = millis();
    strlcpy(t.title, d["title"] | "", sizeof t.title);
    strlcpy(t.artist, d["artist"] | "", sizeof t.artist);
    strlcpy(t.device, d["device"] | "", sizeof t.device);
    const char* aid = d["artId"] | "";
    { Lock l; np = t; statusBuf[0] = 0; }

    if (aid[0] && strcmp(aid, shownArtId) != 0) {
      strlcpy(shownArtId, aid, sizeof shownArtId);   // set first so a failure isn't retried every poll
      fetchArt();
    }
  } else if (code == 401) {
    http.end();
    setStatus("Bridge token mismatch");
  } else {
    http.end();
    Serial.printf("bridge poll -> %d\n", code);
    Lock l;
    np.active = false; np.playing = false;
    strlcpy(statusBuf, code < 0 ? "PC bridge unreachable" : "Bridge error", sizeof statusBuf);
  }
}

void netTask(void*) {
  for (;;) {
    CmdMsg m;
    if (xQueueReceive(cmdQ, &m, pdMS_TO_TICKS(50)) == pdTRUE && WiFi.status() == WL_CONNECTED) doCommand(m);
    uint32_t now = millis();
    if ((int32_t)(now - nextPollAt) >= 0) {
      nextPollAt = now + POLL_MS;
      if (WiFi.status() != WL_CONNECTED) setStatus("Connecting Wi-Fi...");
      else doPoll();
    }
  }
}

}  // namespace

void begin() {
  mtx = xSemaphoreCreateMutex();
  cmdQ = xQueueCreate(8, sizeof(CmdMsg));
  http.setReuse(false);
  http.setConnectTimeout(2000);
  http.setTimeout(4000);
  xTaskCreatePinnedToCore(netTask, "spnet", 12288, nullptr, 1, nullptr, 0);
}

bool snapshot(NowPlaying& out) {
  Lock l;
  out = np;
  return out.active;
}

void getStatus(char* buf, size_t n) {
  Lock l;
  strlcpy(buf, statusBuf, n);
}

void send(Cmd c, int arg) {
  CmdMsg m{c, arg};
  xQueueSend(cmdQ, &m, 0);
}

bool takeArt(ArtImage& out) {
  Lock l;
  if (!artReady) return false;
  out = pendingArt;
  pendingArt = ArtImage();
  artReady = false;
  return true;
}

}  // namespace sp_api
