#!/usr/bin/env python3
"""Records a 360 orbit of the island from the viewer into game background frames.

Usage: python3 record_orbit.py <build_dir> <out_dir> [frames=360] [fps=15]

Serves <build_dir> over HTTP, opens isla_3d.html in headless Chromium in record mode and
receives one PNG per frame, saved as <out_dir>/orbit_000.png ... at 480x270 (true pixel-art size).
"""
import glob
import io
import os
import shutil
import subprocess
import sys
import threading
import time
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

from PIL import Image

build = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build")
out_dir = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else "../Confidential/assets/img/orbit")
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 360
fps = int(sys.argv[4]) if len(sys.argv) > 4 else 15
OUT_W, OUT_H = 480, 270

done = threading.Event()
received = set()


class Handler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if self.path.startswith("/frame/"):
            i = int(self.path.rsplit("/", 1)[1])
            img = Image.open(io.BytesIO(body)).convert("RGB")
            if img.size != (OUT_W, OUT_H):
                img = img.resize((OUT_W, OUT_H), Image.NEAREST)
            img.save(os.path.join(out_dir, f"orbit_{i:03d}.png"), optimize=True)
            received.add(i)
            if len(received) % 30 == 0:
                print(f"  {len(received)}/{frames} frames", flush=True)
        elif self.path == "/done":
            done.set()
        self.send_response(204)
        self.end_headers()


os.makedirs(out_dir, exist_ok=True)
for old in glob.glob(os.path.join(out_dir, "orbit_*.png")):
    os.remove(old)

server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, directory=build))
port = server.server_address[1]
threading.Thread(target=server.serve_forever, daemon=True).start()

url = f"http://127.0.0.1:{port}/isla_3d.html?warm=1500&clean&px=4&record={frames}&fps={fps}"
profile = f"/tmp/isla_orbit_{os.getpid()}"
chromium = subprocess.Popen(
    [
        "chromium", "--headless=new", "--no-sandbox", "--use-angle=gl", "--ignore-gpu-blocklist", "--enable-gpu",
        "--window-size=1920,1080", f"--user-data-dir={profile}", "--remote-debugging-port=0", url,
    ],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
print(f"recording {frames} frames at {fps} fps -> {out_dir}", flush=True)
start = time.time()
ok = done.wait(timeout=1800)
chromium.terminate()
chromium.wait()
server.shutdown()
shutil.rmtree(profile, ignore_errors=True)

if not ok or len(received) != frames:
    sys.exit(f"recording failed: got {len(received)}/{frames} frames")
print(f"done in {time.time() - start:.0f}s")
