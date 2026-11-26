#!/usr/bin/env python3
"""
Clawd Mochi Mac Companion CLI, Background Daemon & Real-Time Web Console Server (tools/mochi.py)

Features:
  - Holds USB serial (/dev/cu.usbmodem*) open with zero chip resets (DTR=False, RTS=False, ~HUPCL).
  - Syncs Mac wall-clock time, tracks screen-lock / sitting duration & CPU spikes.
  - Exposes both local Unix socket (/tmp/clawd-mochi.sock) AND real-time HTTP server
    at http://127.0.0.1:18765 (serving preview.html + /api/status + /api/cmd).
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

SOCK_PATH = "/tmp/clawd-mochi.sock"
PID_PATH = "/tmp/clawd-mochi.pid"
BAUD_RATE = 115200
HTTP_PORT = int(os.environ.get("MOCHI_HTTP_PORT", "18765"))
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


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
        self.active_streak_start = time.monotonic()
        self.was_away = False

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
                            self.latest_status = parse_status_line(line, self.port_name)
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
            return self.latest_status
        return {"online": False, "error": resp}

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
                    # If status older than 0.65s, refresh from device
                    st = daemon_ref.latest_status
                    if not st.get("online") or (time.time() - st.get("ts", 0) > 0.65):
                        st = daemon_ref.refresh_status()
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
                if parsed.path != "/api/cmd":
                    self.send_error(404)
                    return
                length = int(self.headers.get("Content-Length", "0"))
                raw_body = self.rfile.read(length).decode("utf-8", errors="replace")
                try:
                    req = json.loads(raw_body) if raw_body else {}
                except Exception:
                    self._send_json({"ok": False, "error": "invalid json"}, 400)
                    return
                cmd = str(req.get("cmd", "")).strip()
                if not cmd.startswith("MOCHI:"):
                    self._send_json({"ok": False, "error": "cmd must start with MOCHI:"}, 400)
                    return
                reply = daemon_ref.send_locked(cmd)
                # Give firmware 45ms to enter the new event/expression so status reflects it immediately
                time.sleep(0.045)
                st = daemon_ref.refresh_status()
                self._send_json({"ok": reply.startswith("OK:"), "reply": reply, "status": st})

        try:
            ThreadingHTTPServer.allow_reuse_address = True
            httpd = ThreadingHTTPServer(("127.0.0.1", HTTP_PORT), Handler)
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
        server.listen(8)

        httpd = self.build_http_server()
        if httpd is not None:
            ht = threading.Thread(target=httpd.serve_forever, daemon=True)
            ht.start()
            print(f"[mochi-daemon] 实时控制台已就绪: http://127.0.0.1:{HTTP_PORT}/preview.html")

        def handle_sig(_signum, _frame):
            self.running = False

        signal.signal(signal.SIGINT, handle_sig)
        signal.signal(signal.SIGTERM, handle_sig)

        t = threading.Thread(target=self.socket_worker, args=(server,), daemon=True)
        t.start()

        cpu_cores = os.cpu_count() or 8
        print(f"[mochi-daemon] 已启动 (PID={os.getpid()}, socket={SOCK_PATH})")

        try:
            while self.running:
                now_mono = time.monotonic()

                # 0. Poll live hardware status every 1.0s so web UI mirrors device smoothly
                if now_mono - self.last_status_poll >= 1.0:
                    self.last_status_poll = now_mono
                    self.refresh_status()

                # 1. Sync wall-clock time every 60s
                if now_mono - self.last_time_sync >= 60.0 or self.last_time_sync == 0.0:
                    dt = datetime.datetime.now()
                    res = self.send_locked(f"MOCHI:TIME:{dt.hour:02d}:{dt.minute:02d}")
                    if res.startswith("OK:"):
                        self.last_time_sync = now_mono

                # 2. Check Mac presence & continuous sitting every 20s
                if now_mono - self.last_presence_sync >= 20.0 or self.last_presence_sync == 0.0:
                    self.last_presence_sync = now_mono
                    idle_sec = get_macos_hid_idle_seconds()
                    locked = is_macos_screen_locked()
                    if not locked and idle_sec < 1500:
                        if self.was_away and idle_sec < 60:
                            self.was_away = False
                            self.active_streak_start = now_mono
                        self.send_locked(f"MOCHI:PRESENCE:ACTIVE:{min(idle_sec, 120)}")

                        if idle_sec < 240:
                            sitting_min = (now_mono - self.active_streak_start) / 60.0
                            if (
                                sitting_min >= 50.0
                                and (now_mono - self.last_sitting_alert) >= 1800.0
                            ):
                                self.last_sitting_alert = now_mono
                                self.send_locked(
                                    f"MOCHI:SYS:SITTING:连坐 {int(sitting_min)} 分钟啦! 喝口水~"
                                )
                        else:
                            self.active_streak_start = now_mono
                    else:
                        self.was_away = True
                        self.active_streak_start = now_mono
                        self.send_locked(f"MOCHI:PRESENCE:IDLE:{max(idle_sec, 600)}")

                    try:
                        load1, _, _ = os.getloadavg()
                        if (
                            load1 > cpu_cores * 1.35
                            and (now_mono - self.last_cpu_alert) >= 600.0
                        ):
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
    quip_str = f' | 当前气泡: "{st["quip"]}"' if st.get("quip") else ""
    return (
        f"小螃蟹状态:\n"
        f"  当前心情: {st['mood']} (表情 #{st['expr']:02d}, 背光 {round(st['bright']*100/255)}%)  |  在岗状态: {present_str}\n"
        f"  见面次数: 第 {st['boots']} 次  |  累计陪伴: {st['mins']} 分钟\n"
        f"  亲密度  : Lv.{st['lv']} (爱心值 {st['love']})\n"
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
    elif action == "say":
        text = " ".join(args).strip() or "Hello~"
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
