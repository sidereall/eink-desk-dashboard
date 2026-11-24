// Provides Wi-Fi status and the shared HTTPS request interface.

#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

struct WifiStatus {
  bool connected;
  char ip[16];       // e.g. "192.168.1.10"
  char ssid[33];     // 32 characters max, plus the terminator
  char security[12]; // what was negotiated: "WPA3", "WPA2", ...
  int8_t rssi;       // dBm, negative
};

// Sets up the radio and starts the first connection attempt.
// Returns straight away, connected or not.
void wifiBegin();

// Call every loop(). Handles retries and restarts the radio if it gets stuck.
void wifiService();

bool wifiIsConnected();

// Reads the live radio state.
void wifiGetStatus(WifiStatus &out);

// Decode an HTTPS response using the firmware's certificate bundle.
bool wifiGetJson(const char *url, uint16_t timeoutMs, JsonDocument &doc);
