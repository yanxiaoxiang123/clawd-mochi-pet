// dynamics-link.h — Non-blocking USB Serial companion link, AI state & 3px Pomodoro bar
// Protocol (115200 baud, newline-terminated UTF-8):
//   MOCHI:STATUS
//   MOCHI:PET
//   MOCHI:SAY:<text>
//   MOCHI:EXPR:<0..14>
//   MOCHI:TIME:<HH>:<MM>
//   MOCHI:PRESENCE:<ACTIVE|IDLE>:<idle_seconds>
//   MOCHI:AI:<THINK|TOOL|DONE|ASK|IDLE>[:optional_quip]
//   MOCHI:POMO:<minutes>  |  MOCHI:POMO:STOP
//   MOCHI:SYS:<CPU_HIGH|SITTING|GIT_COMMIT>[:optional_quip]
//   MOCHI:BLE:<LEARN|RESET>

#ifndef DYNAMICS_LINK_H
#define DYNAMICS_LINK_H

#include <Arduino.h>
#include "mochi_types.h"

extern Adafruit_ST7789 tft;
extern uint16_t animBgColor;
extern uint16_t C_GREEN;
extern uint16_t C_DARKBG;
extern uint16_t C_WHITE_BAR;

// Wall-clock synced from Mac (drifts gracefully via millis() between syncs)
bool     wallClockValid  = false;
uint8_t  wallBaseHour    = 0;
uint8_t  wallBaseMin     = 0;
uint32_t wallSyncMs      = 0;
int8_t   lastGreetedHour = -1;

// Pomodoro state (3px minimalist bar at y=237..239)
bool     pomoActive      = false;
uint32_t pomoStartMs     = 0;
uint32_t pomoDurationMs  = 0;
int16_t  lastPomoFillW   = -1;

// AI state auto-timeout (prevents getting stuck if host script exits)
const uint32_t AI_THINK_TIMEOUT_MS = 120000UL;

// Serial line buffer
char     serialBuf[128];
uint8_t  serialPos = 0;

bool hasWallClock() { return wallClockValid; }

uint8_t getWallHour() {
  if (!wallClockValid) return 12;
  uint32_t elapsedMin = (millis() - wallSyncMs) / 60000UL;
  uint32_t totalMin   = (uint32_t)wallBaseHour * 60UL + wallBaseMin + elapsedMin;
  return (uint8_t)((totalMin / 60UL) % 24UL);
}

uint8_t getWallMinute() {
  if (!wallClockValid) return 0;
  uint32_t elapsedMin = (millis() - wallSyncMs) / 60000UL;
  uint32_t totalMin   = (uint32_t)wallBaseHour * 60UL + wallBaseMin + elapsedMin;
  return (uint8_t)(totalMin % 60UL);
}

bool isLateNightHours() {
  if (!wallClockValid) return false;
  uint8_t h = getWallHour();
  return (h >= 23 || h < 7);
}

// ── 3px Minimalist Pomodoro Bar (y = 237..239) ───────────────
void clearPomodoroBar() {
  tft.fillRect(0, 236, 240, 4, animBgColor);
  lastPomoFillW = -1;
}

void startPomodoro(uint16_t minutes) {
  if (minutes == 0) minutes = 25;
  if (minutes > 180) minutes = 180;
  pomoDurationMs = (uint32_t)minutes * 60000UL;
  pomoStartMs    = millis();
  pomoActive     = true;
  lastPomoFillW  = -1;
  pendingEvent   = EVT_POMO_START;
}

void stopPomodoro() {
  pomoActive = false;
  clearPomodoroBar();
}

void redrawPomodoroBar(bool force = false) {
  if (!pomoActive) return;
  uint32_t elapsed = millis() - pomoStartMs;
  if (elapsed >= pomoDurationMs) {
    pomoActive = false;
    clearPomodoroBar();
    addAffection(3);
    pendingEvent = EVT_POMO_DONE;
    return;
  }
  int16_t remW = (int16_t)((uint64_t)240 * (pomoDurationMs - elapsed) / pomoDurationMs);
  if (remW < 2) remW = 2;
  if (force || remW != lastPomoFillW) {
    lastPomoFillW = remW;
    tft.fillRect(0, 237, remW, 3, C_GREEN);
    if (remW < 240) {
      tft.fillRect(remW, 237, 240 - remW, 3, C_DARKBG);
    }
  }
}

// ── Serial Command Handler ───────────────────────────────────
static void handleMochiCommand(char* line) {
  // Trim trailing CR/LF/spaces
  int len = strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
    line[--len] = '\0';
  }
  if (strncmp(line, "MOCHI:", 6) != 0) return;
  char* cmd = line + 6;

  if (strcmp(cmd, "STATUS") == 0) {
    const char* moodNames[] = {"HYPER", "HAPPY", "CALM", "SLEEPY"};
    Serial.printf(
      "OK:STATUS:mood=%s,boots=%lu,mins=%lu,love=%lu,lv=%u,temp=%.1f,rssi=%d,present=%d,pomo=%d,ai=%d,clock=%02u:%02u\n",
      moodNames[(uint8_t)currentMood],
      (unsigned long)getBootCount(),
      (unsigned long)getTotalMinutes(),
      (unsigned long)getAffectionPoints(),
      getAffectionLevel(),
      getChipTempC(),
      getLastBleRssi(),
      isOwnerPresent() ? 1 : 0,
      pomoActive ? (int)((pomoDurationMs - (millis() - pomoStartMs)) / 1000UL) : 0,
      (int)aiActiveMode,
      getWallHour(),
      getWallMinute()
    );
    return;
  }

  if (strcmp(cmd, "RESET_STATS") == 0) {
    resetCompanionStats();
    Serial.println("OK:RESET_STATS");
    return;
  }

  if (strcmp(cmd, "PET") == 0) {
    pendingEvent = EVT_PET;
    Serial.println("OK:PET");
    return;
  }

  if (strncmp(cmd, "SAY:", 4) == 0) {
    showQuip(cmd + 4, 3800);
    Serial.println("OK:SAY");
    return;
  }

  if (strncmp(cmd, "THEATER", 7) == 0) {
    startMiniTheater(-1);
    Serial.println("OK:THEATER");
    return;
  }

  if (strncmp(cmd, "EXPR:", 5) == 0) {
    int ex = atoi(cmd + 5);
    if (ex >= 0 && ex < DEMO_COUNT) {
      pendingCustomExpr = (uint8_t)ex;
      pendingEvent      = EVT_CUSTOM_EXPR;
      Serial.printf("OK:EXPR:%d\n", ex);
    }
    return;
  }

  if (strncmp(cmd, "TIME:", 5) == 0) {
    int hh = 0, mm = 0;
    if (sscanf(cmd + 5, "%d:%d", &hh, &mm) == 2 && hh >= 0 && hh < 24 && mm >= 0 && mm < 60) {
      bool wasValid  = wallClockValid;
      wallBaseHour   = (uint8_t)hh;
      wallBaseMin    = (uint8_t)mm;
      wallSyncMs     = millis();
      wallClockValid = true;
      // Gentle meal/night reminders on exact transition
      if (wasValid && mm == 0 && lastGreetedHour != hh) {
        lastGreetedHour = hh;
        if (hh == 9)       showQuip("早安! 今天冲鸭~", 3500);
        else if (hh == 12) showQuip("12点啦! 去吃午饭~", 3800);
        else if (hh == 18) showQuip("下班干饭时间到~", 3800);
        else if (hh == 23) showQuip("23点啦, 护眼调暗~", 3800);
      }
      Serial.printf("OK:TIME:%02d:%02d\n", hh, mm);
    }
    return;
  }

  if (strncmp(cmd, "PRESENCE:", 9) == 0) {
    char* p = cmd + 9;
    if (strncmp(p, "ACTIVE:", 7) == 0) {
      uint32_t idleSec = (uint32_t)strtoul(p + 7, nullptr, 10);
      notifyMacPresence(true, idleSec);
      Serial.println("OK:PRESENCE:ACTIVE");
    } else if (strncmp(p, "IDLE:", 5) == 0) {
      uint32_t idleSec = (uint32_t)strtoul(p + 5, nullptr, 10);
      notifyMacPresence(false, idleSec);
      Serial.println("OK:PRESENCE:IDLE");
    }
    return;
  }

  if (strncmp(cmd, "AI:", 3) == 0) {
    char* sub = cmd + 3;
    char* customText = strchr(sub, ':');
    if (customText) {
      *customText = '\0';
      customText++;
    }
    if (strcmp(sub, "THINK") == 0 || strcmp(sub, "TOOL") == 0) {
      aiActiveMode  = AI_MODE_THINKING;
      aiModeSinceMs = millis();
      pendingEvent  = EVT_AI_THINK;
      showQuip(customText && *customText ? customText :
               (strcmp(sub, "TOOL") == 0 ? "正在跑工具..." : "AI 思考中..."), 3500);
    } else if (strcmp(sub, "DONE") == 0) {
      aiActiveMode = AI_MODE_NONE;
      addAffection(1);
      pendingEvent = EVT_AI_DONE;
      showQuip(customText && *customText ? customText : "搞定啦! 快验收~", 3800);
    } else if (strcmp(sub, "ASK") == 0) {
      aiActiveMode = AI_MODE_NONE;
      pendingEvent = EVT_AI_ASK;
      showQuip(customText && *customText ? customText : "等你拍板决定!", 4000);
    } else if (strcmp(sub, "IDLE") == 0) {
      aiActiveMode = AI_MODE_NONE;
    }
    Serial.printf("OK:AI:%s\n", sub);
    return;
  }

  if (strncmp(cmd, "POMO:", 5) == 0) {
    char* arg = cmd + 5;
    if (strcmp(arg, "STOP") == 0) {
      stopPomodoro();
      showQuip("番茄钟已结束~", 2600);
      Serial.println("OK:POMO:STOP");
    } else {
      int mins = atoi(arg);
      if (mins > 0) {
        startPomodoro((uint16_t)mins);
        char buf[40];
        snprintf(buf, sizeof(buf), "专注 %d 分钟开始!", mins);
        showQuip(buf, 3200);
        Serial.printf("OK:POMO:%d\n", mins);
      }
    }
    return;
  }

  if (strncmp(cmd, "SYS:", 4) == 0) {
    char* sub = cmd + 4;
    char* customText = strchr(sub, ':');
    if (customText) {
      *customText = '\0';
      customText++;
    }
    if (strcmp(sub, "CPU_HIGH") == 0) {
      pendingEvent = EVT_CPU_HIGH;
      showQuip(customText && *customText ? customText : "电脑风扇要起飞啦!", 3800);
    } else if (strcmp(sub, "SITTING") == 0) {
      pendingEvent = EVT_SITTING_LONG;
      showQuip(customText && *customText ? customText : "久坐啦! 快喝口水!", 4000);
    } else if (strcmp(sub, "GIT_COMMIT") == 0) {
      addAffection(1);
      pendingEvent = EVT_GIT_COMMIT;
      showQuip(customText && *customText ? customText : "代码提交成功! 赞~", 3500);
    }
    Serial.printf("OK:SYS:%s\n", sub);
    return;
  }

  if (strncmp(cmd, "BLE:", 4) == 0) {
    if (strcmp(cmd + 4, "LEARN") == 0) {
      showQuip("请把手表/手机贴近~", 3000);
      triggerBleLearn();
      Serial.println("OK:BLE:LEARNING");
    } else if (strcmp(cmd + 4, "RESET") == 0) {
      resetBleBinding();
      showQuip("蓝牙感知已重置~", 2500);
      Serial.println("OK:BLE:RESET");
    }
    return;
  }
}

void pollSerialLink() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      serialBuf[serialPos] = '\0';
      if (serialPos > 0) {
        handleMochiCommand(serialBuf);
      }
      serialPos = 0;
    } else if (c != '\r') {
      if (serialPos < sizeof(serialBuf) - 1) {
        serialBuf[serialPos++] = c;
      }
    }
  }

  // Auto-expire AI thinking mode if host stopped updating
  if (aiActiveMode == AI_MODE_THINKING && (millis() - aiModeSinceMs > AI_THINK_TIMEOUT_MS)) {
    aiActiveMode = AI_MODE_NONE;
  }

  // Keep Pomodoro bar updated
  redrawPomodoroBar(false);
}

#endif // DYNAMICS_LINK_H
