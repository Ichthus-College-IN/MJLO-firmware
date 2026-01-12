#ifndef _CONFIG_H
#define _CONFIG_H

#include <Arduino.h>
#include <RadioLib.h>
#include <Preferences.h>

#include "helpers.h"
#include "config_manager.h"

#define PRINTF(format, ...) \
  do { \
      Serial.printf(format "\r\n", ##__VA_ARGS__); \
  } while (0)

extern bool powerState;
extern bool usbState;
extern bool usbOn;

extern const LoRaWANBand_t *band;

// wake variables
extern RTC_DATA_ATTR uint32_t nextUplink;
extern RTC_DATA_ATTR uint32_t prevUplink;
extern RTC_DATA_ATTR uint32_t uplinkOffset;
extern RTC_DATA_ATTR bool isMotion;
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

struct Cfg2G4 {
  String name;
  String ssid;
  String pass;
  String user;
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

// Compatibility wrapper functions for old code
int doSetting(String &key, String &value);
bool isValidGroupOTAA();
bool isValidGroupABP();
void loadConfig();
String printConfig(int group);
String printFullConfig(bool inclVersion);
String parseError(int errorCode);
int execCommand(String &command);

// ============= Helper for parsing ranges =============
template<typename T>
uint8_t parseRange(String input, uint8_t size, T* array) {
  char buffer[input.length() + 1];
  input.toCharArray(buffer, sizeof(buffer));
  char *token = strtok(buffer, ",");
  int index = 0;
  while (token != nullptr && index < size) {
    array[index] = (T) atoi(token);
    index++;
    token = strtok(nullptr, ",");
  }
  return(index);
}

#endif