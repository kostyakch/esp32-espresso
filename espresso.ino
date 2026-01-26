#include "pid.h"
#include "temperature.h"
#include "brew_fsm.h"
#include "http_server.h"
#include <Preferences.h>

/* ===== PINS ===== */
#define SSR_HEATER_PIN 26
#define SSR_PUMP_PIN   27
#define DEFAULT_SETPOINT 93.0

float currentTemp = 22.0;

PIDController pid(15.0, 0.8, 60.0);

const unsigned long pidWindow = 2000;
unsigned long windowStart = 0;

static Preferences prefs;

void loadSettings() {
  prefs.begin("espresso", true);
  float sp = prefs.getFloat("setpoint", DEFAULT_SETPOINT);
  unsigned long pre = prefs.getULong("pre_ms", DEFAULT_PREINFUSION_MS);
  unsigned long pause = prefs.getULong("pause_ms", DEFAULT_PAUSE_MS);
  unsigned long brew = prefs.getULong("brew_ms", DEFAULT_BREW_MS);
  prefs.end();

  pid.setSetpoint(sp);
  preinfusionMs = pre;
  pauseMs = pause;
  brewMs = brew;
}

void saveSettings() {
  prefs.begin("espresso", false);
  prefs.putFloat("setpoint", pid.getSetpoint());
  prefs.putULong("pre_ms", preinfusionMs);
  prefs.putULong("pause_ms", pauseMs);
  prefs.putULong("brew_ms", brewMs);
  prefs.end();
}

void setup() {
  Serial.begin(115200);

  pinMode(SSR_HEATER_PIN, OUTPUT);
  pinMode(SSR_PUMP_PIN, OUTPUT);
  digitalWrite(SSR_HEATER_PIN, LOW);
  digitalWrite(SSR_PUMP_PIN, LOW);

  temperatureSetup();

  loadSettings();
  pid.setIntegralClamp(40);
  pid.reset();

  httpSetup();
  windowStart = millis();
}

void loop() {
  httpLoop();

  unsigned long now = millis();

  float temp;
  bool ok = temperatureRead(temp);

  if (!ok) {
    digitalWrite(SSR_HEATER_PIN, LOW);   // FAILSAFE
    return;
  }

  currentTemp = temp;

  float power = pid.compute(currentTemp);

  if (now - windowStart > pidWindow)
    windowStart += pidWindow;

  digitalWrite(
    SSR_HEATER_PIN,
    (power / 100.0) * pidWindow > (now - windowStart)
  );
}
