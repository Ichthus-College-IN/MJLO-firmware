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
volatile bool isMotion = false;
bool wasDownlink = false;
float rssi;
float snr;
esp_sleep_wakeup_cause_t wakeup_reason;

Preferences store;
Config cfg;

// ============= Setting parsers =============
//
// One function per setting. Each validates its argument and, when `out` is
// given, writes the parsed result into it. Nothing may touch `out` before the
// value is known to be good: a half-applied setting is worse than a rejected
// one, since the caller is told it failed while `cfg` has already moved.
//
// The old code split this in two - a validateX() that decided yes/no and a
// branch of applySetting() that did the real parsing - which meant every
// accepted format was written out twice and the two could disagree. They did:
// "ADR=DR,nonsense" passed validation and then applied nothing at all.

// Small helpers shared by several parsers. Callers upper-case first.
static bool isTruthy(const String &v) {
  return v == "Y" || v == "YES" || v == "ON" || v == "1" || v == "TRUE";
}
static bool isFalsy(const String &v) {
  return v == "N" || v == "NO" || v == "OFF" || v == "0" || v == "FALSE";
}

// Whole number, optionally signed, with nothing else in the string. toInt()
// answers 0 for "abc" and for "0" alike, so it cannot be used to validate.
static bool parseInt(const String &in, long &out) {
  String v = in;
  v.trim();
  if (v.length() == 0) return false;
  uint16_t i = (v[0] == '-' || v[0] == '+') ? 1 : 0;
  if (i >= v.length()) return false;
  for (; i < v.length(); i++) {
    if (!isDigit(v[i])) return false;
  }
  out = v.toInt();
  return true;
}

// An empty key is legal and means "not provisioned".
static int parseHex(const String &value, uint16_t chars) {
  if (value.length() == 0) return noError;
  if (value.length() != chars) return valueError;
  if (!isHexString(value)) return valueError;
  return noError;
}

// ---- LoRaWAN ----

// The specification version, and with it whether the relay extension is armed;
// "1.0.4r" is 1.0.4 with relay support, which is why one setting writes two
// fields.
static int pVersion(const String &value, Config *out) {
  String v = value;
  v.toLowerCase();
  bool version, relay;
  if (v == "1.0.4" || v == "0")        { version = v104; relay = false; }
  else if (v == "1.0.4r" || v == "0r") { version = v104; relay = true;  }
  else if (v == "1.1" || v == "1")     { version = v11;  relay = false; }
  else return valueError;

  if (out) {
    out->actvn.version = version;
    out->relay.enabled = relay;
  }
  return noError;
}

static int pMethod(const String &value, Config *out) {
  String v = value;
  v.toUpperCase();
  bool method;
  if (v == "ABP" || v == "0")       method = ABP;
  else if (v == "OTAA" || v == "1") method = OTAA;
  else return valueError;

  if (out) out->actvn.method = method;
  return noError;
}

// "OFF", or "<mode>,<smartLevel>,<backOff>".
static int pRelay(const String &value, Config *out) {
  String v = value;
  v.toUpperCase();
  uint8_t mode = 0, smartLevel = 0, backOff = 0;

  if (v != "OFF" && v != "0") {
    uint8_t arr[3];
    if (parseRange<uint8_t>(v, 3, arr) != 3) return valueError;
    mode = arr[0];
    smartLevel = arr[1];
    backOff = arr[2];
  }

  if (out) {
    out->relay.mode = mode;
    out->relay.smartLevel = smartLevel;
    out->relay.backOff = backOff;
  }
  return noError;
}

// ---- Uplink ----

// Off, on, or a rotation the device walks through itself: "DR,5,3,1" (or the
// ODD/EVEN shorthands) and "DBM,16,10,4". A rotation needs at least two
// entries - a one-entry rotation is a fixed setting, which is what +dr is for.
static int pADR(const String &value, Config *out) {
  String v = value;
  v.toUpperCase();

  if (isFalsy(v)) {
    if (out) out->uplink.adr = ADR_OFF;
    return noError;
  }
  if (isTruthy(v)) {
    if (out) out->uplink.adr = ADR_ON;
    return noError;
  }

  uint8_t range[16];
  uint8_t len = 0;
  uint8_t adr;

  if (v.startsWith("DR,")) {
    adr = ADR_DR;
    String list = v.substring(3);
    if (list == "ODD")       { len = 3; range[0] = 5; range[1] = 3; range[2] = 1; }
    else if (list == "EVEN") { len = 3; range[0] = 4; range[1] = 2; range[2] = 0; }
    else {
      len = parseRange<uint8_t>(list, 16, range);
      for (uint8_t i = 0; i < len; i++) {
        if (range[i] > 7) return valueError;
      }
    }
  } else if (v.startsWith("DBM,")) {
    adr = ADR_DBM;
    int8_t signedRange[16];
    len = parseRange<int8_t>(v.substring(4), 16, signedRange);
    for (uint8_t i = 0; i < len; i++) {
      if (signedRange[i] < -16 || signedRange[i] > 16) return valueError;
    }
    memcpy(range, signedRange, len);
  } else {
    return valueError;
  }

  if (len < 2) return valueError;

  if (out) {
    out->uplink.adr = adr;
    out->uplink.rangeLen = len;
    memcpy(out->uplink.range, range, len);
  }
  return noError;
}

static int pDataRate(const String &value, Config *out) {
  String v = value;
  v.toUpperCase();
  uint8_t dr;
  if      (v == "0" || v == "SF12" || v == "SF12BW125") dr = 0;
  else if (v == "1" || v == "SF11" || v == "SF11BW125") dr = 1;
  else if (v == "2" || v == "SF10" || v == "SF10BW125") dr = 2;
  else if (v == "3" || v == "SF9"  || v == "SF9BW125")  dr = 3;
  else if (v == "4" || v == "SF8"  || v == "SF8BW125")  dr = 4;
  else if (v == "5" || v == "SF7"  || v == "SF7BW125")  dr = 5;
  else if (v == "6" || v == "SF7BW250")                 dr = 6;
  else if (v == "7" || v == "FSK")                      dr = 7;
  else return valueError;

  if (out) out->uplink.dr = dr;
  return noError;
}

// EU868 tops out at +16 dBm ERP. The bound is hard-coded because `band` is,
// too; it moves when the band table becomes a setting.
static int pDBm(const String &value, Config *out) {
  long dbm;
  if (!parseInt(value, dbm)) return valueError;
  if (dbm < -16 || dbm > 16) return valueError;

  if (out) out->uplink.dbm = (int8_t)dbm;
  return noError;
}

static int pConfirmed(const String &value, Config *out) {
  String v = value;
  v.toUpperCase();
  if (!isTruthy(v) && !isFalsy(v)) return valueError;

  if (out) out->uplink.confirmed = isTruthy(v);
  return noError;
}

static int pSleep(const String &value, Config *out) {
  String v = value;
  v.toUpperCase();
  if (!isTruthy(v) && !isFalsy(v)) return valueError;

  if (out) out->operation.sleep = isTruthy(v);
  return noError;
}

// "fixed,<seconds>" for a plain period, or "dc,<n>" to derive the period from
// an airtime budget. The named budgets are the fair-use policy (30 s/day),
// 0.1% and 1%.
static int pInterval(const String &value, Config *out) {
  String v = value;
  v.toLowerCase();

  if (v.startsWith("dc,")) {
    String list = v.substring(3);
    uint32_t dutycycle;
    if (list == "fup")       dutycycle = 30;
    else if (list == "0.1%") dutycycle = 86;
    else if (list == "1%")   dutycycle = 864;
    else {
      long n;
      if (!parseInt(list, n) || n <= 0 || n >= 8640) return valueError;
      dutycycle = (uint32_t)n;
    }
    if (out) {
      out->interval.dutycycle = dutycycle;
      out->interval.fixed = false;
    }
    return noError;
  }

  if (v.startsWith("fixed,")) {
    long period;
    if (!parseInt(v.substring(6), period)) return valueError;
    if (period < 10 || period > 65535) return valueError;
    if (out) {
      out->interval.period = (uint32_t)period;
      out->interval.fixed = true;
    }
    return noError;
  }

  return valueError;
}

// "stationary", or "mobile[,<uplinks>]": how many uplinks to send after motion
// stops before falling back to the heartbeat.
static int pOperation(const String &value, Config *out) {
  String v = value;
  v.toLowerCase();

  if (v == "stationary" || v == "0") {
    if (out) out->operation.mobile = false;
    return noError;
  }
  if (v == "mobile" || v == "1") {
    if (out) {
      out->operation.mobile = true;
      out->operation.uplinks = 1;
      out->operation.heartbeat = 86400;
    }
    return noError;
  }
  if (v.startsWith("mobile,") || v.startsWith("1,")) {
    long uplinks;
    if (!parseInt(v.substring(v.indexOf(',') + 1), uplinks)) return valueError;
    if (uplinks <= 0 || uplinks > 255) return valueError;
    if (out) {
      out->operation.mobile = true;
      out->operation.uplinks = (uint8_t)uplinks;
    }
    return noError;
  }

  return valueError;
}

static int pTimeout(const String &value, Config *out) {
  long timeout;
  if (!parseInt(value, timeout)) return valueError;
  if (timeout < 0 || timeout > 3600) return valueError;

  if (out) out->operation.timeout = (uint16_t)timeout;
  return noError;
}

// ---- Activation keys ----
//
// Each key may be empty; isValidGroupOTAA() and isValidGroupABP() decide
// whether the set is complete enough to attempt a join.

static int pDevEUI(const String &value, Config *out) {
  int error = parseHex(value, 16);
  if (error != noError) return error;
  if (out && value.length()) out->actvn.otaa.devEUI = hexStringToUint64(value.c_str());
  return noError;
}

static int pJoinEUI(const String &value, Config *out) {
  int error = parseHex(value, 16);
  if (error != noError) return error;
  if (out && value.length()) out->actvn.otaa.joinEUI = hexStringToUint64(value.c_str());
  return noError;
}

static int pAppKey(const String &value, Config *out) {
  int error = parseHex(value, 32);
  if (error != noError) return error;
  if (out && value.length()) hexStringToByteArray(value.c_str(), out->actvn.otaa.appKey, 32);
  return noError;
}

static int pNwkKey(const String &value, Config *out) {
  int error = parseHex(value, 32);
  if (error != noError) return error;
  if (out && value.length()) hexStringToByteArray(value.c_str(), out->actvn.otaa.nwkKey, 32);
  return noError;
}

static int pDevAddr(const String &value, Config *out) {
  int error = parseHex(value, 8);
  if (error != noError) return error;
  if (out && value.length()) out->actvn.abp.devAddr = hexStringToUint32(value.c_str());
  return noError;
}

static int pAppSKey(const String &value, Config *out) {
  int error = parseHex(value, 32);
  if (error != noError) return error;
  if (out && value.length()) hexStringToByteArray(value.c_str(), out->actvn.abp.appSKey, 32);
  return noError;
}

static int pNwkSEncKey(const String &value, Config *out) {
  int error = parseHex(value, 32);
  if (error != noError) return error;
  if (out && value.length()) hexStringToByteArray(value.c_str(), out->actvn.abp.nwkSEncKey, 32);
  return noError;
}

static int pFNwkSIntKey(const String &value, Config *out) {
  int error = parseHex(value, 32);
  if (error != noError) return error;
  if (out && value.length()) hexStringToByteArray(value.c_str(), out->actvn.abp.fNwkSIntKey, 32);
  return noError;
}

static int pSNwkSIntKey(const String &value, Config *out) {
  int error = parseHex(value, 32);
  if (error != noError) return error;
  if (out && value.length()) hexStringToByteArray(value.c_str(), out->actvn.abp.sNwkSIntKey, 32);
  return noError;
}

// ---- WiFi ----

static int pName(const String &value, Config *out) {
  if (value.length() < 4 || value.length() > 16) return valueError;
  if (out) out->wl2g4.name = value;
  return noError;
}

static int pSSID(const String &value, Config *out) {
  if (value.length() < 1 || value.length() > 32) return valueError;
  if (out) out->wl2g4.ssid = value;
  return noError;
}

// Empty means an open network. The bound is the 802.1X one (128) rather than
// the 8-to-63 a WPA passphrase must obey, because this field holds both: an
// enterprise password has no minimum length and is not a passphrase. The
// PSK-shaped rule is enforced where it belongs, by WIFI SET on the serial
// console and by the dashboard's WiFi page.
//
// The pair also doubles as the credentials of the fallback access point in
// connectWiFi(), which does have the 8-to-63 rule - see wifiFallbackPass().
static int pPassphrase(const String &value, Config *out) {
  if (value.length() > 128) return lengthError;
  if (out) out->wl2g4.pass = value;
  return noError;
}

static int pUser(const String &value, Config *out) {
  if (value.length() > 128) return lengthError;
  if (out) out->wl2g4.user = value;
  return noError;
}

static int pIdentity(const String &value, Config *out) {
  if (value.length() > 128) return lengthError;
  if (out) out->wl2g4.identity = value;
  return noError;
}

static int pEap(const String &value, Config *out) {
  String v = value;
  v.toLowerCase();
  uint8_t eap;
  if (v == "none" || v == "" || v == "psk" || v == "0") eap = EAP_NONE;
  else if (v == "peap" || v == "1")                     eap = EAP_PEAP;
  else if (v == "ttls" || v == "2")                     eap = EAP_TTLS;
  else return valueError;

  if (out) out->wl2g4.eap = eap;
  return noError;
}

static int pPhase2(const String &value, Config *out) {
  String v = value;
  v.toLowerCase();
  uint8_t phase2;
  if (v == "mschapv2" || v == "")  phase2 = P2_MSCHAPV2;
  else if (v == "mschap")          phase2 = P2_MSCHAP;
  else if (v == "pap")             phase2 = P2_PAP;
  else if (v == "chap")            phase2 = P2_CHAP;
  else if (v == "eap")             phase2 = P2_EAP;
  else return valueError;

  if (out) out->wl2g4.phase2 = phase2;
  return noError;
}

static int pCa(const String &value, Config *out) {
  String v = value;
  v.toLowerCase();
  uint8_t ca;
  if (v == "none" || v == "" || v == "0") ca = CA_NONE;
  else if (v == "bundle" || v == "1")     ca = CA_BUNDLE;
  else return valueError;

  if (out) out->wl2g4.ca = ca;
  return noError;
}

static int pAutoWifi(const String &value, Config *out) {
  String v = value;
  v.toUpperCase();
  if (!isTruthy(v) && !isFalsy(v)) return valueError;

  if (out) out->wl2g4.autoStart = isTruthy(v);
  return noError;
}

// ---- Time ----

// Accepts "+1", "60" and "1:30" alike: a bare number is hours when it could
// plausibly be one (|n| <= 14, the widest real offset) and minutes otherwise.
static int pTimezone(const String &value, Config *out) {
  String v = value;
  v.trim();
  if (v.length() == 0) return valueError;

  long minutes;
  int colon = v.indexOf(':');
  if (colon >= 0) {
    long hours, mins;
    if (!parseInt(v.substring(0, colon), hours)) return valueError;
    if (!parseInt(v.substring(colon + 1), mins)) return valueError;
    if (mins < 0 || mins > 59) return valueError;
    if (hours < 0 || v.startsWith("-")) mins = -mins;
    minutes = hours * 60 + mins;
  } else {
    if (!parseInt(v, minutes)) return valueError;
    if (labs(minutes) <= 14) minutes *= 60;
  }

  if (minutes < -720 || minutes > 840) return valueError;   // -12:00 .. +14:00

  if (out) out->timezoneMinutes = (int16_t)minutes;
  return noError;
}

static int pDST(const String &value, Config *out) {
  long minutes;
  if (!parseInt(value, minutes)) return valueError;
  if (labs(minutes) <= 12) minutes *= 60;
  if (minutes < 0 || minutes > 720) return valueError;

  if (out) out->dstOffsetMinutes = (int16_t)minutes;
  return noError;
}

// ============= Settings Metadata =============
// { key, displayName, group, type, default, parse, maxLength, secret, hint }
//
// `key` doubles as the NVS key, so it has to stay within 15 characters, and
// renaming one orphans whatever is already stored under the old name.
const SettingMetadata settingsMetadata[] = {
  // LoRaWAN
  { "version",     "Version",     GROUP_LORAWAN,         TYPE_ENUM, "1.1",       pVersion,     16, false, "1.0.4 | 1.0.4r | 1.1" },
  { "method",      "Method",      GROUP_LORAWAN,         TYPE_ENUM, "OTAA",      pMethod,      16, false, "OTAA | ABP" },
  { "relay",       "Relay",       GROUP_LORAWAN,         TYPE_LIST, "OFF",       pRelay,       32, false, "OFF | <mode>,<smartlevel>,<backoff>" },

  // Uplink
  { "adr",         "ADR",         GROUP_UPLINK,          TYPE_LIST, "OFF",       pADR,         80, false, "OFF | ON | DR,<a,b,..> | DR,ODD | DR,EVEN | DBM,<a,b,..>" },
  { "dr",          "DR",          GROUP_UPLINK,          TYPE_ENUM, "5",         pDataRate,    16, false, "0-7, or SF12..SF7BW250, FSK" },
  { "dbm",         "dBm",         GROUP_UPLINK,          TYPE_INT,  "16",        pDBm,          8, false, "-16 .. 16" },
  { "confirmed",   "Confirmed",   GROUP_UPLINK,          TYPE_BOOL, "0",         pConfirmed,    8, false, "0 | 1" },
  { "interval",    "Interval",    GROUP_UPLINK,          TYPE_LIST, "fixed,30",  pInterval,    24, false, "fixed,<10-65535 s> | dc,fup | dc,0.1% | dc,1% | dc,<1-8639>" },
  { "sleep",       "Sleep",       GROUP_UPLINK,          TYPE_BOOL, "1",         pSleep,        8, false, "0 | 1" },
  { "operation",   "Operation",   GROUP_UPLINK,          TYPE_LIST, "mobile,5",  pOperation,   24, false, "stationary | mobile | mobile,<1-255>" },
  { "timeout",     "Timeout",     GROUP_UPLINK,          TYPE_INT,  "120",       pTimeout,      8, false, "0 .. 3600 s" },

  // OTAA activation
  { "deveui",      "DevEUI",      GROUP_ACTIVATION_OTAA, TYPE_HEX,  "",          pDevEUI,      16, false, "16 hex characters" },
  { "joineui",     "JoinEUI",     GROUP_ACTIVATION_OTAA, TYPE_HEX,  "",          pJoinEUI,     16, false, "16 hex characters" },
  { "appkey",      "AppKey",      GROUP_ACTIVATION_OTAA, TYPE_HEX,  "",          pAppKey,      32, true,  "32 hex characters" },
  { "nwkkey",      "NwkKey",      GROUP_ACTIVATION_OTAA, TYPE_HEX,  "",          pNwkKey,      32, true,  "32 hex characters (LoRaWAN 1.1 only)" },

  // ABP activation
  { "devaddr",     "DevAddr",     GROUP_ACTIVATION_ABP,  TYPE_HEX,  "",          pDevAddr,      8, false, "8 hex characters" },
  { "appskey",     "AppSKey",     GROUP_ACTIVATION_ABP,  TYPE_HEX,  "",          pAppSKey,     32, true,  "32 hex characters" },
  { "nwksenckey",  "NwkSEncKey",  GROUP_ACTIVATION_ABP,  TYPE_HEX,  "",          pNwkSEncKey,  32, true,  "32 hex characters" },
  { "fnwksintkey", "FNwkSIntKey", GROUP_ACTIVATION_ABP,  TYPE_HEX,  "",          pFNwkSIntKey, 32, true,  "32 hex characters (LoRaWAN 1.1 only)" },
  { "snwksintkey", "SNwkSIntKey", GROUP_ACTIVATION_ABP,  TYPE_HEX,  "",          pSNwkSIntKey, 32, true,  "32 hex characters (LoRaWAN 1.1 only)" },

  // 2.4 GHz WiFi
  { "name",        "Name",        GROUP_WIFI_2G4,        TYPE_TEXT, "SensorBox", pName,        16, false, "4 to 16 characters; also the mDNS and BLE name" },
  { "ssid",        "SSID",        GROUP_WIFI_2G4,        TYPE_TEXT, "SensorBox", pSSID,        32, false, "1 to 32 characters" },
  { "pass",        "Pass",        GROUP_WIFI_2G4,        TYPE_TEXT, "S3ns0rB0x", pPassphrase, 128, true,  "WPA passphrase (8-63), or 802.1X password (up to 128); empty for an open network" },
  { "eap",         "EAP",         GROUP_WIFI_2G4,        TYPE_ENUM, "none",      pEap,         16, false, "none | peap | ttls - none means a personal (PSK) network" },
  { "user",        "User",        GROUP_WIFI_2G4,        TYPE_TEXT, "",          pUser,       128, false, "802.1X inner identity (username)" },
  { "identity",    "Identity",    GROUP_WIFI_2G4,        TYPE_TEXT, "",          pIdentity,   128, false, "802.1X outer identity, sent in the clear; empty reuses User" },
  { "phase2",      "Phase2",      GROUP_WIFI_2G4,        TYPE_ENUM, "mschapv2",  pPhase2,      16, false, "mschapv2 | mschap | pap | chap | eap - EAP-TTLS only" },
  { "ca",          "CA",          GROUP_WIFI_2G4,        TYPE_ENUM, "none",      pCa,          16, false, "none | bundle - validate the RADIUS certificate against the built-in CA bundle" },
  { "autowifi",    "AutoWiFi",    GROUP_WIFI_2G4,        TYPE_BOOL, "0",         pAutoWifi,     8, false, "0 | 1 - bring WiFi and the dashboard up at boot" },

  // Time
  { "timezone",    "Timezone",    GROUP_TIME,            TYPE_INT,  "60",        pTimezone,    16, false, "hours (-14..14), minutes, or +HH:MM" },
  { "dst",         "DST",         GROUP_TIME,            TYPE_INT,  "0",         pDST,         16, false, "summer-time offset in hours (0-12) or minutes (0-720)" },
};

const uint16_t NUM_SETTINGS_METADATA = sizeof(settingsMetadata) / sizeof(SettingMetadata);

// Global ConfigManager instance
ConfigManager configMgr(settingsMetadata, NUM_SETTINGS_METADATA);

// ============= Public API =============

int doSetting(String &key, String &value) {
  if (value.length() == 0) {
    const SettingMetadata *meta = configMgr.getMetadata(key.c_str());
    if (!meta) return keyError;
    value = meta->defaultValue;          // an empty write means "back to default"
  }
  return configMgr.set(key.c_str(), value);
}

bool isValidGroupOTAA() {
  return configMgr.has("deveui") &&
         configMgr.has("joineui") &&
         configMgr.has("appkey") &&
         (cfg.actvn.version != v11 || configMgr.has("nwkkey"));
}

bool isValidGroupABP() {
  return configMgr.has("devaddr") &&
         configMgr.has("appskey") &&
         configMgr.has("nwksenckey") &&
         (cfg.actvn.version != v11 || configMgr.has("fnwksintkey")) &&
         (cfg.actvn.version != v11 || configMgr.has("snwksintkey"));
}

// load() applies every setting to `cfg` as it reads it, so there is no second
// pass here any more - and no window in which the cache and `cfg` disagree.
void loadConfig() {
  configMgr.load();
}

String printConfig(int group) {
  return configMgr.printSettings(group);
}

String printFullConfig(bool inclVersion) {
  String ret = "";
  if (inclVersion) {
    ret += ("\r\nMJLO SensorBox by Steven @ Ichthus/Kroonos\r\nFirmware " MJLO_VERSION "\r\nCompiled " __DATE__ "\r\n");
  }
  for (int i = 0; i < GROUP_COUNT; i++) {
    ret += printConfig(i);
  }
  return ret;
}

String parseError(int errorCode) {
  switch (errorCode) {
    case noError:      return "OK";
    case lengthError:  return "The value is longer than this setting accepts";
    case formatError:  return "The command was not formatted as expected";
    case commandError: return "The specified command was not recognized";
    case keyError:     return "There was no setting matching the specified key";
    case valueError:   return "The specified value was not formatted correctly";
    case busyError:    return "Uplink in progress - could not configure; try again later";
    case fileError:    return "The specified file could not be found or read";
    default:           return "An unknown error occured";
  }
}
