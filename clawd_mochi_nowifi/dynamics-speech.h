// dynamics-speech.h — Full-GB2312 Chinese Speech Engine, Adaptive Auto-Fit & Mini-Theater
// Fixes missing characters from the tiny 411-char unifont_t_chinese1 subset by switching
// to full WenQuanYi GB2312 fonts (6,763+ Chinese characters + full punctuation):
//   - <= 216px: single-line 16px crisp font (u8g2_font_wqy16_t_gb2312)
//   - <= 222px: single-line 13px font       (u8g2_font_wqy13_t_gb2312)
//   - longer  : auto-wrapped 2-line 12px    (u8g2_font_wqy12_t_gb2312) within y=195..235

#ifndef DYNAMICS_SPEECH_H
#define DYNAMICS_SPEECH_H

#include <Arduino.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "mochi_types.h"

extern Adafruit_ST7789       tft;
extern U8G2_FOR_ADAFRUIT_GFX u8g2;
extern uint16_t animBgColor;
extern uint16_t C_ORANGE;
extern uint16_t C_DARKBG;
extern uint16_t C_BUBBLE_BORDER;
#define C_WHITE ST77XX_WHITE

extern uint32_t getBootCount();
extern uint32_t getTotalMinutes();
extern uint16_t getAffectionLevel();
extern uint32_t getAffectionPoints();
extern float    getChipTempC();
extern float    getEstRoomTempC();
extern bool     isTempCold();
extern bool     isTempWarm();
extern bool     hasWallClock();
extern uint8_t  getWallHour();

// ── Mood Quip Pools (Full GB2312 Chinese + ASCII) ────────────

const char* QUIPS_HYPER[] = {
  "Let's gooo!",
  "能量满满!",
  "冲冲冲~",
  "今天做点啥?",
  "元气全开!",
  "灵感爆发~",
  "GO GO GO!",
  "随时待命!",
};
const uint8_t QUIPS_HYPER_N = sizeof(QUIPS_HYPER) / sizeof(QUIPS_HYPER[0]);

const char* QUIPS_HAPPY[] = {
  "想你呀~",
  "今天也很棒!",
  "摸鱼快乐~",
  "代码顺顺顺!",
  "加油鸭!",
  "在忙啥呀?",
  "陪你写代码~",
  "Hello~ ^_^",
  "一把过编译!",
};
const uint8_t QUIPS_HAPPY_N = sizeof(QUIPS_HAPPY) / sizeof(QUIPS_HAPPY[0]);

const char* QUIPS_CALM[] = {
  "慢慢来~",
  "喝口水吧",
  "深呼吸...",
  "眨眨眼睛~",
  "窗外看看呀",
  "安静陪着你",
  "稳扎稳打~",
  "(发呆中...)",
};
const uint8_t QUIPS_CALM_N = sizeof(QUIPS_CALM) / sizeof(QUIPS_CALM[0]);

const char* QUIPS_SLEEPY[] = {
  "zzz...",
  "好困呀...",
  "小憩三分钟~",
  "(揉揉眼睛)",
  "打个小盹~",
  "呼噜呼噜...",
  "mrmm...",
};
const uint8_t QUIPS_SLEEPY_N = sizeof(QUIPS_SLEEPY) / sizeof(QUIPS_SLEEPY[0]);

const char* QUIPS_PET[] = {
  "贴贴~ <3",
  "摸头长不高啦!",
  "嘿嘿好开心~",
  "最喜欢你啦!",
  "再摸一下嘛~",
  "电量充满啦!",
};
const uint8_t QUIPS_PET_N = sizeof(QUIPS_PET) / sizeof(QUIPS_PET[0]);

const char* QUIPS_AI_THINK[] = {
  "AI 思考中...",
  "正在跑工具...",
  "翻代码ing~",
  "认真推演中...",
  "快有眉目啦!",
};
const uint8_t QUIPS_AI_THINK_N = sizeof(QUIPS_AI_THINK) / sizeof(QUIPS_AI_THINK[0]);

const char* QUIPS_MORNING[] = {
  "早安! 新的一天",
  "上午效率最高!",
  "来杯咖啡吗?",
};
const char* QUIPS_LUNCH[] = {
  "到饭点啦!",
  "先去干饭吧~",
  "人是铁饭是钢!",
};
const char* QUIPS_DINNER[] = {
  "晚饭时间到~",
  "吃饱再战呀!",
};
const char* QUIPS_NIGHT[] = {
  "夜深啦早点睡~",
  "熬夜伤身体哦",
  "陪你收个尾~",
};

// ── Mini-Theater (2-3 line short monologues) ─────────────────
struct TheaterScript {
  const char* line1;
  const char* line2;
  const char* line3;
};

const TheaterScript THEATER_SCRIPTS[] = {
  {"盯屏幕好久啦", "快去喝口水!", "回来继续冲~"},
  {"偷偷告诉你...", "其实我是螃蟹", "咔嚓咔嚓~"},
  {"刚刚走神了...", "在想好吃的!", "你有零食吗?"},
  {"脖子酸不酸?", "左右转一转~", "舒服多啦!"},
  {"不管写啥项目", "我都在桌边", "陪你一起肝!"},
};
const uint8_t THEATER_COUNT = sizeof(THEATER_SCRIPTS) / sizeof(THEATER_SCRIPTS[0]);

// ── Speech State ─────────────────────────────────────────────
uint8_t  quipSet          = 0;
uint32_t lastQuipMs       = 0;
uint32_t quipDrawnMs      = 0;
uint16_t quipDurationMs   = 0;
char     currentQuip[96]  = "";
char     dynamicBuf[64]   = "";
bool     quipActive       = false;

int8_t   activeScriptIdx  = -1;
uint8_t  activeScriptStep = 0;

const uint8_t  SPEAK_PROB[4]     = { 50, 30, 20, 12 };
const uint32_t SPEAK_COOLDOWN_MS = 5500;

void setupSpeech() {
  quipSet          = random(4);
  lastQuipMs       = 0;
  quipActive       = false;
  quipDrawnMs      = 0;
  quipDurationMs   = 0;
  activeScriptIdx  = -1;
  activeScriptStep = 0;
  currentQuip[0]   = '\0';
}

// Safely copy UTF-8 without slicing multi-byte characters in half
static void safeCopyUtf8(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  size_t i = 0;
  while (src[i] != '\0') {
    uint8_t c = (uint8_t)src[i];
    size_t chLen = 1;
    if ((c & 0x80) == 0)        chLen = 1;
    else if ((c & 0xE0) == 0xC0) chLen = 2;
    else if ((c & 0xF0) == 0xE0) chLen = 3;
    else if ((c & 0xF8) == 0xF0) chLen = 4;

    if (i + chLen >= dstSize) break;
    for (size_t k = 0; k < chLen; k++) {
      if (src[i + k] == '\0') { dst[i] = '\0'; return; }
    }
    i += chLen;
  }
  memcpy(dst, src, i);
  dst[i] = '\0';
}

// Clear only y=195..235 so y=236..239 Pomodoro bar stays intact
void clearQuipArea() {
  if (!quipActive) return;
  tft.fillRect(0, 195, 240, 41, animBgColor);
  quipActive     = false;
  quipDurationMs = 0;
  currentQuip[0] = '\0';
}

// Split a long UTF-8 string into line1 (<= maxPx in current u8g2 font) and line2
static void splitUtf8ForTwoLines(const char* text, int16_t maxPx, char* line1, size_t l1Size, char* line2, size_t l2Size) {
  size_t bestCut = 0;
  size_t pos = 0;
  char probe[96];
  while (text[pos] != '\0') {
    uint8_t c = (uint8_t)text[pos];
    size_t chLen = 1;
    if ((c & 0x80) == 0)         chLen = 1;
    else if ((c & 0xE0) == 0xC0) chLen = 2;
    else if ((c & 0xF0) == 0xE0) chLen = 3;
    else if ((c & 0xF8) == 0xF0) chLen = 4;

    size_t nextPos = pos + chLen;
    if (nextPos >= sizeof(probe)) break;
    memcpy(probe, text, nextPos);
    probe[nextPos] = '\0';
    if (u8g2.getUTF8Width(probe) > maxPx && bestCut > 0) {
      break;
    }
    bestCut = nextPos;
    pos     = nextPos;
  }
  safeCopyUtf8(line1, l1Size, text);
  if (bestCut < l1Size) line1[bestCut] = '\0';
  safeCopyUtf8(line2, l2Size, text + bestCut);
}

void drawQuipBubble(const char* text) {
  if (!text || text[0] == '\0') return;

  u8g2.setFontMode(1);  // transparent glyph background
  u8g2.setForegroundColor(C_WHITE);

  // Clean previous bubble zone (y=195..235)
  tft.fillRect(0, 195, 240, 41, animBgColor);

  // Tier 1: Try crisp 16px full-GB2312 font (fits up to ~13 Chinese chars on 1 line)
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  int16_t w16 = u8g2.getUTF8Width(text);
  if (w16 > 0 && w16 <= 218) {
    int16_t boxW = w16 + 18;
    if (boxW > 236) boxW = 236;
    int16_t boxX = (240 - boxW) / 2;
    int16_t boxY = 199;
    int16_t boxH = 26;
    tft.drawRoundRect(boxX, boxY, boxW, boxH, 5, C_BUBBLE_BORDER);
    u8g2.drawUTF8((240 - w16) / 2, boxY + 19, text);
    return;
  }

  // Tier 2: Try 13px full-GB2312 font on 1 line (fits up to ~17 Chinese chars)
  u8g2.setFont(u8g2_font_wqy13_t_gb2312);
  int16_t w13 = u8g2.getUTF8Width(text);
  if (w13 > 0 && w13 <= 222) {
    int16_t boxW = w13 + 16;
    if (boxW > 236) boxW = 236;
    int16_t boxX = (240 - boxW) / 2;
    int16_t boxY = 201;
    int16_t boxH = 23;
    tft.drawRoundRect(boxX, boxY, boxW, boxH, 5, C_BUBBLE_BORDER);
    u8g2.drawUTF8((240 - w13) / 2, boxY + 16, text);
    return;
  }

  // Tier 3: Auto-wrap into 2 centered lines using 12px full-GB2312 font (y=196..234)
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  char line1[64] = "";
  char line2[64] = "";
  splitUtf8ForTwoLines(text, 218, line1, sizeof(line1), line2, sizeof(line2));
  int16_t lw1  = u8g2.getUTF8Width(line1);
  int16_t lw2  = u8g2.getUTF8Width(line2);
  int16_t maxW = (lw1 > lw2) ? lw1 : lw2;
  int16_t boxW = maxW + 16;
  if (boxW > 236) boxW = 236;
  int16_t boxX = (240 - boxW) / 2;
  int16_t boxY = 196;
  int16_t boxH = (line2[0] != '\0') ? 38 : 22;
  tft.drawRoundRect(boxX, boxY, boxW, boxH, 5, C_BUBBLE_BORDER);
  if (line2[0] != '\0') {
    u8g2.drawUTF8((240 - lw1) / 2, boxY + 14, line1);
    u8g2.drawUTF8((240 - lw2) / 2, boxY + 30, line2);
  } else {
    u8g2.drawUTF8((240 - lw1) / 2, boxY + 15, line1);
  }
}

void showQuip(const char* text, uint16_t customDurationMs = 0) {
  if (!text || text[0] == '\0') return;
  safeCopyUtf8(currentQuip, sizeof(currentQuip), text);
  if (customDurationMs > 0) {
    quipDurationMs = customDurationMs;
  } else {
    quipDurationMs = 1900 + strlen(currentQuip) * 80;
    if (quipDurationMs > 4800) quipDurationMs = 4800;
  }
  lastQuipMs  = millis();
  quipDrawnMs = millis();
  quipActive  = true;
  drawQuipBubble(currentQuip);
}

void startMiniTheater(int8_t idx = -1) {
  if (idx < 0 || idx >= THEATER_COUNT) idx = random(THEATER_COUNT);
  activeScriptIdx  = idx;
  activeScriptStep = 0;
  showQuip(THEATER_SCRIPTS[activeScriptIdx].line1, 2400);
}

bool shouldExpireQuip() {
  if (!quipActive) return false;
  return (millis() - quipDrawnMs) >= quipDurationMs;
}

void tickSpeech() {
  if (!shouldExpireQuip()) return;
  clearQuipArea();

  if (activeScriptIdx >= 0) {
    activeScriptStep++;
    const TheaterScript& sc = THEATER_SCRIPTS[activeScriptIdx];
    const char* nextLine = nullptr;
    if (activeScriptStep == 1) nextLine = sc.line2;
    else if (activeScriptStep == 2) nextLine = sc.line3;

    if (nextLine != nullptr) {
      showQuip(nextLine, 2400);
    } else {
      activeScriptIdx  = -1;
      activeScriptStep = 0;
    }
  }
}

void redrawQuipIfActive() {
  if (quipActive && currentQuip[0] != '\0') {
    drawQuipBubble(currentQuip);
  }
}

const char* pickDynamicOrSituationalQuip() {
  if (aiActiveMode == AI_MODE_THINKING && random(100) < 75) {
    return QUIPS_AI_THINK[random(QUIPS_AI_THINK_N)];
  }

  if (random(100) < 12) {
    if (isTempCold()) {
      return "刚开机有点凉~";
    }
    if (isTempWarm()) {
      snprintf(dynamicBuf, sizeof(dynamicBuf), "室温 %.0fC 暖呼呼~", getEstRoomTempC());
      return dynamicBuf;
    }
  }

  if (hasWallClock() && currentMood != MOOD_SLEEPY && random(100) < 22) {
    uint8_t h = getWallHour();
    if (h >= 6 && h <= 9)   return QUIPS_MORNING[random(3)];
    if (h >= 11 && h <= 13) return QUIPS_LUNCH[random(3)];
    if (h >= 18 && h <= 19) return QUIPS_DINNER[random(2)];
    if (h >= 23 || h <= 4)  return QUIPS_NIGHT[random(3)];
  }

  if ((currentMood == MOOD_HAPPY || currentMood == MOOD_CALM) && random(100) < 14) {
    uint8_t pick = random(3);
    if (pick == 0) {
      uint32_t mins = getTotalMinutes();
      if (mins >= 60) {
        snprintf(dynamicBuf, sizeof(dynamicBuf), "已陪你 %lu 小时啦~", (unsigned long)(mins / 60));
      } else {
        snprintf(dynamicBuf, sizeof(dynamicBuf), "已陪你 %lu 分钟啦~", (unsigned long)(mins > 0 ? mins : 1));
      }
      return dynamicBuf;
    } else if (pick == 1) {
      snprintf(dynamicBuf, sizeof(dynamicBuf), "亲密度 Lv.%u <3", getAffectionLevel());
      return dynamicBuf;
    } else {
      snprintf(dynamicBuf, sizeof(dynamicBuf), "第 %lu 次见面啦~", (unsigned long)getBootCount());
      return dynamicBuf;
    }
  }

  uint8_t mood = (random(100) < 80) ? (uint8_t)currentMood : quipSet;
  switch (mood) {
    case 0: return QUIPS_HYPER[random(QUIPS_HYPER_N)];
    case 1: return QUIPS_HAPPY[random(QUIPS_HAPPY_N)];
    case 2: return QUIPS_CALM[random(QUIPS_CALM_N)];
    case 3: return QUIPS_SLEEPY[random(QUIPS_SLEEPY_N)];
    default: return QUIPS_HAPPY[random(QUIPS_HAPPY_N)];
  }
}

bool shouldSpeak() {
  if (quipActive || activeScriptIdx >= 0) return false;
  if (millis() - lastQuipMs < SPEAK_COOLDOWN_MS) return false;
  uint8_t prob = (aiActiveMode == AI_MODE_THINKING) ? 45 : SPEAK_PROB[(uint8_t)currentMood];
  return random(100) < prob;
}

const char* pickPetQuip() {
  return QUIPS_PET[random(QUIPS_PET_N)];
}

#endif // DYNAMICS_SPEECH_H
