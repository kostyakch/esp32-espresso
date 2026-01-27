#pragma once
#include <WebServer.h>
#include "pid.h"
#include "brew_fsm.h"
#include "web_ui.h"

extern void saveSettings();

static WebServer server(8080);

inline void httpSetup() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html; charset=utf-8", renderHomePage());
  });

  server.on("/api/status", HTTP_GET, []() {
    String json =
      "{"
      "\"temp\":" + String(currentTemp,1) + ","
      "\"setpoint\":" + String(pid.getSetpoint(),1) + ","
      "\"state\":\"" + String(brewGetStateName()) + "\","
      "\"phase_ms\":" + String(brewGetPhaseTotalMs()) + ","
      "\"elapsed_ms\":" + String(brewGetElapsedMs()) + ","
      "\"remaining_ms\":" + String(brewGetRemainingMs()) +
      "}";
    server.send(200, "application/json", json);
  });

  server.on("/set", HTTP_POST, []() {
    if (server.hasArg("t")) pid.setSetpoint(server.arg("t").toFloat());
    if (server.hasArg("pre")) preinfusionMs = server.arg("pre").toInt() * 1000;
    if (server.hasArg("pause")) pauseMs = server.arg("pause").toInt() * 1000;
    if (server.hasArg("brew")) brewMs = server.arg("brew").toInt() * 1000;
    saveSettings();

    server.sendHeader("Location", "/");
    server.send(303);
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

  server.begin();
}

inline void httpLoop() {
  server.handleClient();
}
