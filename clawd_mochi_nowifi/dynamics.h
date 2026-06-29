// dynamics.h — Lively-pet behavior for Clawd Mochi (no-WiFi version)
// Companion to clawd_mochi_nowifi.ino. See spec:
// docs/superpowers/specs/2026-06-29-clawd-mochi-dynamics-design.md

#ifndef DYNAMICS_H
#define DYNAMICS_H

#include <Arduino.h>

// Mood enum (defined here, used by main sketch too)
enum Mood { MOOD_HYPER = 0, MOOD_HAPPY = 1, MOOD_CALM = 2, MOOD_SLEEPY = 3 };

// Public state (set by setupDynamics, read by main sketch for debug)
extern Mood      currentMood;
extern uint32_t  bootMs;

// ── Mood system ────────────────────────────────────────────────
Mood     currentMood     = MOOD_HYPER;
uint32_t bootMs          = 0;
uint32_t lastMoodChangeMs = 0;

// Mood transition thresholds (ms since boot)
const uint32_t MOOD_HYPER_END_MS   =  30000UL;  // 30 s
const uint32_t MOOD_HAPPY_END_MS   = 180000UL;  // 3 min
const uint32_t MOOD_CALM_END_MS    = 900000UL;  // 15 min

// Animation speed multiplier per mood (1=slow, 2=normal, 3=fast)
int moodSpeed(Mood m) {
  switch (m) {
    case MOOD_HYPER:  return 3;
    case MOOD_HAPPY:  return 2;
    case MOOD_CALM:   return 1;
    case MOOD_SLEEPY: return 1;
    default:          return 2;
  }
}

Mood computeMood() {
  uint32_t elapsed = millis() - bootMs;
  if (elapsed < MOOD_HYPER_END_MS) return MOOD_HYPER;
  if (elapsed < MOOD_HAPPY_END_MS) return MOOD_HAPPY;
  if (elapsed < MOOD_CALM_END_MS)  return MOOD_CALM;
  return MOOD_SLEEPY;
}

// Lifecycle
void setupDynamics();
void updateMood();

// Idle pool
void playRandomIdle();
void idleBlink();
void idleLookLeft();
void idleLookRight();
void idleLookUp();
void idleBreathe();
void idleShiver();

// Reactions
void reactAfter(uint8_t lastDemo);

// Easter eggs
bool shouldEasterEgg();
void playEasterEgg();

// ── Lifecycle ──────────────────────────────────────────────────
void setupDynamics() {
  bootMs = millis();
  lastMoodChangeMs = bootMs;
  currentMood = MOOD_HYPER;
  // animSpeed lives in the main .ino; declared extern there.
  // The main sketch must set animSpeed = 3 in setup() before calling us.
}

void updateMood() {
  Mood newMood = computeMood();
  if (newMood != currentMood) {
    currentMood = newMood;
    lastMoodChangeMs = millis();
    // Sync animation speed (animSpeed is a global in the main .ino)
    extern uint8_t animSpeed;  // from clawd_mochi_nowifi.ino
    animSpeed = moodSpeed(newMood);
    // Tiny "aha" reaction: fast blink + bounce via existing animNormalEyes
    extern void animNormalEyes();
    animNormalEyes();
  }
}

// ── Idle animations ───────────────────────────────────────────
extern void drawNormalEyes(int16_t ox, bool blink);
extern int  speedMs(int ms);  // from main sketch

void idleBlink() {
  drawNormalEyes(0, true);  delay(speedMs(80));
  drawNormalEyes(0, false);
}

void idleLookLeft() {
  drawNormalEyes(-14, false); delay(speedMs(500));
  drawNormalEyes(-14, true);  delay(speedMs(60));
  drawNormalEyes(0,   false);
}

void idleLookRight() {
  drawNormalEyes(14, false);  delay(speedMs(500));
  drawNormalEyes(14, true);   delay(speedMs(60));
  drawNormalEyes(0,   false);
}

void idleLookUp() {
  // Approximation: "looking up" via held gaze pattern
  // (a true up-gaze would need a new draw function; this is a
  //  close-enough substitute using blink-once-then-stare)
  drawNormalEyes(0, false); delay(speedMs(400));
  drawNormalEyes(0, true);  delay(speedMs(60));
  drawNormalEyes(0, false);
}

void idleBreathe() {
  // Subtle whole-face pulse: use normal eye at offset ±2
  for (uint8_t i = 0; i < 4; i++) {
    drawNormalEyes(-2, false); delay(speedMs(220));
    drawNormalEyes( 2, false); delay(speedMs(220));
  }
  drawNormalEyes(0, false);
}

void idleShiver() {
  // Fast jitter left-right
  for (uint8_t i = 0; i < 6; i++) {
    drawNormalEyes(-4, false); delay(speedMs(40));
    drawNormalEyes( 4, false); delay(speedMs(40));
  }
  drawNormalEyes(0, false);
}

// ── Idle weights per mood ──────────────────────────────────────
// Index: 0=blink 1=lookL 2=lookR 3=lookUp 4=breathe 5=shiver
const uint8_t IDLE_WEIGHTS_HYPER[]   = {20, 30, 30, 10,  5,  5};
const uint8_t IDLE_WEIGHTS_HAPPY[]   = {30, 20, 20,  5, 15, 10};
const uint8_t IDLE_WEIGHTS_CALM[]    = {35, 10, 10,  5, 30, 10};
const uint8_t IDLE_WEIGHTS_SLEEPY[]  = {40,  0,  0,  0, 20, 40};

const uint8_t* idleWeightsForMood(Mood m) {
  switch (m) {
    case MOOD_HYPER:  return IDLE_WEIGHTS_HYPER;
    case MOOD_HAPPY:  return IDLE_WEIGHTS_HAPPY;
    case MOOD_CALM:   return IDLE_WEIGHTS_CALM;
    case MOOD_SLEEPY: return IDLE_WEIGHTS_SLEEPY;
    default:          return IDLE_WEIGHTS_HAPPY;
  }
}

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

void playRandomIdle() {
  const uint8_t* w = idleWeightsForMood(currentMood);
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

// ── Reactions ──────────────────────────────────────────────────
// Reaction types: 0=sparkle 1=heart_pulse 2=extra_look 3=extra_z
struct ReactionRule {
  uint8_t triggerDemo;
  uint8_t probability;  // 0-100
  uint8_t reactionType;
};

// NOTE: DEMO_NORMAL..DEMO_SLEEPY constants are #defined in
// clawd_mochi_nowifi.ino (the main sketch). They are visible
// to this header because the .ino #includes "dynamics.h"
// AFTER those #defines.

const ReactionRule REACTIONS[] = {
  {DEMO_WINK,    50, 0},
  {DEMO_HEART,   40, 1},
  {DEMO_SPARKLE, 60, 2},
  {DEMO_SLEEPY,  80, 3},
};
const uint8_t REACTIONS_COUNT = sizeof(REACTIONS) / sizeof(REACTIONS[0]);

// Forward-declare reaction implementations (defined in Task 7)
void reactionSparkle();
void reactionHeartPulse();
void reactionExtraLook();
void reactionExtraZ();

void reactAfter(uint8_t lastDemo) {
  for (uint8_t i = 0; i < REACTIONS_COUNT; i++) {
    const ReactionRule& r = REACTIONS[i];
    if (r.triggerDemo == lastDemo) {
      if (random(100) < r.probability) {
        switch (r.reactionType) {
          case 0: reactionSparkle();    break;
          case 1: reactionHeartPulse(); break;
          case 2: reactionExtraLook();  break;
          case 3: reactionExtraZ();     break;
        }
      }
      return;  // only one reaction per main expression
    }
  }
}

#endif // DYNAMICS_H