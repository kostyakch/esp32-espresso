#include "pid.h"
#include "temperature.h"
#include "brew_fsm.h"
#include "http_server.h"
#include "wifi_prov.h"
#include <Preferences.h>

/* ===== PINS ===== */
#define SSR_HEATER_PIN 26
#define SSR_PUMP_PIN   27
#define MODE_BUTTON_PIN 25
#define MODE_BUTTON_DEBOUNCE_MS 50

#define DEFAULT_BREW_SETPOINT 93.0
#define STEAM_SETPOINT 112.0

float currentTemp = 32.0;

PIDController pid(12.0, 0.4, 0.0);

const unsigned long pidWindow = 1000;
unsigned long windowStart = 0;
unsigned long lastTelemetryMs = 0;

struct AutoTuneState {
  bool active = false;
  bool heating = true;
  bool initialized = false;
  float setpoint = DEFAULT_BREW_SETPOINT;
  float outputHigh = 60.0f;
  float outputLow = 0.0f;
  float lastTemp = 0.0f;
  float highPeak = 0.0f;
  float lowPeak = 0.0f;
  float maxTemp = -1000.0f;
  float minTemp = 1000.0f;
  unsigned long lastSwitchMs = 0;
  unsigned long lastHighPeakMs = 0;
  int peakPairs = 0;
  int periodCount = 0;
  float sumAmplitude = 0.0f;
  float sumPeriod = 0.0f;
};

static AutoTuneState autoTune;

static Preferences prefs;

enum Mode {
  MODE_BREW = 0,
  MODE_STEAM = 1
};

static Mode currentMode = MODE_BREW;
static float brewSetpoint = DEFAULT_BREW_SETPOINT;
static bool lastButtonState = HIGH;
static unsigned long lastButtonMs = 0;

void autoTuneStop();
void autoTuneApplySetpoint(float sp);

void logTelemetry(float power) {
  unsigned long now = millis();
  if (now - lastTelemetryMs < 500) {
    return;
  }
  lastTelemetryMs = now;

  float error = pid.getSetpoint() - currentTemp;
  Serial.printf(
    "temp=%.2f sp=%.2f err=%.2f power=%.1f sim=%d tune=%d kp=%.2f ki=%.3f kd=%.2f\n",
    currentTemp,
    pid.getSetpoint(),
    error,
    power,
    temperatureSimulated() ? 1 : 0,
    autoTune.active ? 1 : 0,
    pid.getKp(),
    pid.getKi(),
    pid.getKd()
  );
}

void applyMode(Mode mode) {
  currentMode = mode;
  if (currentMode == MODE_STEAM) {
    pid.setSetpoint(STEAM_SETPOINT);
  } else {
    pid.setSetpoint(brewSetpoint);
  }
  autoTuneStop();
}

void setModeBrew() { applyMode(MODE_BREW); }
void setModeSteam() { applyMode(MODE_STEAM); }
bool isSteamMode() { return currentMode == MODE_STEAM; }
const char* getModeName() { return currentMode == MODE_STEAM ? "steam" : "brew"; }
float getBrewSetpoint() { return brewSetpoint; }
float getSteamSetpoint() { return STEAM_SETPOINT; }
void setBrewSetpoint(float sp) {
  brewSetpoint = sp;
  if (currentMode == MODE_BREW) {
    pid.setSetpoint(brewSetpoint);
  }
  autoTuneApplySetpoint(pid.getSetpoint());
}

void autoTuneStart() {
  autoTune.active = true;
  autoTune.initialized = false;
  autoTune.setpoint = pid.getSetpoint();
  autoTune.outputHigh = 60.0f;
  autoTune.outputLow = 0.0f;
  Serial.println("AutoTune: started");
}

void autoTuneStop() {
  autoTune.active = false;
  autoTune.initialized = false;
  Serial.println("AutoTune: stopped");
}

void autoTuneApplySetpoint(float sp) {
  if (!autoTune.active) {
    return;
  }
  autoTune.setpoint = sp;
  autoTune.initialized = false;
  Serial.printf("AutoTune: setpoint updated to %.2f\n", sp);
}

float autoTuneUpdate(float temp) {
  unsigned long now = millis();
  if (!autoTune.initialized) {
    autoTune.heating = temp < autoTune.setpoint;
    autoTune.lastTemp = temp;
    autoTune.maxTemp = temp;
    autoTune.minTemp = temp;
    autoTune.lastSwitchMs = now;
    autoTune.lastHighPeakMs = 0;
    autoTune.peakPairs = 0;
    autoTune.periodCount = 0;
    autoTune.sumAmplitude = 0.0f;
    autoTune.sumPeriod = 0.0f;
    autoTune.initialized = true;
    return autoTune.heating ? autoTune.outputHigh : autoTune.outputLow;
  }

  if (autoTune.heating) {
    autoTune.maxTemp = max(autoTune.maxTemp, temp);
    if (temp >= autoTune.setpoint && autoTune.lastTemp < autoTune.setpoint) {
      autoTune.highPeak = autoTune.maxTemp;
      if (autoTune.lastHighPeakMs != 0) {
        float period = (now - autoTune.lastHighPeakMs) / 1000.0f;
        if (period > 0) {
          autoTune.sumPeriod += period;
          autoTune.periodCount++;
        }
      }
      autoTune.lastHighPeakMs = now;
      autoTune.maxTemp = temp;
      autoTune.heating = false;
    }
  } else {
    autoTune.minTemp = min(autoTune.minTemp, temp);
    if (temp <= autoTune.setpoint && autoTune.lastTemp > autoTune.setpoint) {
      autoTune.lowPeak = autoTune.minTemp;
      float amplitude = (autoTune.highPeak - autoTune.lowPeak) / 2.0f;
      if (amplitude > 0.05f) {
        autoTune.sumAmplitude += amplitude;
        autoTune.peakPairs++;
      }
      autoTune.minTemp = temp;
      autoTune.heating = true;
    }
  }

  autoTune.lastTemp = temp;

  if (autoTune.peakPairs >= 5 && autoTune.periodCount >= 3) {
    float avgAmplitude = autoTune.sumAmplitude / autoTune.peakPairs;
    float avgPeriod = autoTune.sumPeriod / autoTune.periodCount;
    float relayAmp = (autoTune.outputHigh - autoTune.outputLow) / 2.0f;
    float ku = (4.0f * relayAmp) / (PI * avgAmplitude);
    float kp = 0.45f * ku;
    float ki = 1.2f * kp / avgPeriod;

    pid.setTunings(kp, ki, 0.0f);
    pid.reset();
    autoTuneStop();

    Serial.printf(
      "AutoTune: done Ku=%.3f Pu=%.2f Kp=%.3f Ki=%.3f\n",
      ku,
      avgPeriod,
      kp,
      ki
    );
  }

  return autoTune.heating ? autoTune.outputHigh : autoTune.outputLow;
}

void loadSettings() {
  prefs.begin("espresso", true);
  float sp = prefs.getFloat("brew_sp", DEFAULT_BREW_SETPOINT);
  unsigned long pre = prefs.getULong("pre_ms", DEFAULT_PREINFUSION_MS);
  unsigned long pause = prefs.getULong("pause_ms", DEFAULT_PAUSE_MS);
  unsigned long brew = prefs.getULong("brew_ms", DEFAULT_BREW_MS);
  prefs.end();

  brewSetpoint = sp;
  applyMode(MODE_BREW);
  preinfusionMs = pre;
  pauseMs = pause;
  brewMs = brew;
}

void saveSettings() {
  prefs.begin("espresso", false);
  prefs.putFloat("brew_sp", brewSetpoint);
  prefs.putULong("pre_ms", preinfusionMs);
  prefs.putULong("pause_ms", pauseMs);
  prefs.putULong("brew_ms", brewMs);
  prefs.end();
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(10);

  pinMode(SSR_HEATER_PIN, OUTPUT);
  pinMode(SSR_PUMP_PIN, OUTPUT);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(SSR_HEATER_PIN, LOW);
  digitalWrite(SSR_PUMP_PIN, LOW);

  temperatureSetup();

  loadSettings();
  pid.setIntegralClamp(40);
  pid.setDtLimits(0.1, 2.0);
  pid.reset();

  wifiSetup();
  httpSetup();
  windowStart = millis();
  lastTelemetryMs = millis();
}

void loop() {
  httpLoop();
  brewLoop();

  unsigned long now = millis();

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "autotune" || cmd == "a") autoTuneStart();
    if (cmd == "stop" || cmd == "s") autoTuneStop();
  }

  bool buttonState = digitalRead(MODE_BUTTON_PIN);
  if (buttonState != lastButtonState) {
    lastButtonMs = now;
  }
  if ((now - lastButtonMs) > MODE_BUTTON_DEBOUNCE_MS) {
    if (lastButtonState == HIGH && buttonState == LOW) {
      applyMode(currentMode == MODE_BREW ? MODE_STEAM : MODE_BREW);
    }
  }
  lastButtonState = buttonState;

  float temp;
  bool ok = temperatureRead(temp);

  if (!ok) {
    if (!temperatureSimulated()) {
      temperatureEnableSim(currentTemp);
      Serial.println("Temperature: sensor missing, simulation enabled");
    }
    temp = temperatureSimGet();
  }

  currentTemp = temp;

  float power = autoTune.active
    ? autoTuneUpdate(currentTemp)
    : pid.compute(currentTemp);

  if (now - windowStart > pidWindow)
    windowStart += pidWindow;

  if (temperatureSimulated()) {
    digitalWrite(SSR_HEATER_PIN, LOW);
    temperatureSimUpdate(power);
  } else {
    digitalWrite(
      SSR_HEATER_PIN,
      (power / 100.0) * pidWindow > (now - windowStart)
    );
  }

  logTelemetry(power);
}
