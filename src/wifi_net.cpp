// wifi_net.cpp
#include <WiFi.h>
#include <esp_wifi.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "secrets.h"
#include "wifi_net.h"

static uint32_t s_nextAttempt = 0;
static uint32_t s_backoffMs = WIFI_RETRY_MIN_MS; // doubles on each failure
static uint32_t s_offlineSince = 0;

// The only place the credentials are read.
static const char *credSsid() { return WIFI_SSID; }
static const char *credPassword() { return WIFI_PASSWORD; }

// Runs on the Wi-Fi task, not in loop(). Log and set flags only, never draw.
static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    Serial.printf("[wifi] got ip %s\n", WiFi.localIP().toString().c_str());
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:

    // 201 = no AP found, 202 = wrong password, 15 = handshake timeout.
    Serial.printf("[wifi] disconnected, reason %d\n", (int)info.wifi_sta_disconnected.reason);
    break;

  default:
    break;
  }
}

// Called at startup and after a radio restart, which clears these.
static void applyRadioConfig() {
  // Station mode only, so no unasked-for access point is left running.
  WiFi.mode(WIFI_STA);

  // Won't join anything weaker than WPA2, and takes WPA3 where it's offered.
  WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);

  // Keeps the credentials out of the library's own stored settings.
  WiFi.persistent(false);

  // Automatic retries, with wifiService() as the backstop.
  WiFi.setAutoReconnect(true);

  WiFi.setHostname(DEVICE_HOSTNAME);

  // Sleep saves power but adds latency to every web request.
  WiFi.setSleep(false);
}

void wifiBegin() {
  WiFi.onEvent(onWiFiEvent);
  applyRadioConfig();

  Serial.printf("[wifi] connecting to %s\n", credSsid());
  WiFi.begin(credSsid(), credPassword()); // returns immediately

  s_nextAttempt = millis() + s_backoffMs;
  s_offlineSince = millis();
}

bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }

// Retries with a growing delay, and restarts the radio if that isn't working.
void wifiService() {
  if (wifiIsConnected()) {
    s_backoffMs = WIFI_RETRY_MIN_MS; // reset the ladder
    s_offlineSince = 0;
    return;
  }

  if (s_offlineSince == 0)
    s_offlineSince = millis();
  if (millis() < s_nextAttempt)
    return;

  // Down this long means begin() isn't getting anywhere. Restart the radio.
  if (millis() - s_offlineSince > WIFI_STACK_RESET_MS) {
    Serial.println("[wifi] offline too long, bouncing the radio");
    WiFi.disconnect(/*wifioff=*/true, /*eraseap=*/false);
    WiFi.mode(WIFI_OFF);
    delay(100);
    applyRadioConfig();
    s_offlineSince = millis();
  }

  Serial.printf("[wifi] retry (next attempt in %lus)\n", (unsigned long)(s_backoffMs / 1000));
  WiFi.begin(credSsid(), credPassword());

  s_nextAttempt = millis() + s_backoffMs;
  s_backoffMs = (s_backoffMs * 2 > WIFI_RETRY_MAX_MS) ? WIFI_RETRY_MAX_MS : s_backoffMs * 2;
}

static const char *authModeName(wifi_auth_mode_t m) {
  switch (m) {
  case WIFI_AUTH_OPEN:
    return "OPEN";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA/WPA2";
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2/WPA3";
  default:
    return "?";
  }
}

void wifiGetStatus(WifiStatus &out) {
  memset(&out, 0, sizeof(out));
  out.connected = wifiIsConnected();

  if (!out.connected) {
    snprintf(out.ip, sizeof(out.ip), "%s", "0.0.0.0");
    snprintf(out.ssid, sizeof(out.ssid), "%s", credSsid());
    snprintf(out.security, sizeof(out.security), "%s", "-");
    out.rssi = 0;
    return;
  }

  snprintf(out.ip, sizeof(out.ip), "%s", WiFi.localIP().toString().c_str());
  snprintf(out.ssid, sizeof(out.ssid), "%s", WiFi.SSID().c_str());
  out.rssi = (int8_t)WiFi.RSSI();

  // The mode actually negotiated (WPA2/WPA3).
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    snprintf(out.security, sizeof(out.security), "%s", authModeName(ap.authmode));
  } else {
    snprintf(out.security, sizeof(out.security), "%s", "?");
  }
}
