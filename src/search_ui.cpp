#include "search_ui.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include "bridge_cfg.h"
#include "secrets.h"
#include "config.h"

#ifndef SHOW_SEARCH_BUTTON
#define SHOW_SEARCH_BUTTON 0   // 1 = show the search button on the player screen
#endif

namespace search_ui {
namespace {

constexpr int MAX_RESULTS = 8;
enum State : uint8_t { IDLE = 0, BUSY = 1, DONE = 2, FAILED = 3 };

struct Result { char id[24]; char title[56]; char artist[44]; };

Result results[MAX_RESULTS];
volatile int nResults = 0;
volatile uint8_t state = IDLE;          // written by the worker task, read by the LVGL timer
char errText[56] = "";
char queryBuf[80] = "";
char playId[24] = "";
volatile bool taskRunning = false;
uint8_t shown = IDLE;                    // last state the LVGL timer rendered

lv_obj_t *scr, *openBtn, *ta, *kb, *list, *status;
lv_obj_t* prevScr = nullptr;

String urlEncode(const char* s) {
  String o;
  const char* hex = "0123456789ABCDEF";
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
  }
  return o;
}

bool openUrl(HTTPClient& h, WiFiClient& c, const String& path) {
  if (!h.begin(c, bridge_cfg::host(), BRIDGE_PORT, path)) return false;
  if (BRIDGE_TOKEN[0]) h.addHeader("X-Token", BRIDGE_TOKEN);
  h.setConnectTimeout(2000);
  h.setTimeout(8000);
  return true;
}

void searchTask(void*) {
  WiFiClient c;
  HTTPClient h;
  uint8_t outcome = FAILED;
  int n = 0;
  strlcpy(errText, "Search failed", sizeof errText);
  if (WiFi.status() != WL_CONNECTED) {
    strlcpy(errText, "No Wi-Fi", sizeof errText);
  } else if (openUrl(h, c, String("/search?q=") + urlEncode(queryBuf))) {
    int code = h.GET();
    if (code == 200) {
      String body = h.getString();
      JsonDocument d;
      if (!deserializeJson(d, body)) {
        if (d["error"].is<const char*>()) {
          strlcpy(errText, d["error"] | "Search failed", sizeof errText);
        } else {
          for (JsonObject t : d["results"].as<JsonArray>()) {
            if (n >= MAX_RESULTS) break;
            strlcpy(results[n].id, t["id"] | "", sizeof results[n].id);
            strlcpy(results[n].title, t["title"] | "", sizeof results[n].title);
            strlcpy(results[n].artist, t["artist"] | "", sizeof results[n].artist);
            if (results[n].id[0]) n++;
          }
          nResults = n;
          if (n == 0) strlcpy(errText, "No results", sizeof errText);
          outcome = n ? DONE : FAILED;
        }
      }
    } else if (code == 401) {
      strlcpy(errText, "Bridge token mismatch", sizeof errText);
    } else if (code < 0) {
      strlcpy(errText, "PC bridge unreachable", sizeof errText);
    }
    h.end();
  }
  state = outcome;
  taskRunning = false;
  vTaskDelete(nullptr);
}

void playTask(void*) {
  WiFiClient c;
  HTTPClient h;
  if (WiFi.status() == WL_CONNECTED && openUrl(h, c, String("/play?id=") + playId)) {
    h.POST("");
    h.end();
  }
  taskRunning = false;
  vTaskDelete(nullptr);
}

void startSearch() {
  if (taskRunning) return;
  const char* q = lv_textarea_get_text(ta);
  if (!q || !q[0]) return;
  strlcpy(queryBuf, q, sizeof queryBuf);
  taskRunning = true;
  state = BUSY;
  shown = BUSY;
  lv_obj_clean(list);
  lv_label_set_text(status, "Searching...");
  lv_obj_remove_flag(status, LV_OBJ_FLAG_HIDDEN);
  if (xTaskCreate(searchTask, "spsearch", 10240, nullptr, 1, nullptr) != pdPASS) {
    taskRunning = false;
    state = FAILED;
    strlcpy(errText, "Out of memory", sizeof errText);
  }
}

void layout(bool keyboardShown) {
  if (keyboardShown) {
    lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(list, 122);
  } else {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(list, 266);
  }
}

void closeSearch() {
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  if (SHOW_SEARCH_BUTTON) lv_obj_remove_flag(openBtn, LV_OBJ_FLAG_HIDDEN);
  if (prevScr) lv_screen_load(prevScr);
}

void openSearch() {
  prevScr = lv_screen_active();
  lv_obj_add_flag(openBtn, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr);
  layout(true);
}

void onResultClick(lv_event_t* e) {
  intptr_t i = (intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= nResults || taskRunning) return;
  strlcpy(playId, results[i].id, sizeof playId);
  taskRunning = true;
  if (xTaskCreate(playTask, "spplay", 8192, nullptr, 1, nullptr) != pdPASS) taskRunning = false;
  closeSearch();
}

void onKeyboard(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) { layout(false); startSearch(); }
  else if (code == LV_EVENT_CANCEL) layout(false);
}

void onTextarea(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) layout(true);
}

void fillList() {
  lv_obj_clean(list);
  for (int i = 0; i < nResults; i++) {
    char line[112];
    snprintf(line, sizeof line, "%s - %s", results[i].title, results[i].artist);
    lv_obj_t* b = lv_list_add_button(list, nullptr, line);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x16201E), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(0xE8F1EE), 0);
    lv_obj_add_event_cb(b, onResultClick, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
}

void pollCb(lv_timer_t*) {
  uint8_t s = state;
  if (s == shown) return;
  shown = s;
  if (s == DONE) {
    lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
    fillList();
  } else if (s == FAILED) {
    lv_label_set_text(status, errText);
    lv_obj_remove_flag(status, LV_OBJ_FLAG_HIDDEN);
  }
}

lv_obj_t* roundBtn(lv_obj_t* parent, int x, int y, const char* sym, lv_event_cb_t cb) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, 34, 34);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x16201E), 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, sym);
  lv_obj_set_style_text_color(l, lv_color_hex(0xE8F1EE), 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

}  // namespace

void begin() {
  // the search screen
  scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0F0E), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = roundBtn(scr, 4, 4, LV_SYMBOL_LEFT, [](lv_event_t*) { closeSearch(); });
  (void)back;

  ta = lv_textarea_create(scr);
  lv_obj_set_pos(ta, 44, 4);
  lv_obj_set_size(ta, 192, 34);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_max_length(ta, 60);
  lv_textarea_set_placeholder_text(ta, "Search songs");
  lv_obj_set_style_bg_color(ta, lv_color_hex(0x16201E), 0);
  lv_obj_set_style_text_color(ta, lv_color_hex(0xE8F1EE), 0);
  lv_obj_set_style_border_color(ta, lv_color_hex(0x2A3835), 0);
  lv_obj_add_event_cb(ta, onTextarea, LV_EVENT_CLICKED, nullptr);

  list = lv_list_create(scr);
  lv_obj_set_pos(list, 0, 44);
  lv_obj_set_size(list, 240, 122);
  lv_obj_set_style_bg_color(list, lv_color_hex(0x0A0F0E), 0);
  lv_obj_set_style_border_width(list, 0, 0);

  status = lv_label_create(scr);
  lv_label_set_text(status, "Type a song, then tap OK");
  lv_obj_set_style_text_color(status, lv_color_hex(0x8AA39C), 0);
  lv_obj_set_pos(status, 12, 56);

  kb = lv_keyboard_create(scr);
  lv_obj_set_size(kb, 240, 150);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_add_event_cb(kb, onKeyboard, LV_EVENT_ALL, nullptr);

  // floating open button on the top layer, between the mode and settings buttons
  openBtn = lv_button_create(lv_layer_top());
  lv_obj_set_size(openBtn, 34, 34);
  lv_obj_set_pos(openBtn, 103, 8);
  lv_obj_set_style_radius(openBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(openBtn, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(openBtn, LV_OPA_50, 0);
  lv_obj_set_style_shadow_width(openBtn, 0, 0);
  lv_obj_t* l = lv_label_create(openBtn);
  lv_label_set_text(l, LV_SYMBOL_LIST);
  lv_obj_set_style_text_color(l, lv_color_hex(0xE8F1EE), 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(openBtn, [](lv_event_t*) { openSearch(); }, LV_EVENT_CLICKED, nullptr);
  if (!SHOW_SEARCH_BUTTON) lv_obj_add_flag(openBtn, LV_OBJ_FLAG_HIDDEN);

  lv_timer_create(pollCb, 150, nullptr);
}

}  // namespace search_ui
