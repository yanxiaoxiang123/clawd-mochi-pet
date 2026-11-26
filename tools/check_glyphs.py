#!/usr/bin/env python3
"""
Guard check: ensures firmware speech rendering never regresses to subset fonts
(like unifont_t_chinese1 which drops characters such as '啦', '欢', '迎', '座'),
and verifies every non-ASCII character in firmware speech strings encodes cleanly
under GB2312 (covered by u8g2_font_wqy16/13/12_t_gb2312) without 4-byte emojis.
"""

import glob
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW_DIR = os.path.join(REPO_ROOT, "clawd_mochi_nowifi")


def main() -> int:
    errors = []

    speech_h = os.path.join(FW_DIR, "dynamics-speech.h")
    content = open(speech_h, encoding="utf-8").read()

    if "unifont_t_chinese1" in re.sub(r"//.*", "", content):
        errors.append(
            "clawd_mochi_nowifi/dynamics-speech.h: 禁止使用子集字体 u8g2_font_unifont_t_chinese1（仅约400字，会导致'啦/欢/迎/座'等常用字缺失）；请使用 u8g2_font_wqy16_t_gb2312 全字库字体。"
        )

    for required_font in ("u8g2_font_wqy16_t_gb2312", "u8g2_font_wqy12_t_gb2312"):
        if required_font not in content:
            errors.append(
                f"clawd_mochi_nowifi/dynamics-speech.h: 缺少全字库字体 {required_font}，请保留自适应字号以防止缺字或右侧截断。"
            )

    for path in sorted(glob.glob(os.path.join(FW_DIR, "*.h")) + glob.glob(os.path.join(FW_DIR, "*.ino"))):
        rel = os.path.relpath(path, REPO_ROOT)
        lines = open(path, encoding="utf-8").readlines()
        for lineno, line in enumerate(lines, start=1):
            # Strip C++ single-line comments before inspecting string literals
            code_part = re.sub(r"//.*$", "", line)
            for m in re.finditer(r'"((?:\\.|[^"\\])*)"', code_part):
                lit = m.group(1)
                for ch in lit:
                    if ord(ch) < 128:
                        continue
                    if ord(ch) > 0xFFFF:
                        errors.append(
                            f"{rel}:{lineno}: 台词字面量包含 4 字节 Emoji {ch!r}，点阵字库无法显示，请改用中文或 ASCII 颜文字（如 <3、^_^）。"
                        )
                        continue
                    try:
                        ch.encode("gb2312")
                    except UnicodeEncodeError:
                        errors.append(
                            f"{rel}:{lineno}: 字符 {ch!r} (U+{ord(ch):04X}) 不在 GB2312 字库范围内，屏幕会漏字；请替换为常用简体汉字或 ASCII 符号。"
                        )

    if errors:
        print("字库与气泡完整性检查未通过：", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        return 1

    print("OK: 全部固件台词均通过 GB2312 全字库与防截断守卫检查。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
