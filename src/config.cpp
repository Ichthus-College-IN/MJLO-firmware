#include <Arduino.h>

#include "config.h"

bool powerState = true;
bool usbState = false;
bool usbOn = true;

const LoRaWANBand_t *band = &EU868;

// wake variables
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

setting_t Settings[NUM_SETTINGS] = {
  { "Version",      "version",      gLW,    "1.1",  setVersion },
  { "Method",       "method",       gLW,    "OTAA", setMethod },
  { "Relay",        "relay",        gLW,    "OFF",  setRelay },
  { "ADR",          "adr",          gLW,    "OFF",  setADR },
  { "DR",           "dr",           gLW,    "5",    setDatarate },
  { "dBm",          "dbm",          gLW,    "16",   setDBm },
  { "Confirmed",    "confirmed",    gLW,    "0",    setConfirmed },

  { "Interval",     "interval",     gUp,    "fixed,30", setInterval },
  { "Sleep",        "sleep",        gUp,    "1",    setSleep },
  { "Operation",    "operation",    gUp,    "mobile,5", setOperation },
  { "Timeout",      "timeout",      gUp,    "120",  setTimeout },

  { "DevEUI",       "deveui",       gOTAA,  "",     setDEUI },
  { "JoinEUI",      "joineui",      gOTAA,  "",     setJEUI },
  { "AppKey",       "appkey",       gOTAA,  "",     setAppK },
  { "NwkKey",       "nwkkey",       gOTAA,  "",     setNwkK },

  { "DevAddr",      "devaddr",      gABP,   "",     setDevA },
  { "AppSKey",      "appskey",      gABP,   "",     setAppS },
  { "NwkSEncKey",   "nwksenckey",   gABP,   "",     setNwkS },
  { "FNwkSIntKey",  "fnwksintkey",  gABP,   "",     setFNwk },
  { "SNwkSIntKey",  "snwksintkey",  gABP,   "",     setSNwk },
  
  { "Name",         "name",         g2G4,   "LRF-1", setName },
  { "SSID",         "ssid",         g2G4,   "LoRangeFinder-1", setSSID },
  { "Pass",         "pass",         g2G4,   "L0R4ngeF1nder", setPass },
  { "User",         "user",         g2G4,   "",      setUser },

  { "Timezone",     "timezone",     gTime,  "60",    setTimezone },
  { "DST",          "dst",          gTime,  "0",     setDST }
};

void saveParameter(String key, String value) {
  bool open = store.begin("config");
  store.putString(key.c_str(), value);
  if(open) {
    store.end();
  }
}

int doSetting(String &key, String &value) {
  key.toLowerCase();

  for(int idx = 0; idx < NUM_SETTINGS; idx++) {
    if(key == Settings[idx].lower) {
      if(value == "")
        value = Settings[idx].deflt;

      int error = Settings[idx].set(value);
      if(!error) {
        saveParameter(key, value);
      }

      return(error);
    }
  }
  return(keyError);
}

bool isValidGroupOTAA() {
  store.begin("config");
  bool valid = true;
  if(store.getString(Settings[cfgDevEUI].lower.c_str()) == "")  valid = false;
  if(store.getString(Settings[cfgJoinEUI].lower.c_str()) == "") valid = false;
  if(store.getString(Settings[cfgAppKey].lower.c_str()) == "")  valid = false;
  if(cfg.actvn.version == v11 && store.getString(Settings[cfgNwkKey].lower.c_str()) == "")  valid = false;
  store.end();
  return(valid);
}

bool isValidGroupABP() {
  store.begin("config");
  bool valid = true;
  if(store.getString(Settings[cfgDevAddr].lower.c_str()) == "")   valid = false;
  if(store.getString(Settings[cfgAppSKey].lower.c_str()) == "")   valid = false;
  if(store.getString(Settings[cfgNwkSEncKey].lower.c_str()) == "")  valid = false;
  if(cfg.actvn.version == v11 && store.getString(Settings[cfgFNwkSIntKey].lower.c_str()) == "") valid = false;
  if(cfg.actvn.version == v11 && store.getString(Settings[cfgSNwkSIntKey].lower.c_str()) == "") valid = false;
  store.end();
  return(valid);
}

int setVersion(String val) {
  val.toLowerCase();

  if(val == "1.0.4" || val == "0") {
    cfg.actvn.version = v104;
    cfg.relay.enabled = false;
  } else if(val == "1.0.4r" || val == "0r") {
    cfg.actvn.version = v104;
    cfg.relay.enabled = true;
  } else if(val == "1.1" || val == "1") {
    cfg.actvn.version = v11;
    cfg.relay.enabled = false;
  } else {
    return(valueError);
  }

  return(noError);
}

int setMethod(String val) {
  val.toUpperCase();

  if(val == "ABP" || val == "0") {
    cfg.actvn.method = ABP;
  } else if (val == "OTAA" || val == "1") {
    cfg.actvn.method = OTAA;
  } else {
    return(valueError);
  }

  return(noError);
}

int setRelay(String val) {
  val.toUpperCase();

  uint8_t mode, smartlevel, backoff;
  if(val == "OFF" || val == "0") {
    cfg.relay.mode = 0;
    cfg.relay.smartLevel = 0;
    cfg.relay.backOff = 0;
  } else if(sscanf(val.c_str(), "%hhu,%hhu,%hhu", &mode, &smartlevel, &backoff) == 3) {
    // success
    cfg.relay.mode = mode;
    cfg.relay.smartLevel = smartlevel;
    cfg.relay.backOff = backoff;
  } else {
    return(valueError);
  }

  return(noError);
}

int setADR(String keyVal) {
  keyVal.toUpperCase();

  if(keyVal == "N" || keyVal == "NO" || keyVal == "OFF" || keyVal == "0") {
    cfg.uplink.adr = ADR_OFF;
  } else if(keyVal == "Y" || keyVal == "YES" || keyVal == "ON" || keyVal == "1") {
    cfg.uplink.adr = ADR_ON;
  } else if(keyVal.startsWith("DR,")) {
    cfg.uplink.adr = ADR_DR;
    
    String val = keyVal.substring(3, keyVal.length());
    if(val == "ODD") {
      cfg.uplink.rangeLen = 3;
      cfg.uplink.range[0] = 5;
      cfg.uplink.range[1] = 3;
      cfg.uplink.range[2] = 1;
    } else
    if(val == "EVEN") {
      cfg.uplink.rangeLen = 3;
      cfg.uplink.range[0] = 4;
      cfg.uplink.range[1] = 2;
      cfg.uplink.range[2] = 0;
    } else {
      uint8_t arr[16];
      uint8_t num = parseRange<uint8_t>(val, 16, arr);
      if(num > 1) {
        cfg.uplink.rangeLen = num;
        memcpy(cfg.uplink.range, arr, num);
      } else {
        return(valueError);
      }
    }

  } else if(keyVal.startsWith("DBM,")) {
    cfg.uplink.adr = ADR_DBM;

    String val = keyVal.substring(4, keyVal.length());
    int8_t arr[16];
    uint8_t num = parseRange<int8_t>(val, 16, arr);
    if(num > 1) {
      cfg.uplink.rangeLen = num;
      memcpy(cfg.uplink.range, arr, num);
    } else {
      return(valueError);
    }


  } else {
    return(valueError);
  }

  return(noError);
}

int setDatarate(String val) {
  val.toUpperCase();

  if(val == "0" || val == "SF12" || val == "SF12BW125") {
    cfg.uplink.dr = 0;
  } else
  if(val == "1" || val == "SF11" || val == "SF11BW125") {
    cfg.uplink.dr = 1;
  } else
  if(val == "2" || val == "SF10" || val == "SF10BW125") {
    cfg.uplink.dr = 2;
  } else
  if(val == "3" || val == "SF9" || val == "SF9BW125") {
    cfg.uplink.dr = 3;
  } else
  if(val == "4" || val == "SF8" || val == "SF8BW125") {
    cfg.uplink.dr = 4;
  } else
  if(val == "5" || val == "SF7" || val == "SF7BW125") {
    cfg.uplink.dr = 5;
  } else
  if(val == "6" || val == "SF7BW250") {
    cfg.uplink.dr = 6;
  } else
  if(val == "7" || val == "FSK") {
    cfg.uplink.dr = 7;
  } else {
    return valueError;
  }
  
  return(noError);
}

int setDBm(String val) {
  int valInt = val.toInt();
  
  // only accept if the maximum Tx power does not exceed the band's allowed value
  if (valInt >= -16 || valInt <= band->powerMax) {
    cfg.uplink.dbm = valInt;
  } else {
    return valueError;
  }
  
  return(noError);
}

int setConfirmed(String val) {
  val.toUpperCase();

  if(val == "N" || val == "NO" || val == "OFF" || val == "0") {
    cfg.uplink.confirmed = false;
  } else if(val == "Y" || val == "YES" || val == "ON" || val == "1") {
    cfg.uplink.confirmed = false;
  } else {
    return(valueError);
  }

  return(noError);
}

int setInterval(String keyVal) {
  keyVal.toLowerCase();

  if(keyVal.startsWith("dc,")) {
    String val = keyVal.substring(3, keyVal.length());
    if(val == "fup") {
      cfg.interval.dutycycle = 30;
    } else if(val == "0.1%") {
      cfg.interval.dutycycle = 86;
    } else if(val == "1%") {
      cfg.interval.dutycycle = 864;
    } else if(val.toInt() > 0 && val.toInt() < 8640) {
      cfg.interval.dutycycle = val.toInt();
    } else {
      return(valueError);
    }
    cfg.interval.fixed = false;
  } else if(keyVal.startsWith("fixed,")) {
    String val = keyVal.substring(6, keyVal.length());
    if(val.toInt() >= 10 && val.toInt() <= 65535) {
      cfg.interval.period = val.toInt();
    } else {
      return(valueError);
    }
    cfg.interval.fixed = true;
  } else {
    return(valueError);
  }

  return(noError);
}

int setSleep(String val) {
  val.toUpperCase();

  if(val == "N" || val == "NO" || val == "OFF" || val == "0") {
    cfg.operation.sleep = false;
  
  } else if(val == "Y" || val == "YES" || val == "ON" || val == "1") {
    cfg.operation.sleep = true;
  
  } else {
    return(valueError);
  }

  return(noError);
}

int setOperation(String keyVal) {
  keyVal.toLowerCase();

  if(keyVal == "stationary" || keyVal == "0") {
    cfg.operation.mobile = false;
  
  } else if(keyVal == "mobile" || keyVal == "1") {
    cfg.operation.mobile = true;
    cfg.operation.uplinks = 1;
    cfg.operation.heartbeat = 86400;
  
  } else if(keyVal.startsWith("mobile,") || keyVal.startsWith("1,")) {
    cfg.operation.mobile = true;

    // TODO parse heartbeat
    String val = keyVal.substring(keyVal.indexOf(",") + 1, keyVal.length());
    if(val.toInt() > 0 && val.toInt() < 256) {
      cfg.operation.uplinks = val.toInt();
    } else {
      return(valueError);
    }

  } else {
    return(valueError);
  }
  
  return(noError);
}

int setTimeout(String val) {
  int valInt = val.toInt();
  if(valInt >= 0 && valInt < 3600) {
    cfg.operation.timeout = valInt;
  
  } else {
    return(valueError);
  }

  return(noError);
}

int setDEUI(String val) {
  if((val.length() != 0 && val.length() != 16) || !isHexString(val))
    return valueError;
  
  if(val.length())
    cfg.actvn.otaa.devEUI = hexStringToUint64(val.c_str());
  
  return(noError);
}

int setJEUI(String val) {
  if((val.length() != 0 && val.length() != 16) || !isHexString(val))
    return valueError;
  if(val.length())
    cfg.actvn.otaa.joinEUI = hexStringToUint64(val.c_str());
  
  return(noError);
}

int setAppK(String val) {
  if((val.length() != 0 && val.length() != 32) || !isHexString(val))
    return valueError;
  if(val.length())
    hexStringToByteArray(val.c_str(), cfg.actvn.otaa.appKey, 32);
  
  return(noError);
}

int setNwkK(String val) {
  if((val.length() != 0 && val.length() != 32) || !isHexString(val))
    return valueError;
  if(val.length())
    hexStringToByteArray(val.c_str(), cfg.actvn.otaa.nwkKey, 32);
  
  return(noError);
}

int setDevA(String val) {
  if((val.length() != 0 && val.length() != 8) || !isHexString(val))
    return valueError;
  if(val.length())
    cfg.actvn.abp.devAddr = hexStringToUint32(val.c_str());
  
  return(noError);
}

int setAppS(String val) {
  if((val.length() != 0 && val.length() != 32) || !isHexString(val))
    return valueError;
  if(val.length())
    hexStringToByteArray(val.c_str(), cfg.actvn.abp.appSKey, 32);
  
  return(noError);
}

int setNwkS(String val) {
  if((val.length() != 0 && val.length() != 32) || !isHexString(val))
    return valueError;
  if(val.length())
    hexStringToByteArray(val.c_str(), cfg.actvn.abp.nwkSEncKey, 32);
  
  return(noError);
}

int setFNwk(String val) {
  if((val.length() != 0 && val.length() != 32) || !isHexString(val))
    return valueError;
  if(val.length())
    hexStringToByteArray(val.c_str(), cfg.actvn.abp.fNwkSIntKey, 32);
  
  return(noError);
}

int setSNwk(String val) {
  if((val.length() != 0 && val.length() != 32) || !isHexString(val))
    return valueError;
  if(val.length())
    hexStringToByteArray(val.c_str(), cfg.actvn.abp.sNwkSIntKey, 32);
  
  return(noError);
}

int setName(String val) {
  if (val.length() < 4 || val.length() > 16)
    return valueError;
  cfg.wl2g4.name = val;

  return(noError);
}

int setSSID(String val) {
  if (val.length() < 1 || val.length() > 32)
    return valueError;
  cfg.wl2g4.ssid = val;

  return(noError);
}

int setPass(String val) {
  if (val.length() < 8 || val.length() > 64)
    return valueError;
  cfg.wl2g4.pass = val;

  return(noError);
}

int setUser(String val) {
  if (val.length() > 64)
    return valueError;
  cfg.wl2g4.user = val;

  return(noError);
}

int setTimezone(String val) {
  val.trim();
  if(val.length() == 0) return valueError;

  int minutes = 0;
  if (val.indexOf(":") >= 0) {
    // format HH:MM or -HH:MM
    int colon = val.indexOf(":");
    int hours = val.substring(0, colon).toInt();
    int mins = val.substring(colon+1).toInt();
    if (hours < 0 || val.startsWith("-")) mins = -abs(mins);
    minutes = hours * 60 + mins;
  } else {
    int v = val.toInt();
    // if user provided a small value assume hours
    if (abs(v) <= 14) minutes = v * 60;
    else minutes = v; // assume minutes
  }

  if (minutes < -720 || minutes > 840) // -12:00 .. +14:00
    return valueError;

  cfg.timezoneMinutes = minutes;
  return(noError);
}

int setDST(String val) {
  val.trim();
  if(val.length() == 0) return valueError;
  int v = val.toInt();
  // accept hours (0/1) or minutes (0/60)
  if (abs(v) <= 12) v = v * 60;
  if (v < 0 || v > 720) return valueError;
  cfg.dstOffsetMinutes = v;
  return(noError);
}

void loadConfig() {
  store.begin("config");
  for (int i = 0; i < NUM_SETTINGS; i++) {
  String key = Settings[i].lower;
  String val = store.getString(key.c_str(), Settings[i].deflt);
  doSetting(key, val);
  }
  store.end();
}

String printConfig(int group) {
  store.begin("config");
  String ret = "";
  for (int i = 0; i < NUM_SETTINGS; i++) {
    if(Settings[i].group == group) {
      ret += ("\r\n+" + Settings[i].pretty + "=" + store.getString(Settings[i].lower.c_str()));
    if(Settings[i].mex && Settings[i].mex()) {
    ret += ("\t -setting ignored (" + Settings[i].error + ")-");
    }
    }
  }
  store.end();
  return ret;
}

String printFullConfig(bool inclVersion) {
  String ret = "";
  if (inclVersion) {
    ret += ("\r\nMJLO by Steven @ Ichthus\r\nFirmware " MJLO_VERSION "\r\nCompiled " __DATE__ "\r\n");
  }
  for(int i = 0; i < NUM_GROUPS; i++) {
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