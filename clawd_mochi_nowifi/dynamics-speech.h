// dynamics-speech.h — Mood-aware quips, dynamic stat bubbles & Mini-Theater
// Pure clean face layout: transparent rounded pill at y=200..224, leaving
// y=0..194 for eyes and y=236..239 for the optional 3px Pomodoro bar.

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

// Sensor & memory getters from dynamics-sensors.h / dynamics-link.h
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

// ── Mood Quip Pools (GB2312 level-1 safe + ASCII) ────────────

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

// Situational pools
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
  const char* line3;  // nullptr if 2-line script
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
char     currentQuip[48]  = "";
char     dynamicBuf[48]   = "";
bool     quipActive       = false;

// Mini-theater queue
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

// Clear only y=195..235 so y=236..239 Pomodoro bar stays intact
void clearQuipArea() {
  if (!quipActive) return;
  tft.fillRect(0, 195, 240, 41, animBgColor);
  quipActive     = false;
  quipDurationMs = 0;
  currentQuip[0] = '\0';
}

void drawQuipBubble(const char* text) {
  if (!text || text[0] == '\0') return;

  u8g2.setFont(u8g2_font_unifont_t_chinese1);
  uint16_t textW = u8g2.getUTF8Width(text);
  if (textW == 0) return;
  if (textW > 220) textW = 220;

  const int16_t padX = 10;
  const int16_t padY = 4;
  const int16_t boxH = 24;
  int16_t boxW       = textW + padX * 2;
  if (boxW > 236) boxW = 236;
  const int16_t boxX = (240 - boxW) / 2;
  const int16_t boxY = 200;

  // Clean area behind previous/new pill before drawing transparent border
  tft.fillRect(0, 195, 240, 41, animBgColor);
  tft.drawRoundRect(boxX, boxY, boxW, boxH, 5, C_BUBBLE_BORDER);

  u8g2.setFontMode(1);
  u8g2.setForegroundColor(C_WHITE);

  int16_t tx = boxX + padX;
  int16_t ty = boxY + padY + 16;
  const uint8_t* p = (const uint8_t*)text;
  while (*p) {
    uint16_t cp = 0;
    uint8_t c = *p++;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0 && *p) {
      cp = ((c & 0x1F) << 6) | (*p++ & 0x3F);
    } else if ((c & 0xF0) == 0xE0 && p[0] && p[1]) {
      cp = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
      p += 2;
    } else if ((c & 0xF8) == 0xF0 && p[0] && p[1] && p[2]) {
      // Skip 4-byte emoji outside BMP font
      p += 3;
      continue;
    }
    if (cp != 0 && tx < boxX + boxW - 6) {
      tx += u8g2.drawGlyph(tx, ty, cp);
    }
  }
}

void showQuip(const char* text, uint16_t customDurationMs = 0) {
  if (!text || text[0] == '\0') return;
  strncpy(currentQuip, text, sizeof(currentQuip) - 1);
  currentQuip[sizeof(currentQuip) - 1] = '\0';
  if (customDurationMs > 0) {
    quipDurationMs = customDurationMs;
  } else {
    quipDurationMs = 1800 + strlen(currentQuip) * 85;
    if (quipDurationMs > 4500) quipDurationMs = 4500;
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

// Called regularly inside smartDelay() so bubbles expire & mini-theater advances smoothly
void tickSpeech() {
  if (!shouldExpireQuip()) return;
  clearQuipArea();

  // Advance mini-theater if a script is playing
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
  // 1. If AI is actively thinking, prefer AI companion quips
  if (aiActiveMode == AI_MODE_THINKING && random(100) < 75) {
    return QUIPS_AI_THINK[random(QUIPS_AI_THINK_N)];
  }

  // 2. Temperature remark (~12% chance when noticeable)
  if (random(100) < 12) {
    if (isTempCold()) {
      return "刚开机有点凉~";
    }
    if (isTempWarm()) {
      snprintf(dynamicBuf, sizeof(dynamicBuf), "室温 %.0fC 暖呼呼~", getEstRoomTempC());
      return dynamicBuf;
    }
  }

  // 3. Wall-clock time-of-day quips (~22% chance if Mac synced time)
  if (hasWallClock() && currentMood != MOOD_SLEEPY && random(100) < 22) {
    uint8_t h = getWallHour();
    if (h >= 6 && h <= 9)   return QUIPS_MORNING[random(3)];
    if (h >= 11 && h <= 13) return QUIPS_LUNCH[random(3)];
    if (h >= 18 && h <= 19) return QUIPS_DINNER[random(2)];
    if (h >= 23 || h <= 4)  return QUIPS_NIGHT[random(3)];
  }

  // 4. Growth / memory stats woven naturally into speech (~14% chance in HAPPY/CALM)
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

  // 5. Standard mood pool (80% current mood, 20% boot personality voice)
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
