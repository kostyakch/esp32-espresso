#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiProv.h>
#include <ArduinoOTA.h>

extern void httpStop();

static bool mdnsStarted = false;
static bool provisioningStarted = false;
bool otaInProgress = false;

inline bool connectToWifiSta() {
  Serial.println("WiFi: connecting in STA mode...");

  WiFi.mode(WIFI_STA);
  WiFi.begin();

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi: connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.print("WiFi: connect failed, status: ");
    Serial.println(WiFi.status());
  }

  return WiFi.status() == WL_CONNECTED;
}

inline void startProvisioning() {
  if (provisioningStarted) {
    return;
  }

  const char* serviceName = "espresso-setup";

  Serial.println("Provisioning: BLE start (no security)");
  WiFiProv.beginProvision(
    NETWORK_PROV_SCHEME_BLE,
    NETWORK_PROV_SCHEME_HANDLER_NONE,
    NETWORK_PROV_SECURITY_0,
    nullptr,
    serviceName
  );

  provisioningStarted = true;
}

inline void onWifiEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.print("WiFi: connected, IP: ");
    Serial.println(WiFi.localIP());

    if (provisioningStarted) {
      // WiFiProv.stopProvision(); is not necessary or available in this way
      provisioningStarted = false;
      Serial.println(F("Provisioning: Stopped"));
      // Give some time for the system to settle after provisioning
      delay(500);
    }

    if (!mdnsStarted) {
      bool ok = MDNS.begin("espresso");
      mdnsStarted = ok;
      Serial.print(F("mDNS: "));
      Serial.println(ok ? F("espresso.local") : F("failed"));
    }

    ArduinoOTA.setHostname("espresso");
    ArduinoOTA.setPort(3232);
    ArduinoOTA.setMdnsEnabled(true);
    ArduinoOTA.setRebootOnSuccess(true);
    // Explicitly set the password or remove it to ensure no auth issues
    // ArduinoOTA.setPassword("admin"); 

    ArduinoOTA
      .onStart([]() {
        otaInProgress = true;
        // Stop mDNS to free up UDP sockets and CPU
        MDNS.end(); 
        httpStop(); // Free TCP resources
        
        // Final safety check: ensure outputs are off
        digitalWrite(26, LOW); // SSR_HEATER_PIN
        digitalWrite(27, LOW); // SSR_PUMP_PIN
        
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";
          
        Serial.print(F("OTA: Start updating "));
        Serial.println(type);
        Serial.printf("OTA: Free heap: %u\n", ESP.getFreeHeap());
        Serial.printf("OTA: Flash size: %u\n", ESP.getFlashChipSize());
        Serial.printf("OTA: Sketch space: %u\n", ESP.getFreeSketchSpace());
      })
      .onEnd([]() {
        otaInProgress = false;
        Serial.println("\nOTA: End");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        if (total > 0) {
          Serial.printf("OTA: Progress: %u%%\r", (progress * 100) / total);
        }
      })
      .onError([](ota_error_t error) {
        otaInProgress = false;
        Serial.printf("OTA: Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      });

    static bool otaStarted = false;
    if (!otaStarted) {
      ArduinoOTA.begin();
      otaStarted = true;
      Serial.println("OTA: Ready");
    }
  }
}

inline void wifiSetup() {
  WiFi.onEvent(onWifiEvent);
  WiFi.setSleep(false); // Disable power saving globally for stability

  if (!connectToWifiSta()) {
    startProvisioning();
    Serial.println(F("WiFi: No credentials or connection failed."));
  }
}
