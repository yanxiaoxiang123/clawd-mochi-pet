#!/usr/bin/env python3
"""
Clawd Mochi Mac Companion CLI & Background Daemon (tools/mochi.py)

Zero-conflict architecture:
  - `daemon` holds the USB serial connection (/dev/cu.usbmodem*) without toggling
    RTS/DTR reset lines, syncs wall-clock time, tracks macOS keyboard/mouse idle &
    continuous sitting time, monitors CPU load spikes, and exposes a local Unix
    socket (/tmp/clawd-mochi.sock) so CLI commands run in <5ms with zero port conflicts.
  - If `daemon` is not running, CLI commands talk directly to `/dev/cu.usbmodem*`.

Usage:
  python3 tools/mochi.py status
  python3 tools/mochi.py pet
  python3 tools/mochi.py say "喝口水再继续写~"
  python3 tools/mochi.py focus 25       # start 25-min Pomodoro (3px bar at bottom)
  python3 tools/mochi.py focus stop
  python3 tools/mochi.py ai think [msg] # AI started thinking / running tools
  python3 tools/mochi.py ai done  [msg] # AI finished task
  python3 tools/mochi.py ai ask   [msg] # AI waiting for approval
  python3 tools/mochi.py expr <0..14>
  python3 tools/mochi.py theater
  python3 tools/mochi.py bind-ble       # calibrate Apple Watch / iPhone tap-to-pet
  python3 tools/mochi.py daemon         # run background Mac companion loop
  python3 tools/mochi.py stop-daemon    # cleanly stop running daemon (e.g. before flashing)
"""

import datetime
import glob
import os
import re
import signal
import socket
import subprocess
import sys
import threading
import time
from typing import Optional

SOCK_PATH = "/tmp/clawd-mochi.sock"
PID_PATH = "/tmp/clawd-mochi.pid"
BAUD_RATE = 115200


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
    # ESP32-C3 USB-Serial/JTAG: keep DTR=False, RTS=False so we never enter DOWNLOAD mode
    ser.dtr = False
    ser.rts = False
    ser.open()
    # Prevent macOS HUPCL from dropping DTR on close (which triggers ESP32-C3 USB_UART_CHIP_RESET)
    try:
        fd = ser.fileno()
        attrs = termios.tcgetattr(fd)
        attrs[2] &= ~termios.HUPCL
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except Exception:
        pass
    return ser


def is_macos_screen_locked() -> bool:
    """Return True if macOS session screen is currently locked."""
    try:
        out = subprocess.check_output(
            ["ioreg", "-n", "Root", "-d1", "-a"],
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=2.0,
        )
        if "<key>CGSSessionScreenIsLocked</key>\n\t\t<true/>" in out or (
            "CGSSessionScreenIsLocked" in out and "<true/>" in out.split("CGSSessionScreenIsLocked", 1)[1][:80]
        ):
            return True
    except Exception:
        pass
    return False


def get_macos_hid_idle_seconds() -> int:
    """Return seconds since last macOS keyboard/mouse/trackpad event via IOHIDSystem."""
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


def ensure_daemon_running(wait_sec: float = 5.0) -> bool:
    if send_via_daemon_socket("MOCHI:STATUS", timeout=2.5) is not None:
        return True
    if not find_serial_port():
        return False
    # Auto-start background daemon so ESP32-C3 USB-JTAG port stays open with zero resets
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
        self.last_time_sync = 0.0
        self.last_presence_sync = 0.0
        self.last_cpu_alert = 0.0
        self.last_sitting_alert = 0.0
        self.active_streak_start = time.monotonic()
        self.was_away = False

    def ensure_serial(self) -> bool:
        if self.ser is not None and self.ser.is_open:
            return True
        port = find_serial_port()
        if not port:
            return False
        try:
            self.ser = open_serial_no_reset(port, timeout=0.35)
            # Allow ESP32-C3 setup() + BLE init (~2.2s) to finish if USB attach triggered boot
            time.sleep(2.5)
            self.ser.reset_input_buffer()
            return True
        except Exception:
            self.ser = None
            return False

    def send_locked(self, raw_cmd: str) -> str:
        with self.lock:
            if not self.ensure_serial():
                return "ERR:NO_DEVICE"
            try:
                self.ser.reset_input_buffer()
                self.ser.write((raw_cmd.strip() + "\n").encode("utf-8"))
                self.ser.flush()
                deadline = time.monotonic() + 0.8
                while time.monotonic() < deadline:
                    line = self.ser.readline().decode("utf-8", errors="replace").strip()
                    if line.startswith("OK:"):
                        return line
                return "OK:SENT"
            except Exception as exc:
                try:
                    self.ser.close()
                except Exception:
                    pass
                self.ser = None
                return f"ERR:{exc}"

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

    def run(self):
        if os.path.exists(SOCK_PATH):
            if send_via_daemon_socket("MOCHI:STATUS") is not None:
                print("守护进程已在运行中。")
                return
            os.unlink(SOCK_PATH)

        with open(PID_PATH, "w", encoding="utf-8") as f:
            f.write(str(os.getpid()))

        # Open & stabilize serial port BEFORE exposing UNIX socket so first client gets <10ms reply
        self.ensure_serial()

        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.settimeout(1.0)
        server.bind(SOCK_PATH)
        server.listen(8)

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

                # 1. Sync wall-clock time every 60s
                if now_mono - self.last_time_sync >= 60.0 or self.last_time_sync == 0.0:
                    dt = datetime.datetime.now()
                    res = self.send_locked(f"MOCHI:TIME:{dt.hour:02d}:{dt.minute:02d}")
                    if res.startswith("OK:"):
                        self.last_time_sync = now_mono

                # 2. Check Mac presence (screen lock / 25m deep idle) & continuous sitting every 20s
                if now_mono - self.last_presence_sync >= 20.0 or self.last_presence_sync == 0.0:
                    self.last_presence_sync = now_mono
                    idle_sec = get_macos_hid_idle_seconds()
                    locked = is_macos_screen_locked()
                    if not locked and idle_sec < 1500:
                        if self.was_away and idle_sec < 60:
                            self.was_away = False
                            self.active_streak_start = now_mono
                        # Send active presence (<180 clamp so firmware keeps ownerPresent=true while reading/watching)
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
                            # Took a >4 min break from typing -> reset sitting timer
                            self.active_streak_start = now_mono
                    else:
                        self.was_away = True
                        self.active_streak_start = now_mono
                        self.send_locked(f"MOCHI:PRESENCE:IDLE:{max(idle_sec, 600)}")

                    # 3. Check Mac CPU load spike
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

                time.sleep(2.0)
        finally:
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
    if not resp.startswith("OK:STATUS:"):
        return resp
    kv = {}
    for item in resp[len("OK:STATUS:") :].split(","):
        if "=" in item:
            k, v = item.split("=", 1)
            kv[k] = v
    pomo_s = int(kv.get("pomo", "0"))
    pomo_str = f"{pomo_s // 60}分{pomo_s % 60:02d}秒" if pomo_s > 0 else "未开启"
    present_str = "在桌前" if kv.get("present") == "1" else "离开工位"
    return (
        f"小螃蟹状态:\n"
        f"  当前心情: {kv.get('mood')}  |  在岗状态: {present_str}\n"
        f"  见面次数: 第 {kv.get('boots')} 次  |  累计陪伴: {kv.get('mins')} 分钟\n"
        f"  亲密度  : Lv.{kv.get('lv')} (爱心值 {kv.get('love')})\n"
        f"  芯片温度: {kv.get('temp')} °C  |  最近蓝牙信号: {kv.get('rssi')} dBm\n"
        f"  专注番茄: {pomo_str}  |  同步时间: {kv.get('clock')}"
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
