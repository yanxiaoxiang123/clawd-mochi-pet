#!/usr/bin/env python3
"""
Clawd Mochi Mac Companion CLI, Background Daemon, LLM Brain & Real-Time Web Console Server (tools/mochi.py)

Features:
  - Holds USB serial (/dev/cu.usbmodem*) open with zero chip resets (DTR=False, RTS=False, ~HUPCL).
  - Real-time LLM dynamic speech (reuses existing Anthropic-compatible config, GB2312-safe).
  - 8-bit electronic pet chiptune SFX synthesizer + optional macOS TTS (night auto-mute 23:00-08:00).
  - Exposes local Unix socket (/tmp/clawd-mochi.sock) AND LAN HTTP server at http://0.0.0.0:18765
    for both Mac web console, macOS native Menu Bar app, and iPhone LAN remote control.
"""

import datetime
import glob
import json
import mimetypes
import os
import re
import signal
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, Optional
from urllib.parse import urlparse

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from mochi_brain import (  # noqa: E402
    MochiLLMBrain,
    MochiSoundEngine,
    get_lan_ip,
    get_mac_battery,
    inspect_git_repo,
    sanitize_gb2312,
)

SOCK_PATH = "/tmp/clawd-mochi.sock"
PID_PATH = "/tmp/clawd-mochi.pid"
BAUD_RATE = 115200
HTTP_PORT = int(os.environ.get("MOCHI_HTTP_PORT", "18765"))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)


def find_serial_port() -> Optional[str]:
    env_port = os.environ.get("MOCHI_PORT")
    if env_port and os.path.exists(env_port):
        return env_port
    candidates = sorted(
        glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*")
    )
    return candidates[0] if candidates else None


def open_serial_no_reset(port: str, timeout: float = 1.2):
    import serial
    import termios

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = BAUD_RATE
    ser.timeout = timeout
    ser.write_timeout = 1.2
    ser.dsrdtr = False
    ser.rtscts = False
    ser.dtr = False
    ser.rts = False
    ser.open()
    try:
        fd = ser.fileno()
        attrs = termios.tcgetattr(fd)
        attrs[2] &= ~termios.HUPCL
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except Exception:
        pass
    return ser


def is_macos_screen_locked() -> bool:
    try:
        out = subprocess.check_output(
            ["ioreg", "-n", "Root", "-d1", "-a"],
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=2.0,
        )
        if "CGSSessionScreenIsLocked" in out and "<true/>" in out.split("CGSSessionScreenIsLocked", 1)[1][:80]:
            return True
    except Exception:
        pass
    return False


def get_macos_hid_idle_seconds() -> int:
    try:
        out = subprocess.check_output(
            ["ioreg", "-c", "IOHIDSystem", "-d", "4"],
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=2.0,
        )
        m = re.search(r'"HIDIdleTime"\s*=\s*(\d+)', out)
        if m:
            return int(m.group(1)) // 1_000_000_000
    except Exception:
        pass
    return 0


def parse_status_line(resp: str, port: Optional[str] = None) -> Dict[str, Any]:
    if not resp or not resp.startswith("OK:STATUS:"):
        return {"online": False, "port": port, "raw": resp}
    body = resp[len("OK:STATUS:") :]
    quip = ""
    if "|quip=" in body:
        body, quip = body.split("|quip=", 1)
    kv: Dict[str, str] = {}
    for item in body.split(","):
        if "=" in item:
            k, v = item.split("=", 1)
            kv[k] = v
    return {
        "online": True,
        "port": port or find_serial_port() or "",
        "mood": kv.get("mood", "HAPPY"),
        "expr": int(kv.get("expr", "0")),
        "bright": int(kv.get("bright", "255")),
        "boots": int(kv.get("boots", "1")),
        "mins": int(kv.get("mins", "0")),
        "love": int(kv.get("love", "0")),
        "lv": int(kv.get("lv", "1")),
        "temp": float(kv.get("temp", "25.0")),
        "rssi": int(kv.get("rssi", "-100")),
        "present": kv.get("present", "1") == "1",
        "pomo": int(kv.get("pomo", "0")),
        "pomototal": int(kv.get("pomototal", "0")),
        "ai": int(kv.get("ai", "0")),
        "bonded": kv.get("bonded", "0") == "1",
        "pairing": int(kv.get("pairing", "0")),
        "theme": int(kv.get("theme", "0")),
        "etheme": int(kv.get("etheme", "1")),
        "hat": int(kv.get("hat", "0")),
        "ehat": int(kv.get("ehat", "1")),
        "clock": kv.get("clock", "12:00"),
        "quip": quip,
        "ts": time.time(),
    }


def send_via_daemon_socket(raw_cmd: str, timeout: float = 3.5) -> Optional[str]:
    if not os.path.exists(SOCK_PATH):
        return None
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(timeout)
            client.connect(SOCK_PATH)
            client.sendall((raw_cmd.strip() + "\n").encode("utf-8"))
            data = client.recv(4096)
            return data.decode("utf-8", errors="replace").strip()
    except Exception:
        return None


def send_direct_serial(raw_cmd: str, wait_reply_sec: float = 1.0) -> str:
    port = find_serial_port()
    if not port:
        raise RuntimeError("未检测到小螃蟹串口 (/dev/cu.usbmodem*)，请确认 USB 已连接。")
    ser = open_serial_no_reset(port, timeout=0.35)
    try:
        ser.reset_input_buffer()
        ser.write((raw_cmd.strip() + "\n").encode("utf-8"))
        ser.flush()
        deadline = time.monotonic() + wait_reply_sec
        lines = []
        while time.monotonic() < deadline:
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if line:
                lines.append(line)
                if line.startswith("OK:"):
                    break
        return "\n".join(lines) if lines else "OK:SENT"
    finally:
        ser.close()


def ensure_daemon_running(wait_sec: float = 5.5) -> bool:
    if send_via_daemon_socket("MOCHI:STATUS", timeout=2.5) is not None:
        return True
    if not find_serial_port():
        return False
    with open("/tmp/clawd-mochi.log", "ab") as logf:
        subprocess.Popen(
            [sys.executable, os.path.abspath(__file__), "daemon"],
            stdout=logf,
            stderr=logf,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
    deadline = time.monotonic() + wait_sec
    while time.monotonic() < deadline:
        time.sleep(0.25)
        if os.path.exists(SOCK_PATH):
            resp = send_via_daemon_socket("MOCHI:STATUS", timeout=2.5)
            if resp and resp.startswith("OK:"):
                return True
    return False


def send_command(raw_cmd: str) -> str:
    via_sock = send_via_daemon_socket(raw_cmd)
    if via_sock is not None:
        return via_sock
    if ensure_daemon_running():
        via_sock = send_via_daemon_socket(raw_cmd)
        if via_sock is not None:
            return via_sock
    return send_direct_serial(raw_cmd)


class MochiDaemon:
    def __init__(self):
        self.running = True
        self.lock = threading.Lock()
        self.ser = None
        self.port_name: Optional[str] = None
        self.latest_status: Dict[str, Any] = {"online": False}
        self.last_time_sync = 0.0
        self.last_presence_sync = 0.0
        self.last_status_poll = 0.0
        self.last_cpu_alert = 0.0
        self.last_sitting_alert = 0.0
        self.last_care_quip = time.monotonic() - 240.0
        self.last_batt_alert = 0.0
        self.active_streak_start = time.monotonic()
        self.was_away = False
        self.away_since_mono = 0.0
        self.prev_pomo = 0
        self.prev_present = True
        self.active_cwd = REPO_ROOT
        self.last_agent = "AGY"
        self.lan_ip = get_lan_ip()
        self.sound = MochiSoundEngine()
        self.brain = MochiLLMBrain(self.sound)

    def ensure_serial(self) -> bool:
        if self.ser is not None and self.ser.is_open:
            return True
        port = find_serial_port()
        if not port:
            self.port_name = None
            return False
        try:
            self.ser = open_serial_no_reset(port, timeout=0.35)
            self.port_name = port
            time.sleep(2.5)
            self.ser.reset_input_buffer()
            return True
        except Exception:
            self.ser = None
            self.port_name = None
            return False

    def _enrich_status(self, st: Dict[str, Any]) -> Dict[str, Any]:
        out = dict(st)
        out["audio"] = self.sound.get_state()
        out["llm_ready"] = self.brain.is_configured()
        out["lan_url"] = f"http://{self.lan_ip}:{HTTP_PORT}/preview.html"
        out["active_repo"] = os.path.basename(os.path.abspath(self.active_cwd)) if self.active_cwd else "clawpet"
        out["streak_min"] = max(0, int((time.monotonic() - self.active_streak_start) / 60.0))
        return out

    def send_locked(self, raw_cmd: str) -> str:
        with self.lock:
            if not self.ensure_serial():
                self.latest_status = {"online": False}
                return "ERR:NO_DEVICE"
            try:
                self.ser.reset_input_buffer()
                self.ser.write((raw_cmd.strip() + "\n").encode("utf-8"))
                self.ser.flush()
                deadline = time.monotonic() + 0.85
                while time.monotonic() < deadline:
                    line = self.ser.readline().decode("utf-8", errors="replace").strip()
                    if line.startswith("OK:"):
                        if line.startswith("OK:STATUS:"):
                            new_st = parse_status_line(line, self.port_name)
                            # Detect hardware-side transitions to play subtle 8-bit SFX
                            if self.prev_pomo > 0 and new_st.get("pomo", 0) == 0 and new_st.get("pomototal", 0) == 0:
                                self.sound.play("pomo_done")
                            if (not self.prev_present) and new_st.get("present", True):
                                self.sound.play("welcome")
                                away_m = max(1, int((time.monotonic() - self.away_since_mono) / 60.0)) if self.away_since_mono else 5
                                self._async_welcome_quip(away_m)
                            if self.prev_present and (not new_st.get("present", True)):
                                self.away_since_mono = time.monotonic()
                            self.prev_pomo = new_st.get("pomo", 0)
                            self.prev_present = new_st.get("present", True)
                            self.latest_status = new_st
                        return line
                return "OK:SENT"
            except Exception as exc:
                try:
                    self.ser.close()
                except Exception:
                    pass
                self.ser = None
                self.latest_status = {"online": False}
                return f"ERR:{exc}"

    def refresh_status(self) -> Dict[str, Any]:
        resp = self.send_locked("MOCHI:STATUS")
        if resp.startswith("OK:STATUS:"):
            return self._enrich_status(self.latest_status)
        return self._enrich_status({"online": False, "error": resp})

    def _async_welcome_quip(self, away_min: int) -> None:
        def _run():
            line = self.brain.generate_welcome(away_min)
            if line:
                self.send_locked(f"MOCHI:SAY:{line}")
                self.sound.speak(line)

        threading.Thread(target=_run, daemon=True).start()

    def handle_ai_done_async(self, agent: str, cwd: str) -> None:
        if cwd and os.path.isdir(cwd):
            self.active_cwd = cwd
        self.last_agent = agent or self.last_agent
        self.sound.play("ai_done")
        # Immediate visual celebration on device first (<15ms)
        self.send_locked(f"MOCHI:AI:DONE:{self.last_agent} 搞定啦!")

        def _gen():
            git_info = inspect_git_repo(self.active_cwd)
            streak = max(1, int((time.monotonic() - self.active_streak_start) / 60.0))
            line = self.brain.generate_ai_done(
                self.last_agent,
                str(git_info["repo"]),
                int(git_info["changed_files"]),
                streak,
            )
            if line:
                self.send_locked(f"MOCHI:SAY:{line}")
                self.sound.speak(line)

        threading.Thread(target=_gen, daemon=True).start()

    def handle_feed(self, food_key: str) -> Dict[str, Any]:
        key = food_key.upper().strip()
        if key not in ("FISH", "COFFEE", "MOCHI", "TEA"):
            return {"ok": False, "error": "invalid food"}
        self.sound.play(f"feed_{key.lower()}")
        mood = str(self.latest_status.get("mood", "HAPPY"))
        lv = int(self.latest_status.get("lv", 1))
        # Generate live LLM foodie reaction (fast timeout with instant fallback)
        reply = self.brain.generate_feed_reply(key, mood, lv)
        res = self.send_locked(f"MOCHI:FEED:{key}:{reply}")
        self.sound.speak(reply)
        time.sleep(0.05)
        st = self.refresh_status()
        return {"ok": res.startswith("OK:"), "food": key, "reply": reply, "status": st}

    def handle_chat(self, user_text: str) -> Dict[str, Any]:
        clean_in = str(user_text or "").strip()
        if not clean_in:
            return {"ok": False, "error": "empty text"}
        self.send_locked("MOCHI:AI:THINK:让我想想...")
        mood = str(self.latest_status.get("mood", "HAPPY"))
        lv = int(self.latest_status.get("lv", 1))
        repo = os.path.basename(os.path.abspath(self.active_cwd)) if self.active_cwd else "clawpet"
        expr_id, reply = self.brain.generate_chat_reply(clean_in, mood, lv, repo)
        self.sound.play("chat")
        res = self.send_locked(f"MOCHI:CHAT:{expr_id}:{reply}")
        self.sound.speak(reply)
        time.sleep(0.05)
        st = self.refresh_status()
        return {"ok": res.startswith("OK:"), "expr": expr_id, "reply": reply, "status": st}

    def _trigger_sfx_for_cmd(self, cmd: str) -> None:
        if cmd == "MOCHI:PET":
            self.sound.play("pet")
        elif cmd.startswith("MOCHI:SCENE:WELCOME") or cmd.startswith("MOCHI:SCENE:LONG_AWAY"):
            self.sound.play("welcome")
        elif cmd.startswith("MOCHI:AI:DONE") or cmd.startswith("MOCHI:SYS:GIT_COMMIT"):
            self.sound.play("ai_done")
        elif cmd.startswith("MOCHI:SAY:"):
            self.sound.play("chat")
            self.sound.speak(cmd[len("MOCHI:SAY:") :])

    def socket_worker(self, server_sock: socket.socket):
        while self.running:
            try:
                conn, _ = server_sock.accept()
            except socket.timeout:
                continue
            except Exception:
                break
            with conn:
                try:
                    data = conn.recv(2048).decode("utf-8", errors="replace").strip()
                    if data == "__STOP__":
                        conn.sendall(b"OK:STOPPING\n")
                        self.running = False
                        break
                    if data.startswith("__CTX__:"):
                        parts = data.split(":", 2)
                        if len(parts) == 3:
                            self.last_agent = parts[1] or self.last_agent
                            if parts[2] and os.path.isdir(parts[2]):
                                self.active_cwd = parts[2]
                        conn.sendall(b"OK:CTX\n")
                        continue
                    if data.startswith("__AI_DONE__:"):
                        parts = data.split(":", 2)
                        agent = parts[1] if len(parts) > 1 else "AGY"
                        cwd = parts[2] if len(parts) > 2 else ""
                        self.handle_ai_done_async(agent, cwd)
                        conn.sendall(b"OK:AI_DONE\n")
                        continue
                    if data.startswith("__FEED__:"):
                        food = data.split(":", 1)[1].strip()
                        res = self.handle_feed(food)
                        conn.sendall((json.dumps(res, ensure_ascii=False) + "\n").encode("utf-8"))
                        continue
                    if data.startswith("__CHAT__:"):
                        msg = data.split(":", 1)[1].strip()
                        res = self.handle_chat(msg)
                        conn.sendall((json.dumps(res, ensure_ascii=False) + "\n").encode("utf-8"))
                        continue
                    self._trigger_sfx_for_cmd(data)
                    reply = self.send_locked(data)
                    conn.sendall((reply + "\n").encode("utf-8"))
                except Exception as exc:
                    try:
                        conn.sendall(f"ERR:{exc}\n".encode("utf-8"))
                    except Exception:
                        pass

    def build_http_server(self) -> Optional[ThreadingHTTPServer]:
        daemon_ref = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, _fmt, *_args):
                pass

            def _send_json(self, payload: Any, code: int = 200):
                raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
                self.send_response(code)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)

            def do_OPTIONS(self):
                self.send_response(204)
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
                self.send_header("Access-Control-Allow-Headers", "Content-Type")
                self.end_headers()

            def do_GET(self):
                parsed = urlparse(self.path)
                path = parsed.path

                if path == "/api/status":
                    st = daemon_ref.latest_status
                    if not st.get("online") or (time.time() - st.get("ts", 0) > 0.65):
                        st = daemon_ref.refresh_status()
                    else:
                        st = daemon_ref._enrich_status(st)
                    self._send_json(st)
                    return

                if path in ("/", "/preview.htm", "/preview.html"):
                    target = os.path.join(REPO_ROOT, "preview.html")
                else:
                    rel = os.path.normpath(path.lstrip("/"))
                    if rel.startswith(".."):
                        self.send_error(403)
                        return
                    target = os.path.join(REPO_ROOT, rel)

                if not os.path.isfile(target):
                    self.send_error(404)
                    return

                ctype, _ = mimetypes.guess_type(target)
                if not ctype:
                    ctype = "application/octet-stream"
                if ctype.startswith("text/") or ctype == "application/javascript":
                    ctype += "; charset=utf-8"
                with open(target, "rb") as f:
                    content = f.read()
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Content-Length", str(len(content)))
                self.end_headers()
                self.wfile.write(content)

            def do_POST(self):
                parsed = urlparse(self.path)
                length = int(self.headers.get("Content-Length", "0"))
                raw_body = self.rfile.read(length).decode("utf-8", errors="replace")
                try:
                    req = json.loads(raw_body) if raw_body else {}
                except Exception:
                    self._send_json({"ok": False, "error": "invalid json"}, 400)
                    return

                if parsed.path == "/api/feed":
                    food = str(req.get("food", "FISH"))
                    self._send_json(daemon_ref.handle_feed(food))
                    return

                if parsed.path == "/api/chat":
                    text = str(req.get("text", ""))
                    self._send_json(daemon_ref.handle_chat(text))
                    return

                if parsed.path == "/api/audio":
                    if "sound_enabled" in req:
                        daemon_ref.sound.sound_enabled = bool(req["sound_enabled"])
                    if "tts_enabled" in req:
                        daemon_ref.sound.tts_enabled = bool(req["tts_enabled"])
                    if "night_mute" in req:
                        daemon_ref.sound.night_mute = bool(req["night_mute"])
                    if "llm_enabled" in req:
                        daemon_ref.sound.llm_enabled = bool(req["llm_enabled"])
                    daemon_ref.sound.save_prefs()
                    if req.get("preview_sfx"):
                        daemon_ref.sound.play(str(req["preview_sfx"]), force=True)
                    self._send_json({"ok": True, "audio": daemon_ref.sound.get_state()})
                    return

                if parsed.path == "/api/cmd":
                    cmd = str(req.get("cmd", "")).strip()
                    if not cmd.startswith("MOCHI:"):
                        self._send_json({"ok": False, "error": "cmd must start with MOCHI:"}, 400)
                        return
                    if cmd.startswith("MOCHI:SAY:"):
                        clean = sanitize_gb2312(cmd[len("MOCHI:SAY:") :], max_chars=26)
                        cmd = f"MOCHI:SAY:{clean}" if clean else cmd
                    daemon_ref._trigger_sfx_for_cmd(cmd)
                    reply = daemon_ref.send_locked(cmd)
                    time.sleep(0.045)
                    st = daemon_ref.refresh_status()
                    self._send_json({"ok": reply.startswith("OK:"), "reply": reply, "status": st})
                    return

                self.send_error(404)

        try:
            ThreadingHTTPServer.allow_reuse_address = True
            httpd = ThreadingHTTPServer(("0.0.0.0", HTTP_PORT), Handler)
            return httpd
        except Exception as exc:
            print(f"[mochi-daemon] HTTP 端口 {HTTP_PORT} 启动失败: {exc}")
            return None

    def run(self):
        if os.path.exists(SOCK_PATH):
            if send_via_daemon_socket("MOCHI:STATUS") is not None:
                print("守护进程已在运行中。")
                return
            os.unlink(SOCK_PATH)

        with open(PID_PATH, "w", encoding="utf-8") as f:
            f.write(str(os.getpid()))

        self.ensure_serial()
        self.refresh_status()

        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.settimeout(1.0)
        server.bind(SOCK_PATH)
        server.listen(12)

        httpd = self.build_http_server()
        if httpd is not None:
            ht = threading.Thread(target=httpd.serve_forever, daemon=True)
            ht.start()
            print(f"[mochi-daemon] 实时控制台已就绪: http://127.0.0.1:{HTTP_PORT}/preview.html (局域网: http://{self.lan_ip}:{HTTP_PORT}/preview.html)")

        def handle_sig(_signum, _frame):
            self.running = False

        signal.signal(signal.SIGINT, handle_sig)
        signal.signal(signal.SIGTERM, handle_sig)

        t = threading.Thread(target=self.socket_worker, args=(server,), daemon=True)
        t.start()

        cpu_cores = os.cpu_count() or 8
        print(f"[mochi-daemon] 已启动 (PID={os.getpid()}, LLM={'ON' if self.brain.is_configured() else 'FALLBACK'})")

        try:
            while self.running:
                now_mono = time.monotonic()

                # 0. Poll live hardware status every 1.0s
                if now_mono - self.last_status_poll >= 1.0:
                    self.last_status_poll = now_mono
                    self.refresh_status()

                # 1. Sync wall-clock time every 60s
                if now_mono - self.last_time_sync >= 60.0 or self.last_time_sync == 0.0:
                    dt = datetime.datetime.now()
                    res = self.send_locked(f"MOCHI:TIME:{dt.hour:02d}:{dt.minute:02d}")
                    if res.startswith("OK:"):
                        self.last_time_sync = now_mono

                # 2. Check Mac presence, continuous sitting, battery & periodic LLM care every 20s
                if now_mono - self.last_presence_sync >= 20.0 or self.last_presence_sync == 0.0:
                    self.last_presence_sync = now_mono
                    idle_sec = get_macos_hid_idle_seconds()
                    locked = is_macos_screen_locked()
                    if not locked and idle_sec < 1500:
                        if self.was_away and idle_sec < 60:
                            self.was_away = False
                            self.active_streak_start = now_mono
                        self.send_locked(f"MOCHI:PRESENCE:ACTIVE:{min(idle_sec, 120)}")

                        streak_min = int((now_mono - self.active_streak_start) / 60.0)
                        if idle_sec < 240:
                            if streak_min >= 50 and (now_mono - self.last_sitting_alert) >= 1800.0:
                                self.last_sitting_alert = now_mono
                                self.send_locked(f"MOCHI:SYS:SITTING:连坐 {streak_min} 分钟啦! 喝口水~")
                                self.sound.play("alert")
                        else:
                            self.active_streak_start = now_mono

                        # Periodic LLM dynamic situational care (~every 9 minutes when awake & not AI-busy)
                        pct, on_ac = get_mac_battery()
                        low_batt_trigger = (
                            pct is not None
                            and not on_ac
                            and pct <= 22
                            and (now_mono - self.last_batt_alert) >= 900.0
                        )
                        periodic_trigger = (
                            idle_sec < 180
                            and self.latest_status.get("online")
                            and self.latest_status.get("ai", 0) == 0
                            and self.latest_status.get("mood") != "SLEEPY"
                            and (now_mono - self.last_care_quip) >= 540.0
                        )
                        if low_batt_trigger or periodic_trigger:
                            self.last_care_quip = now_mono
                            if low_batt_trigger:
                                self.last_batt_alert = now_mono
                                self.sound.play("alert")

                            def _care_job(b_pct=pct, b_ac=on_ac, st_m=streak_min):
                                git_info = inspect_git_repo(self.active_cwd)
                                line = self.brain.generate_care_quip(
                                    str(git_info["repo"]),
                                    int(git_info["changed_files"]),
                                    st_m,
                                    b_pct,
                                    b_ac,
                                )
                                if line:
                                    self.send_locked(f"MOCHI:SAY:{line}")
                                    self.sound.speak(line)

                            threading.Thread(target=_care_job, daemon=True).start()
                    else:
                        self.was_away = True
                        self.active_streak_start = now_mono
                        self.send_locked(f"MOCHI:PRESENCE:IDLE:{max(idle_sec, 600)}")

                    try:
                        load1, _, _ = os.getloadavg()
                        if load1 > cpu_cores * 1.35 and (now_mono - self.last_cpu_alert) >= 600.0:
                            self.last_cpu_alert = now_mono
                            self.send_locked("MOCHI:SYS:CPU_HIGH:电脑火力全开好烫呀!")
                    except Exception:
                        pass

                time.sleep(0.5)
        finally:
            if httpd is not None:
                try:
                    httpd.shutdown()
                    httpd.server_close()
                except Exception:
                    pass
            server.close()
            if self.ser is not None:
                try:
                    self.ser.close()
                except Exception:
                    pass
            for p in (SOCK_PATH, PID_PATH):
                if os.path.exists(p):
                    try:
                        os.unlink(p)
                    except Exception:
                        pass
            print("[mochi-daemon] 已退出。")


def format_status(resp: str) -> str:
    st = parse_status_line(resp)
    if not st.get("online"):
        return resp
    pomo_s = st["pomo"]
    pomo_str = f"{pomo_s // 60}分{pomo_s % 60:02d}秒" if pomo_s > 0 else "未开启"
    present_str = "在桌前" if st["present"] else "离开工位"
    bond_str = (
        f"等待配对中 ({st['pairing']}s)"
        if st.get("pairing", 0) > 0
        else ("已绑定专属 iPhone" if st.get("bonded") else "未绑定(通用模式)")
    )
    theme_names = ["自动", "珊瑚橙", "赛博青", "樱花粉", "暖金护眼", "薄荷绿"]
    hat_names = ["自动", "无配饰", "小皇冠", "小耳机", "小睡帽", "必胜头带"]
    et = theme_names[st["etheme"]] if 0 <= st["etheme"] < len(theme_names) else str(st["etheme"])
    eh = hat_names[st["ehat"]] if 0 <= st["ehat"] < len(hat_names) else str(st["ehat"])
    quip_str = f' | 当前气泡: "{st["quip"]}"' if st.get("quip") else ""
    return (
        f"小螃蟹状态:\n"
        f"  当前心情: {st['mood']} (表情 #{st['expr']:02d}, 背光 {round(st['bright']*100/255)}%)  |  在岗状态: {present_str}\n"
        f"  外观装扮: 主题「{et}」 · 配饰「{eh}」\n"
        f"  见面次数: 第 {st['boots']} 次  |  累计陪伴: {st['mins']} 分钟\n"
        f"  亲密度  : Lv.{st['lv']} (爱心值 {st['love']})  |  蓝牙认主: {bond_str}\n"
        f"  芯片温度: {st['temp']:.1f} °C  |  最近蓝牙信号: {st['rssi']} dBm\n"
        f"  专注番茄: {pomo_str}  |  同步时间: {st['clock']}{quip_str}"
    )


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip())
        sys.exit(0)

    action = sys.argv[1].lower()
    args = sys.argv[2:]

    if action == "daemon":
        MochiDaemon().run()
        return

    if action == "stop-daemon":
        res = send_via_daemon_socket("__STOP__")
        print("守护进程已停止。" if res else "守护进程当前未运行。")
        return

    if action == "status":
        print(format_status(send_command("MOCHI:STATUS")))
    elif action == "pet":
        print(send_command("MOCHI:PET"))
    elif action == "feed":
        food = (args[0] if args else "FISH").upper()
        print(send_command(f"__FEED__:{food}"))
    elif action == "chat":
        text = " ".join(args).strip() or "在干嘛呀"
        print(send_command(f"__CHAT__:{text}"))
    elif action == "theme":
        idx = int(args[0]) if args else 0
        print(send_command(f"MOCHI:THEME:{idx}"))
    elif action == "hat":
        idx = int(args[0]) if args else 0
        print(send_command(f"MOCHI:HAT:{idx}"))
    elif action == "say":
        text = sanitize_gb2312(" ".join(args).strip() or "Hello~", max_chars=26)
        print(send_command(f"MOCHI:SAY:{text}"))
    elif action == "theater":
        print(send_command("MOCHI:THEATER"))
    elif action in ("focus", "pomo"):
        sub = args[0].lower() if args else "25"
        if sub in ("stop", "off", "0"):
            print(send_command("MOCHI:POMO:STOP"))
        elif sub == "demo":
            print(send_command("MOCHI:POMO:DEMO"))
        else:
            print(send_command(f"MOCHI:POMO:{int(sub)}"))
    elif action == "ai":
        sub = args[0].upper() if args else "THINK"
        msg = " ".join(args[1:]).strip()
        cmd = f"MOCHI:AI:{sub}:{msg}" if msg else f"MOCHI:AI:{sub}"
        print(send_command(cmd))
    elif action == "git-commit":
        msg = " ".join(args).strip()
        cmd = f"MOCHI:SYS:GIT_COMMIT:{msg}" if msg else "MOCHI:SYS:GIT_COMMIT"
        print(send_command(cmd))
    elif action == "expr":
        idx = int(args[0]) if args else 0
        print(send_command(f"MOCHI:EXPR:{idx}"))
    elif action == "pair-ble":
        print("已开启 3 分钟蓝牙认主广播，请打开 iPhone「设置 → 蓝牙」点击 Clawd-Mochi 配对！")
        print(send_command("MOCHI:BLE:PAIR"))
    elif action == "unpair-ble":
        print(send_command("MOCHI:BLE:UNPAIR"))
    elif action == "bind-ble":
        print("请现在把戴着 Apple Watch 的手腕或 iPhone 贴近小螃蟹外壳 (5~10cm 内)...")
        print(send_command("MOCHI:BLE:LEARN"))
    elif action == "reset-ble":
        print(send_command("MOCHI:BLE:RESET"))
    elif action == "reset-stats":
        print(send_command("MOCHI:RESET_STATS"))
    elif action == "sync-time":
        dt = datetime.datetime.now()
        print(send_command(f"MOCHI:TIME:{dt.hour:02d}:{dt.minute:02d}"))
    else:
        print(f"未知子命令: {action}\n")
        print(__doc__.strip())
        sys.exit(1)


if __name__ == "__main__":
    main()
