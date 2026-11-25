// dynamics-sensors.h — Zero-extra-hardware sensing & NVS persistence:
//   1. NVS Flash memory (boot count, total companion minutes, affection points, BLE calibration)
//   2. ESP32-C3 internal silicon temperature sensor (calibrated for enclosed 3D-printed PLA case)
//   3. Per-device BLE RSSI Delta Radar: isolates stationary nearby Mac (-40 dBm constant)
//      from moving Apple Watch / iPhone brought close to shell (+10 dB spike or >= -32 dBm touch!)

#ifndef DYNAMICS_SENSORS_H
#define DYNAMICS_SENSORS_H

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <Preferences.h>
#if ESP_ARDUINO_VERSION_MAJOR >= 4
  #include <BLE.h>
#else
  #include <BLEDevice.h>
  #include <BLEUtils.h>
  #include <BLEScan.h>
  #include <BLEAdvertisedDevice.h>
#endif
#include <math.h>
#include "mochi_types.h"

Preferences mochiPrefs;

// ── NVS Persistent Companion Stats ───────────────────────────
uint32_t statBootCount    = 1;
uint32_t statTotalMinutes = 0;
uint32_t statAffection    = 0;
uint32_t lastMinuteSaveMs = 0;
uint32_t lastMinuteTickMs = 0;

uint32_t getBootCount()       { return statBootCount; }
uint32_t getTotalMinutes()    { return statTotalMinutes; }
uint32_t getAffectionPoints() { return statAffection; }
uint16_t getAffectionLevel()  { return 1 + (uint16_t)sqrtf((float)statAffection); }

void addAffection(uint16_t delta) {
  statAffection += delta;
  mochiPrefs.putUInt("love", statAffection);
}

void resetCompanionStats() {
  statBootCount    = 1;
  statTotalMinutes = 0;
  statAffection    = 0;
  mochiPrefs.putUInt("boots", 1);
  mochiPrefs.putUInt("mins", 0);
  mochiPrefs.putUInt("love", 0);
}

// ── Internal Silicon Temperature State ───────────────────────
// Note: Inside the enclosed Clawd Mochi 3D case at ~24C room temp, ESP32-C3 die
// steady-state is ~49-51C (~25C self-heating offset). Cold boot starts near room temp (~24-30C).
float    bootTempC      = 26.0f;
float    currentTempC   = 26.0f;
uint32_t lastTempReadMs = 0;

float getChipTempC()    { return currentTempC; }
float getEstRoomTempC() { return currentTempC - 24.5f; }
bool  isTempCold()      { return (millis() < 90000UL) && (currentTempC < 33.0f); }
bool  isTempWarm()      { return currentTempC > 59.0f; }

// ── Per-MAC BLE RSSI Delta Tracker (filters out stationary desk Mac!) ──
struct BlePeerTrack {
  uint32_t addrHash;
  float    avgRssi;
  uint16_t seenCount;
  uint32_t lastSeenMs;
};

static BlePeerTrack peerTable[10];

static uint32_t hashAddressBytes(const uint8_t* b, size_t n) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < n; i++) {
    h ^= b[i];
    h *= 16777619UL;
  }
  return (h == 0) ? 1 : h;
}

volatile bool    bleScanRunning         = false;
volatile int     bleWindowPeakAppleRssi = -127;
volatile int     bleWindowPeakAnyRssi   = -127;
volatile uint8_t bleWindowAppleSubType  = 0;

int      lastBleAppleRssi    = -100;
int8_t   petTouchThreshold   = -32;   // Direct shell proximity (~2-6 cm)
int8_t   petApproachMinRssi  = -46;   // Approach zone (~5-12 cm) when combined with >= +10dB motion jump
int8_t   deskRssiThreshold   = -78;
uint8_t  boundSubType        = 0;
bool     bleLearnMode        = false;

uint32_t lastBleScanTriggerMs = 0;
uint32_t lastPetTriggerMs     = 0;
uint32_t lastOwnerSeenMs      = 0;
uint32_t ownerAwaySinceMs     = 0;
bool     ownerPresent         = true;
bool     macPresenceFresh     = false;
uint32_t lastMacPresenceMs    = 0;

const uint32_t BLE_SCAN_INTERVAL_MS = 5000UL;   // 380ms passive scan every 5s (~7.5% radio duty)
const uint32_t PET_COOLDOWN_MS      = 8000UL;
const uint32_t AWAY_TIMEOUT_MS      = 480000UL; // 8 min

int  getLastBleRssi() { return lastBleAppleRssi; }
bool isOwnerPresent() { return ownerPresent; }

void notifyMacPresence(bool active, uint32_t idleSec) {
  macPresenceFresh  = true;
  lastMacPresenceMs = millis();
  if (active && idleSec < 180) {
    if (!ownerPresent) {
      uint32_t awayDur = millis() - ownerAwaySinceMs;
      ownerPresent = true;
      if (pendingEvent == EVT_NONE) {
        pendingEvent = (awayDur > 1500000UL) ? EVT_LONG_AWAY : EVT_WELCOME_BACK;
      }
    }
    lastOwnerSeenMs = millis();
  } else if (idleSec >= 480 && ownerPresent) {
    ownerPresent     = false;
    ownerAwaySinceMs = millis();
  }
}

static void processAdvertisedPeer(uint32_t addrHash, int rssi, bool isApple, uint8_t subType) {
  uint32_t now = millis();
  if (rssi > bleWindowPeakAnyRssi) {
    bleWindowPeakAnyRssi = rssi;
  }
  if (isApple) {
    if (boundSubType == 0 || subType == boundSubType || rssi >= -42) {
      if (rssi > bleWindowPeakAppleRssi) {
        bleWindowPeakAppleRssi = rssi;
        bleWindowAppleSubType  = subType;
      }
    }
  }

  // Look up or insert peer in tracking table
  int slot = -1;
  int oldestSlot = 0;
  uint32_t oldestMs = 0xFFFFFFFFUL;
  for (int i = 0; i < 10; i++) {
    if (peerTable[i].addrHash == addrHash) {
      slot = i;
      break;
    }
    if (peerTable[i].addrHash == 0) {
      oldestSlot = i;
      oldestMs = 0;
    } else if (peerTable[i].lastSeenMs < oldestMs) {
      oldestMs = peerTable[i].lastSeenMs;
      oldestSlot = i;
    }
  }

  bool triggeredPet = false;
  if (slot >= 0) {
    float prevAvg = peerTable[slot].avgRssi;
    float jumpDb  = (float)rssi - prevAvg;
    // Moving Watch/iPhone brought close to crab: jumps >= +10 dB into >= -46 dBm zone,
    // OR pressed right against shell (>= petTouchThreshold, e.g. -32 dBm) while jumping >= +5 dB
    if (isApple && peerTable[slot].seenCount >= 2) {
      if ((rssi >= petApproachMinRssi && jumpDb >= 10.5f) ||
          (rssi >= petTouchThreshold  && jumpDb >= 5.5f)) {
        triggeredPet = true;
      }
    }
    // Update baseline slowly (don't let a brief pet spike pull baseline all the way up)
    if (!triggeredPet) {
      peerTable[slot].avgRssi = prevAvg * 0.82f + (float)rssi * 0.18f;
    }
    if (peerTable[slot].seenCount < 1000) peerTable[slot].seenCount++;
    peerTable[slot].lastSeenMs = now;
  } else {
    slot = oldestSlot;
    peerTable[slot].addrHash   = addrHash;
    peerTable[slot].avgRssi    = (float)rssi;
    peerTable[slot].seenCount  = 1;
    peerTable[slot].lastSeenMs = now;
    // Brand-new wake advertisement right against the shell (after initial 15s boot settle)
    if (isApple && now > 15000UL && rssi >= petTouchThreshold) {
      triggeredPet = true;
    }
  }

  if (triggeredPet && (now - lastPetTriggerMs > PET_COOLDOWN_MS)) {
    lastPetTriggerMs = now;
    lastOwnerSeenMs  = now;
    ownerPresent     = true;
    pendingEvent     = EVT_PET;
  }
}

static void finalizeScanWindow() {
  int peakApple = bleWindowPeakAppleRssi;
  int peakAny   = bleWindowPeakAnyRssi;
  uint8_t sub   = bleWindowAppleSubType;
  bleScanRunning = false;

  int effectiveRssi = (peakApple > -120) ? peakApple : peakAny;
  if (effectiveRssi > -120) {
    lastBleAppleRssi = effectiveRssi;

    if (bleLearnMode) {
      bleLearnMode = false;
      if (sub != 0) {
        boundSubType = sub;
        mochiPrefs.putUChar("blesub", boundSubType);
      }
      petTouchThreshold = (int8_t)constrain(effectiveRssi - 2, -40, -26);
      mochiPrefs.putChar("blepet", petTouchThreshold);
      Serial.printf("OK:BLE_LEARNED:rssi=%d,touch_thr=%d,sub=0x%02X\n",
                    effectiveRssi, petTouchThreshold, boundSubType);
      pendingEvent = EVT_PET;
      return;
    }

    bool macFresh = macPresenceFresh && (millis() - lastMacPresenceMs < 150000UL);
    if (!macFresh && effectiveRssi >= deskRssiThreshold) {
      if (!ownerPresent && effectiveRssi >= deskRssiThreshold + 6) {
        uint32_t awayDur = millis() - ownerAwaySinceMs;
        ownerPresent = true;
        if (pendingEvent == EVT_NONE) {
          pendingEvent = (awayDur > 1500000UL) ? EVT_LONG_AWAY : EVT_WELCOME_BACK;
        }
      }
      lastOwnerSeenMs = millis();
    }
  }
}

#if ESP_ARDUINO_VERSION_MAJOR >= 4

BLEScan mochiBleScan;

static void startAsyncBleWindow(uint32_t durationMs) {
  if (!mochiBleScan || mochiBleScan.isScanning()) return;
  bleWindowPeakAppleRssi = -127;
  bleWindowPeakAnyRssi   = -127;
  bleWindowAppleSubType  = 0;
  bleScanRunning         = true;
  lastBleScanTriggerMs   = millis();
  mochiBleScan.clearDuplicateCache();
  if (!mochiBleScan.start(durationMs, false)) {
    bleScanRunning = false;
  }
}

static void initBleRadar() {
  if (!BLE.begin("ClawdMochi")) {
    Serial.println("[BLE] init failed");
    return;
  }
  mochiBleScan = BLE.getScan();
  if (!mochiBleScan) return;
  mochiBleScan.setActiveScan(false);
  mochiBleScan.setFilterDuplicates(false);
  mochiBleScan.setInterval(80);
  mochiBleScan.setWindow(35);
  mochiBleScan.onResult([](const BLEAdvertisedDevice &dev) {
    int rssi = dev.getRSSI();
    bool isApple = false;
    uint8_t subType = 0;
    if (dev.haveManufacturerData() && dev.getManufacturerCompanyId() == 0x004C) {
      isApple = true;
      size_t len = 0;
      const uint8_t* raw = dev.getManufacturerData(&len);
      if (raw && len >= 3) {
        subType = raw[2];
      }
    }
    String addrStr = dev.getAddress().toString();
    uint32_t h = hashAddressBytes((const uint8_t*)addrStr.c_str(), addrStr.length());
    processAdvertisedPeer(h, rssi, isApple, subType);
  });
  mochiBleScan.onComplete([](BLEScan::Results &results) {
    (void)results;
    mochiBleScan.clearResults();
    finalizeScanWindow();
  });
}

#else

BLEScan* pMochiBleScan = nullptr;

class MochiBleCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    int rssi = advertisedDevice.getRSSI();
    bool isApple = false;
    uint8_t subType = 0;
    if (advertisedDevice.haveManufacturerData()) {
      auto mfg = advertisedDevice.getManufacturerData();
      size_t len = mfg.length();
      const uint8_t* data = (const uint8_t*)mfg.c_str();
      if (len >= 3 && data[0] == 0x4C && data[1] == 0x00) {
        isApple = true;
        subType = data[2];
      }
    }
    auto addrStr = advertisedDevice.getAddress().toString();
    uint32_t h = hashAddressBytes((const uint8_t*)addrStr.c_str(), addrStr.length());
    processAdvertisedPeer(h, rssi, isApple, subType);
  }
};
static MochiBleCallbacks mochiBleCb;

static void onLegacyBleScanDone(BLEScanResults results) {
  (void)results;
  if (pMochiBleScan) pMochiBleScan->clearResults();
  finalizeScanWindow();
}

static void startAsyncBleWindow(uint32_t durationMs) {
  if (!pMochiBleScan || bleScanRunning) return;
  bleWindowPeakAppleRssi = -127;
  bleWindowPeakAnyRssi   = -127;
  bleWindowAppleSubType  = 0;
  bleScanRunning         = true;
  lastBleScanTriggerMs   = millis();
  uint32_t sec = (durationMs >= 1500) ? 2 : 1;
  if (!pMochiBleScan->start(sec, onLegacyBleScanDone, false)) {
    bleScanRunning = false;
  }
}

static void initBleRadar() {
  BLEDevice::init("");
  pMochiBleScan = BLEDevice::getScan();
  pMochiBleScan->setAdvertisedDeviceCallbacks(&mochiBleCb, false);
  pMochiBleScan->setActiveScan(false);
  pMochiBleScan->setInterval(90);
  pMochiBleScan->setWindow(30);
}

#endif

void triggerBleLearn() {
  bleLearnMode = true;
  startAsyncBleWindow(1800);
}

void resetBleBinding() {
  boundSubType      = 0;
  petTouchThreshold = -32;
  mochiPrefs.putUChar("blesub", 0);
  mochiPrefs.putChar("blepet", -32);
}

void setupSensorsAndMemory() {
  memset(peerTable, 0, sizeof(peerTable));
  bootTempC      = temperatureRead();
  currentTempC   = bootTempC;
  lastTempReadMs = millis();

  mochiPrefs.begin("mochi", false);
  statBootCount     = mochiPrefs.getUInt("boots", 0) + 1;
  statTotalMinutes  = mochiPrefs.getUInt("mins", 0);
  statAffection     = mochiPrefs.getUInt("love", 0);
  boundSubType      = mochiPrefs.getUChar("blesub", 0);
  petTouchThreshold = mochiPrefs.getChar("blepet", -32);
  mochiPrefs.putUInt("boots", statBootCount);

  lastMinuteTickMs = millis();
  lastMinuteSaveMs = millis();
  lastOwnerSeenMs  = millis();
  ownerPresent     = true;

  initBleRadar();
}

void pollSensors() {
  uint32_t now = millis();

  if (now - lastMinuteTickMs >= 60000UL) {
    lastMinuteTickMs += 60000UL;
    statTotalMinutes++;
    if (now - lastMinuteSaveMs >= 600000UL) {
      lastMinuteSaveMs = now;
      mochiPrefs.putUInt("mins", statTotalMinutes);
    }
  }

  if (now - lastTempReadMs >= 15000UL) {
    lastTempReadMs = now;
    float t = temperatureRead();
    if (!isnan(t) && t > -20.0f && t < 95.0f) {
      currentTempC = currentTempC * 0.7f + t * 0.3f;
    }
  }

  if (!bleScanRunning && (now - lastBleScanTriggerMs >= BLE_SCAN_INTERVAL_MS)) {
    startAsyncBleWindow(380);
  }

  bool macFresh = macPresenceFresh && (now - lastMacPresenceMs < 150000UL);
  if (!macFresh && ownerPresent && (now - lastOwnerSeenMs > AWAY_TIMEOUT_MS)) {
    ownerPresent     = false;
    ownerAwaySinceMs = now;
  }
}

#endif // DYNAMICS_SENSORS_H
