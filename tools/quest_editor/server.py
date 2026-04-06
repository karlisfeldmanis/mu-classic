#!/usr/bin/env python3
"""Quest Editor server — serves the web UI, proxies the game DB, signals reload."""

import http.server
import os
import sys
import json
import signal
import subprocess

DB_PATH = os.environ.get("MU_DB_PATH", "")
PORT = int(os.environ.get("EDITOR_PORT", "8090"))

def find_server_pid():
    """Find PID of running MuServer process."""
    try:
        out = subprocess.check_output(["pgrep", "-f", "MuServer"], text=True).strip()
        pids = [int(p) for p in out.split("\n") if p]
        return pids[0] if pids else None
    except Exception:
        return None

def signal_server_reload():
    """Send SIGUSR1 to MuServer to trigger quest reload."""
    pid = find_server_pid()
    if pid:
        os.kill(pid, signal.SIGUSR1)
        return True
    return False

class EditorHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/api/db":
            if not os.path.isfile(DB_PATH):
                self.send_error(404, "Database not found: " + DB_PATH)
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Cache-Control", "no-cache")
            with open(DB_PATH, "rb") as f:
                data = f.read()
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        else:
            super().do_GET()

    def do_POST(self):
        if self.path == "/api/db":
            length = int(self.headers.get("Content-Length", 0))
            if length == 0:
                self.send_error(400, "Empty body")
                return
            data = self.rfile.read(length)
            try:
                with open(DB_PATH, "wb") as f:
                    f.write(data)
                # Signal game server to reload quests
                reloaded = signal_server_reload()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({
                    "ok": True, "bytes": len(data), "reloaded": reloaded
                }).encode())
            except Exception as e:
                self.send_error(500, str(e))
        else:
            self.send_error(404)

    def log_message(self, fmt, *args):
        pass

if __name__ == "__main__":
    if not DB_PATH:
        print("ERROR: MU_DB_PATH not set", file=sys.stderr)
        sys.exit(1)
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    print(f"[QuestEditor] Serving on http://localhost:{PORT}")
    print(f"[QuestEditor] Database: {DB_PATH}")
    server = http.server.HTTPServer(("127.0.0.1", PORT), EditorHandler)
    server.serve_forever()
