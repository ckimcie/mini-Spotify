#include "ui.h"
#include <lvgl.h>
#include "spotify_api.h"

// ---- palette (change here to retheme) ----
#define C_BG     lv_color_hex(0x0A0F0E)
#define C_PANEL  lv_color_hex(0x16201E)
#define C_TEXT   lv_color_hex(0xE8F1EE)
#define C_DIM    lv_color_hex(0x8AA39C)
#define C_ACCENT lv_color_hex(0x1ED760)
#define C_TRACK  lv_color_hex(0x2A3835)

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

lv_obj_t *artBox, *artImg, *artPh, *lblTitle, *lblArtist, *seek, *lblPos, *lblDur, *lblPlay, *vol;
lv_image_dsc_t artDsc[2];   // alternate so LVGL always sees a new source pointer
int artIdx = 0;
uint16_t* artPx = nullptr;
uint32_t curDur = 0;

void setText(lv_obj_t* l, const char* s) {
  if (strcmp(lv_label_get_text(l), s) != 0) lv_label_set_text(l, s);  // avoid resetting the scroll
}

void fmt(char* b, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  snprintf(b, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

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
  lv_obj_set_size(artImg, a.w, a.h);
  lv_obj_invalidate(artImg);
  if (old) free(old);
}

void tick(lv_timer_t*) {
  sp_api::ArtImage a;
  if (sp_api::takeArt(a)) setArt(a);

  sp_api::NowPlaying n;
  sp_api::snapshot(n);
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
  if (showArt) lv_obj_remove_flag(artImg, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(artImg, LV_OBJ_FLAG_HIDDEN);
  if (showArt) lv_obj_add_flag(artPh, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(artPh, LV_OBJ_FLAG_HIDDEN);

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

  if (n.volume >= 0 && !lv_obj_has_state(vol, LV_STATE_PRESSED)) lv_slider_set_value(vol, n.volume, LV_ANIM_OFF);
}

void onPrev(lv_event_t*)  { sp_api::send(sp_api::CMD_PREV); }
void onNext(lv_event_t*)  { sp_api::send(sp_api::CMD_NEXT); }
void onPlay(lv_event_t*)  { sp_api::send(sp_api::CMD_TOGGLE); }
void onVol(lv_event_t*)   { sp_api::send(sp_api::CMD_VOLUME, lv_slider_get_value(vol)); }
void onSeek(lv_event_t*)  { if (curDur) sp_api::send(sp_api::CMD_SEEK, (int)((uint64_t)lv_slider_get_value(seek) * curDur / 1000)); }

lv_obj_t* mkBtn(lv_obj_t* parent, int size, const char* sym, lv_color_t bg, lv_color_t fg, lv_event_cb_t cb) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, size, size);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, sym);
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

}  // namespace

void build() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // album art (150x150) + placeholder
  artBox = lv_obj_create(scr);
  lv_obj_remove_style_all(artBox);
  lv_obj_set_size(artBox, 150, 150);
  lv_obj_align(artBox, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_radius(artBox, 14, 0);
  lv_obj_set_style_bg_color(artBox, C_PANEL, 0);
  lv_obj_set_style_bg_opa(artBox, LV_OPA_COVER, 0);
  lv_obj_set_style_clip_corner(artBox, true, 0);

  artPh = lv_label_create(artBox);
  lv_label_set_text(artPh, LV_SYMBOL_AUDIO);
  lv_obj_set_style_text_color(artPh, C_DIM, 0);
  lv_obj_center(artPh);
  artImg = lv_image_create(artBox);
  lv_obj_align(artImg, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_add_flag(artImg, LV_OBJ_FLAG_HIDDEN);

  lblTitle  = mkLabel(scr, F_TITLE, C_TEXT, 166, 220);
  lblArtist = mkLabel(scr, F_SMALL, C_DIM, 194, 220);

  seek = lv_slider_create(scr);
  styleSlider(seek, 208, 6, 4);
  lv_obj_align(seek, LV_ALIGN_TOP_MID, 0, 220);
  lv_obj_add_event_cb(seek, onSeek, LV_EVENT_RELEASED, nullptr);

  lblPos = lv_label_create(scr);
  lv_obj_set_style_text_font(lblPos, F_SMALL, 0);
  lv_obj_set_style_text_color(lblPos, C_DIM, 0);
  lv_obj_align(lblPos, LV_ALIGN_TOP_LEFT, 16, 230);
  lv_label_set_text(lblPos, "0:00");
  lblDur = lv_label_create(scr);
  lv_obj_set_style_text_font(lblDur, F_SMALL, 0);
  lv_obj_set_style_text_color(lblDur, C_DIM, 0);
  lv_obj_align(lblDur, LV_ALIGN_TOP_RIGHT, -16, 230);
  lv_label_set_text(lblDur, "0:00");

  lv_obj_t* row = lv_obj_create(scr);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 240, 52);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 250);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  mkBtn(row, 44, LV_SYMBOL_PREV, C_PANEL, C_TEXT, onPrev);
  lv_obj_t* bp = mkBtn(row, 52, LV_SYMBOL_PLAY, C_ACCENT, C_BG, onPlay);
  lblPlay = lv_obj_get_child(bp, 0);
  mkBtn(row, 44, LV_SYMBOL_NEXT, C_PANEL, C_TEXT, onNext);

  vol = lv_slider_create(scr);
  styleSlider(vol, 160, 4, 3);
  lv_obj_align(vol, LV_ALIGN_TOP_MID, 0, 310);
  lv_slider_set_range(vol, 0, 100);
  lv_obj_add_event_cb(vol, onVol, LV_EVENT_RELEASED, nullptr);

  lv_timer_create(tick, 250, nullptr);
}

}  // namespace ui
