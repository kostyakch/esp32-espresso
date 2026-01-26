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

inline void temperatureSetup() {
  max31865.begin(MAX31865_2WIRE);
}

inline bool temperatureRead(float &outTemp) {
  outTemp = max31865.temperature(RNOMINAL, RREF);

  if (isnan(outTemp)) return false;
  if (outTemp < -10 || outTemp > MAX_SAFE_TEMP) return false;

  return true;
}
