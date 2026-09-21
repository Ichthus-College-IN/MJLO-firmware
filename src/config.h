#ifndef _CONFIG_H
#define _CONFIG_H

#include <Arduino.h>
#include <RadioLib.h>
#include <Preferences.h>

#include "helpers.h"
#include "config_manager.h"
#include "sercmd.h"

// The firmware's own chatter, and the one thing the LOG command can quieten.
// Output written straight to Serial elsewhere is unaffected, which is why the
// serial protocol has clients ignore every line that does not start with '@'
// rather than promising a silent port.
#define PRINTF(format, ...) \
  do { \
      if (serialLogLevel >= SERLOG_INFO) Serial.printf(format "\r\n", ##__VA_ARGS__); \
  } while (0)

extern bool powerState;
extern bool usbState;
extern bool usbOn;

extern const LoRaWANBand_t *band;

// wake variables
extern RTC_DATA_ATTR uint32_t nextUplink;
extern RTC_DATA_ATTR uint32_t prevUplink;
extern RTC_DATA_ATTR uint32_t uplinkOffset;
extern RTC_DATA_ATTR volatile bool isMotion;
extern RTC_DATA_ATTR bool wasDownlink;
extern RTC_DATA_ATTR float rssi;
extern RTC_DATA_ATTR float snr;
extern esp_sleep_wakeup_cause_t wakeup_reason;

extern Preferences store;

enum ActivationMethod {
  ABP,
  OTAA
};
enum ActivationVersion {
  v104,
  v11
};
enum UplinkADR {
  ADR_OFF,
  ADR_ON,
  ADR_DR,
  ADR_DBM
};

struct KeysOTAA {
  uint64_t devEUI;
  uint64_t joinEUI;
  uint8_t appKey[16];
  uint8_t nwkKey[16];
};

struct KeysABP {
  uint32_t devAddr;
  uint8_t appSKey[16];
  uint8_t nwkSEncKey[16];
  uint8_t fNwkSIntKey[16];
  uint8_t sNwkSIntKey[16];
};

struct CfgActivation {
  bool version = v11;
  bool method = OTAA;
  KeysOTAA otaa;
  KeysABP abp;
};

struct CfgRelay {
  bool enabled = false;
  uint8_t mode = 0;
  uint8_t smartLevel = 0;
  uint8_t backOff = 0;
};

struct CfgUplink {
  uint8_t dr = 5;     // 0 - 15
  int8_t dbm = 16;     // -31 - +31
  uint8_t adr = false;    // off, on, sf, dbm
  uint8_t range[16];
  uint8_t rangeLen = 0;
  bool confirmed = false; // off or on
};

struct CfgInterval {
  bool fixed = true;
  uint32_t period = 60;
  uint32_t dutycycle = 864;
};

struct CfgOperation {
  bool sleep = false;     // off or on
  bool mobile = true;    // stationary or mobile
  uint8_t uplinks = 5;// number of uplinks when no motion in mobile mode
  uint32_t heartbeat = 600;
  uint16_t timeout = 120;
};

// 802.1X outer method. EAP_NONE is what makes a network a PSK network: it is
// the single flag everything branches on, rather than "is the username set",
// which could not tell an enterprise network from a leftover field.
enum EapMethod {
  EAP_NONE = 0,
  EAP_PEAP = 1,
  EAP_TTLS = 2
};

// Inner method, EAP-TTLS only. PEAP always uses MSCHAPv2.
enum EapPhase2 {
  P2_MSCHAPV2 = 0,
  P2_MSCHAP = 1,
  P2_PAP = 2,
  P2_CHAP = 3,
  P2_EAP = 4
};

// Server certificate validation. There is no private-CA option: a PEM does not
// fit through a line-oriented console, so it is the built-in bundle or nothing.
enum EapCa {
  CA_NONE = 0,
  CA_BUNDLE = 1
};

struct Cfg2G4 {
  String name;
  String ssid;
  // The PSK on a personal network, the inner 802.1X password on an enterprise
  // one. One field, because a box is on one network at a time and two password
  // boxes on the dashboard would only invite putting the wrong one in each.
  String pass;
  String user;       // inner identity (username), 802.1X only
  String identity;   // outer identity; empty means "use `user`"
  uint8_t eap = EAP_NONE;
  uint8_t phase2 = P2_MSCHAPV2;
  uint8_t ca = CA_NONE;
  // Bring WiFi (and the dashboard) up on its own at boot. Off by default:
  // WiFi costs the radio, 240 MHz and the async server, which a battery unit
  // should not pay unasked. Serial provisioning turns it on, because someone
  // who just typed credentials over the cable plainly wants the link.
  bool autoStart = false;
};

struct Config {
  CfgActivation actvn;
  CfgRelay relay;
  CfgUplink uplink;
  CfgInterval interval;
  CfgOperation operation;
  Cfg2G4 wl2g4;
  int16_t timezoneMinutes = 0; // minutes offset from UTC (e.g. +60)
  int16_t dstOffsetMinutes = 0; // summer time offset in minutes (usually 60 or 0)
};

extern Config cfg;

// Settings metadata and count (defined in config.cpp)
extern const SettingMetadata settingsMetadata[];
extern const uint16_t NUM_SETTINGS_METADATA;

// Apply one setting by key; `value` empty means "restore the default".
// `key` is matched case-insensitively.
int doSetting(String &key, String &value);

bool isValidGroupOTAA();
bool isValidGroupABP();
void loadConfig();
String printConfig(int group);
String printFullConfig(bool inclVersion);
String parseError(int errorCode);

// The legacy '+' console. Implemented in main.cpp, because most of what it
// does is reach into the device state machine.
int execCommand(String &command);

// ============= Helper for parsing ranges =============
// Splits "1,2,3" into `array`, returning the number of entries written, or 0
// when the input is not a clean comma-separated list of numbers. The strictness
// matters: these lists reach the radio, and a half-parsed one used to be
// accepted by the validator and then quietly dropped on the way to `cfg`.
template<typename T>
uint8_t parseRange(const String &input, uint8_t size, T *array) {
  uint8_t index = 0;
  int start = 0;
  while (start <= (int)input.length() && index < size) {
    int comma = input.indexOf(',', start);
    String token = (comma < 0) ? input.substring(start) : input.substring(start, comma);
    token.trim();
    if (token.length() == 0) return 0;
    for (uint16_t i = 0; i < token.length(); i++) {
      if (i == 0 && (token[i] == '-' || token[i] == '+') && token.length() > 1) continue;
      if (!isDigit(token[i])) return 0;
    }
    array[index++] = (T)token.toInt();
    if (comma < 0) break;
    start = comma + 1;
  }
  return index;
}

#endif
