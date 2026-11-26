// dynamics-sensors.h — Zero-extra-hardware sensing, NVS persistence & Cryptographic iPhone Bonding (IRK):
//   1. NVS Flash memory (boot count, total companion minutes, affection points, 16-byte Owner IRK)
//   2. ESP32-C3 internal silicon temperature sensor (calibrated for enclosed 3D-printed PLA case)
//   3. One-time iOS Settings -> Bluetooth pairing ("Clawd-Mochi", HID Consumer Control so iOS keyboard is never hidden):
//      Extracts the iPhone's 16-byte Identity Resolving Key (IRK), disconnects cleanly (zero phone battery drain),
//      and thereafter resolves rotating random BLE addresses via AES-128 `ah(IRK, prand)`!
//      - Recognizes ONLY the owner's iPhone for walk-away (90s) / walk-back & petting!
//      - Plays tsundere angry face ("哼! 你不是我主人!") if a stranger's phone touches the shell!

#ifndef DYNAMICS_SENSORS_H
#define DYNAMICS_SENSORS_H

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <Preferences.h>
#include <aes/esp_aes.h>
#if ESP_ARDUINO_VERSION_MAJOR >= 4
  #include <BLE.h>
  #include <hid/HIDTypes.h>
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
float    bootTempC      = 26.0f;
float    currentTempC   = 26.0f;
uint32_t lastTempReadMs = 0;

float getChipTempC()    { return currentTempC; }
float getEstRoomTempC() { return currentTempC - 24.5f; }
bool  isTempCold()      { return (millis() < 90000UL) && (currentTempC < 33.0f); }
bool  isTempWarm()      { return currentTempC > 59.0f; }

// ── Owner Cryptographic Identity (16-byte BLE IRK) ───────────
bool     ownerBonded          = false;
uint8_t  ownerIrk[16]         = {0};
uint8_t  ownerIrkRev[16]      = {0};
uint8_t  ownerIdAddrBytes[6]  = {0};  // LSB-first static/public identity address if available

bool     pairingWindowActive  = false;
uint32_t pairingWindowEndMs   = 0;
volatile bool     pendingTearDownConn   = false;
volatile uint16_t pendingTearDownHandle = 0xFFFF;
volatile uint32_t pendingTearDownAtMs   = 0;

int      lastBleAppleRssi     = -100;
int      lastOwnerRssi        = -127;
uint32_t lastOwnerBlePacketMs = 0;

bool isOwnerBonded() { return ownerBonded; }
int  getOwnerRssi()  { return lastOwnerRssi; }
int  getPairingRemainingSec() {
  if (!pairingWindowActive) return 0;
  uint32_t now = millis();
  return (now < pairingWindowEndMs) ? (int)((pairingWindowEndMs - now) / 1000UL) : 0;
}

static void refreshReversedIrk() {
  for (int i = 0; i < 16; i++) {
    ownerIrkRev[i] = ownerIrk[15 - i];
  }
}

// Bluetooth Core Spec v4.2 Vol 3, Part H, 2.2.2: ah(k, r)
// addrLsb[0..2] = 24-bit hash (LSB..MSB), addrLsb[3..5] = 24-bit prand (LSB..MSB)
static bool rpaMatchesKeyBe(const uint8_t addrLsb[6], const uint8_t keyBe[16]) {
  // Top two bits of MSB (addrLsb[5] bits 7..6) MUST be 0b01 (0x40) for Resolvable Private Address
  if ((addrLsb[5] & 0xC0) != 0x40) return false;
  uint8_t pt[16] = {0};
  pt[13] = addrLsb[5];
  pt[14] = addrLsb[4];
  pt[15] = addrLsb[3];

  uint8_t ct[16] = {0};
  esp_aes_context ctx;
  esp_aes_init(&ctx);
  esp_aes_setkey(&ctx, keyBe, 128);
  esp_aes_crypt_ecb(&ctx, ESP_AES_ENCRYPT, pt, ct);
  esp_aes_free(&ctx);

  return (ct[13] == addrLsb[2] && ct[14] == addrLsb[1] && ct[15] == addrLsb[0]);
}

static bool matchesOwnerIdentity(const uint8_t addrLsb[6], uint8_t addrType) {
  if (!ownerBonded) return false;
  // 1. Direct identity address match (if controller hardware resolving list resolved it)
  bool hasSavedId = false;
  for (int i = 0; i < 6; i++) if (ownerIdAddrBytes[i] != 0) hasSavedId = true;
  if (hasSavedId && memcmp(addrLsb, ownerIdAddrBytes, 6) == 0) {
    return true;
  }
  // 2. Cryptographic AES-128 RPA resolution against both endian conventions of IRK
  if ((addrLsb[5] & 0xC0) == 0x40) {
    if (rpaMatchesKeyBe(addrLsb, ownerIrkRev) || rpaMatchesKeyBe(addrLsb, ownerIrk)) {
      return true;
    }
  }
  (void)addrType;
  return false;
}

// ── Per-MAC Fallback Tracker (used before owner bonds, or for stranger detection) ──
struct BlePeerTrack {
  uint32_t addrHash;
  float    avgRssi;
  uint16_t seenCount;
  uint32_t lastSeenMs;
};
static BlePeerTrack peerTable[10];

static uint32_t hashAddressBytes(const uint8_t* b, size_t n) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619UL; }
  return (h == 0) ? 1 : h;
}

volatile bool    bleScanRunning          = false;
volatile int     bleWindowPeakAppleRssi  = -127;
volatile int     bleWindowPeakAnyRssi    = -127;
volatile int     bleWindowPeakOwnerRssi  = -127;
volatile uint8_t bleWindowAppleSubType   = 0;

int8_t   petTouchThreshold    = -32;
int8_t   petApproachMinRssi   = -46;
int8_t   deskRssiThreshold    = -80;
uint8_t  boundSubType         = 0;
bool     bleLearnMode         = false;

uint32_t lastBleScanTriggerMs = 0;
uint32_t lastPetTriggerMs     = 0;
uint32_t lastStrangerMs       = 0;
uint32_t lastOwnerSeenMs      = 0;
uint32_t ownerAwaySinceMs     = 0;
bool     ownerPresent         = true;
bool     macPresenceFresh     = false;
uint32_t lastMacPresenceMs    = 0;

const uint32_t BLE_SCAN_INTERVAL_MS   = 4200UL;   // Scan every 4.2s
const uint32_t PET_COOLDOWN_MS        = 7500UL;
const uint32_t STRANGER_COOLDOWN_MS   = 10000UL;
const uint32_t BONDED_AWAY_TIMEOUT_MS = 90000UL;  // 90s without owner's iPhone -> away!
const uint32_t UNBONDED_AWAY_MS       = 480000UL; // 8m fallback when unbonded

int  getLastBleRssi() { return ownerBonded && lastOwnerRssi > -120 ? lastOwnerRssi : lastBleAppleRssi; }
bool isOwnerPresent() { return ownerPresent; }

void notifyMacPresence(bool active, uint32_t idleSec) {
  macPresenceFresh  = true;
  lastMacPresenceMs = millis();
  // When bonded to owner's iPhone, trust the iPhone's cryptographic BLE presence first!
  // Only let explicit Mac lock / manual scene override when unbonded or screen just locked.
  if (ownerBonded) {
    if (active && idleSec < 15 && !ownerPresent) {
      ownerPresent    = true;
      lastOwnerSeenMs = millis();
      if (pendingEvent == EVT_NONE) pendingEvent = EVT_WELCOME_BACK;
    }
    return;
  }

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

static void processAdvertisedPeer(const uint8_t addrLsb[6], uint8_t addrType, int rssi, bool isApple, uint8_t subType) {
  uint32_t now = millis();
  if (rssi > bleWindowPeakAnyRssi) bleWindowPeakAnyRssi = rssi;
  if (isApple && rssi > bleWindowPeakAppleRssi) {
    bleWindowPeakAppleRssi = rssi;
    bleWindowAppleSubType  = subType;
  }

  bool isOwner = matchesOwnerIdentity(addrLsb, addrType);
  if (isOwner) {
    if (rssi > bleWindowPeakOwnerRssi) bleWindowPeakOwnerRssi = rssi;
    lastOwnerRssi        = rssi;
    lastOwnerBlePacketMs = now;

    // Owner returned to desk!
    if (!ownerPresent && rssi >= -82) {
      uint32_t awayDur = now - ownerAwaySinceMs;
      ownerPresent = true;
      if (pendingEvent == EVT_NONE) {
        pendingEvent = (awayDur > 1500000UL) ? EVT_LONG_AWAY : EVT_WELCOME_BACK;
      }
    }
    if (rssi >= -86) {
      lastOwnerSeenMs = now;
    }
  }

  // Track per-MAC RSSI baseline for motion jump detection
  uint32_t addrHash = hashAddressBytes(addrLsb, 6);
  int slot = -1, oldestSlot = 0;
  uint32_t oldestMs = 0xFFFFFFFFUL;
  for (int i = 0; i < 10; i++) {
    if (peerTable[i].addrHash == addrHash) { slot = i; break; }
    if (peerTable[i].addrHash == 0) { oldestSlot = i; oldestMs = 0; }
    else if (peerTable[i].lastSeenMs < oldestMs) { oldestMs = peerTable[i].lastSeenMs; oldestSlot = i; }
  }

  float jumpDb = 0.0f;
  uint16_t seenCnt = 1;
  if (slot >= 0) {
    jumpDb  = (float)rssi - peerTable[slot].avgRssi;
    seenCnt = peerTable[slot].seenCount;
    if (jumpDb < 8.0f) {
      peerTable[slot].avgRssi = peerTable[slot].avgRssi * 0.80f + (float)rssi * 0.20f;
    }
    if (peerTable[slot].seenCount < 1000) peerTable[slot].seenCount++;
    peerTable[slot].lastSeenMs = now;
  } else {
    slot = oldestSlot;
    peerTable[slot].addrHash   = addrHash;
    peerTable[slot].avgRssi    = (float)rssi;
    peerTable[slot].seenCount  = 1;
    peerTable[slot].lastSeenMs = now;
  }

  if (ownerBonded) {
    // STRICT OWNER-ONLY MODE!
    if (isOwner) {
      // Owner's iPhone brought close (~5-12 cm): either direct close >= -41 dBm or approach jump >= +7 dB into >= -48 dBm!
      if ((rssi >= -41 || (seenCnt >= 2 && rssi >= -48 && jumpDb >= 7.5f)) &&
          (now - lastPetTriggerMs > PET_COOLDOWN_MS)) {
        lastPetTriggerMs = now;
        ownerPresent     = true;
        lastOwnerSeenMs  = now;
        pendingEvent     = EVT_PET;
      }
    } else if (isApple && rssi >= -32 && jumpDb >= 6.0f && (now - lastStrangerMs > STRANGER_COOLDOWN_MS)) {
      // Stranger's phone touched the shell!
      lastStrangerMs = now;
      pendingEvent   = EVT_STRANGER;
    }
  } else {
    // Unbonded fallback mode
    bool triggeredPet = false;
    if (isApple && seenCnt >= 2) {
      if ((rssi >= petApproachMinRssi && jumpDb >= 10.5f) ||
          (rssi >= petTouchThreshold  && jumpDb >= 5.5f)) {
        triggeredPet = true;
      }
    } else if (isApple && seenCnt == 1 && now > 15000UL && rssi >= petTouchThreshold) {
      triggeredPet = true;
    }
    if (triggeredPet && (now - lastPetTriggerMs > PET_COOLDOWN_MS)) {
      lastPetTriggerMs = now;
      lastOwnerSeenMs  = now;
      ownerPresent     = true;
      pendingEvent     = EVT_PET;
    }
  }
}

static void finalizeScanWindow() {
  int peakApple = bleWindowPeakAppleRssi;
  int peakAny   = bleWindowPeakAnyRssi;
  int peakOwner = bleWindowPeakOwnerRssi;
  uint8_t sub   = bleWindowAppleSubType;
  bleScanRunning = false;

  int effectiveRssi = (peakApple > -120) ? peakApple : peakAny;
  if (effectiveRssi > -120) {
    lastBleAppleRssi = effectiveRssi;
    if (bleLearnMode) {
      bleLearnMode = false;
      if (sub != 0) { boundSubType = sub; mochiPrefs.putUChar("blesub", boundSubType); }
      petTouchThreshold = (int8_t)constrain(effectiveRssi - 2, -40, -26);
      mochiPrefs.putChar("blepet", petTouchThreshold);
      pendingEvent = EVT_PET;
      return;
    }
  }

  if (ownerBonded && peakOwner > -120) {
    lastOwnerRssi = peakOwner;
  }
}

#if ESP_ARDUINO_VERSION_MAJOR >= 4

BLEScan   mochiBleScan;
BLEServer mochiBleServer;
bool      hidInitialized = false;

// Minimal HID Consumer Control Report Descriptor (Media Remote -> appears in iOS Settings > Bluetooth
// without EVER suppressing the iOS on-screen virtual keyboard!)
static const uint8_t kMochiConsumerReportMap[] = {
  USAGE_PAGE(1),      0x0C,  // Consumer Devices
  USAGE(1),           0x01,  // Consumer Control
  COLLECTION(1),      0x01,  // Application
  REPORT_ID(1),       0x01,
  LOGICAL_MINIMUM(1), 0x00,
  LOGICAL_MAXIMUM(1), 0x01,
  REPORT_SIZE(1),     0x01,
  REPORT_COUNT(1),    0x02,
  USAGE(1),           0xE9,  // Volume Up
  USAGE(1),           0xEA,  // Volume Down
  HIDINPUT(1),        0x02,
  REPORT_COUNT(1),    0x06,
  HIDINPUT(1),        0x03,
  END_COLLECTION(0)
};

static bool tryCapturePeerIrk(const BTAddress &peerAddr) {
  uint8_t captured[16] = {0};
  bool got = BLE.getPeerIRK(peerAddr, captured);
  if (!got) {
    // Also check all bonded devices in case peerAddr was an OTA RPA before identity update
    auto bonded = BLE.getSecurity().getBondedDevices();
    for (size_t i = 0; i < bonded.size(); i++) {
      if (BLE.getPeerIRK(bonded[i], captured)) {
        memcpy(ownerIdAddrBytes, bonded[i].data(), 6);
        got = true;
        break;
      }
    }
  } else {
    memcpy(ownerIdAddrBytes, peerAddr.data(), 6);
  }

  if (got) {
    memcpy(ownerIrk, captured, 16);
    refreshReversedIrk();
    ownerBonded         = true;
    ownerPresent        = true;
    lastOwnerSeenMs     = millis();
    lastOwnerRssi       = -45;
    mochiPrefs.putBytes("irk", ownerIrk, 16);
    mochiPrefs.putBytes("idaddr", ownerIdAddrBytes, 6);
    mochiPrefs.putBool("bonded", true);
    Serial.printf("[BLE] Owner iPhone IRK captured! Peer=%s\n", peerAddr.toString().c_str());
    return true;
  }
  return false;
}

static void ensurePairingServerReady() {
  if (hidInitialized) return;
  hidInitialized = true;

  BLESecurity sec = BLE.getSecurity();
  sec.setIOCapability(BLEIOCapability::NoInputNoOutput);
  sec.setAuthenticationMode(true, false, true);  // Bonding=true, MITM=false (Just Works), LESC=true
  sec.setInitiatorKeys(BLESecurity::KeyDist::EncKey | BLESecurity::KeyDist::IdKey);
  sec.setResponderKeys(BLESecurity::KeyDist::EncKey | BLESecurity::KeyDist::IdKey);

  mochiBleServer = BLE.createServer();
  mochiBleServer.advertiseOnDisconnect(false);

  mochiBleServer.onConnect([](BLEServer &s, const BLEConnInfo &conn) {
    (void)s;
    Serial.printf("[BLE] Pairing client connected: %s\n", conn.getAddress().toString().c_str());
    BLE.getSecurity().startSecurity(conn);
  });

  mochiBleServer.onIdentity([](BLEServer &s, const BLEConnInfo &conn) {
    (void)s;
    tryCapturePeerIrk(conn.getAddress());
  });

  sec.onAuthenticationComplete([](const BLEConnInfo &conn, bool success) {
    Serial.printf("[BLE] Auth complete: success=%d addr=%s\n", success ? 1 : 0, conn.getAddress().toString().c_str());
    if (success) {
      tryCapturePeerIrk(conn.getAddress());
      // Schedule gentle disconnect & stop advertising after 900ms so iPhone saves bond cleanly
      // and then drops active connection (zero ongoing phone battery usage!)
      pendingTearDownHandle = conn.getHandle();
      pendingTearDownAtMs   = millis() + 900UL;
      pendingTearDownConn   = true;
      pendingEvent          = EVT_BOND_SUCCESS;
    }
  });

  static BLEHIDDevice hid(mochiBleServer);
  hid.manufacturer("ClawdMochi");
  hid.pnp(0x02, 0x05AC, 0x820A, 0x0100);
  hid.hidInfo(0x00, 0x02);
  hid.reportMap(kMochiConsumerReportMap, sizeof(kMochiConsumerReportMap));
  (void)hid.inputReport(0x01);
  hid.setBatteryLevel(100);
  mochiBleServer.start();

  BLEAdvertising adv = BLE.getAdvertising();
  adv.setAppearance(0x0180);  // Generic Remote Control
  adv.setScanResponse(true);
}

void startBlePairingWindow(uint16_t durationSec = 180) {
  if (mochiBleScan && mochiBleScan.isScanning()) {
    mochiBleScan.stop();
    bleScanRunning = false;
  }
  ensurePairingServerReady();
  pairingWindowActive = true;
  pairingWindowEndMs  = millis() + (uint32_t)durationSec * 1000UL;
  BLE.startAdvertising();
  Serial.println("OK:BLE:PAIRING_STARTED");
}

void stopBlePairingWindow() {
  pairingWindowActive = false;
  BLE.stopAdvertising();
}

void clearOwnerBond() {
  stopBlePairingWindow();
  BLE.getSecurity().deleteAllBonds();
  ownerBonded   = false;
  lastOwnerRssi = -127;
  memset(ownerIrk, 0, 16);
  memset(ownerIrkRev, 0, 16);
  memset(ownerIdAddrBytes, 0, 6);
  mochiPrefs.putBool("bonded", false);
  mochiPrefs.remove("irk");
  mochiPrefs.remove("idaddr");
}

static void startAsyncBleWindow(uint32_t durationMs) {
  if (pairingWindowActive || !mochiBleScan || mochiBleScan.isScanning()) return;
  bleWindowPeakAppleRssi = -127;
  bleWindowPeakAnyRssi   = -127;
  bleWindowPeakOwnerRssi = -127;
  bleWindowAppleSubType  = 0;
  bleScanRunning         = true;
  lastBleScanTriggerMs   = millis();
  mochiBleScan.clearDuplicateCache();
  if (!mochiBleScan.start(durationMs, false)) {
    bleScanRunning = false;
  }
}

static void initBleRadar() {
  if (!BLE.begin("Clawd-Mochi")) {
    Serial.println("[BLE] init failed");
    return;
  }
  mochiBleScan = BLE.getScan();
  if (!mochiBleScan) return;
  mochiBleScan.setActiveScan(false);
  mochiBleScan.setFilterDuplicates(false);
  mochiBleScan.setInterval(75);
  mochiBleScan.setWindow(40);
  mochiBleScan.onResult([](const BLEAdvertisedDevice &dev) {
    int rssi = dev.getRSSI();
    bool isApple = false;
    uint8_t subType = 0;
    if (dev.haveManufacturerData() && dev.getManufacturerCompanyId() == 0x004C) {
      isApple = true;
      size_t len = 0;
      const uint8_t* raw = dev.getManufacturerData(&len);
      if (raw && len >= 3) subType = raw[2];
    }
    BTAddress addr = dev.getAddress();
    processAdvertisedPeer(addr.data(), (uint8_t)dev.getAddressType(), rssi, isApple, subType);
  });
  mochiBleScan.onComplete([](BLEScan::Results &results) {
    (void)results;
    mochiBleScan.clearResults();
    finalizeScanWindow();
  });
}

#else
// Legacy 3.x fallback stubs
void startBlePairingWindow(uint16_t durationSec = 180) { (void)durationSec; }
void stopBlePairingWindow() {}
void clearOwnerBond() {}
static void startAsyncBleWindow(uint32_t durationMs) { (void)durationMs; }
static void initBleRadar() {}
#endif

void triggerBleLearn() {
  bleLearnMode = true;
  startAsyncBleWindow(1800);
}

void resetBleBinding() {
  clearOwnerBond();
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
  themeSetting      = mochiPrefs.getUChar("theme", THEME_AUTO);
  hatSetting        = mochiPrefs.getUChar("hat", HAT_AUTO);
  if (themeSetting >= THEME_COUNT) themeSetting = THEME_AUTO;
  if (hatSetting >= HAT_COUNT)     hatSetting   = HAT_AUTO;
  boundSubType      = mochiPrefs.getUChar("blesub", 0);
  petTouchThreshold = mochiPrefs.getChar("blepet", -32);
  ownerBonded       = mochiPrefs.getBool("bonded", false);
  if (ownerBonded) {
    size_t n = mochiPrefs.getBytes("irk", ownerIrk, 16);
    if (n == 16) {
      refreshReversedIrk();
      mochiPrefs.getBytes("idaddr", ownerIdAddrBytes, 6);
    } else {
      ownerBonded = false;
    }
  }
  mochiPrefs.putUInt("boots", statBootCount);

  lastMinuteTickMs = millis();
  lastMinuteSaveMs = millis();
  lastOwnerSeenMs  = millis();
  ownerPresent     = true;

  initBleRadar();
}

void pollSensors() {
  uint32_t now = millis();

  // 0. Complete deferred pairing teardown cleanly from loop task
  #if ESP_ARDUINO_VERSION_MAJOR >= 4
  if (pendingTearDownConn && now >= pendingTearDownAtMs) {
    pendingTearDownConn = false;
    // Retry IRK capture once more right before disconnecting in case IdKey arrived just after auth event
    if (!ownerBonded) {
      auto bonded = BLE.getSecurity().getBondedDevices();
      if (!bonded.empty()) tryCapturePeerIrk(bonded[0]);
    }
    stopBlePairingWindow();
    if (mochiBleServer && pendingTearDownHandle != 0xFFFF) {
      mochiBleServer.disconnect(pendingTearDownHandle);
    }
    pendingTearDownHandle = 0xFFFF;
  }
  if (pairingWindowActive && now >= pairingWindowEndMs) {
    stopBlePairingWindow();
  }
  #endif

  // 1. Companion minutes counter
  if (now - lastMinuteTickMs >= 60000UL) {
    lastMinuteTickMs += 60000UL;
    statTotalMinutes++;
    if (now - lastMinuteSaveMs >= 600000UL) {
      lastMinuteSaveMs = now;
      mochiPrefs.putUInt("mins", statTotalMinutes);
    }
  }

  // 2. Silicon temperature sensor
  if (now - lastTempReadMs >= 15000UL) {
    lastTempReadMs = now;
    float t = temperatureRead();
    if (!isnan(t) && t > -20.0f && t < 95.0f) {
      currentTempC = currentTempC * 0.7f + t * 0.3f;
    }
  }

  // 3. Passive BLE scan window (420ms every 4.2s when not in pairing mode)
  if (!pairingWindowActive && !bleScanRunning && (now - lastBleScanTriggerMs >= BLE_SCAN_INTERVAL_MS)) {
    startAsyncBleWindow(420);
  }

  // 4. Away detection:
  //    - If bonded to owner's iPhone: 90s without hearing owner's iPhone -> owner walked away!
  //    - If unbonded: fallback to Mac lock / 8 min timeout.
  if (ownerBonded) {
    if (!pairingWindowActive && ownerPresent && (now - lastOwnerSeenMs > BONDED_AWAY_TIMEOUT_MS)) {
      ownerPresent     = false;
      ownerAwaySinceMs = now;
    }
  } else {
    bool macFresh = macPresenceFresh && (now - lastMacPresenceMs < 150000UL);
    if (!macFresh && ownerPresent && (now - lastOwnerSeenMs > UNBONDED_AWAY_MS)) {
      ownerPresent     = false;
      ownerAwaySinceMs = now;
    }
  }
}

#endif // DYNAMICS_SENSORS_H
