// mochi_types.h — Shared enums & declarations for Clawd Mochi Smart Pet
// Included first in clawd_mochi_nowifi.ino to avoid Arduino auto-prototype ordering issues.

#ifndef MOCHI_TYPES_H
#define MOCHI_TYPES_H

#include <Arduino.h>

enum Mood : uint8_t {
  MOOD_HYPER  = 0,
  MOOD_HAPPY  = 1,
  MOOD_CALM   = 2,
  MOOD_SLEEPY = 3
};

enum ExprId : uint8_t {
  DEMO_NORMAL    = 0,
  DEMO_SQUISH    = 1,
  DEMO_HEART     = 2,
  DEMO_SPARKLE   = 3,
  DEMO_WINK      = 4,
  DEMO_SLEEPY    = 5,
  DEMO_SURPRISE  = 6,
  DEMO_HAPPY_ARC = 7,
  DEMO_THINKING  = 8,
  DEMO_ANGRY     = 9,
  DEMO_DIZZY     = 10,
  DEMO_CRY       = 11,
  DEMO_CURIOUS   = 12,
  DEMO_YAWN      = 13,
  DEMO_FOCUS     = 14,
  DEMO_COUNT     = 15
};

enum MochiEvent : uint8_t {
  EVT_NONE         = 0,
  EVT_PET          = 1,   // BLE near-field tap-to-pet or USB MOCHI:PET
  EVT_WELCOME_BACK = 2,   // Owner returned to desk
  EVT_LONG_AWAY    = 3,   // Owner returned after >25 min away
  EVT_AI_THINK     = 4,   // AI coding agent thinking / running tools
  EVT_AI_DONE      = 5,   // AI coding agent finished task
  EVT_AI_ASK       = 6,   // AI coding agent waiting for owner approval
  EVT_POMO_START   = 7,   // Pomodoro focus timer started
  EVT_POMO_DONE    = 8,   // Pomodoro focus timer completed
  EVT_CPU_HIGH     = 9,   // Mac CPU load high
  EVT_SITTING_LONG = 10,  // Owner sitting continuously too long
  EVT_GIT_COMMIT   = 11,  // Git commit celebration
  EVT_CUSTOM_EXPR  = 12,  // Explicit expression trigger via USB
  EVT_CUSTOM_EGG   = 13,  // Explicit easter egg trigger (0..3)
  EVT_CUSTOM_IDLE  = 14,  // Explicit idle micro-animation trigger (0..5)
  EVT_NAP_DEMO     = 15   // Demo nap -> stretch wake sequence
};

enum AiMode : uint8_t {
  AI_MODE_NONE     = 0,
  AI_MODE_THINKING = 1
};

// Shared state defined across modules
extern Mood       currentMood;
extern uint8_t    currentExprId;
extern uint8_t    currentBrightness;
extern MochiEvent pendingEvent;
extern uint8_t    pendingCustomExpr;
extern uint8_t    pendingSubId;
extern AiMode     aiActiveMode;
extern uint32_t   aiModeSinceMs;

// Non-blocking frame/hold helpers implemented in main .ino
bool frameDelay(int ms);
bool holdDelay(uint32_t ms);
void setTargetBrightness(uint8_t level);

#endif // MOCHI_TYPES_H
