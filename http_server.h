#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include "pid.h"
#include "brew_fsm.h"

extern float currentTemp;
extern PIDController pid;
extern unsigned long brewMs;
extern unsigned long preinfusionMs;
extern unsigned long pauseMs;
extern void saveSettings();

static WebServer server(80);

inline void httpSetup() {
  WiFi.softAP("ESP32-ESPRESSO", "12345678");

  server.on("/", HTTP_GET, []() {
    String html =
      "<h1>ESPRESSO</h1>"
      "<p>Temp: " + String(currentTemp,1) + " C</p>"
      "<form action='/set' method='POST'>"
      "Setpoint: <input name='t' value='" + String(pid.getSetpoint(),1) + "'><br>"
      "Preinfusion (s): <input name='pre' value='" + String(preinfusionMs/1000) + "'><br>"
      "Pause (s): <input name='pause' value='" + String(pauseMs/1000) + "'><br>"
      "Brew (s): <input name='brew' value='" + String(brewMs/1000) + "'><br>"
      "<input type='submit' value='SAVE'>"
      "</form>"
      "<br>"
      "<form action='/start' method='POST'><button>START</button></form>"
      "<form action='/stop' method='POST'><button>STOP</button></form>";

    server.send(200, "text/html", html);
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
