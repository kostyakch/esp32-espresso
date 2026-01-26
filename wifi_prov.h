#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiProv.h>

static bool mdnsStarted = false;
static bool provisioningStarted = false;

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

inline void startBleProvisioning() {
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
    serviceName,
    nullptr
  );

  provisioningStarted = true;
}

inline void onWifiEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.print("WiFi: connected, IP: ");
    Serial.println(WiFi.localIP());

    if (!mdnsStarted) {
      bool ok = MDNS.begin("espresso");
      mdnsStarted = ok;
      Serial.print("mDNS: ");
      Serial.println(ok ? "espresso.local" : "failed");
    }
  }
}

inline void wifiSetup() {
  WiFi.onEvent(onWifiEvent);

  if (!connectToWifiSta()) {
    startBleProvisioning();
  }
}
