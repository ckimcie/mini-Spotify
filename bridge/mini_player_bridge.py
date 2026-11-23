#!/usr/bin/env python3
"""mini-Spotify PC bridge: exposes the desktop media player (via playerctl/MPRIS) to the ESP32.

Works with any MPRIS player: the Spotify desktop app (free plan is fine), browsers, VLC...
  GET  /state            -> JSON now-playing
  GET  /art              -> album art as baseline JPEG
  POST /cmd?c=<toggle|next|prev|volume|seek>&v=<n>

Env: MINI_PLAYER (default "spotify,%any"), MINI_PORT (8765), MINI_TOKEN (optional shared secret,
sent by the device as X-Token). Optional: `pip install pillow` converts PNG/odd art to JPEG.
"""
import hashlib, io, json, os, socket, subprocess, threading, urllib.parse, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PLAYER = os.environ.get("MINI_PLAYER", "spotify,%any")
PORT = int(os.environ.get("MINI_PORT", "8765"))
TOKEN = os.environ.get("MINI_TOKEN", "")
SEP = "\x1f"
FIELDS = ["status", "title", "artist", "mpris:length", "position", "mpris:artUrl", "volume", "playerName"]
FMT = SEP.join("{{%s}}" % f for f in FIELDS)

try:
    from PIL import Image
except ImportError:
    Image = None

_art_cache = {}          # url -> jpeg bytes (last few)
_art_lock = threading.Lock()


def ctl(*args, timeout=2):
    try:
        r = subprocess.run(["playerctl", "-p", PLAYER, *args], capture_output=True, text=True, timeout=timeout)
        return r.returncode, r.stdout.strip()
    except FileNotFoundError:
        return 127, ""
    except subprocess.TimeoutExpired:
        return 124, ""


def num(x, default=0.0):
    try:
        return float(x)
    except (TypeError, ValueError):
        return default


def get_state():
    rc, out = ctl("metadata", "--format", FMT)
    if rc == 127:
        return {"active": False, "error": "playerctl not installed on PC"}
    if rc != 0 or not out:
        return {"active": False}
    p = (out.split(SEP) + [""] * len(FIELDS))[:len(FIELDS)]
    status, title, artist, length, pos, art, vol, name = p
    if status not in ("Playing", "Paused"):
        return {"active": False}
    return {
        "active": True,
        "playing": status == "Playing",
        "title": title,
        "artist": artist,
        "durationMs": int(num(length) / 1000),
        "progressMs": int(num(pos) / 1000),
        "volume": int(round(num(vol, -0.01) * 100)) if vol != "" else -1,
        "device": name,
        "artId": hashlib.sha1(art.encode()).hexdigest()[:10] if art else "",
    }


def load_art(url):
    with _art_lock:
        if url in _art_cache:
            return _art_cache[url]
    try:
        if url.startswith("file://"):
            with open(urllib.parse.unquote(url[7:]), "rb") as f:
                raw = f.read()
        else:
            req = urllib.request.Request(url, headers={"User-Agent": "mini-spotify-bridge"})
            with urllib.request.urlopen(req, timeout=6) as r:
                raw = r.read(2_000_000)
    except Exception:
        return None
    if Image is not None:
        try:
            im = Image.open(io.BytesIO(raw)).convert("RGB")
            im.thumbnail((300, 300))
            buf = io.BytesIO()
            im.save(buf, "JPEG", quality=82, progressive=False)
            raw = buf.getvalue()
        except Exception:
            return None
    elif not raw.startswith(b"\xff\xd8"):
        return None   # not a JPEG and no Pillow to convert it
    with _art_lock:
        if len(_art_cache) > 8:
            _art_cache.clear()
        _art_cache[url] = raw
    return raw


def run_cmd(c, v):
    if c == "toggle":
        ctl("play-pause")
    elif c == "next":
        ctl("next")
    elif c == "prev":
        ctl("previous")
    elif c == "volume":
        ctl("volume", "%.2f" % max(0.0, min(1.0, v / 100.0)))
    elif c == "seek":
        ctl("position", "%.3f" % (max(0, v) / 1000.0))
    else:
        return False
    return True


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, body=b"", ctype="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _authed(self):
        if TOKEN and self.headers.get("X-Token", "") != TOKEN:
            self._send(401, b'{"error":"bad token"}')
            return False
        return True

    def do_GET(self):
        if not self._authed():
            return
        path = urllib.parse.urlparse(self.path).path
        if path == "/state":
            self._send(200, json.dumps(get_state()).encode())
        elif path == "/art":
            rc, url = ctl("metadata", "mpris:artUrl")
            data = load_art(url) if rc == 0 and url else None
            if data:
                self._send(200, data, "image/jpeg")
            else:
                self._send(404, b"")
        else:
            self._send(404, b"")

    def do_POST(self):
        if not self._authed():
            return
        u = urllib.parse.urlparse(self.path)
        if u.path != "/cmd":
            return self._send(404, b"")
        q = urllib.parse.parse_qs(u.query)
        c = (q.get("c") or [""])[0]
        v = int(num((q.get("v") or ["0"])[0]))
        self._send(200 if run_cmd(c, v) else 400, b"{}")


def lan_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "<your-pc-ip>"


if __name__ == "__main__":
    print("mini-Spotify bridge on http://%s:%d  (player: %s, art conversion: %s)"
          % (lan_ip(), PORT, PLAYER, "Pillow" if Image else "JPEG pass-through"))
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
