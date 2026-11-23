#include "ui.h"
#include <lvgl.h>
#include "spotify_api.h"
#include "ui_globals.h"
#include "settings.h"
#include "ble_remote.h"
#include "wifi_setup.h"

#define C_BG     lv_color_hex(UI_C_BG)
#define C_PANEL  lv_color_hex(UI_C_PANEL)
#define C_TEXT   lv_color_hex(UI_C_TEXT)
#define C_DIM    lv_color_hex(UI_C_DIM)
#define C_ACCENT lv_color_hex(UI_C_ACCENT)
#define C_TRACK  lv_color_hex(UI_C_TRACK)
#define C_BLACK  lv_color_hex(0x000000)

#if LV_FONT_MONTSERRAT_20
#define F_TITLE (&lv_font_montserrat_20)
#else
#define F_TITLE LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_14
#define F_SMALL (&lv_font_montserrat_14)
#else
#define F_SMALL LV_FONT_DEFAULT
#endif

namespace ui {
namespace {

lv_obj_t *artBox, *artImg, *artPh, *lblTitle, *lblArtist, *seek, *lblPos, *lblDur, *lblPlay;
lv_obj_t *lblModeSym, *lblBle;
lv_obj_t *panelSettings, *panelPortal;
lv_image_dsc_t artDsc[2];   // alternate so LVGL always sees a new source pointer
int artIdx = 0;
uint16_t* artPx = nullptr;
uint32_t curDur = 0;
int lastVolume = -1;

bool bleMode() { return settings::get().mode == settings::MODE_BLE; }

void setText(lv_obj_t* l, const char* s) {
  if (strcmp(lv_label_get_text(l), s) != 0) lv_label_set_text(l, s);  // avoid resetting the scroll
}

void setHidden(lv_obj_t* o, bool hide) {
  if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void fmt(char* b, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  snprintf(b, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

// The bridge may deliver any cover size; the image widget stretches it to the full 240x240 slot.
void setArt(const sp_api::ArtImage& a) {
  uint16_t* old = artPx;
  artPx = a.px;
  artIdx ^= 1;
  lv_image_dsc_t* d = &artDsc[artIdx];
  memset(d, 0, sizeof *d);
  d->header.magic = LV_IMAGE_HEADER_MAGIC;
  d->header.cf = LV_COLOR_FORMAT_RGB565;
  d->header.w = a.w;
  d->header.h = a.h;
  d->header.stride = a.stride * 2;
  d->data = (const uint8_t*)a.px;
  d->data_size = (uint32_t)a.stride * a.h * 2;
  lv_image_set_src(artImg, d);
  lv_obj_invalidate(artImg);
  if (old) free(old);
}

void refreshMode() {
  bool ble = bleMode();
  setText(lblModeSym, ble ? LV_SYMBOL_BLUETOOTH : LV_SYMBOL_WIFI);
  setHidden(lblBle, !ble);
  lv_obj_set_style_opa(seek, ble ? LV_OPA_40 : LV_OPA_COVER, 0);   // seeking only works through the bridge
}

void tick(lv_timer_t*) {
  sp_api::ArtImage a;
  if (sp_api::takeArt(a)) setArt(a);

  sp_api::NowPlaying n;
  sp_api::snapshot(n);
  lastVolume = n.volume;
  char st[72];
  sp_api::getStatus(st, sizeof st);

  if (n.active && n.title[0]) {
    setText(lblTitle, n.title);
    setText(lblArtist, n.artist);
  } else {
    setText(lblTitle, "Nothing playing");
    setText(lblArtist, st[0] ? st : "");
  }
  bool showArt = n.active && artPx;
  setHidden(artImg, !showArt);
  setHidden(artPh, showArt);

  curDur = n.durationMs;
  uint32_t pos = n.progressMs + (n.playing ? millis() - n.stamp : 0);
  if (n.durationMs && pos > n.durationMs) pos = n.durationMs;
  if (!n.active) pos = 0;

  if (!lv_obj_has_state(seek, LV_STATE_PRESSED))
    lv_slider_set_value(seek, n.durationMs ? (int32_t)((uint64_t)pos * 1000 / n.durationMs) : 0, LV_ANIM_OFF);
  char b[12];
  fmt(b, sizeof b, pos);              setText(lblPos, b);
  fmt(b, sizeof b, n.durationMs);     setText(lblDur, n.active ? b : "0:00");
  setText(lblPlay, n.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

  if (bleMode()) setText(lblBle, ble_remote::statusText());
  setHidden(panelPortal, !wifi_setup::portalActive());
}

// ---- actions: PC bridge over Wi-Fi, or Bluetooth media keys, depending on the mode ----
void onPrev(lv_event_t*) {
  if (bleMode()) ble_remote::key(ble_remote::PREV); else sp_api::send(sp_api::CMD_PREV);
}
void onNext(lv_event_t*) {
  if (bleMode()) ble_remote::key(ble_remote::NEXT); else sp_api::send(sp_api::CMD_NEXT);
}
void onPlay(lv_event_t*) {
  if (bleMode()) ble_remote::key(ble_remote::PLAY_PAUSE); else sp_api::send(sp_api::CMD_TOGGLE);
}
void stepVol(int dir) {
  if (bleMode()) { ble_remote::key(dir > 0 ? ble_remote::VOL_UP : ble_remote::VOL_DOWN); return; }
  if (lastVolume < 0) return;                       // device doesn't report a volume
  int v = lastVolume + dir * UI_VOL_STEP;
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  sp_api::send(sp_api::CMD_VOLUME, v);
}
void onVolDown(lv_event_t*) { stepVol(-1); }
void onVolUp(lv_event_t*)   { stepVol(+1); }
void onSeek(lv_event_t*) {
  if (!bleMode() && curDur)
    sp_api::send(sp_api::CMD_SEEK, (int)((uint64_t)lv_slider_get_value(seek) * curDur / 1000));
}
void onMode(lv_event_t*) {
  uint8_t m = bleMode() ? settings::MODE_BRIDGE : settings::MODE_BLE;
  settings::saveMode(m);
  ble_remote::setEnabled(m == settings::MODE_BLE);
  refreshMode();
}
void onSettings(lv_event_t*)    { setHidden(panelSettings, false); }
void onCloseSettings(lv_event_t*) { setHidden(panelSettings, true); }
void onWifiSetup(lv_event_t*)   { setHidden(panelSettings, true); wifi_setup::startPortal(); }
void onForgetWifi(lv_event_t*)  { settings::clearWifi(); setHidden(panelSettings, true); wifi_setup::startPortal(); }
void onPortalCancel(lv_event_t*) { wifi_setup::stopPortal(); }

// ---- widget helpers ----
lv_obj_t* mkBtn(lv_obj_t* parent, int size, const char* sym, lv_color_t bg, lv_opa_t bgOpa,
                lv_color_t fg, lv_event_cb_t cb, lv_obj_t** symLabel = nullptr) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, size, size);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_bg_opa(b, bgOpa, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_obj_set_style_text_font(l, F_SMALL, 0);
  lv_label_set_text(l, sym);
  lv_obj_set_style_text_color(l, fg, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  if (symLabel) *symLabel = l;
  return b;
}

lv_obj_t* mkTextBtn(lv_obj_t* parent, const char* text, lv_color_t bg, lv_color_t fg, lv_event_cb_t cb) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, 190, 36);
  lv_obj_set_style_radius(b, 18, 0);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_obj_set_style_text_font(l, F_SMALL, 0);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, fg, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

lv_obj_t* mkLabel(lv_obj_t* p, const lv_font_t* f, lv_color_t c, int y, int w) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
  lv_label_set_text(l, "");
  return l;
}

void styleSlider(lv_obj_t* s, int w, int h, int knob) {
  lv_slider_set_range(s, 0, 1000);
  lv_obj_set_size(s, w, h);
  lv_obj_set_style_bg_color(s, C_TRACK, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s, C_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s, C_TEXT, LV_PART_KNOB);
  lv_obj_set_style_pad_all(s, knob, LV_PART_KNOB);
  lv_obj_set_ext_click_area(s, 14);
}

lv_obj_t* mkPanel(lv_obj_t* scr, int w, int h, lv_color_t bg) {
  lv_obj_t* p = lv_obj_create(scr);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_bg_color(p, bg, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

void buildOverlays(lv_obj_t* scr) {
  // ---- settings overlay ----
  panelSettings = mkPanel(scr, 220, 190, C_PANEL);
  lv_obj_set_style_radius(panelSettings, 16, 0);
  lv_obj_align(panelSettings, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_flex_flow(panelSettings, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panelSettings, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  mkTextBtn(panelSettings, "Wi-Fi setup",        C_ACCENT, C_BG,   onWifiSetup);
  mkTextBtn(panelSettings, "Forget Wi-Fi",       C_TRACK,  C_TEXT, onForgetWifi);
  mkTextBtn(panelSettings, "Close",              C_TRACK,  C_TEXT, onCloseSettings);
  lv_obj_add_flag(panelSettings, LV_OBJ_FLAG_HIDDEN);

  // ---- setup-portal overlay ----
  panelPortal = mkPanel(scr, UI_W, UI_H, C_BG);
  lv_obj_align(panelPortal, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t* t = lv_label_create(panelPortal);
  lv_obj_set_style_text_font(t, F_TITLE, 0);
  lv_obj_set_style_text_color(t, C_TEXT, 0);
  lv_label_set_text(t, "Wi-Fi setup");
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_t* d = lv_label_create(panelPortal);
  lv_obj_set_style_text_font(d, F_SMALL, 0);
  lv_obj_set_style_text_color(d, C_DIM, 0);
  lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(d, 210);
  lv_label_set_text(d, "1. On your phone, join the\nWi-Fi network\nMiniSpotify-Setup\n\n2. Open 192.168.4.1\nif the page doesn't appear\n\n3. Pick your network\nand save");
  lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 80);
  lv_obj_t* c = mkTextBtn(panelPortal, "Cancel", C_TRACK, C_TEXT, onPortalCancel);
  lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -24);
  lv_obj_add_flag(panelPortal, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

void build() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // ---- full-width cover (240x240) + placeholder ----
  artBox = lv_obj_create(scr);
  lv_obj_remove_style_all(artBox);
  lv_obj_set_size(artBox, UI_W, UI_ART);
  lv_obj_align(artBox, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(artBox, C_PANEL, 0);
  lv_obj_set_style_bg_opa(artBox, LV_OPA_COVER, 0);
  lv_obj_remove_flag(artBox, LV_OBJ_FLAG_SCROLLABLE);

  artPh = lv_label_create(artBox);
  lv_label_set_text(artPh, LV_SYMBOL_AUDIO);
  lv_obj_set_style_text_color(artPh, C_DIM, 0);
  lv_obj_center(artPh);
  artImg = lv_image_create(artBox);
  lv_image_set_inner_align(artImg, LV_IMAGE_ALIGN_STRETCH);
  lv_obj_set_size(artImg, UI_W, UI_ART);
  lv_obj_align(artImg, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_add_flag(artImg, LV_OBJ_FLAG_HIDDEN);

  // ---- dark gradient over the bottom of the cover (transparent -> black) ----
  lv_obj_t* fade = lv_obj_create(scr);
  lv_obj_remove_style_all(fade);
  lv_obj_set_size(fade, UI_W, UI_FADE_H);
  lv_obj_align(fade, LV_ALIGN_TOP_LEFT, 0, UI_ART - UI_FADE_H);
  lv_obj_set_style_bg_color(fade, C_BLACK, 0);
  lv_obj_set_style_bg_grad_color(fade, C_BLACK, 0);
  lv_obj_set_style_bg_grad_dir(fade, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_opa(fade, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_grad_opa(fade, LV_OPA_90, 0);
  lv_obj_set_style_bg_opa(fade, LV_OPA_COVER, 0);
  lv_obj_remove_flag(fade, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(fade, LV_OBJ_FLAG_CLICKABLE);

  // ---- title / artist / progress on the gradient ----
  lblTitle  = mkLabel(scr, F_TITLE, C_TEXT, 154, 220);
  lblArtist = mkLabel(scr, F_SMALL, C_DIM, 180, 220);

  seek = lv_slider_create(scr);
  styleSlider(seek, 208, 6, 4);
  lv_obj_align(seek, LV_ALIGN_TOP_MID, 0, 206);
  lv_obj_add_event_cb(seek, onSeek, LV_EVENT_RELEASED, nullptr);

  lblPos = lv_label_create(scr);
  lv_obj_set_style_text_font(lblPos, F_SMALL, 0);
  lv_obj_set_style_text_color(lblPos, C_DIM, 0);
  lv_obj_align(lblPos, LV_ALIGN_TOP_LEFT, 16, 218);
  lv_label_set_text(lblPos, "0:00");
  lblDur = lv_label_create(scr);
  lv_obj_set_style_text_font(lblDur, F_SMALL, 0);
  lv_obj_set_style_text_color(lblDur, C_DIM, 0);
  lv_obj_align(lblDur, LV_ALIGN_TOP_RIGHT, -16, 218);
  lv_label_set_text(lblDur, "0:00");

  // ---- corner buttons on the cover: mode switch (left), settings (right) ----
  lv_obj_t* bm = mkBtn(scr, 34, LV_SYMBOL_WIFI, C_BLACK, LV_OPA_50, C_TEXT, onMode, &lblModeSym);
  lv_obj_align(bm, LV_ALIGN_TOP_LEFT, 8, 8);
  lv_obj_t* bs = mkBtn(scr, 34, LV_SYMBOL_SETTINGS, C_BLACK, LV_OPA_50, C_TEXT, onSettings);
  lv_obj_align(bs, LV_ALIGN_TOP_RIGHT, -8, 8);

  lblBle = lv_label_create(scr);
  lv_obj_set_style_text_font(lblBle, F_SMALL, 0);
  lv_obj_set_style_text_color(lblBle, C_TEXT, 0);
  lv_obj_set_style_bg_color(lblBle, C_BLACK, 0);
  lv_obj_set_style_bg_opa(lblBle, LV_OPA_50, 0);
  lv_obj_set_style_radius(lblBle, 10, 0);
  lv_obj_set_style_pad_hor(lblBle, 8, 0);
  lv_obj_set_style_pad_ver(lblBle, 3, 0);
  lv_label_set_text(lblBle, "");
  lv_obj_align(lblBle, LV_ALIGN_TOP_MID, 0, 14);

  // ---- bottom row: vol-  prev  play  next  vol+ ----
  lv_obj_t* row = lv_obj_create(scr);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, UI_W, UI_BTN_ROW_H);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, UI_BTN_ROW);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  mkBtn(row, 36, LV_SYMBOL_VOLUME_MID, C_PANEL, LV_OPA_COVER, C_TEXT, onVolDown);
  mkBtn(row, 42, LV_SYMBOL_PREV,       C_PANEL, LV_OPA_COVER, C_TEXT, onPrev);
  lv_obj_t* bp = mkBtn(row, 52, LV_SYMBOL_PLAY, C_ACCENT, LV_OPA_COVER, C_BG, onPlay, &lblPlay);
  (void)bp;
  mkBtn(row, 42, LV_SYMBOL_NEXT,       C_PANEL, LV_OPA_COVER, C_TEXT, onNext);
  mkBtn(row, 36, LV_SYMBOL_VOLUME_MAX, C_PANEL, LV_OPA_COVER, C_TEXT, onVolUp);

  buildOverlays(scr);
  refreshMode();
  lv_timer_create(tick, UI_TICK_MS, nullptr);
}

}  // namespace ui
