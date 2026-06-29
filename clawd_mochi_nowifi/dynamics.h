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

#endif // DYNAMICS_H