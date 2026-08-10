// Copyright (C) 2024-2026 StevenCellist (https://github.com/StevenCellist)
// Licensed under GPL-3.0

#ifndef _FSBROWSER_H
#define _FSBROWSER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Update.h>
#include "esp_wifi.h"

extern wifi_mode_t wifiMode;
extern IPAddress IP;

// Set true by ISR (onMotion), cleared and dispatched by main loop
extern volatile bool webAccelPending;

bool connectWiFi();
void disconnectWiFi();

void start_file_browser();
void end_file_browser();

// Feed each raw GNSS byte; buffers lines and fires SSE when '\n' arrives
void sendNmeaByte(char c);

// Send one accelerometer-motion SSE event
void sendAccelEvent();

// Implemented in main.cpp (needs node.getDevAddr())
uint32_t webGetDevAddr();

// Implemented in main.cpp; declared here so display.h can bind them to the
// Connections menu without needing main.cpp's definitions in scope
void wifiEnable(int val = -1);
void wifiDisable(int val = -1);

// Web login recovery: the default password (derived from the chip ID), and
// clearing a custom one so the default applies again
void webDefaultPassword(char *out, size_t len);
void webResetPassword();

// Call from main loop while WiFi is active (handles post-OTA reboot)
void otaLoop();

#endif
