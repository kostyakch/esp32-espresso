#pragma once
#include <Adafruit_MAX31865.h>

/* ===== PINS (must match wiring: README says CS=GPIO5) ===== */
#define MAX31865_CS   5
#define MAX31865_MOSI 23
#define MAX31865_MISO 19
#define MAX31865_CLK  18

#define RNOMINAL 100.0
#define RREF     430.0
#define MAX_SAFE_TEMP 120.0   // °C
/* 2-wire: MAX31865_2WIRE, 3-wire: MAX31865_3WIRE, 4-wire: MAX31865_4WIRE. Diag showed 2/4-wire OK, 3-wire wrong for this board. */
#define MAX31865_WIRES MAX31865_2WIRE

static Adafruit_MAX31865 max31865(
  MAX31865_CS,
  MAX31865_MOSI,
  MAX31865_MISO,
  MAX31865_CLK
);

static uint8_t lastFault = 0;
static uint16_t lastRtd = 0;
static float lastRatio = 0.0f;
static float lastTempRaw = 0.0f;
static bool simEnabled = false;
static float simTemp = 22.0;
static unsigned long simLastMs = 0;

// Simple thermal model for simulation
static const float SIM_AMBIENT_C = 22.0f;
static const float SIM_HEATER_W = 1200.0f;
static const float SIM_THERMAL_MASS_J_PER_C = 1500.0f;
static const float SIM_LOSS_W_PER_C = 6.0f;

inline void temperatureSetup() {
  max31865.begin(MAX31865_WIRES);
}

inline bool temperatureRead(float &outTemp) {
  lastFault = 0;
  lastRtd = max31865.readRTD();
  lastRatio = lastRtd;
  lastRatio /= 32768.0f;
  uint8_t fault = max31865.readFault();
  if (fault) {
    lastFault = fault;
    max31865.clearFault();
    return false;
  }

  lastTempRaw = max31865.temperature(RNOMINAL, RREF);
  outTemp = lastTempRaw;

  if (isnan(outTemp)) return false;
  // We allow readings above MAX_SAFE_TEMP here so the main loop can detect and handle the over-temp condition
  if (outTemp < -10 || outTemp > 200.0) return false;

  return true;
}

inline uint8_t temperatureLastFault() {
  return lastFault;
}

inline uint16_t temperatureLastRtd() {
  return lastRtd;
}

inline float temperatureLastRatio() {
  return lastRatio;
}

inline float temperatureLastTempRaw() {
  return lastTempRaw;
}

inline void temperatureEnableSim(float startTemp) {
  simEnabled = true;
  simTemp = startTemp;
  simLastMs = millis();
}

inline void temperatureDisableSim() {
  simEnabled = false;
}

inline bool temperatureSimulated() {
  return simEnabled;
}

inline float temperatureSimGet() {
  return simTemp;
}

inline void temperatureSimUpdate(float powerPercent) {
  if (!simEnabled) {
    return;
  }

  unsigned long now = millis();
  if (simLastMs == 0) {
    simLastMs = now;
    return;
  }

  float dt = (now - simLastMs) / 1000.0f;
  if (dt <= 0) {
    return;
  }

  simLastMs = now;

  float heaterPower = SIM_HEATER_W * constrain(powerPercent, 0.0f, 100.0f) / 100.0f;
  float lossPower = (simTemp - SIM_AMBIENT_C) * SIM_LOSS_W_PER_C;
  float netPower = heaterPower - lossPower;

  simTemp += (netPower / SIM_THERMAL_MASS_J_PER_C) * dt;

  if (simTemp < SIM_AMBIENT_C) simTemp = SIM_AMBIENT_C;
  if (simTemp > MAX_SAFE_TEMP) simTemp = MAX_SAFE_TEMP;
}
