#include "pid.h"
#include "temperature.h"
#include "brew_fsm.h"
#include "http_server.h"
#include "wifi_prov.h"
#include <Preferences.h>
#include <math.h>

/* ===== PINS ===== */
#define SSR_HEATER_PIN 26
#define SSR_PUMP_PIN   27
#define MODE_BUTTON_PIN 32
#define MODE_BUTTON_DEBOUNCE_MS 50
#define BREW_BUTTON_PIN 33
#define BREW_BUTTON_DEBOUNCE_MS 80   /* 80 ms — отсекает помехи от помпы/SSR */
#define BREW_BUTTON_LONG_MS 500   /* удержание не меньше 500 ms запускает экстракцию */
/* После загрузки игнорируем кнопку пролива, чтобы помехи от SSR/нагревателя при выходе на температуру не вызывали самопроизвольный старт */
#define BREW_BUTTON_ARM_AFTER_MS 8000UL

// Wiring spec (ESP32 DevKit)
// PT100 + MAX31865 (SPI): CS=GPIO5, MOSI=GPIO23, MISO=GPIO19, CLK=GPIO18, 3V3, GND
// Heater SSR (RexC-100 SSR-40DA): IN+=GPIO26, IN-=GND, AC load in series with heater
// Pump SSR (SSR-10DA/SSR-25DA): IN+=GPIO27, IN-=GND, AC load in series with pump
// Brew button: GPIO33 -> GND (internal pull-up enabled)
// Steam mode button: GPIO32 -> GND (internal pull-up enabled)

#define STEAM_SETPOINT 125.0
/* Fallback уставки заваривания, если в настройках ещё нет значения */
#define FALLBACK_BREW_SETPOINT 90.0f
/* Во время пролива — полная мощность и подъём уставки на 5° для меньшей просадки */
#define BREW_SETPOINT_BOOST  5.0f
/* Ограничение мощности при приближении к уставке (landing). Пороги в % пути: 100% = setpoint. */
#define APPROACH_T_BASE       20.0f   /* база для процента (0% ≈ комнатная), 100% = setpoint */
#define LANDING_START_PCT     0.70f   /* начало плавного снижения мощности (70% пути) */
#define LANDING_CAP_FAR       85.0f   /* макс мощность до посадки (далеко от цели) */
#define LANDING_CAP_3         3.0f    /* макс мощность PID у самой цели */
/* Учёт инерции: длинное окно ШИМ близко к цели, подогрев чуть выше уставки */
#define PID_WINDOW_NEAR_MS  20000UL /* окно 10 с в зонах 90%/97% — реже включения, меньше перелёт */
#define MAINTENANCE_HEAT_ABOVE 6.0f /* мощность 6% при 0…+0.3° выше уставки (против инерции остывания) */
#define MAINTENANCE_BAND_ABOVE 0.3f /* полоса выше уставки для подогрева, °C */
#define TEMP_TREND_COOLING_THRESHOLD 0.0f  /* подогрев выше уставки только когда T не растёт (tempRate ≤ 0), иначе не усиливаем перелёт */
#define TEMP_EMA_ALPHA  0.3f

float currentTemp = 25.0;

PIDController pid(8.0, 0.22, 32.0);   // чуть мягче PID: меньше Kp и Ki для снижения перелёта

const unsigned long pidWindow = 4000;  /* PWM window 2–5 s for SSR */
unsigned long windowStart = 0;         /* начало текущего окна ШИМ */
unsigned long lastTelemetryMs = 0;
static float tempFiltered = 0.0f;
static bool tempFilterInitialized = false;

/* Состояние приближения к уставке (landing): ограничение мощности по % пути (100% = setpoint) */
static int approachZone = 0;                 /* 0=далеко, 1=посадка активна (≥LANDING_START_PCT) */

/* Тренд температуры для подогрева выше уставки: не греем, если T ещё растёт по инерции */
static float lastTempForTrend = 0.0f;
static unsigned long lastTempTrendMs = 0;
#define TEMP_TREND_MIN_DT_MS 500UL

struct AutoTuneState {
  bool active = false;
  bool heating = true;
  bool initialized = false;
  float setpoint = 0;
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
static float brewSetpoint = FALLBACK_BREW_SETPOINT;
static bool lastButtonState = HIGH;
static bool lastButtonReading = HIGH;
static unsigned long lastButtonMs = 0;
static bool lastBrewButtonState = HIGH;
static bool lastBrewButtonReading = HIGH;
static unsigned long lastBrewButtonMs = 0;
static unsigned long brewButtonPressMs = 0;
static bool manualBrewActive = false;
static bool brewLongActive = false;
static bool brewButtonArmed = false;  /* true после BREW_BUTTON_ARM_AFTER_MS с момента загрузки */

bool emergencyStop = false;
String emergencyReason = "";
bool heaterOn = false;
/* После загрузки нагрев всегда включён (standby = false) */
bool heaterStandby = false;
#define HEATER_STANDBY_SETPOINT 20.0f
#define EMERGENCY_RESET_TEMP 90.0f  /* сброс аварии при охлаждении ниже этой температуры */

void triggerEmergencyStop(String reason) {
  if (!emergencyStop) {
    emergencyStop = true;
    emergencyReason = reason;
    digitalWrite(SSR_HEATER_PIN, LOW);
    digitalWrite(SSR_PUMP_PIN, LOW);
    Serial.println("!!! EMERGENCY STOP: " + reason + " !!!");
  }
}

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
    "temp=%.2f sp=%.2f err=%.2f power=%.1f sim=%d tune=%d kp=%.2f ki=%.3f kd=%.2f rtd=%u ratio=%.6f r_ohms=%.2f raw=%.2f fault=0x%02X\n",
    currentTemp,
    pid.getSetpoint(),
    error,
    power,
    temperatureSimulated() ? 1 : 0,
    autoTune.active ? 1 : 0,
    pid.getKp(),
    pid.getKi(),
    pid.getKd(),
    temperatureLastRtd(),
    temperatureLastRatio(),
    temperatureLastRatio() * RREF,
    temperatureLastTempRaw(),
    temperatureLastFault()
  );
}

void printMax31865Diag(const char *label, max31865_numwires_t wires) {
  max31865.begin(wires);
  delay(10);
  uint16_t rtd = max31865.readRTD();
  float ratio = rtd / 32768.0f;
  float temp = max31865.temperature(RNOMINAL, RREF);
  uint8_t fault = max31865.readFault();
  max31865.clearFault();

  Serial.printf(
    "Diag %s: rtd=%u ratio=%.6f r_ohms=%.2f temp=%.2f fault=0x%02X\n",
    label,
    rtd,
    ratio,
    ratio * RREF,
    temp,
    fault
  );
}

void runMax31865Diag() {
  Serial.println("MAX31865 diag: starting");
  printMax31865Diag("2-wire", MAX31865_2WIRE);
  printMax31865Diag("3-wire", MAX31865_3WIRE);
  printMax31865Diag("4-wire", MAX31865_4WIRE);
  max31865.begin(MAX31865_WIRES);
  Serial.println("MAX31865 diag: done");
}

static void resetApproachState() {
  approachZone = 0;
}

void applyMode(Mode mode) {
  currentMode = mode;
  if (currentMode == MODE_STEAM) {
    pid.setSetpoint(STEAM_SETPOINT);
  } else {
    pid.setSetpoint(brewSetpoint);
  }
  pid.reset();  // clear integral when mode/setpoint changes
  resetApproachState();
  autoTuneApplySetpoint(pid.getSetpoint());
}

void updatePump() {
  BrewState state = brewGetState();
  bool pumpOn = manualBrewActive || state == PREINFUSION || state == BREW;
  digitalWrite(SSR_PUMP_PIN, pumpOn ? HIGH : LOW);
}

void setModeBrew() { applyMode(MODE_BREW); }
void setModeSteam() { applyMode(MODE_STEAM); }

bool getHeaterStandby() { return heaterStandby; }
void setHeaterStandby(bool standby) {
  heaterStandby = standby;
  if (heaterStandby) {
    pid.setSetpoint(HEATER_STANDBY_SETPOINT);
    pid.reset();
  } else {
    if (currentMode == MODE_STEAM)
      pid.setSetpoint(STEAM_SETPOINT);
    else
      pid.setSetpoint(brewSetpoint);
    pid.reset();
  }
  resetApproachState();
  autoTuneApplySetpoint(pid.getSetpoint());
}
bool isSteamMode() { return currentMode == MODE_STEAM; }
const char* getModeName() { return currentMode == MODE_STEAM ? "steam" : "brew"; }
float getBrewSetpoint() { return brewSetpoint; }
float getSteamSetpoint() { return STEAM_SETPOINT; }
bool isManualBrewActive() { return manualBrewActive; }
void setBrewSetpoint(float sp) {
  brewSetpoint = sp;
  if (currentMode == MODE_BREW) {
    pid.setSetpoint(brewSetpoint);
    pid.reset();  // avoid overshoot: clear integral when setpoint changes
    resetApproachState();
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
  float sp = prefs.getFloat("brew_sp", FALLBACK_BREW_SETPOINT);
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
  pinMode(BREW_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(SSR_HEATER_PIN, LOW);
  digitalWrite(SSR_PUMP_PIN, LOW);

  temperatureSetup();

  loadSettings();
  pid.setIntegralClamp(40);
  pid.setDtLimits(0.5, 2.0);  // PID cycle 500 ms (variant B)
  pid.reset();

  wifiSetup();
  httpSetup();
  windowStart = millis();
  lastTelemetryMs = millis();
}

void loop() {
  /* В режиме аварии только нагрев выключен, кнопка пролива и насос работают */
  if (emergencyStop) {
    heaterOn = false;
    digitalWrite(SSR_HEATER_PIN, LOW);
    static unsigned long lastErrorMs = 0;
    if (millis() - lastErrorMs > 2000) {
      Serial.println("!!! EMERGENCY (heater off, pump works): " + emergencyReason + " !!!");
      lastErrorMs = millis();
    }
  }

  ArduinoOTA.handle();
  if (otaInProgress) {
    digitalWrite(SSR_HEATER_PIN, LOW);
    digitalWrite(SSR_PUMP_PIN, LOW);

    delay(250);
    yield();
    return;
  }

  httpLoop();
  brewLoop();

  unsigned long now = millis();

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "autotune" || cmd == "a") autoTuneStart();
    if (cmd == "stop" || cmd == "s") autoTuneStop();
    if (cmd == "diag" || cmd == "d") runMax31865Diag();
  }

  bool buttonReading = digitalRead(MODE_BUTTON_PIN);
  if (buttonReading != lastButtonReading) {
    lastButtonMs = now;
    lastButtonReading = buttonReading;
  }
  if ((now - lastButtonMs) > MODE_BUTTON_DEBOUNCE_MS) {
    if (lastButtonState != buttonReading) {
      lastButtonState = buttonReading;
      applyMode(lastButtonState == LOW ? MODE_STEAM : MODE_BREW);
    }
  }

  if (!brewButtonArmed && (now > BREW_BUTTON_ARM_AFTER_MS)) {
    brewButtonArmed = true;
  }

  bool brewButtonReading = digitalRead(BREW_BUTTON_PIN);
  if (brewButtonReading != lastBrewButtonReading) {
    lastBrewButtonMs = now;
    lastBrewButtonReading = brewButtonReading;
  }
  if ((now - lastBrewButtonMs) > BREW_BUTTON_DEBOUNCE_MS) {
    if (lastBrewButtonState != brewButtonReading) {
      lastBrewButtonState = brewButtonReading;
      if (lastBrewButtonState == LOW) {
        brewButtonPressMs = now;
        brewLongActive = false;
        Serial.println("Brew button: down");
      } else {
        /* Отпускание: останавливаем экстракцию, только если она ещё идёт (не завершилась по профилю) */
        if (brewLongActive && brewGetState() != IDLE) {
          brewStop();
          Serial.println("Brew button: release -> stop extraction");
        }
        brewLongActive = false;
        manualBrewActive = false;
      }
    }
  }
  /* Только удержание запускает экстракцию; короткое нажатие не делает ничего */
  if (brewButtonArmed && lastBrewButtonState == LOW && !brewLongActive &&
      (now - brewButtonPressMs) > BREW_BUTTON_LONG_MS) {
    brewLongActive = true;
    if (brewGetState() == IDLE) {
      brewStart();
      Serial.println("Brew button: long -> start extraction");
    }
  }

  float temp;
  bool ok = temperatureRead(temp);

  if (ok && temperatureSimulated()) {
    temperatureDisableSim();
    tempFilterInitialized = false;
    Serial.println("Temperature: sensor OK, simulation disabled");
  }
  if (!ok) {
    if (!temperatureSimulated()) {
      uint8_t fault = temperatureLastFault();
      if (fault) {
        Serial.printf(
          "Temperature fault: 0x%02X (H=%d L=%d R=%d R0=%d R1=%d O=%d)\n",
          fault,
          (fault & 0x80) ? 1 : 0,
          (fault & 0x40) ? 1 : 0,
          (fault & 0x20) ? 1 : 0,
          (fault & 0x10) ? 1 : 0,
          (fault & 0x08) ? 1 : 0,
          (fault & 0x04) ? 1 : 0
        );
      } else {
        Serial.printf(
          "Temperature: invalid reading (rtd=%u ratio=%.6f raw=%.2f r_ohms=%.2f)\n",
          temperatureLastRtd(),
          temperatureLastRatio(),
          temperatureLastTempRaw(),
          temperatureLastRatio() * RREF
        );
        Serial.println("  -> Check: CS/MOSI/MISO/CLK/3V3/GND. Send 'diag' for 2/3/4-wire test. In temperature.h use MAX31865_3WIRE for 3-wire PT100.");
      }
      temperatureEnableSim(currentTemp);
      Serial.println("Temperature: sensor missing, simulation enabled");
    }
    temp = temperatureSimGet();
  }
  if (!ok) temp = temperatureSimGet();

  if (ok) {
    if (!tempFilterInitialized) {
      tempFiltered = temp;
      tempFilterInitialized = true;
    } else {
      tempFiltered = TEMP_EMA_ALPHA * temp + (1.0f - TEMP_EMA_ALPHA) * tempFiltered;
    }
    currentTemp = tempFiltered;
  } else {
    currentTemp = temp;
  }

  float safeLimit = (currentMode == MODE_STEAM) ? MAX_SAFE_TEMP_STEAM : MAX_SAFE_TEMP;
  if (currentTemp > safeLimit) {
    triggerEmergencyStop("Temperature too high: " + String(currentTemp) + "°C");
  } else if (emergencyStop && currentTemp < EMERGENCY_RESET_TEMP) {
    emergencyStop = false;
    emergencyReason = "";
    Serial.println("Emergency cleared: T < " + String(EMERGENCY_RESET_TEMP) + "°C");
  }

  bool inBrew = (brewGetState() != IDLE || manualBrewActive);

  if (heaterStandby)
    pid.setSetpoint(HEATER_STANDBY_SETPOINT);
  else if (currentMode == MODE_STEAM)
    pid.setSetpoint(STEAM_SETPOINT);
  else
    pid.setSetpoint(brewSetpoint);

  float power;
  if (autoTune.active) {
    power = autoTuneUpdate(currentTemp);
  } else if (inBrew) {
    /* Во время пролива PID не используется, нагреватель на 100% */
    power = 100.0f;
  } else {
    /* Базовый PID-выход */
    power = pid.compute(currentTemp);

    /* Landing-фаза: ограничение мощности по % пути к уставке (100% = setpoint). Только при нагреве, не в проливе. */
    if (!heaterStandby) {
      float setpoint = pid.getSetpoint();
      float range = setpoint - APPROACH_T_BASE;
      float progress = (range > 0.1f) ? constrain((currentTemp - APPROACH_T_BASE) / range, 0.0f, 1.0f) : 0.0f;

      if (progress < LANDING_START_PCT) {
        /* Далеко от цели: работаем по PID с верхним ограничением LANDING_CAP_FAR, посадка не активна */
        approachZone = 0;
        power = min(power, LANDING_CAP_FAR);
      } else {
        /* Чем ближе к уставке, тем меньше доступная мощность (плавная линейная функция) */
        approachZone = 1;
        float denom = (1.0f - LANDING_START_PCT);
        float t = (denom > 0.0001f)
                    ? constrain((progress - LANDING_START_PCT) / denom, 0.0f, 1.0f)
                    : 1.0f;
        /* t=0 -> далеко (cap≈LANDING_CAP_FAR), t=1 -> у цели (cap≈LANDING_CAP_3) */
        float maxCap = LANDING_CAP_FAR + (LANDING_CAP_3 - LANDING_CAP_FAR) * t;
        power = min(power, maxCap);
      }
    }
  }

  /* Чуть выше уставки — подогрев только когда T не растёт по инерции (тренд падает или стабилен) */
  float tempRate = 0.0f;
  if (lastTempTrendMs != 0 && (now - lastTempTrendMs) >= TEMP_TREND_MIN_DT_MS)
    tempRate = (currentTemp - lastTempForTrend) / ((now - lastTempTrendMs) / 1000.0f);
  if (!inBrew && !heaterStandby && !autoTune.active && approachZone >= 1 &&
      tempRate <= TEMP_TREND_COOLING_THRESHOLD) {
    float err = pid.getSetpoint() - currentTemp;
    if (err >= -MAINTENANCE_BAND_ABOVE && err < 0.0f)
      power = max(power, MAINTENANCE_HEAT_ABOVE);
  }
  lastTempForTrend = currentTemp;
  lastTempTrendMs = now;

  /* Окно ШИМ длиннее в посадочной фазе — реже включения, меньше перелёт от инерции */
  unsigned long currentWindow = (approachZone >= 1) ? PID_WINDOW_NEAR_MS : pidWindow;
  if (now - windowStart > currentWindow)
    windowStart += currentWindow;

  if (temperatureSimulated()) {
    heaterOn = false;
    digitalWrite(SSR_HEATER_PIN, LOW);
    temperatureSimUpdate(power);
  } else {
    heaterOn = ((power / 100.0f) * currentWindow > (now - windowStart));
    if (emergencyStop || heaterStandby)
      heaterOn = false;
    digitalWrite(SSR_HEATER_PIN, heaterOn ? HIGH : LOW);
  }

  updatePump();
  logTelemetry(power);
}
