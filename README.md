# mini-Spotify (PC bridge edition)

Now-playing display + remote for the Viewe 2.8" ESP32-S3 (PlatformIO, LVGL 9.5).
No Spotify account/API needed: a small script on your PC reads the desktop player over MPRIS
(`playerctl`) and the ESP32 talks to it over Wi-Fi. Works with the Spotify desktop app on the
free plan, and with browsers, VLC, etc.

## PC
1. `sudo apt install playerctl` (or your distro's package). Optional: `pip install pillow` (converts art to JPEG).
2. `python3 bridge/mini_player_bridge.py` - prints the URL. Test: `curl localhost:8765/state`.
3. Open the firewall if needed: `sudo ufw allow 8765/tcp`.
Env: `MINI_PLAYER` (default `spotify,%any`), `MINI_PORT`, `MINI_TOKEN` (shared secret).

## ESP32
Edit `include/secrets.h`: `WIFI_SSID`, `WIFI_PASS`, `BRIDGE_HOST` (PC IP from `hostname -I`).
Then `pio run -t upload && pio device monitor`. The PC and the board must be on the same network
(public/campus Wi-Fi often isolates devices; a phone hotspot or home router avoids that).

## Controls
Prev / play-pause / next, seek slider (release to seek), volume slider.
The bridge is plain HTTP on your LAN: anyone on it can control playback unless you set `MINI_TOKEN`.

## PC bridge service

Run `bridge/install-service.sh` to start the bridge at login. Logs: `journalctl --user -u mini-spotify-bridge -f`.
Set the bridge address from the board: gear icon, Wi-Fi setup, then the "PC bridge address" field.
The `BRIDGE_HOST` value in `include/secrets.h` is used when nothing is saved.
