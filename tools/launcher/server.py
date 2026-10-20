"""AnyPS5 Modern Desktop Launcher Server.

Provides a local REST API and Web UI to manage, relink, and launch PS5 titles on Windows/Linux.
"""

import base64
import json
import mimetypes
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import urllib.parse
try:
    from http.server import ThreadingHTTPServer as HTTPServer, BaseHTTPRequestHandler
except ImportError:
    from http.server import HTTPServer, BaseHTTPRequestHandler

# Ensure stdout and stderr exist even when spawned without a console
class _NullWriter:
    def write(self, s): pass
    def flush(self): pass

if sys.stdout is None:
    sys.stdout = _NullWriter()
if sys.stderr is None:
    sys.stderr = _NullWriter()

ROOT_DIR = Path(__file__).resolve().parent.parent.parent
STATIC_DIR = Path(__file__).resolve().parent / "web"
CONFIG_FILE = Path(__file__).resolve().parent / "config.json"


def load_config():
    root_relinker = ROOT_DIR / ("relinker.exe" if sys.platform == "win32" else "relinker")
    sub_relinker = ROOT_DIR / "build_relinker" / "core" / "relinker" / ("relinker.exe" if sys.platform == "win32" else "relinker")
    bin_relinker = ROOT_DIR / "build" / "bin" / ("anyps5.exe" if sys.platform == "win32" else "anyps5")
    
    if root_relinker.exists():
        default_relinker = root_relinker
    elif sub_relinker.exists():
        default_relinker = sub_relinker
    else:
        default_relinker = bin_relinker
    defaults = {
        "relinkerPath": str(default_relinker),
        "gameDirs": [],
        "windowsGui": True,
        "windowsDynamicBase": True,
        "skipSceModule": False,
        "jobAffinity": False,
        "jobAffinityMask": "0x0F",
        "traceAudio": False,
        "enableShaderCache": True,
        "shaderCacheDir": "",
    }
    if CONFIG_FILE.exists():
        try:
            with CONFIG_FILE.open("r", encoding="utf-8") as f:
                saved = json.load(f)
                defaults.update(saved)
        except Exception:
            pass
    return defaults


def save_config(cfg):
    try:
        with CONFIG_FILE.open("w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2)
    except Exception as e:
        print(f"[Launcher] Error saving config: {e}", file=sys.stderr)


active_processes = {}
log_buffer = []


def append_log(line):
    log_buffer.append(f"[{time.strftime('%H:%M:%S')}] {line}")
    if len(log_buffer) > 1000:
        log_buffer.pop(0)


def scan_games(game_dirs):
    games = []
    for d in game_dirs:
        path = Path(d)
        if not path.is_dir():
            continue
        for item in path.iterdir():
            if not item.is_dir():
                continue
            eboot = item / "eboot.bin"
            if not eboot.exists():
                eboot = item / "sce_module" / "eboot.bin"
            if not eboot.exists():
                continue

            param_path = item / "sce_sys" / "param.json"
            title_id = item.name
            title_name = item.name
            category = "Game"
            version = "1.00"

            if param_path.exists():
                try:
                    with param_path.open("r", encoding="utf-8") as f:
                        data = json.load(f)
                        title_id = data.get("titleId", title_id)
                        title_name = data.get("localizedParameters", {}).get("defaultLanguage", {}).get("titleName", data.get("titleName", title_name))
                        category = data.get("category", category)
                        version = data.get("contentVersion", version)
                except Exception:
                    pass

            icon_path = item / "sce_sys" / "icon0.png"
            has_icon = icon_path.exists()

            relinked_exe = item / "eboot.exe"
            is_relinked = relinked_exe.exists()

            games.append({
                "id": title_id,
                "name": title_name,
                "category": category,
                "version": version,
                "path": str(item),
                "eboot": str(eboot),
                "relinkedExe": str(relinked_exe) if is_relinked else None,
                "isRelinked": is_relinked,
                "hasIcon": has_icon,
                "running": title_id in active_processes and active_processes[title_id].poll() is None,
            })
    return games


class LauncherHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        append_log(format % args)

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/api/games":
            cfg = load_config()
            games = scan_games(cfg.get("gameDirs", []))
            self.send_json(200, {"games": games})
            return

        if path == "/api/config":
            self.send_json(200, load_config())
            return

        if path == "/api/logs":
            crash_log_path = Path("anyps5-crash.log")
            crash_log = ""
            if crash_log_path.exists():
                try:
                    crash_log = crash_log_path.read_text(encoding="utf-8", errors="replace")
                except Exception:
                    pass
            self.send_json(200, {"logs": log_buffer[-100:], "crashLog": crash_log})
            return

        if path.startswith("/api/icon"):
            qs = urllib.parse.parse_qs(parsed.query)
            game_path = qs.get("path", [""])[0]
            if game_path:
                icon_file = Path(game_path) / "sce_sys" / "icon0.png"
                if icon_file.exists():
                    try:
                        data = icon_file.read_bytes()
                        self.send_response(200)
                        self.send_header("Content-Type", "image/png")
                        self.send_header("Content-Length", str(len(data)))
                        self.end_headers()
                        self.wfile.write(data)
                        return
                    except Exception:
                        pass
            self.send_response(404)
            self.end_headers()
            return

        # Serve static web files
        target = STATIC_DIR / (path[1:] if path != "/" else "index.html")
        if target.is_file():
            mime, _ = mimetypes.guess_type(str(target))
            data = target.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", mime or "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        self.send_response(404)
        self.end_headers()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode("utf-8") if length > 0 else "{}"
        try:
            req = json.loads(body)
        except Exception:
            req = {}

        if path == "/api/config":
            save_config(req)
            self.send_json(200, {"status": "ok"})
            return

        if path == "/api/relink":
            game_dir = req.get("path")
            if not game_dir:
                self.send_json(400, {"error": "Missing game path"})
                return
            cfg = load_config()
            relinker = Path(cfg.get("relinkerPath", ""))
            eboot = Path(game_dir) / "eboot.bin"
            if not eboot.exists():
                eboot = Path(game_dir) / "sce_module" / "eboot.bin"
            output_exe = Path(game_dir) / "eboot.exe"

            cmd = [str(relinker)]
            if sys.platform == "win32":
                cmd.append("--windows")
                if cfg.get("windowsGui", True):
                    cmd.append("--windows-gui")
                if cfg.get("windowsDynamicBase", True):
                    cmd.append("--windows-dynamic-base")
            if cfg.get("skipSceModule", False):
                cmd.append("--skip-sce-module")
            cmd.extend([str(eboot), str(output_exe)])

            append_log(f"Starting relinker: {' '.join(cmd)}")

            def run_relink():
                try:
                    proc = subprocess.run(cmd, cwd=game_dir, capture_output=True, text=True, timeout=120)
                    for line in proc.stdout.splitlines():
                        append_log(f"[Relinker] {line}")
                    if proc.stderr:
                        for line in proc.stderr.splitlines():
                            append_log(f"[Relinker ERR] {line}")
                    if proc.returncode == 0:
                        append_log("Relinking completed successfully.")
                    else:
                        append_log(f"Relinking failed with code {proc.returncode}")
                except Exception as e:
                    append_log(f"Relinking exception: {e}")

            threading.Thread(target=run_relink, daemon=True).start()
            self.send_json(200, {"status": "relinking_started"})
            return

        if path == "/api/launch":
            game_dir = req.get("path")
            title_id = req.get("id")
            output_exe = Path(game_dir) / "eboot.exe"
            if not output_exe.exists():
                self.send_json(400, {"error": "Executable not found. Please relink first."})
                return

            cfg = load_config()
            env = dict(os.environ)
            if cfg.get("jobAffinity", False):
                env["APS5_JOB_AFFINITY"] = "1"
                env["APS5_JOB_AFFINITY_MASK"] = cfg.get("jobAffinityMask", "0x0F")
            if cfg.get("traceAudio", False):
                env["APS5_TRACE_AUDIOOUT2"] = "1"
            if cfg.get("enableShaderCache", True):
                if cfg.get("shaderCacheDir"):
                    env["ANYPS5_SHADER_CACHE_DIR"] = cfg["shaderCacheDir"]
            else:
                env["ANYPS5_NO_SHADER_CACHE"] = "1"

            append_log(f"Launching {output_exe} in {game_dir}")
            try:
                proc = subprocess.Popen([str(output_exe)], cwd=game_dir, env=env)
                active_processes[title_id] = proc
                self.send_json(200, {"status": "launched", "pid": proc.pid})
            except Exception as e:
                append_log(f"Launch failed: {e}")
                self.send_json(500, {"error": str(e)})
            return

        if path == "/api/stop":
            title_id = req.get("id")
            proc = active_processes.get(title_id)
            if proc and proc.poll() is None:
                proc.terminate()
                append_log(f"Terminated process for {title_id}")
            self.send_json(200, {"status": "stopped"})
            return

        self.send_response(404)
        self.end_headers()

    def send_json(self, status, payload):
        data = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main():
    port = 8555
    HTTPServer.allow_reuse_address = True
    server = HTTPServer(("127.0.0.1", port), LauncherHandler)
    print(f"AnyPS5 Desktop Launcher serving on http://127.0.0.1:{port}")
    append_log(f"Launcher server started on port {port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
