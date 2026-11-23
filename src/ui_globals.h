#pragma once
// Shared UI constants ("globals"). Keep in sync with lvgl_ui/globals.xml, which mirrors these
// values for the LVGL XML / editor view of the same layout.

// ---- screen ----
#define UI_W        240
#define UI_H        320
#define UI_ART      240      // cover is shown full width, 240x240
#define UI_FADE_H   120      // height of the dark gradient over the bottom of the cover
#define UI_BTN_ROW  252      // y of the bottom control row
#define UI_BTN_ROW_H 52

// ---- palette (0xRRGGBB) ----
#define UI_C_BG      0x0A0F0E
#define UI_C_PANEL   0x16201E
#define UI_C_TEXT    0xE8F1EE
#define UI_C_DIM     0x8AA39C
#define UI_C_ACCENT  0x1ED760
#define UI_C_TRACK   0x2A3835

// ---- behaviour ----
#define UI_VOL_STEP  5       // % per press of the volume buttons in PC-bridge mode
#define UI_TICK_MS   250
