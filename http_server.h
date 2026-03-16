#pragma once
#include <WebServer.h>
#include "pid.h"
#include "brew_fsm.h"
#include "web_ui.h"

extern void saveSettings();
extern void autoTuneApplySetpoint(float sp);
extern void autoTuneStart();
extern void autoTuneStop();
extern void setModeBrew();
extern void setModeSteam();
extern const char* getModeName();
extern float getBrewSetpoint();
extern float getSteamSetpoint();
extern void setBrewSetpoint(float sp);
extern bool isManualBrewActive();
extern bool getHeaterStandby();
extern void setHeaterStandby(bool standby);
extern float getPumpPowerPercent();
extern void setPumpPowerPercent(float pct);

static WebServer server(8080);

extern bool otaInProgress;
extern bool heaterOn;
inline void httpStop() {
  server.stop();
  Serial.println("HTTP: Server stopped");
}

inline void httpSetup() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html; charset=utf-8", renderHomePage());
  });

  server.on("/api/status", HTTP_GET, []() {
    String json =
      "{"
      "\"temp\":" + String(currentTemp, 1) + ","
      "\"heater\":" + String(heaterOn ? 1 : 0) + ","
      "\"setpoint\":" + String(pid.getSetpoint(),1) + ","
      "\"brew_sp\":" + String(getBrewSetpoint(),1) + ","
      "\"steam_sp\":" + String(getSteamSetpoint(),1) + ","
      "\"mode\":\"" + String(getModeName()) + "\","
      "\"state\":\"" + String(brewGetStateName()) + "\","
      "\"manual\":" + String(isManualBrewActive() ? 1 : 0) + ","
      "\"phase_ms\":" + String(brewGetPhaseTotalMs()) + ","
      "\"elapsed_ms\":" + String(brewGetElapsedMs()) + ","
      "\"remaining_ms\":" + String(brewGetRemainingMs()) + ","
      "\"emergency\":" + String(emergencyStop ? 1 : 0) + ","
      "\"emergency_reason\":\"" + emergencyReason + "\","
      "\"heater_standby\":" + String(getHeaterStandby() ? 1 : 0) + ","
      "\"pump_pct\":" + String(getPumpPowerPercent(), 1) + ""
      "}";
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.send(200, "application/json", json);
  });

  server.on("/set", HTTP_POST, []() {
    if (server.hasArg("t")) setBrewSetpoint(server.arg("t").toFloat());
    if (server.hasArg("pre")) preinfusionMs = server.arg("pre").toInt() * 1000;
    if (server.hasArg("brew")) brewMs = server.arg("brew").toInt() * 1000;
    if (server.hasArg("finish")) finishMs = server.arg("finish").toInt() * 1000;
    if (server.hasArg("pump_pct")) setPumpPowerPercent(server.arg("pump_pct").toFloat());
    saveSettings();

    server.sendHeader("Location", "/");
    server.send(303);
  });

  /* Применить время без перезагрузки (в т.ч. во время пролива — FSM читает значения каждый цикл) */
  server.on("/api/set_times", HTTP_POST, []() {
    if (server.hasArg("pre")) {
      int v = server.arg("pre").toInt();
      if (v >= 0) preinfusionMs = (unsigned long)v * 1000;
    }
    if (server.hasArg("brew")) {
      int v = server.arg("brew").toInt();
      if (v >= 0) brewMs = (unsigned long)v * 1000;
    }
    if (server.hasArg("finish")) {
      int v = server.arg("finish").toInt();
      if (v >= 0) finishMs = (unsigned long)v * 1000;
    }
    if (server.hasArg("pump_pct")) {
      float v = server.arg("pump_pct").toFloat();
      if (v >= 0 && v <= 100) {
        setPumpPowerPercent(v);
        saveSettings();
      }
    }
    server.send(200, "application/json", "{\"ok\":1}");
  });

  server.on("/start", HTTP_POST, []() {
    brewStart();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/stop", HTTP_POST, []() {
    brewStop();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/autotune_start", HTTP_POST, []() {
    autoTuneStart();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/autotune_stop", HTTP_POST, []() {
    autoTuneStop();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/mode_brew", HTTP_POST, []() {
    setModeBrew();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/mode_steam", HTTP_POST, []() {
    setModeSteam();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/heater_off", HTTP_POST, []() {
    setHeaterStandby(true);
    server.sendHeader("Location", "/");
    server.send(303);
  });
  server.on("/heater_on", HTTP_POST, []() {
    setHeaterStandby(false);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.begin();
}

inline void httpLoop() {
  if (otaInProgress) return;
  server.handleClient();
}
