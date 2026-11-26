// dynamics.h — Natural Circadian/Nap Rhythm, Weighted 15-Expression Selector,
//              Idle Micro-Animations, Reaction Chains & Easter Eggs.

#ifndef DYNAMICS_H
#define DYNAMICS_H

#include <Arduino.h>
#include "mochi_types.h"

Mood     currentMood      = MOOD_HYPER;
uint32_t bootMs           = 0;
uint32_t cycleStartMs     = 0;  // resets each wake-up / pet / desk-return!
uint32_t lastMoodChangeMs = 0;
bool     wasDeepSleeping  = false;

// Natural cyclic rhythm (never permanently stuck in SLEEPY!):
//   0 .. 45s       -> HYPER  (45s burst of energy)
//   45s .. 4m45s   -> HAPPY  (4 min cheerful company)
//   4m45s .. 15m   -> CALM   (10+ min gentle company)
//   15m .. 18m     -> SLEEPY (3 min cozy power nap with dimmed backlight, then wakes up!)
const uint32_t CYCLE_HYPER_END_MS  =   45000UL;  // 45 s
const uint32_t CYCLE_HAPPY_END_MS  =  285000UL;  // 4 min 45 s
const uint32_t CYCLE_CALM_END_MS   =  900000UL;  // 15 min
const uint32_t CYCLE_NAP_END_MS    = 1080000UL;  // 18 min -> auto stretch & wake!

extern uint8_t animSpeed;
extern void drawNormalEyes(int16_t ox, bool blink);
extern void drawHeartEyes(float scale);
extern void drawSleepyEyes(int16_t eyeH, int8_t zFrame);
extern void drawSurpriseEyes();
extern void animNormalEyes();
extern void animSquishEyes();
extern void animHeartEyes();
extern void animSparkleEyes();
extern void animWink();
extern void animSleepy();
extern void animHappyArcEyes();
extern void animYawnStretch();
extern int  speedMs(int ms);

int moodSpeed(Mood m) {
  switch (m) {
    case MOOD_HYPER:  return 3;
    case MOOD_HAPPY:  return 2;
    case MOOD_CALM:   return 1;
    case MOOD_SLEEPY: return 1;
    default:          return 2;
  }
}

uint8_t desiredBrightnessForState() {
  if (!isOwnerPresent()) {
    return 45;   // Owner away from desk -> low-power cozy rest glow
  }
  if (currentMood == MOOD_SLEEPY) {
    return isLateNightHours() ? 45 : 85;  // Soft dimmed nap
  }
  if (isLateNightHours()) {
    return 115;  // Gentle eye-care night mode when coding past 23:00
  }
  return 255;    // Full vibrant daytime brightness
}

void energizeCycle(bool toHyper = true) {
  cycleStartMs = toHyper ? millis() : (millis() - CYCLE_HYPER_END_MS);
  currentMood  = toHyper ? MOOD_HYPER : MOOD_HAPPY;
  animSpeed    = moodSpeed(currentMood);
  setTargetBrightness(desiredBrightnessForState());
}

void setCalmMood() {
  cycleStartMs = millis() - CYCLE_HAPPY_END_MS;
  currentMood  = MOOD_CALM;
  animSpeed    = moodSpeed(currentMood);
  setTargetBrightness(desiredBrightnessForState());
}

void setupDynamics() {
  bootMs           = millis();
  cycleStartMs     = bootMs;
  lastMoodChangeMs = bootMs;
  currentMood      = MOOD_HYPER;
  animSpeed        = 3;
  setTargetBrightness(255);
}

void updateMood() {
  uint32_t now = millis();

  // If owner has left the desk (>8 min), drift straight into restful sleep
  if (!isOwnerPresent()) {
    if (currentMood != MOOD_SLEEPY) {
      currentMood      = MOOD_SLEEPY;
      lastMoodChangeMs = now;
      animSpeed        = 1;
    }
    wasDeepSleeping = true;
    setTargetBrightness(desiredBrightnessForState());
    return;
  }

  // Keep awake in HAPPY/CALM during active Pomodoro or AI thinking
  if (pomoActive || aiActiveMode == AI_MODE_THINKING) {
    if (currentMood == MOOD_SLEEPY) {
      energizeCycle(false);
    }
    setTargetBrightness(desiredBrightnessForState());
    return;
  }

  uint32_t elapsed = now - cycleStartMs;

  // Finished 3-minute power nap -> stretch, yawn, restore brightness, and wake up fresh!
  if (elapsed >= CYCLE_NAP_END_MS) {
    // Late night (23:00-07:00) takes longer 8-min naps before checking in quietly
    uint32_t napLimit = isLateNightHours() ? (CYCLE_CALM_END_MS + 480000UL) : CYCLE_NAP_END_MS;
    if (elapsed >= napLimit) {
      cycleStartMs     = now - CYCLE_HYPER_END_MS;  // Wake into HAPPY (not jarring HYPER)
      currentMood      = isLateNightHours() ? MOOD_CALM : MOOD_HAPPY;
      lastMoodChangeMs = now;
      animSpeed        = moodSpeed(currentMood);
      setTargetBrightness(desiredBrightnessForState());
      animYawnStretch();
      showQuip(isLateNightHours() ? "醒来陪你啦~" : "睡饱啦! 满血复活~", 3000);
      return;
    }
  }

  Mood newMood;
  if (elapsed < CYCLE_HYPER_END_MS)      newMood = MOOD_HYPER;
  else if (elapsed < CYCLE_HAPPY_END_MS) newMood = MOOD_HAPPY;
  else if (elapsed < CYCLE_CALM_END_MS)  newMood = MOOD_CALM;
  else                                   newMood = MOOD_SLEEPY;

  if (newMood != currentMood) {
    Mood prev        = currentMood;
    currentMood      = newMood;
    lastMoodChangeMs = now;
    animSpeed        = moodSpeed(newMood);
    setTargetBrightness(desiredBrightnessForState());
    if (newMood == MOOD_SLEEPY && prev != MOOD_SLEEPY) {
      animYawnStretch();
      showQuip("有点困..眯三分钟~", 2800);
    }
  } else {
    setTargetBrightness(desiredBrightnessForState());
  }
}

// ── Weighted pick helper ─────────────────────────────────────
int weightedPick(const uint8_t* weights, int n) {
  uint16_t total = 0;
  for (int i = 0; i < n; i++) total += weights[i];
  if (total == 0) return 0;
  uint16_t r = random(total);
  for (int i = 0; i < n; i++) {
    if (r < weights[i]) return i;
    r -= weights[i];
  }
  return n - 1;
}

// ── Mood-Aware 15-Expression Selector ────────────────────────
// Indices 0..14:
//  0:Normal 1:Squish 2:Heart 3:Sparkle 4:Wink 5:Sleepy 6:Surprise
//  7:HappyArc 8:Thinking 9:Angry 10:Dizzy 11:Cry 12:Curious 13:Yawn 14:Focus
const uint8_t EXPR_WEIGHTS_HYPER[15]  = {16, 16, 12, 16, 12,  0, 4, 14,  2, 0, 0, 0,  8, 0, 0};
const uint8_t EXPR_WEIGHTS_HAPPY[15]  = {22, 14, 10, 10, 12,  0, 0, 16,  5, 0, 0, 0,  8, 0, 3};
const uint8_t EXPR_WEIGHTS_CALM[15]   = {28,  8,  5,  4,  8,  6, 0, 14, 10, 0, 0, 0, 10, 3, 4};
const uint8_t EXPR_WEIGHTS_SLEEPY[15] = { 0,  0,  0,  0,  0, 88, 0,  0,  0, 0, 0, 0,  0,12, 0};

uint8_t pickNextExpression(uint8_t lastExpr) {
  if (aiActiveMode == AI_MODE_THINKING) {
    const uint8_t aiPool[] = {DEMO_THINKING, DEMO_FOCUS, DEMO_CURIOUS, DEMO_THINKING};
    return aiPool[random(4)];
  }
  if (pomoActive && currentMood != MOOD_SLEEPY && random(100) < 40) {
    return (random(2) == 0) ? DEMO_FOCUS : DEMO_THINKING;
  }

  const uint8_t* w = EXPR_WEIGHTS_HAPPY;
  switch (currentMood) {
    case MOOD_HYPER:  w = EXPR_WEIGHTS_HYPER;  break;
    case MOOD_HAPPY:  w = EXPR_WEIGHTS_HAPPY;  break;
    case MOOD_CALM:   w = EXPR_WEIGHTS_CALM;   break;
    case MOOD_SLEEPY: w = EXPR_WEIGHTS_SLEEPY; break;
  }

  // Avoid repeating the exact same non-sleepy expression twice in a row
  for (uint8_t tries = 0; tries < 4; tries++) {
    uint8_t pick = (uint8_t)weightedPick(w, DEMO_COUNT);
    if (pick != lastExpr || currentMood == MOOD_SLEEPY) return pick;
  }
  return DEMO_NORMAL;
}

// ── Idle micro-animations ────────────────────────────────────
void idleBlink() {
  drawNormalEyes(0, true);  if (frameDelay(speedMs(80))) return;
  drawNormalEyes(0, false);
}

void idleLookLeft() {
  drawNormalEyes(-14, false); if (frameDelay(speedMs(450))) return;
  drawNormalEyes(-14, true);  if (frameDelay(speedMs(60)))  return;
  drawNormalEyes(0,   false);
}

void idleLookRight() {
  drawNormalEyes(14, false);  if (frameDelay(speedMs(450))) return;
  drawNormalEyes(14, true);   if (frameDelay(speedMs(60)))  return;
  drawNormalEyes(0,  false);
}

void idleLookUp() {
  extern void drawNormalEyesOffset(int16_t ox, int16_t oy, bool blink);
  drawNormalEyesOffset(0, -10, false); if (frameDelay(speedMs(420))) return;
  drawNormalEyesOffset(0, -10, true);  if (frameDelay(speedMs(60)))  return;
  drawNormalEyesOffset(0,   0, false);
}

void idleBreathe() {
  for (uint8_t i = 0; i < 3; i++) {
    drawNormalEyes(-2, false); if (frameDelay(speedMs(210))) return;
    drawNormalEyes( 2, false); if (frameDelay(speedMs(210))) return;
  }
  drawNormalEyes(0, false);
}

void idleShiver() {
  for (uint8_t i = 0; i < 5; i++) {
    drawNormalEyes(-4, false); if (frameDelay(speedMs(40))) return;
    drawNormalEyes( 4, false); if (frameDelay(speedMs(40))) return;
  }
  drawNormalEyes(0, false);
}

const uint8_t IDLE_WEIGHTS_HYPER[]  = {20, 28, 28, 14,  5,  5};
const uint8_t IDLE_WEIGHTS_HAPPY[]  = {28, 20, 20, 12, 14,  6};
const uint8_t IDLE_WEIGHTS_CALM[]   = {34, 10, 10,  8, 32,  6};
const uint8_t IDLE_WEIGHTS_SLEEPY[] = { 0,  0,  0,  0,  0,  0};

void playRandomIdle() {
  if (currentMood == MOOD_SLEEPY) return;
  const uint8_t* w = IDLE_WEIGHTS_HAPPY;
  if (currentMood == MOOD_HYPER) w = IDLE_WEIGHTS_HYPER;
  else if (currentMood == MOOD_CALM) w = IDLE_WEIGHTS_CALM;

  int idx = weightedPick(w, 6);
  switch (idx) {
    case 0: idleBlink();     break;
    case 1: idleLookLeft();  break;
    case 2: idleLookRight(); break;
    case 3: idleLookUp();    break;
    case 4: idleBreathe();   break;
    case 5: idleShiver();    break;
  }
}

// ── Reaction Chains ──────────────────────────────────────────
struct ReactionRule {
  uint8_t triggerDemo;
  uint8_t probability;
  uint8_t reactionType;
};

const ReactionRule REACTIONS[] = {
  {DEMO_WINK,      50, 0},  // -> Sparkle
  {DEMO_HEART,     45, 1},  // -> Heart pulse
  {DEMO_SPARKLE,   55, 2},  // -> Extra glance
  {DEMO_SLEEPY,    75, 3},  // -> Extra floating Z
  {DEMO_CURIOUS,   50, 4},  // -> Aha happy arc!
};
const uint8_t REACTIONS_COUNT = sizeof(REACTIONS) / sizeof(REACTIONS[0]);

void reactAfter(uint8_t lastDemo) {
  if (pendingEvent != EVT_NONE) return;
  for (uint8_t i = 0; i < REACTIONS_COUNT; i++) {
    const ReactionRule& r = REACTIONS[i];
    if (r.triggerDemo == lastDemo) {
      if (random(100) < r.probability) {
        switch (r.reactionType) {
          case 0: animSparkleEyes(); break;
          case 1:
            for (uint8_t k = 0; k < 3; k++) {
              drawHeartEyes(1.2f); if (frameDelay(speedMs(85))) return;
              drawHeartEyes(1.0f); if (frameDelay(speedMs(85))) return;
            }
            break;
          case 2:
            drawNormalEyes(random(2) ? 12 : -12, false);
            if (frameDelay(speedMs(260))) return;
            drawNormalEyes(0, false);
            break;
          case 3:
            drawSleepyEyes(6, 0); if (frameDelay(speedMs(450))) return;
            drawSleepyEyes(6, 1); if (frameDelay(speedMs(450))) return;
            drawSleepyEyes(6, 2);
            break;
          case 4:
            animHappyArcEyes();
            break;
        }
      }
      return;
    }
  }
}

// ── Easter Eggs ──────────────────────────────────────────────
struct EasterEggRule {
  uint8_t eggId;
  uint8_t probability[4];
};

const EasterEggRule EGGS[] = {
  {0, { 7, 14, 35, 0}},  // ZOOM
  {1, {12, 26, 60, 0}},  // DANCE
  {2, {14, 30, 70, 0}},  // FLASH
  {3, {16, 32, 80, 0}},  // SECRET
};
const uint8_t EGGS_COUNT = sizeof(EGGS) / sizeof(EGGS[0]);

bool shouldEasterEgg() {
  if (currentMood == MOOD_SLEEPY || aiActiveMode == AI_MODE_THINKING) return false;
  uint8_t bestDenom = 255;
  for (uint8_t i = 0; i < EGGS_COUNT; i++) {
    uint8_t d = EGGS[i].probability[currentMood];
    if (d > 0 && d < bestDenom) bestDenom = d;
  }
  if (bestDenom == 255) return false;
  return random(bestDenom) == 0;
}

void eggZoom() {
  for (int s = 10; s <= 15; s++) {
    drawHeartEyes(s / 10.0f); if (frameDelay(speedMs(40))) return;
  }
  drawHeartEyes(1.5f); if (frameDelay(speedMs(120))) return;
  drawHeartEyes(0.5f); if (frameDelay(speedMs(120))) return;
  drawHeartEyes(1.0f); if (frameDelay(speedMs(120))) return;
}

void eggDance() {
  drawNormalEyes(-14, false); if (frameDelay(140)) return;
  drawNormalEyes( 14, false); if (frameDelay(140)) return;
  extern void drawSquishEyes(bool closed);
  drawSquishEyes(false);      if (frameDelay(160)) return;
  drawHeartEyes(1.1f);        if (frameDelay(160)) return;
  extern void drawSparkleEyes(float scale, bool rotated);
  drawSparkleEyes(1.0f, false); if (frameDelay(160)) return;
  extern void drawHappyArcEyes(int16_t bounceY);
  drawHappyArcEyes(-6);       if (frameDelay(220)) return;
}

void eggFlash() {
  extern uint16_t animBgColor;
  extern uint16_t C_ORANGE;
  uint16_t saved = animBgColor;
  for (uint8_t i = 0; i < 2; i++) {
    animBgColor = 0xFFFF;   drawNormalEyes(0, false); if (frameDelay(95)) { animBgColor = saved; return; }
    animBgColor = 0x0000;   drawNormalEyes(0, false); if (frameDelay(95)) { animBgColor = saved; return; }
    animBgColor = C_ORANGE; drawNormalEyes(0, false); if (frameDelay(95)) { animBgColor = saved; return; }
  }
  animBgColor = saved;
  tft.fillScreen(animBgColor);
  drawNormalEyes(0, false);
  redrawQuipIfActive();
  redrawPomodoroBar(true);
}

void eggSecret() {
  drawSurpriseEyes();
  if (frameDelay(speedMs(750))) return;
  drawNormalEyes(0, false);
}

void playEasterEgg() {
  uint16_t total = 0;
  for (uint8_t i = 0; i < EGGS_COUNT; i++) {
    uint8_t d = EGGS[i].probability[currentMood];
    if (d > 0) total += 255 / d;
  }
  if (total == 0) return;
  uint16_t r = random(total);
  for (uint8_t i = 0; i < EGGS_COUNT; i++) {
    uint8_t d = EGGS[i].probability[currentMood];
    if (d == 0) continue;
    uint16_t w = 255 / d;
    if (r < w) {
      switch (EGGS[i].eggId) {
        case 0: eggZoom();   return;
        case 1: eggDance();  return;
        case 2: eggFlash();  return;
        case 3: eggSecret(); return;
      }
    }
    r -= w;
  }
}

#endif // DYNAMICS_H
