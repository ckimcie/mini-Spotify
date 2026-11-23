#!/usr/bin/env bash
# Pull the working Viewe display/touch layer from PULSEON. Safe to re-run (never overwrites).
set -e
SRC="${1:-$HOME/my-project/PULSEON/PULSEON_FIRMWARE}"
cd "$(dirname "$0")"
[ -d "$SRC" ] || { echo "PULSEON_FIRMWARE not found at $SRC (pass the path as arg 1)"; exit 1; }
mkdir -p lib include src/display
cp_new() { cp -r --update=none "$@"; }

for d in ESP32_Display_Panel ESP32_IO_Expander; do
  [ -d "$SRC/lib/$d" ] && cp_new "$SRC/lib/$d" lib/ || echo "warn: lib/$d not found"
done
for f in include/lvgl_compat.h include/lv_conf.h lv_conf.h; do
  [ -f "$SRC/$f" ] && cp_new "$SRC/$f" "$(dirname "$f")/" || true
done
shopt -s nullglob
for f in "$SRC"/esp_panel_*.h "$SRC"/ESP_Panel_*.h "$SRC"/include/esp_panel_*.h "$SRC"/include/ESP_Panel_*.h; do
  rel="${f#$SRC/}"; mkdir -p "$(dirname "$rel")"; cp_new "$f" "$rel"
done
[ -d "$SRC/src/display" ] && cp_new "$SRC/src/display/." src/display/
cp_new "$SRC/platformio.ini" platformio.pulseon.ini.ref
[ -f include/secrets.h ] || cp include/secrets.example.h include/secrets.h

# warn about local includes in src/display that don't resolve in this project
grep -ho '#include "[^"]*"' src/display/* 2>/dev/null | sed 's/#include "\(.*\)"/\1/' | sort -u | while read -r inc; do
  [ -e "src/display/$inc" ] || [ -e "src/$inc" ] || [ -e "include/$inc" ] || [ -e "$inc" ] || echo "warn: src/display needs \"$inc\" (not found here)"
done
echo "Done. Edit include/secrets.h, then: pio run -t upload"
