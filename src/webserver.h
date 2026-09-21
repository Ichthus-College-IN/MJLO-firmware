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

// Verdict of one association attempt, in the terms the serial protocol reports
// them: the client's next move differs for each, and "it did not work" does
// not tell an operator whether to fix the name or the passphrase.
enum WifiJoinResult {
  WIFI_JOIN_OK = 0,
  WIFI_JOIN_AUTH,        // the access point refused the passphrase
  WIFI_JOIN_NOTFOUND,    // the SSID was not seen on any channel
  WIFI_JOIN_TIMEOUT,     // no association in time, cause unclassified
  WIFI_JOIN_BUSY         // another attempt is already running
};

// One network's credentials, personal or enterprise. `eap` is the flag that
// decides which: EAP_NONE means `pass` is a WPA passphrase (or empty, for an
// open network) and the rest is ignored.
struct WifiCredentials {
  String ssid;
  String pass;       // WPA passphrase, or the 802.1X inner password
  uint8_t eap;       // EapMethod
  String user;       // 802.1X inner identity
  String identity;   // 802.1X outer identity; empty reuses `user`
  uint8_t phase2;    // EapPhase2, EAP-TTLS only
  uint8_t ca;        // EapCa
};

// Associate with one network and wait for an address, without the SoftAP
// fallback connectWiFi() applies. Blocks for up to `timeoutMs`. The
// credentials are the caller's to store: this does not touch the config.
//
// Personal networks are joined WPA3-capable: the station advertises PMF and
// offers both SAE password-derivation methods, so a WPA3-only access point
// associates without anything extra being asked of the operator.
int wifiJoin(const WifiCredentials &cred, uint32_t timeoutMs);

// The same, read out of the stored settings.
WifiCredentials wifiStoredCredentials();

// The passphrase the fallback access point is raised with. Not simply the
// stored one: that field may hold an 802.1X password, which has no minimum
// length and would leave the access point open or refused.
String wifiFallbackPass();

// The raw WIFI_REASON_* code behind the last disconnect, or 0. More specific
// than the verdict above (15 is a 4-way handshake timeout, 201 no AP found,
// 202 an authentication failure) and worth passing through to a client.
uint8_t wifiLastDisconnectReason();

// Housekeeping every path onto a network needs: publish the mDNS name and
// prime the background scan the dashboard's WiFi page reads. Idempotent.
void wifiAfterJoin();

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

// Does this match the current device password? The serial console authenticates
// against the same secret as the dashboard, so that one password (and one
// Security page) covers both ways in.
bool webCheckPassword(const String &pw);

// Call from main loop while WiFi is active (handles post-OTA reboot)
void otaLoop();

#endif
