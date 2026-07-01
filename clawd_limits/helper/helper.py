"""
Clawd Mochi helper — localhost bridge from the browser extension to the crab.

The browser extension fetches your Claude limits (using its own session) and
POSTs a line "session%,weekly%,reset" to this local server. We forward it to
the crab over USB serial, and also RE-SEND the last value every 30 s so the
crab recovers after a reconnect/reset and stays in sync with the popup.

The helper never touches any credentials.

Runs as a tray app (pixel-crab icon) by default; falls back to the console.

The COM port is auto-detected (finds the crab by its USB id, any COM); pass a
port only to override.

Run:  pythonw helper.py              (tray, auto-detect port, no console window)
      python  helper.py [COMPORT] --console
Stop: tray menu -> Quit   (or Ctrl+C in console mode)
"""
import sys, time, threading
from http.server import BaseHTTPRequestHandler, HTTPServer
import serial

try:
    import pystray
    from PIL import Image, ImageDraw
except Exception:
    pystray = None

_args = sys.argv[1:]
CONSOLE = "--console" in _args
_pos = [a for a in _args if not a.startswith("--")]

HTTP_PORT   = 7654
COM         = _pos[0] if _pos else None   # explicit port, or None -> auto-detect
BAUD        = 115200
RESEND_SECS = 30

ser = None
last_value = None
last_update = None
connected = False
cur_port = None
_fail_logged = False
lock = threading.Lock()

# ── find the crab on any COM port by its USB id ────────────────────
def find_crab_ports():
    """Candidate ports, most-likely first: Espressif native USB, then UART bridges."""
    try:
        from serial.tools import list_ports
    except Exception:
        return []
    ports = list(list_ports.comports())
    esp = [p.device for p in ports if p.vid == 0x303A]        # ESP32-C3/S3 native USB
    bridges = {(0x10C4, 0xEA60), (0x1A86, 0x7523),            # CP2102, CH340
               (0x1A86, 0x55D4), (0x0403, 0x6001)}            # CH9102, FT232
    uart = [p.device for p in ports if (p.vid, p.pid) in bridges]
    return esp + uart

def resolve_ports():
    return [COM] if COM else find_crab_ports()

# ── serial ────────────────────────────────────────────────────────
def open_serial():
    """Find the crab and open it WITHOUT toggling DTR/RTS (so we don't reset it).
    Tries each candidate port and keeps the first that opens (skips dead ports)."""
    global ser, connected, cur_port, _fail_logged
    for port in resolve_ports():
        try:
            s = serial.Serial()
            s.port = port; s.baudrate = BAUD; s.timeout = 1
            s.dtr = False; s.rts = False
            s.open()
            ser = s; connected = True; cur_port = port; _fail_logged = False
            time.sleep(0.5)
            print(f"[serial] open {port} (no-reset)")
            return
        except Exception:
            continue
    ser = None; connected = False; cur_port = None
    if not _fail_logged:
        print("[serial] no crab found (auto-detect) — waiting for it to be plugged in")
        _fail_logged = True

def write_crab(line):
    global ser, connected
    with lock:
        if ser is None:
            open_serial()
        if ser is None:
            return False
        try:
            ser.write((line.strip() + "\n").encode("ascii"))
            ser.flush()
            connected = True
            return True
        except Exception as e:
            print(f"[serial] write failed: {e}")
            try: ser.close()
            except Exception: pass
            ser = None; connected = False
            return False

def reconnect():
    global ser
    with lock:
        try:
            if ser: ser.close()
        except Exception: pass
        ser = None
    open_serial()
    if last_value:
        write_crab(last_value)

def resender():
    while True:
        time.sleep(RESEND_SECS)
        if last_value:
            write_crab(last_value)

# ── http ──────────────────────────────────────────────────────────
class Handler(BaseHTTPRequestHandler):
    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def do_OPTIONS(self):
        self.send_response(204); self._cors(); self.end_headers()

    def do_POST(self):
        global last_value, last_update
        n = int(self.headers.get("Content-Length", 0) or 0)
        body = self.rfile.read(n).decode("utf-8", "ignore").strip()
        last_value = body; last_update = time.time()
        ok = write_crab(body)
        print(f"[recv] {body!r} -> serial {'OK' if ok else 'FAIL'}", flush=True)
        self.send_response(200 if ok else 500)
        self._cors(); self.send_header("Content-Type", "text/plain"); self.end_headers()
        self.wfile.write(b"ok" if ok else b"serial-fail")

    def log_message(self, *a):
        pass

def start_http():
    srv = HTTPServer(("127.0.0.1", HTTP_PORT), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print(f"[http] http://127.0.0.1:{HTTP_PORT}  crab={cur_port or 'auto'}  re-send={RESEND_SECS}s", flush=True)
    return srv

# ── tray ──────────────────────────────────────────────────────────
_GRID = [" XXXXXXX ", " XOXXXOX ", "XXXXXXXXX", "XXXXXXXXX",
         " XXXXXXX ", " XXXXXXX ", " X X X X ", " X X X X "]

def crab_icon(ok=True):
    N = 64
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    tile = (217, 119, 79) if ok else (118, 112, 108)   # clay / muted gray when no crab
    d.rounded_rectangle([0, 0, N - 1, N - 1], radius=int(N * 0.22), fill=tile)
    cols, rows = 9, 8
    cell = max(1, int(N * 0.64 / cols))
    ox, oy = (N - cell * cols) // 2, (N - cell * rows) // 2
    body, eye = (244, 239, 230), (42, 22, 8)
    for y in range(rows):
        for x in range(cols):
            k = _GRID[y][x]
            if k in "XO":
                col = body if k == "X" else eye
                d.rectangle([ox + x * cell, oy + y * cell, ox + x * cell + cell - 1, oy + y * cell + cell - 1], fill=col)
    return img

def _ago():
    if not last_update: return "never"
    s = int(time.time() - last_update)
    return f"{s}s ago" if s < 60 else (f"{s // 60}m ago" if s < 3600 else f"{s // 3600}h ago")

def run_tray():
    open_serial()
    threading.Thread(target=resender, daemon=True).start()
    srv = start_http()

    menu = pystray.Menu(
        pystray.MenuItem(lambda i: ("● " + cur_port if connected else "○ searching for crab…"), None, enabled=False),
        pystray.MenuItem(lambda i: f"Last: {last_value or '—'}", None, enabled=False),
        pystray.MenuItem(lambda i: f"Updated: {_ago()}", None, enabled=False),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Reconnect serial", lambda i, item: reconnect()),
        pystray.MenuItem("Quit", lambda i, item: i.stop()),
    )
    icon = pystray.Icon("clawd_helper", crab_icon(connected), "Clawd Mochi helper", menu=menu)

    def updater():
        was = None
        while True:
            if not connected:
                with lock:
                    if ser is None: open_serial()      # keep hunting for the crab on any COM
            if connected != was:
                icon.icon = crab_icon(connected); was = connected
            icon.title = (f"Clawd Mochi · {cur_port} · {last_value or '—'}") if connected else "Clawd Mochi · searching for crab…"
            try: icon.update_menu()
            except Exception: pass
            time.sleep(3)
    threading.Thread(target=updater, daemon=True).start()

    icon.run()           # blocks until Quit
    try: srv.shutdown()
    except Exception: pass

# ── console fallback ──────────────────────────────────────────────
def run_console():
    open_serial()
    threading.Thread(target=resender, daemon=True).start()
    srv = HTTPServer(("127.0.0.1", HTTP_PORT), Handler)
    print(f"[http] http://127.0.0.1:{HTTP_PORT}  crab={cur_port or 'auto'}  re-send={RESEND_SECS}s  (console)", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")

if __name__ == "__main__":
    if pystray is None or CONSOLE:
        if pystray is None and not CONSOLE:
            print("[tray] pystray not available -> console mode")
        run_console()
    else:
        run_tray()
