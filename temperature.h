#pragma once
#include <Adafruit_MAX31865.h>

/* ===== PINS ===== */
#define MAX31865_CS   5
#define MAX31865_MOSI 23
#define MAX31865_MISO 19
#define MAX31865_CLK  18

#define RNOMINAL 100.0
#define RREF     430.0
#define MAX_SAFE_TEMP 120.0   // °C

static Adafruit_MAX31865 max31865(
  MAX31865_CS,
  MAX31865_MOSI,
  MAX31865_MISO,
  MAX31865_CLK
);

static bool simEnabled = false;
static float simTemp = 22.0;
static unsigned long simLastMs = 0;

inline void temperatureSetup() {
  max31865.begin(MAX31865_2WIRE);
}

inline bool temperatureRead(float &outTemp) {
  outTemp = max31865.temperature(RNOMINAL, RREF);

  if (isnan(outTemp)) return false;
  if (outTemp < -10 || outTemp > MAX_SAFE_TEMP) return false;

  return true;
}

inline void temperatureEnableSim(float startTemp) {
  simEnabled = true;
  simTemp = startTemp;
  simLastMs = millis();
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

  const float ambient = 22.0f;
  const float heaterGain = 0.25f;
  const float coolRate = 0.05f;

  simTemp += (powerPercent / 100.0f) * heaterGain * dt;
  simTemp += (ambient - simTemp) * coolRate * dt;

  if (simTemp < ambient) {
    simTemp = ambient;
  }
}
