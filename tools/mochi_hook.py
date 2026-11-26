#!/usr/bin/env python3
"""
Clawd Mochi Universal AI Coding Hook Bridge (tools/mochi_hook.py)

Supports:
  1. Antigravity IDE & CLI (`agy`)      -> ~/.gemini/config/hooks.json
  2. Claude Code CLI & IDE (`claude`)   -> ~/.claude/settings.json
  3. Codex Desktop & CLI (`codex` / `codex-notify`) -> ~/.codex/hooks.json & ~/.codex/config.toml

Safety guarantees:
  - Non-blocking (<50ms typical, hard 0.2s socket timeout).
  - Silent no-op if Clawd Mochi USB is unplugged (never errors or blocks any AI tool).
  - Rate-limits high-frequency THINK/TOOL events (1 update per 3.5s) while letting
    ASK (waiting for user) and DONE (finished) through immediately.
  - Strictly emits required stdout JSON for Antigravity (`{"decision": "allow"}` / `{"decision": "stop"}`).
"""

import glob
import json
import os
import socket
import subprocess
import sys
import time

SOCK_PATH = "/tmp/clawd-mochi.sock"
RATE_FILE = "/tmp/clawd-mochi-hook-rate.json"
DAEMON_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mochi.py")
SKY_NOTIFY_BIN = (
    "/Users/yan/.codex/computer-use/Codex Computer Use.app/Contents/SharedSupport/"
    "SkyComputerUseClient.app/Contents/MacOS/SkyComputerUseClient"
)


def friendly_tool_quip(tool_name: str, agent_label: str) -> tuple[str, str]:
    """Return (kind, quip) where kind is 'ASK' or 'TOOL'."""
    t = (tool_name or "").lower()
    if t in ("ask_question", "askuserquestion", "request_user_input"):
        return ("ASK", f"{agent_label} 等你拍板!")
    if any(k in t for k in ("run_command", "bash", "shell", "exec")):
        return ("TOOL", f"{agent_label} 正在跑命令...")
    if any(k in t for k in ("edit", "write", "patch", "notebook")):
        return ("TOOL", f"{agent_label} 正在改代码...")
    if any(k in t for k in ("read", "view", "grep", "glob", "find", "list")):
        return ("TOOL", f"{agent_label} 正在查代码...")
    if any(k in t for k in ("web", "browser", "search", "fetch")):
        return ("TOOL", f"{agent_label} 正在查资料...")
    if "subagent" in t or "agent" in t or "task" in t:
        return ("TOOL", f"{agent_label} 派小助手干活中~")
    return ("TOOL", f"{agent_label} 忙碌干活中...")


def should_throttle_think(new_kind: str) -> bool:
    now = time.time()
    try:
        if os.path.exists(RATE_FILE):
            with open(RATE_FILE, "r", encoding="utf-8") as f:
                st = json.load(f)
            if new_kind in ("THINK", "TOOL") and st.get("kind") in ("THINK", "TOOL"):
                if now - float(st.get("ts", 0)) < 3.5:
                    return True
    except Exception:
        pass
    try:
        with open(RATE_FILE, "w", encoding="utf-8") as f:
            json.dump({"kind": new_kind, "ts": now}, f)
    except Exception:
        pass
    return False


def send_mochi_fast(cmd: str, kind: str) -> None:
    if kind in ("THINK", "TOOL") and should_throttle_think(kind):
        return
    if kind in ("ASK", "DONE"):
        should_throttle_think(kind)  # record state reset

    if os.path.exists(SOCK_PATH):
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
                s.settimeout(0.22)
                s.connect(SOCK_PATH)
                s.sendall((cmd.strip() + "\n").encode("utf-8"))
                s.recv(512)
            return
        except Exception:
            pass

    # If daemon isn't running yet, only auto-spawn in background if USB hardware is present
    if glob.glob("/dev/cu.usbmodem*") or glob.glob("/dev/cu.usbserial*"):
        try:
            with open("/tmp/clawd-mochi.log", "ab") as logf:
                subprocess.Popen(
                    [sys.executable, DAEMON_SCRIPT, "daemon"],
                    stdout=logf,
                    stderr=logf,
                    stdin=subprocess.DEVNULL,
                    start_new_session=True,
                )
        except Exception:
            pass


def read_stdin_json() -> dict:
    try:
        if sys.stdin.isatty():
            return {}
        raw = sys.stdin.read()
        return json.loads(raw) if raw.strip() else {}
    except Exception:
        return {}


def handle_antigravity() -> None:
    payload = read_stdin_json()
    ev = payload.get("hookEventName", "")

    if ev == "PreInvocation":
        send_mochi_fast("MOCHI:AI:THINK:Antigravity 思考中...", "THINK")
        sys.stdout.write("{}\n")
    elif ev == "PreToolUse":
        tc = payload.get("toolCall") or {}
        tname = tc.get("name", "")
        kind, quip = friendly_tool_quip(tname, "AGY")
        send_mochi_fast(f"MOCHI:AI:{kind}:{quip}", kind)
        sys.stdout.write('{"decision":"allow"}\n')
    elif ev == "Stop":
        reason = payload.get("terminationReason", "")
        fully_idle = payload.get("fullyIdle", True)
        if reason == "ERROR":
            send_mochi_fast("MOCHI:SYS:SITTING:AGY 执行出错啦!", "ASK")
        elif fully_idle and reason != "USER_CANCELED":
            send_mochi_fast("MOCHI:AI:DONE:AGY 搞定啦! 快验收~", "DONE")
        sys.stdout.write('{"decision":"stop"}\n')
    else:
        sys.stdout.write("{}\n")
    sys.stdout.flush()


def handle_claude_or_codex(agent_label: str) -> None:
    payload = read_stdin_json()
    ev = payload.get("hook_event_name") or payload.get("hookEventName") or ""

    if ev == "UserPromptSubmit":
        send_mochi_fast(f"MOCHI:AI:THINK:{agent_label} 思考中...", "THINK")
    elif ev == "PreToolUse":
        tname = payload.get("tool_name") or (payload.get("toolCall") or {}).get("name", "")
        kind, quip = friendly_tool_quip(tname, agent_label)
        send_mochi_fast(f"MOCHI:AI:{kind}:{quip}", kind)
    elif ev in ("PermissionRequest", "Notification"):
        ntype = str(payload.get("notification_type", "")).lower()
        if ev == "PermissionRequest" or any(k in ntype for k in ("permission", "elicitation", "idle", "input")):
            send_mochi_fast(f"MOCHI:AI:ASK:{agent_label} 等你拍板!", "ASK")
    elif ev in ("Stop", "TaskCompleted"):
        send_mochi_fast(f"MOCHI:AI:DONE:{agent_label} 搞定啦!", "DONE")


def handle_codex_notify(extra_args: list[str]) -> None:
    # 1. Preserve original SkyComputerUseClient turn-ended notification seamlessly
    if os.path.exists(SKY_NOTIFY_BIN):
        try:
            subprocess.Popen(
                [SKY_NOTIFY_BIN, "turn-ended"] + extra_args,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                stdin=subprocess.DEVNULL,
                start_new_session=True,
            )
        except Exception:
            pass

    # 2. Parse Codex notify JSON payload if present
    raw_joined = " ".join(extra_args).lower()
    if "approval" in raw_joined or "permission" in raw_joined:
        send_mochi_fast("MOCHI:AI:ASK:Codex 等你确认!", "ASK")
    else:
        send_mochi_fast("MOCHI:AI:DONE:Codex 搞定啦!", "DONE")


def main() -> int:
    try:
        mode = sys.argv[1].lower() if len(sys.argv) > 1 else "claude"
        if mode == "agy":
            handle_antigravity()
        elif mode == "claude":
            handle_claude_or_codex("Claude")
        elif mode == "codex":
            handle_claude_or_codex("Codex")
        elif mode == "codex-notify":
            handle_codex_notify(sys.argv[2:])
    except Exception:
        # Never break host agent even on unexpected error
        if len(sys.argv) > 1 and sys.argv[1].lower() == "agy":
            sys.stdout.write('{"decision":"allow"}\n')
            sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
