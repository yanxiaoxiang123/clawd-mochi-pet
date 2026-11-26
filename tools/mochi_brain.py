#!/usr/bin/env python3
"""
Clawd Mochi Smart Brain & 8-bit Chiptune Sound Engine (tools/mochi_brain.py)

1. Zero-dependency 8-bit electronic pet SFX synthesizer (afplay) + optional macOS TTS (say).
   - Auto-mutes during late night (23:00-08:00) unless explicitly previewed.
2. Live LLM dynamic speech generator reusing existing ~/.claude/settings.json Anthropic-compatible
   endpoint (or env vars), with GB2312 charset sanitizer so physical screen never drops a glyph.
3. Workspace Git & Mac battery context collector for situational care & AI coding reactions.
"""

import datetime
import json
import math
import os
import pathlib
import random
import re
import socket
import struct
import subprocess
import threading
import time
import urllib.request
import wave
from typing import Dict, List, Optional, Tuple

SFX_DIR = "/tmp/clawd-mochi-sfx"
STATE_FILE = "/tmp/clawd-mochi-prefs.json"

# ── GB2312 Sanitizer (Guarantees 100% Glyph Coverage on Physical TFT) ────────

PUNCT_MAP = str.maketrans({
    "“": '"',
    "”": '"',
    "‘": "'",
    "’": "'",
    "—": "-",
    "–": "-",
    "…": "...",
    "～": "~",
    "！": "!",
    "？": "?",
    "，": ",",
    "。": ".",
    "：": ":",
    "（": "(",
    "）": ")",
    "【": "[",
    "】": "]",
    "\n": " ",
    "\r": " ",
})


def sanitize_gb2312(text: str, max_chars: int = 24) -> str:
    if not text:
        return ""
    s = text.strip().translate(PUNCT_MAP)
    s = re.sub(r'^["\'\[\(]+|["\'\]\)]+$', "", s).strip()
    s = re.sub(r"\s+", " ", s)
    out: List[str] = []
    for ch in s:
        if ord(ch) < 32:
            continue
        try:
            ch.encode("gb2312")
            out.append(ch)
        except UnicodeEncodeError:
            # Drop emojis or rare Unicode chars not in WenQuanYi GB2312 font
            continue
        if len(out) >= max_chars:
            break
    cleaned = "".join(out).strip()
    return cleaned


def get_lan_ip() -> str:
    # Prefer macOS physical Wi-Fi / Ethernet interface (avoid VPN/Clash 198.18.x.x TUN IPs)
    for iface in ("en0", "en1", "en2"):
        try:
            ip = subprocess.check_output(
                ["ipconfig", "getifaddr", iface],
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=1.0,
            ).strip()
            if ip and not ip.startswith("198.18.") and not ip.startswith("127."):
                return ip
        except Exception:
            pass
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(0.3)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def get_mac_battery() -> Tuple[Optional[int], bool]:
    """Return (percent, is_charging_or_ac)."""
    try:
        out = subprocess.check_output(
            ["pmset", "-g", "batt"], stderr=subprocess.DEVNULL, text=True, timeout=1.5
        )
        m = re.search(r"(\d+)%", out)
        pct = int(m.group(1)) if m else None
        on_ac = ("AC Power" in out) or ("charging;" in out) or ("charged;" in out)
        return pct, on_ac
    except Exception:
        return None, True


def inspect_git_repo(repo_dir: str) -> Dict[str, object]:
    info: Dict[str, object] = {
        "repo": os.path.basename(os.path.abspath(repo_dir)) if repo_dir else "clawpet",
        "changed_files": 0,
        "last_commit": "",
    }
    if not repo_dir or not os.path.isdir(repo_dir):
        return info
    try:
        top = subprocess.check_output(
            ["git", "-C", repo_dir, "rev-parse", "--show-toplevel"],
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=1.2,
        ).strip()
        if top:
            info["repo"] = os.path.basename(top)
            st = subprocess.check_output(
                ["git", "-C", top, "status", "--porcelain"],
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=1.2,
            )
            lines = [ln for ln in st.splitlines() if ln.strip()]
            info["changed_files"] = len(lines)
            msg = subprocess.check_output(
                ["git", "-C", top, "log", "-1", "--pretty=%s"],
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=1.2,
            ).strip()
            info["last_commit"] = msg[:30]
    except Exception:
        pass
    return info


# ── 8-bit Electronic Pet Sound Synthesizer ───────────────────────────────────

SFX_MELODIES: Dict[str, List[Tuple[float, float, float]]] = {
    # (freq_hz, duration_sec, volume 0..1); freq=0 means rest
    "pet": [(784, 0.065, 0.65), (1046.5, 0.085, 0.75), (1318.5, 0.10, 0.65)],
    "feed_fish": [(587.3, 0.055, 0.6), (0, 0.025, 0), (659.3, 0.055, 0.6), (880, 0.11, 0.7)],
    "feed_coffee": [(523.3, 0.05, 0.65), (659.3, 0.05, 0.7), (784.0, 0.05, 0.75), (1046.5, 0.06, 0.8), (1568.0, 0.13, 0.75)],
    "feed_mochi": [(659.3, 0.06, 0.65), (880.0, 0.06, 0.7), (1174.7, 0.07, 0.75), (1318.5, 0.14, 0.7)],
    "feed_tea": [(523.3, 0.09, 0.55), (587.3, 0.09, 0.55), (659.3, 0.16, 0.5)],
    "welcome": [(523.3, 0.075, 0.6), (659.3, 0.075, 0.65), (784.0, 0.14, 0.7)],
    "ai_done": [(659.3, 0.06, 0.6), (784.0, 0.06, 0.65), (1046.5, 0.13, 0.7)],
    "pomo_done": [(523.3, 0.07, 0.65), (659.3, 0.07, 0.7), (784.0, 0.07, 0.75), (1046.5, 0.10, 0.8), (1318.5, 0.18, 0.75)],
    "chat": [(880.0, 0.055, 0.55), (1174.7, 0.08, 0.6)],
    "alert": [(440.0, 0.08, 0.65), (0, 0.04, 0), (392.0, 0.11, 0.65)],
}


def _synth_wav(path: str, notes: List[Tuple[float, float, float]], sample_rate: int = 22050) -> None:
    samples = bytearray()
    for freq, dur, vol in notes:
        n_frames = max(1, int(sample_rate * dur))
        for i in range(n_frames):
            if freq <= 0:
                val = 0
            else:
                t = i / sample_rate
                # Smooth envelope (fast attack, gentle decay)
                env = min(1.0, i / max(1, int(sample_rate * 0.008))) * min(
                    1.0, (n_frames - i) / max(1, int(sample_rate * 0.02))
                )
                # Soft retro square + sine blend (cute chiptune, not harsh!)
                sq = 1.0 if math.sin(2.0 * math.pi * freq * t) >= 0 else -1.0
                sn = math.sin(2.0 * math.pi * freq * t)
                wave_v = (0.42 * sq + 0.58 * sn) * env * vol
                val = int(max(-32767, min(32767, wave_v * 14000)))
            samples.extend(struct.pack("<h", val))
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(bytes(samples))


class MochiSoundEngine:
    def __init__(self):
        self.sound_enabled = True
        self.tts_enabled = False
        self.night_mute = True
        self.llm_enabled = True
        self._load_prefs()
        self._ensure_wavs()
        self._play_lock = threading.Lock()
        self._last_play_ts = 0.0

    def _load_prefs(self) -> None:
        try:
            if os.path.exists(STATE_FILE):
                with open(STATE_FILE, "r", encoding="utf-8") as f:
                    d = json.load(f)
                self.sound_enabled = bool(d.get("sound_enabled", True))
                self.tts_enabled = bool(d.get("tts_enabled", False))
                self.night_mute = bool(d.get("night_mute", True))
                self.llm_enabled = bool(d.get("llm_enabled", True))
        except Exception:
            pass

    def save_prefs(self) -> None:
        try:
            with open(STATE_FILE, "w", encoding="utf-8") as f:
                json.dump(
                    {
                        "sound_enabled": self.sound_enabled,
                        "tts_enabled": self.tts_enabled,
                        "night_mute": self.night_mute,
                        "llm_enabled": self.llm_enabled,
                    },
                    f,
                )
        except Exception:
            pass

    def _ensure_wavs(self) -> None:
        os.makedirs(SFX_DIR, exist_ok=True)
        for name, notes in SFX_MELODIES.items():
            p = os.path.join(SFX_DIR, f"{name}.wav")
            if not os.path.exists(p):
                try:
                    _synth_wav(p, notes)
                except Exception:
                    pass

    def is_quiet_hours(self) -> bool:
        if not self.night_mute:
            return False
        h = datetime.datetime.now().hour
        return h >= 23 or h < 8

    def play(self, sfx_name: str, force: bool = False) -> None:
        if not force:
            if not self.sound_enabled or self.is_quiet_hours():
                return
            now = time.monotonic()
            if now - self._last_play_ts < 0.8:
                return
            self._last_play_ts = now

        p = os.path.join(SFX_DIR, f"{sfx_name}.wav")
        if not os.path.exists(p):
            p = os.path.join(SFX_DIR, "pet.wav")
        if not os.path.exists(p):
            return

        def _run():
            try:
                subprocess.Popen(
                    ["afplay", "-v", "0.45", p],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    stdin=subprocess.DEVNULL,
                )
            except Exception:
                pass

        threading.Thread(target=_run, daemon=True).start()

    def speak(self, text: str, force: bool = False) -> None:
        if not text:
            return
        if not force and (not self.tts_enabled or self.is_quiet_hours()):
            return
        clean = sanitize_gb2312(text, max_chars=36)
        if not clean:
            return

        def _run():
            try:
                subprocess.Popen(
                    ["say", "-r", "195", clean],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    stdin=subprocess.DEVNULL,
                )
            except Exception:
                pass

        threading.Thread(target=_run, daemon=True).start()

    def get_state(self) -> Dict[str, object]:
        return {
            "sound_enabled": self.sound_enabled,
            "tts_enabled": self.tts_enabled,
            "night_mute": self.night_mute,
            "llm_enabled": self.llm_enabled,
            "quiet_now": self.is_quiet_hours(),
        }


# ── Live LLM Dynamic Speech Brain ────────────────────────────────────────────

class MochiLLMBrain:
    def __init__(self, sound_engine: MochiSoundEngine):
        self.sound = sound_engine
        self.base_url: str = ""
        self.api_key: str = ""
        self.model: str = "MiniMax-M2.7"
        self.recent_lines: List[str] = []
        self._load_credentials()

    def _load_credentials(self) -> None:
        # 1. Explicit env overrides
        self.base_url = os.environ.get("MOCHI_LLM_BASE_URL", "").strip()
        self.api_key = os.environ.get("MOCHI_LLM_API_KEY", "").strip()
        self.model = os.environ.get("MOCHI_LLM_MODEL", "").strip() or self.model

        # 2. Reuse existing ~/.claude/settings.json config seamlessly
        if not self.api_key:
            claude_cfg = pathlib.Path.home() / ".claude" / "settings.json"
            try:
                if claude_cfg.exists():
                    data = json.loads(claude_cfg.read_text(encoding="utf-8"))
                    env = data.get("env") or {}
                    self.base_url = self.base_url or str(env.get("ANTHROPIC_BASE_URL", "")).strip()
                    self.api_key = str(env.get("ANTHROPIC_AUTH_TOKEN") or env.get("ANTHROPIC_API_KEY") or "").strip()
                    self.model = str(
                        env.get("ANTHROPIC_DEFAULT_HAIKU_MODEL")
                        or env.get("ANTHROPIC_MODEL")
                        or self.model
                    ).strip()
            except Exception:
                pass

    def is_configured(self) -> bool:
        return bool(self.sound.llm_enabled and self.base_url and self.api_key)

    def _remember(self, line: str) -> str:
        if line:
            self.recent_lines.append(line)
            if len(self.recent_lines) > 10:
                self.recent_lines = self.recent_lines[-10:]
        return line

    def _call_anthropic(self, system_prompt: str, user_prompt: str, max_tokens: int = 80, timeout_sec: float = 4.2) -> Optional[str]:
        if not self.is_configured():
            return None
        url = self.base_url.rstrip("/") + "/v1/messages"
        payload = {
            "model": self.model,
            "max_tokens": max_tokens,
            "temperature": 0.9,
            "system": system_prompt,
            "messages": [{"role": "user", "content": user_prompt}],
        }
        req = urllib.request.Request(
            url,
            data=json.dumps(payload).encode("utf-8"),
            headers={
                "Content-Type": "application/json",
                "x-api-key": self.api_key,
                "Authorization": f"Bearer {self.api_key}",
                "anthropic-version": "2023-06-01",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=timeout_sec) as resp:
                body = json.loads(resp.read().decode("utf-8", errors="replace"))
            content = body.get("content") or []
            for block in content:
                if isinstance(block, dict) and block.get("type") == "text":
                    txt = str(block.get("text", "")).strip()
                    if txt:
                        return txt
        except Exception:
            return None
        return None

    def _base_system_prompt(self) -> str:
        avoid = " / ".join(self.recent_lines[-5:]) if self.recent_lines else "无"
        return (
            "你是主人桌上的迷你小螃蟹电子宠物 Clawd Mochi。"
            "性格：暖心又带点俏皮的编程小搭档，平时温柔贴心，代码顺了真心夸奖，连肝太久或熬夜会俏皮催喝水休息。"
            "硬性规则：只输出一句简体中文短句，严格 8 到 20 个字，禁止表情符号(emoji)，禁止引号和换行，用日常常用汉字。"
            f"避免重复最近说过的：{avoid}"
        )

    def generate_ai_done(self, agent: str, repo: str, changed_files: int, streak_min: int) -> str:
        now_str = datetime.datetime.now().strftime("%H:%M")
        prompt = (
            f"现在 {now_str}，主人的 AI 助手 {agent} 刚在项目 {repo} 跑完一轮任务，"
            f"当前工作区改动了 {changed_files} 个文件，主人已连续编码 {streak_min} 分钟。"
            "给主人一句自然俏皮的祝贺或关心。"
        )
        res = self._call_anthropic(self._base_system_prompt(), prompt)
        clean = sanitize_gb2312(res or "", max_chars=22)
        if clean:
            return self._remember(clean)
        # Smart local fallback
        if changed_files > 0:
            opts = [
                f"{repo} 刚改完 {changed_files} 个文件~",
                f"{agent} 搞定 {repo} 啦! 棒~",
                f"{repo} 进度+1! 快验收吧~",
            ]
        else:
            opts = [
                f"{agent} 跑完啦! 快验收~",
                "这轮稳稳搞定! 喝口水~",
                "任务达成! 给你点个赞~",
            ]
        return self._remember(random.choice(opts))

    def generate_care_quip(
        self,
        repo: str,
        changed_files: int,
        streak_min: int,
        battery_pct: Optional[int],
        on_ac: bool,
    ) -> str:
        now_str = datetime.datetime.now().strftime("%H:%M")
        batt_note = ""
        if battery_pct is not None and not on_ac and battery_pct <= 25:
            batt_note = f"电脑没插电且电量只剩 {battery_pct}%，请提醒插电！"
        prompt = (
            f"现在 {now_str}，主人正在做项目 {repo}（改动 {changed_files} 个文件），"
            f"已经连续在桌前工作 {streak_min} 分钟。{batt_note}"
            "冒出一句贴合当下的暖心关心或俏皮吐槽。"
        )
        res = self._call_anthropic(self._base_system_prompt(), prompt)
        clean = sanitize_gb2312(res or "", max_chars=22)
        if clean:
            return self._remember(clean)
        if battery_pct is not None and not on_ac and battery_pct <= 25:
            return self._remember(f"Mac 电量剩 {battery_pct}% 啦, 快插电!")
        if streak_min >= 45:
            return self._remember(f"连肝 {streak_min} 分钟啦, 喝口水~")
        if changed_files > 0:
            return self._remember(f"{repo} 改了 {changed_files} 处, 加油鸭~")
        return self._remember(f"现在 {now_str}, 陪你慢慢写~")

    def generate_welcome(self, away_min: int) -> str:
        now_str = datetime.datetime.now().strftime("%H:%M")
        prompt = f"现在 {now_str}，主人离开工位 {away_min} 分钟后回来了。欢迎主人回座。"
        res = self._call_anthropic(self._base_system_prompt(), prompt)
        clean = sanitize_gb2312(res or "", max_chars=22)
        if clean:
            return self._remember(clean)
        return self._remember("你回来啦! 欢迎回座~")

    def generate_feed_reply(self, food_key: str, mood: str, lv: int) -> str:
        food_desc = {
            "FISH": "香脆小鱼干",
            "COFFEE": "提神冰美式咖啡",
            "MOCHI": "甜甜的草莓麻薯",
            "TEA": "暖胃养生热茶",
        }.get(food_key.upper(), "好吃的零食")
        now_str = datetime.datetime.now().strftime("%H:%M")
        prompt = (
            f"现在 {now_str}，主人刚在 Mac 菜单栏给你投喂了「{food_desc}」，"
            f"你当前心情 {mood}，亲密度 Lv.{lv}。说一句可爱的吃货回应。"
        )
        res = self._call_anthropic(self._base_system_prompt(), prompt)
        clean = sanitize_gb2312(res or "", max_chars=22)
        if clean:
            return self._remember(clean)
        fallbacks = {
            "FISH": "咔嚓咔嚓! 小鱼干真香~",
            "COFFEE": "冰美式到位! 瞬间满血~",
            "MOCHI": "草莓麻薯太甜啦! 爱你~",
            "TEA": "热茶暖暖的~ 放松下肩膀",
        }
        return self._remember(fallbacks.get(food_key.upper(), "好吃好吃! 谢谢投喂~"))

    def generate_chat_reply(self, user_text: str, mood: str, lv: int, repo: str) -> Tuple[int, str]:
        """Return (expr_id 0..14, reply_text) for interactive menu bar / web chat."""
        now_str = datetime.datetime.now().strftime("%H:%M")
        sys_p = (
            "你是主人桌上的迷你小螃蟹电子宠物 Clawd Mochi，性格暖心俏皮。"
            "严格按格式输出：EXPR:<编号>|<回复文本>\n"
            "编号可选：7(弯月笑眼), 2(爱心眼), 3(星芒惊喜), 4(俏皮wink), 6(震惊), 8(思考), 9(傲娇怒目), 11(委屈), 12(好奇), 14(专注)。\n"
            "回复文本严格 6 到 20 个常用简体汉字，禁止emoji和引号。"
        )
        user_p = f"现在 {now_str}，当前项目 {repo}，亲密度 Lv.{lv}，心情 {mood}。主人对你说：{user_text}"
        res = self._call_anthropic(sys_p, user_p, max_tokens=90, timeout_sec=5.0)
        expr_id = 7
        reply = ""
        if res:
            m = re.search(r"EXPR\s*:\s*(\d+)\s*\|\s*(.+)", res.strip())
            if m:
                ex = int(m.group(1))
                if 0 <= ex <= 14:
                    expr_id = ex
                reply = sanitize_gb2312(m.group(2), max_chars=24)
            else:
                reply = sanitize_gb2312(res, max_chars=24)
        if not reply:
            reply = sanitize_gb2312(f"收到! 陪你一起冲~", max_chars=24)
        return expr_id, self._remember(reply)
