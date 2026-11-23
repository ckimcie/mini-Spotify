#pragma once
// Boundary between the app and the board's display/touch/LVGL bring-up.
// LVGL runs entirely on the Arduino loop task, so no locking is needed: the Spotify
// network task never touches LVGL objects (it only exchanges data under its own mutex).
bool hal_display_begin();   // panel + touch + LVGL init
void hal_loop();            // call every loop(): display housekeeping + lv_timer_handler()
