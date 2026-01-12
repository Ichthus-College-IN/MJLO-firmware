#include <Arduino.h>
#include "config.h"
#include "config_manager.h"

// ============= Global Variables =============
bool powerState = true;
bool usbState = false;
bool usbOn = true;

const LoRaWANBand_t *band = &EU868;

// Wake variables
uint32_t nextUplink = 0;
uint32_t prevUplink = 0;
uint32_t uplinkOffset = 0;
bool isMotion = false;
bool wasDownlink = false;
float rssi;
float snr;
esp_sleep_wakeup_cause_t wakeup_reason;

Preferences store;
Config cfg;

// ============= Settings Metadata =============
// Define all settings with their metadata (name, group, default, validator)
const SettingMetadata settingsMetadata[] = {
  // LoRaWAN Settings
  { "version",      "Version",       GROUP_LORAWAN, "1.1",            validateVersion },
  { "method",       "Method",        GROUP_LORAWAN, "OTAA",           validateMethod },
  { "relay",        "Relay",         GROUP_LORAWAN, "OFF",            validateRelay },
  
  // Uplink Settings
  { "adr",          "ADR",           GROUP_UPLINK,  "OFF",            validateADR },
  { "dr",           "DR",            GROUP_UPLINK,  "5",              validateDataRate },
  { "dbm",          "dBm",           GROUP_UPLINK,  "16",             validateDBm },
  { "confirmed",    "Confirmed",     GROUP_UPLINK,  "0",              validateBoolean },
  { "interval",     "Interval",      GROUP_UPLINK,  "fixed,30",       validateInterval },
  { "sleep",        "Sleep",         GROUP_UPLINK,  "1",              validateBoolean },
  { "operation",    "Operation",     GROUP_UPLINK,  "mobile,5",       validateOperation },
  { "timeout",      "Timeout",       GROUP_UPLINK,  "120",            validateTimeout },
  
  // OTAA Activation
  { "deveui",       "DevEUI",        GROUP_ACTIVATION_OTAA, "",       validateHex16 },
  { "joineui",      "JoinEUI",       GROUP_ACTIVATION_OTAA, "",       validateHex16 },
  { "appkey",       "AppKey",        GROUP_ACTIVATION_OTAA, "",       validateHex32 },
  { "nwkkey",       "NwkKey",        GROUP_ACTIVATION_OTAA, "",       validateHex32 },
  
  // ABP Activation
  { "devaddr",      "DevAddr",       GROUP_ACTIVATION_ABP,  "",       validateHex8 },
  { "appskey",      "AppSKey",       GROUP_ACTIVATION_ABP,  "",       validateHex32 },
  { "nwksenckey",   "NwkSEncKey",    GROUP_ACTIVATION_ABP,  "",       validateHex32 },
  { "fnwksintkey",  "FNwkSIntKey",   GROUP_ACTIVATION_ABP,  "",       validateHex32 },
  { "snwksintkey",  "SNwkSIntKey",   GROUP_ACTIVATION_ABP,  "",       validateHex32 },
  
  // 2.4GHz WiFi Settings
  { "name",         "Name",          GROUP_WIFI_2G4,        "LRF-1",  validateName },
  { "ssid",         "SSID",          GROUP_WIFI_2G4,        "LoRangeFinder-1", validateSSID },
  { "pass",         "Pass",          GROUP_WIFI_2G4,        "L0R4ngeF1nder",   validatePassword },
  { "user",         "User",          GROUP_WIFI_2G4,        "",       validateUser },
  
  // Time Settings
  { "timezone",     "Timezone",      GROUP_TIME,            "60",     validateTimezone },
  { "dst",          "DST",           GROUP_TIME,            "0",      validateDST },
};

const uint16_t NUM_SETTINGS_METADATA = sizeof(settingsMetadata) / sizeof(SettingMetadata);

// Global ConfigManager instance
ConfigManager configMgr(settingsMetadata, NUM_SETTINGS_METADATA);

// ============= Helper Functions =============

// Apply a setting value to the cfg structure
void applySetting(const char* key, const String& value) {
  String v = value;
  
  // LoRaWAN Settings
  if (strcmp(key, "version") == 0) {
    v.toLowerCase();
    if (v == "1.0.4" || v == "0") {
      cfg.actvn.version = v104;
      cfg.relay.enabled = false;
    } else if (v == "1.0.4r" || v == "0r") {
      cfg.actvn.version = v104;
      cfg.relay.enabled = true;
    } else if (v == "1.1" || v == "1") {
      cfg.actvn.version = v11;
      cfg.relay.enabled = false;
    }
  } 
  else if (strcmp(key, "method") == 0) {
    v.toUpperCase();
    cfg.actvn.method = (v == "ABP" || v == "0") ? ABP : OTAA;
  }
  else if (strcmp(key, "relay") == 0) {
    v.toUpperCase();
    if (v == "OFF" || v == "0") {
      cfg.relay.mode = 0;
      cfg.relay.smartLevel = 0;
      cfg.relay.backOff = 0;
    } else {
      uint8_t mode, smartlevel, backoff;
      if (sscanf(v.c_str(), "%hhu,%hhu,%hhu", &mode, &smartlevel, &backoff) == 3) {
        cfg.relay.mode = mode;
        cfg.relay.smartLevel = smartlevel;
        cfg.relay.backOff = backoff;
      }
    }
  }
  // Uplink Settings
  else if (strcmp(key, "adr") == 0) {
    v.toUpperCase();
    if (v == "N" || v == "NO" || v == "OFF" || v == "0") {
      cfg.uplink.adr = ADR_OFF;
    } else if (v == "Y" || v == "YES" || v == "ON" || v == "1") {
      cfg.uplink.adr = ADR_ON;
    } else if (v.startsWith("DR,")) {
      cfg.uplink.adr = ADR_DR;
      String val = v.substring(3);
      if (val == "ODD") {
        cfg.uplink.rangeLen = 3;
        cfg.uplink.range[0] = 5;
        cfg.uplink.range[1] = 3;
        cfg.uplink.range[2] = 1;
      } else if (val == "EVEN") {
        cfg.uplink.rangeLen = 3;
        cfg.uplink.range[0] = 4;
        cfg.uplink.range[1] = 2;
        cfg.uplink.range[2] = 0;
      } else {
        uint8_t arr[16];
        uint8_t num = parseRange<uint8_t>(val, 16, arr);
        if (num > 1) {
          cfg.uplink.rangeLen = num;
          memcpy(cfg.uplink.range, arr, num);
        }
      }
    } else if (v.startsWith("DBM,")) {
      cfg.uplink.adr = ADR_DBM;
      String val = v.substring(4);
      int8_t arr[16];
      uint8_t num = parseRange<int8_t>(val, 16, arr);
      if (num > 1) {
        cfg.uplink.rangeLen = num;
        memcpy(cfg.uplink.range, arr, num);
      }
    }
  }
  else if (strcmp(key, "dr") == 0) {
    v.toUpperCase();
    uint8_t dr = 5;
    if (v == "0" || v == "SF12" || v == "SF12BW125") dr = 0;
    else if (v == "1" || v == "SF11" || v == "SF11BW125") dr = 1;
    else if (v == "2" || v == "SF10" || v == "SF10BW125") dr = 2;
    else if (v == "3" || v == "SF9" || v == "SF9BW125") dr = 3;
    else if (v == "4" || v == "SF8" || v == "SF8BW125") dr = 4;
    else if (v == "5" || v == "SF7" || v == "SF7BW125") dr = 5;
    else if (v == "6" || v == "SF7BW250") dr = 6;
    else if (v == "7" || v == "FSK") dr = 7;
    cfg.uplink.dr = dr;
  }
  else if (strcmp(key, "dbm") == 0) {
    cfg.uplink.dbm = (int8_t)v.toInt();
  }
  else if (strcmp(key, "confirmed") == 0) {
    v.toUpperCase();
    cfg.uplink.confirmed = (v == "Y" || v == "YES" || v == "ON" || v == "1");
  }
  else if (strcmp(key, "interval") == 0) {
    v.toLowerCase();
    if (v.startsWith("dc,")) {
      String val = v.substring(3);
      if (val == "fup") {
        cfg.interval.dutycycle = 30;
      } else if (val == "0.1%") {
        cfg.interval.dutycycle = 86;
      } else if (val == "1%") {
        cfg.interval.dutycycle = 864;
      } else {
        cfg.interval.dutycycle = val.toInt();
      }
      cfg.interval.fixed = false;
    } else if (v.startsWith("fixed,")) {
      String val = v.substring(6);
      cfg.interval.period = val.toInt();
      cfg.interval.fixed = true;
    }
  }
  else if (strcmp(key, "sleep") == 0) {
    v.toUpperCase();
    cfg.operation.sleep = (v == "Y" || v == "YES" || v == "ON" || v == "1");
  }
  else if (strcmp(key, "operation") == 0) {
    v.toLowerCase();
    if (v == "stationary" || v == "0") {
      cfg.operation.mobile = false;
    } else if (v == "mobile" || v == "1") {
      cfg.operation.mobile = true;
      cfg.operation.uplinks = 1;
      cfg.operation.heartbeat = 86400;
    } else if (v.startsWith("mobile,") || v.startsWith("1,")) {
      cfg.operation.mobile = true;
      String val = v.substring(v.indexOf(",") + 1);
      cfg.operation.uplinks = val.toInt();
    }
  }
  else if (strcmp(key, "timeout") == 0) {
    cfg.operation.timeout = (uint16_t)v.toInt();
  }
  // OTAA Activation
  else if (strcmp(key, "deveui") == 0) {
    if (v.length() > 0) cfg.actvn.otaa.devEUI = hexStringToUint64(v.c_str());
  }
  else if (strcmp(key, "joineui") == 0) {
    if (v.length() > 0) cfg.actvn.otaa.joinEUI = hexStringToUint64(v.c_str());
  }
  else if (strcmp(key, "appkey") == 0) {
    if (v.length() > 0) hexStringToByteArray(v.c_str(), cfg.actvn.otaa.appKey, 32);
  }
  else if (strcmp(key, "nwkkey") == 0) {
    if (v.length() > 0) hexStringToByteArray(v.c_str(), cfg.actvn.otaa.nwkKey, 32);
  }
  // ABP Activation
  else if (strcmp(key, "devaddr") == 0) {
    if (v.length() > 0) cfg.actvn.abp.devAddr = hexStringToUint32(v.c_str());
  }
  else if (strcmp(key, "appskey") == 0) {
    if (v.length() > 0) hexStringToByteArray(v.c_str(), cfg.actvn.abp.appSKey, 32);
  }
  else if (strcmp(key, "nwksenckey") == 0) {
    if (v.length() > 0) hexStringToByteArray(v.c_str(), cfg.actvn.abp.nwkSEncKey, 32);
  }
  else if (strcmp(key, "fnwksintkey") == 0) {
    if (v.length() > 0) hexStringToByteArray(v.c_str(), cfg.actvn.abp.fNwkSIntKey, 32);
  }
  else if (strcmp(key, "snwksintkey") == 0) {
    if (v.length() > 0) hexStringToByteArray(v.c_str(), cfg.actvn.abp.sNwkSIntKey, 32);
  }
  // WiFi Settings
  else if (strcmp(key, "name") == 0) {
    cfg.wl2g4.name = v;
  }
  else if (strcmp(key, "ssid") == 0) {
    cfg.wl2g4.ssid = v;
  }
  else if (strcmp(key, "pass") == 0) {
    cfg.wl2g4.pass = v;
  }
  else if (strcmp(key, "user") == 0) {
    cfg.wl2g4.user = v;
  }
  // Time Settings
  else if (strcmp(key, "timezone") == 0) {
    v.trim();
    int minutes = 0;
    if (v.indexOf(":") >= 0) {
      int colon = v.indexOf(":");
      int hours = v.substring(0, colon).toInt();
      int mins = v.substring(colon + 1).toInt();
      if (hours < 0 || v.startsWith("-")) mins = -abs(mins);
      minutes = hours * 60 + mins;
    } else {
      int vInt = v.toInt();
      if (abs(vInt) <= 14) minutes = vInt * 60;
      else minutes = vInt;
    }
    cfg.timezoneMinutes = minutes;
  }
  else if (strcmp(key, "dst") == 0) {
    v.trim();
    int vInt = v.toInt();
    if (abs(vInt) <= 12) vInt = vInt * 60;
    cfg.dstOffsetMinutes = vInt;
  }
}

// ============= Compatibility Wrappers =============

int doSetting(String &key, String &value) {
  key.toLowerCase();
  if (value == "") {
    // Find default value
    const SettingMetadata* meta = configMgr.getMetadata(key.c_str());
    if (meta) value = String(meta->defaultValue);
  }
  
  int error = configMgr.set(key.c_str(), value);
  if (error == noError) {
    applySetting(key.c_str(), value);
  }
  return error;
}

bool isValidGroupOTAA() {
  return (configMgr.get("deveui").length() > 0 &&
          configMgr.get("joineui").length() > 0 &&
          configMgr.get("appkey").length() > 0 &&
          (cfg.actvn.version != v11 || configMgr.get("nwkkey").length() > 0));
}

bool isValidGroupABP() {
  return (configMgr.get("devaddr").length() > 0 &&
          configMgr.get("appskey").length() > 0 &&
          configMgr.get("nwksenckey").length() > 0 &&
          (cfg.actvn.version != v11 || configMgr.get("fnwksintkey").length() > 0) &&
          (cfg.actvn.version != v11 || configMgr.get("snwksintkey").length() > 0));
}

void loadConfig() {
  configMgr.load();
  
  // Apply all loaded settings to cfg structure
  for (uint16_t i = 0; i < NUM_SETTINGS_METADATA; i++) {
    String value = configMgr.getByIndex(i);
    applySetting(settingsMetadata[i].key, value);
  }
}

String printConfig(int group) {
  return configMgr.printSettings(group);
}

String printFullConfig(bool inclVersion) {
  String ret = "";
  if (inclVersion) {
    ret += ("\r\nMJLO by Steven @ Ichthus\r\nFirmware " MJLO_VERSION "\r\nCompiled " __DATE__ "\r\n");
  }
  for (int i = 0; i < GROUP_COUNT; i++) {
    ret += printConfig(i);
  }
  return ret;
}

String parseError(int errorCode) {
  switch (errorCode) {
    case noError:
      return "OK";
    case lengthError:
      return "Exceeded the maximum length of 64 characters";
    case formatError:
      return "The command was not formatted as expected";
    case commandError:
      return "The specified command was not recognized";
    case keyError:
      return "There was no setting matching the specified key";
    case valueError:
      return "The specified value was not formatted correctly";
    case busyError:
      return "Uplink in progress - could not configure; try again later";
    default:
      return "An unknown error occured";
  }
}
