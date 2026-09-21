// Copyright (C) 2024-2026 StevenCellist (https://github.com/StevenCellist)
// Licensed under GPL-3.0

/*
 * sercmd.cpp - the serial command console. See sercmd.h for the contract and
 * serial-provisioning.md for the protocol as a client sees it.
 *
 * Structure
 *   - sercmdPoll() is the only reader of Serial. It assembles lines and hands
 *     each one to either the legacy '+' console or dispatch() below.
 *   - Replies go out through emit(), one Serial.write() per line, so a line is
 *     never split across two calls and cannot be interleaved with anything the
 *     rest of the loop prints.
 *   - Everything runs on the main loop, so there is exactly one command in
 *     flight and the handlers need no locking. A long one (SCAN, WIFI SET)
 *     simply blocks the loop, and the client is waiting for that reply anyway.
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include "config.h"
#include "sercmd.h"
#include "webserver.h"

// Protocol revision, reported in the banner and by INFO. Bump it when a change
// would break a client written against the old shape.
#define SERCMD_PROTO 1

#define SERCMD_MODEL "MJLO SensorBox"

// Longest accepted command line. Sized for the widest real command, which is
// a WIFI SET-ENT carrying a 32-byte SSID plus a username, password and outer
// identity of up to 128 bytes each, all fully escaped.
#define SERCMD_MAX_LINE 640
// WIFI SET-ENT is also what sets this: the verb, the subcommand, the SSID and
// seven named options.
#define SERCMD_MAX_ARGS 12
// One reply line. The widest are an enterprise WIFI GET and a CFG LIST row;
// anything longer is truncated rather than allowed to split across two lines.
#define SERCMD_MAX_OUT 768

#define SCAN_MAX_RESULTS 24

// Bounds for the WIFI SET verdict wait. Association plus DHCP is comfortably
// under 20 s on a healthy network; the default leaves room for a slow DHCP
// server without stalling the loop for a minute on a typo.
#define WIFI_SET_TIMEOUT_DEFAULT_MS 30000
#define WIFI_SET_TIMEOUT_MIN_MS 5000
#define WIFI_SET_TIMEOUT_MAX_MS 120000

// ---- Session authentication ----
//
// A session is authenticated by AUTH against the device password and stays
// that way until LOGOUT, a reboot, or the idle timeout. The timeout is what
// makes the gate mean anything on a serial port: there is no hang-up to
// notice, so without it a provisioning jig that authenticates and unplugs
// leaves the next person's session already open.
//
// A wrong password costs an escalating delay, paid before the reply so it
// lands on a client that fires and forgets just as much as on one that waits.
#define AUTH_IDLE_TIMEOUT_S 600
#define AUTH_FAIL_DELAY_STEP_MS 1000
#define AUTH_FAIL_DELAY_MAX_MS 15000

// Set to 1 to require an authenticated session for the legacy '+' commands
// too. Off by default: '+' is the console a person uses with the box in front
// of them, and '+id' prints the default password anyway, so gating it buys
// nothing and costs the habit. Turn it on for a unit whose port is not
// physically private - and read the note in sercmd.h before relying on it.
#define SERCMD_GATE_LEGACY 0

uint8_t serialLogLevel = SERLOG_INFO;

static bool s_authed = false;
static unsigned s_authFailures = 0;
static uint32_t s_lastCmdMs = 0;

// ============================================================
// Output
// ============================================================

// Emit one reply line: '@' + the formatted text + CRLF, in a single write so
// nothing can land in the middle of it.
static void emit(const char *fmt, ...) {
  char line[SERCMD_MAX_OUT];
  const int cap = (int)sizeof(line) - 4;   // '@' + text + CRLF + NUL
  line[0] = '@';

  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line + 1, (size_t)cap + 1, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > cap) n = cap;                    // truncated; keep the line well-formed

  line[1 + n] = '\r';
  line[2 + n] = '\n';
  Serial.write((const uint8_t *)line, n + 3);
}

// Render `src` as a protocol string value: always double-quoted, with '\' and
// '"' backslash-escaped and control characters dropped. Quoting is
// unconditional so a client never has to decide whether a value is quoted - if
// the grammar says string, it is quoted. Returns `dst`.
static const char *qstr(char *dst, size_t dsz, const char *src) {
  size_t w = 0;
  if (dsz < 3) {                           // cannot even hold "" + NUL
    if (dsz) dst[0] = '\0';
    return dst;
  }
  dst[w++] = '"';
  for (const unsigned char *p = (const unsigned char *)(src ? src : ""); *p; p++) {
    if (*p < 0x20 || *p == 0x7F) continue;
    size_t need = (*p == '"' || *p == '\\') ? 2 : 1;
    if (w + need + 2 > dsz) break;         // + closing quote + NUL
    if (need == 2) dst[w++] = '\\';
    dst[w++] = (char)*p;
  }
  dst[w++] = '"';
  dst[w] = '\0';
  return dst;
}

static void err(const char *verb, const char *code, const char *msg) {
  char q[192];
  emit("ERR %s %s %s", verb, code, qstr(q, sizeof(q), msg));
}

// Same, plus one trailing key=value carrying the machine-readable detail
// behind the message (the WiFi reason code, the retry delay, ...).
static void errKv(const char *verb, const char *code, const char *msg,
                  const char *key, long value) {
  char q[192];
  emit("ERR %s %s %s %s=%ld", verb, code, qstr(q, sizeof(q), msg), key, value);
}

// The ErrorCode a setting rejection comes back as, in protocol terms.
static const char *settingErrCode(int error) {
  switch (error) {
    case keyError:    return "ENOKEY";
    case valueError:  return "EARG";
    case lengthError: return "EARG";
    case busyError:   return "EBUSY";
    default:          return "ESTATE";
  }
}

// ============================================================
// Line tokenising
//
// Arguments are whitespace-separated bare words, or double-quoted strings with
// \\ \" \n \t \r escapes. Quoting matters: SSIDs and passphrases routinely
// contain spaces, and a positional grammar cannot represent one without it.
// ============================================================

// Tokenise `line` in place. Returns the argument count, or -1 if a quoted
// string is left open.
static int tokenize(char *line, char *argv[], int max) {
  int argc = 0;
  char *r = line;

  while (*r && argc < max) {
    while (*r == ' ' || *r == '\t') r++;
    if (*r == '\0') break;

    char *w = r;                           // unescaped output, written in place
    argv[argc++] = w;

    if (*r == '"') {
      r++;
      bool closed = false;
      while (*r) {
        if (*r == '"') { r++; closed = true; break; }
        if (*r == '\\' && r[1]) {
          r++;
          switch (*r) {
            case 'n': *w++ = '\n'; break;
            case 't': *w++ = '\t'; break;
            case 'r': *w++ = '\r'; break;
            default:  *w++ = *r;   break;  // covers \\ and \"
          }
          r++;
          continue;
        }
        *w++ = *r++;
      }
      if (!closed) return -1;
    } else {
      while (*r && *r != ' ' && *r != '\t') *w++ = *r++;
    }
    // `w` never overtakes `r`, so this terminator only ever lands on input
    // that has already been consumed.
    char next = *r;
    *w = '\0';
    if (next == '\0') break;
    r++;
  }
  return argc;
}

static void upcase(char *s) {
  for (; *s; s++) {
    if (*s >= 'a' && *s <= 'z') *s -= 32;
  }
}

static bool parseMillis(const char *s, unsigned long &out) {
  char *end = NULL;
  unsigned long v = strtoul(s, &end, 10);
  if (end == s || *end != '\0') return false;
  out = v;
  return true;
}

// ============================================================
// Shared field renderers
// ============================================================

static const char *authmodeStr(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN:                    return "OPEN";
    case WIFI_AUTH_OWE:                     return "OWE";
    case WIFI_AUTH_WEP:                     return "WEP";
    case WIFI_AUTH_WPA_PSK:                 return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK:                return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:            return "WPA/WPA2-PSK";
    case WIFI_AUTH_WPA3_PSK:                return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:           return "WPA2/WPA3-PSK";
    /* The driver reports these for a WPA3 access point that advertises the
     * extended element; they mean the same thing to a client as the two
     * above, so they are reported as the same thing. */
    case WIFI_AUTH_WPA3_EXT_PSK:            return "WPA3-PSK";
    case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE: return "WPA2/WPA3-PSK";
    case WIFI_AUTH_WAPI_PSK:                return "WAPI-PSK";
    /* This IDF has no separate WPA3-Enterprise enumerator: a WPA3-ENT access
     * point is reported as plain enterprise, so that is what is passed on. */
    case WIFI_AUTH_ENTERPRISE:              return "WPA2-ENT";
    case WIFI_AUTH_WPA3_ENT_192:            return "WPA3-ENT-192";
    case WIFI_AUTH_DPP:                     return "DPP";
    default:                                return "UNKNOWN";
  }
}

// Whether joining this network needs a credential at all. This - not the auth
// mode string - is what a client branches on to decide whether to prompt. OWE
// is opportunistic encryption with no credential, so it counts as open.
static bool apNeedsPassword(wifi_auth_mode_t m) {
  return !(m == WIFI_AUTH_OPEN || m == WIFI_AUTH_OWE);
}

// Whether this network wants a username and password rather than a shared key,
// i.e. WIFI SET-ENT rather than WIFI SET.
static bool apIsEnterprise(wifi_auth_mode_t m) {
  return m == WIFI_AUTH_ENTERPRISE || m == WIFI_AUTH_WPA3_ENT_192;
}

// What the box is configured for, which is meaningful even when the radio is
// down - unlike the auth mode of an access point it is not associated with.
static const char *configuredModeStr() {
  return cfg.wl2g4.eap == EAP_NONE ? "psk" : "enterprise";
}

static const char *eapStr(uint8_t eap) {
  return eap == EAP_PEAP ? "PEAP" : eap == EAP_TTLS ? "TTLS" : "NONE";
}

static const char *phase2Str(uint8_t phase2) {
  switch (phase2) {
    case P2_MSCHAP: return "MSCHAP";
    case P2_PAP:    return "PAP";
    case P2_CHAP:   return "CHAP";
    case P2_EAP:    return "EAP";
    default:        return "MSCHAPV2";
  }
}

// Has anyone ever given this box a network? There is no "no SSID" state to
// test for - the stored pair doubles as the fallback access point's own
// credentials and therefore always has a value - so the question is really
// whether the stored SSID is still the one it left the factory with.
static bool wifiIsProvisioned() {
  const SettingMetadata *meta = configMgr.getMetadata("ssid");
  return meta && configMgr.get("ssid") != meta->defaultValue;
}

// Current link state as one token. Derived from the driver, not from whatever
// the display last showed.
//
// OFF is this device's own state and has no counterpart on a gateway: WiFi
// here is a thing you switch on, not the reason the box exists, so "the radio
// is deliberately down" is a normal answer and not a failure.
static const char *netStateStr() {
  if (wifiMode == WIFI_MODE_AP) return "SOFTAP";
  if (wifiMode != WIFI_MODE_NULL) {
    return (WiFi.status() == WL_CONNECTED) ? "CONNECTED" : "ACQUIRING";
  }
  return wifiIsProvisioned() ? "OFF" : "UNCONFIGURED";
}

// The device ID the web dashboard uses as its username, and the low half of it
// that is the default password. Kept here in the same shape as '+id' prints.
static void deviceIdStr(char *out, size_t len) {
  uint64_t id = ESP.getEfuseMac();
  snprintf(out, len, "%02X%02X%02X%02X%02X%02X",
           (uint8_t)(id >> 40), (uint8_t)(id >> 32), (uint8_t)(id >> 24),
           (uint8_t)(id >> 16), (uint8_t)(id >> 8), (uint8_t)(id));
}

static uint32_t uptimeSeconds() {
  return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

// ============================================================
// Commands
// ============================================================

static void cmdHelp() {
  static const char *const help[] = {
    "open (no authentication needed):",
    "  PING                        liveness check; reports authed=0|1",
    "  HELP                        this list",
    "  AUTH <password>             the device password (see +id for the default)",
    "  LOGOUT                      end the session now",
    "authenticated only:",
    "  INFO                        firmware / identity",
    "  STATUS                      link state, address, signal",
    "  SCAN                        visible access points + encryption",
    "  WIFI GET                    configured SSID and 802.1X settings",
    "  WIFI SET <ssid> [pass] [ms] open / WPA2 / WPA3-Personal, then connect",
    "  WIFI SET-ENT <ssid> eap=peap|ttls user=<u> pass=<p> [identity=] [phase2=]",
    "                              [ca=none|bundle] [timeout=<ms>]   802.1X",
    "  WIFI CLEAR                  restore the built-in fallback credentials",
    "  CFG LIST [group]            every setting, with type and accepted values",
    "  CFG GET <key>               one setting",
    "  CFG SET <key> <value>       change one setting",
    "  CFG RESET [key]             one setting, or all of them, back to default",
    "  LORA                        activation state and session",
    "  UPLINK                      bring the next uplink forward",
    "  JOIN                        rejoin the network",
    "  WIPE CFG | LORA             erase the settings, or the LoRaWAN session",
    "  FS LIST                     files on the internal filesystem",
    "  FS DELETE <name>            remove one file",
    "  PASS RESET                  restore the default device password",
    "  LOG <level>                 NONE ERROR WARN INFO DEBUG VERBOSE",
    "  SLEEP                       power the unit down",
    "  RESTART [ms]                reboot the unit",
    "quote arguments containing spaces: WIFI SET \"My Net\" \"pass word\"",
    "the legacy '+' console is unchanged and still open; '+at' lists everything",
  };
  for (size_t i = 0; i < sizeof(help) / sizeof(help[0]); i++) {
    emit("# %s", help[i]);
  }
  emit("OK HELP count=%u", (unsigned)(sizeof(help) / sizeof(help[0])));
}

static void cmdAuth(int argc, char **argv) {
  if (argc < 2) {
    err("AUTH", "EARG", "usage: AUTH <password>");
    return;
  }
  if (webCheckPassword(String(argv[1]))) {
    s_authed = true;
    s_authFailures = 0;
    emit("OK AUTH authed=1 idle=%d", AUTH_IDLE_TIMEOUT_S);
    return;
  }

  // A failure also ends any session that was already open: an operator who
  // mistypes is no worse off, but a second person at the port cannot ride on a
  // session someone else left behind.
  s_authed = false;
  if (s_authFailures < 1000) s_authFailures++;

  unsigned long delayMs = (unsigned long)s_authFailures * AUTH_FAIL_DELAY_STEP_MS;
  if (delayMs > AUTH_FAIL_DELAY_MAX_MS) delayMs = AUTH_FAIL_DELAY_MAX_MS;

  delay(delayMs);
  errKv("AUTH", "EDENIED", "incorrect password", "delay", (long)delayMs);
}

static void cmdInfo() {
  uint8_t mac[6] = { 0 };
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  char id[16];
  deviceIdStr(id, sizeof(id));

  // `eui` carries the LoRaWAN DevEUI, which is the identity this device is
  // known by on the network server. All zeros means none is provisioned.
  String eui = configMgr.get("deveui");
  if (eui.length() != 16) eui = "0000000000000000";

  char qname[48], qmodel[48];
  emit("OK INFO proto=%d model=%s name=%s fw=%s idf=%s "
       "eui=%s mac=%02X%02X%02X%02X%02X%02X id=%s uptime=%u",
       SERCMD_PROTO,
       qstr(qmodel, sizeof(qmodel), SERCMD_MODEL),
       qstr(qname, sizeof(qname), cfg.wl2g4.name.c_str()),
       MJLO_VERSION, ESP.getSdkVersion(),
       eui.c_str(),
       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
       id, uptimeSeconds());
}

static void cmdStatus() {
  const char *state = netStateStr();
  String ssid = cfg.wl2g4.ssid;
  char ip[16] = "0.0.0.0";
  char gw[16] = "0.0.0.0";
  char bssid[18] = "00:00:00:00:00:00";
  int rssi = 0, channel = 0;
  const char *auth = "UNKNOWN";

  if (wifiMode == WIFI_MODE_STA && WiFi.status() == WL_CONNECTED) {
    ssid = WiFi.SSID();
    rssi = WiFi.RSSI();
    channel = WiFi.channel();
    snprintf(bssid, sizeof(bssid), "%s", WiFi.BSSIDstr().c_str());
    snprintf(ip, sizeof(ip), "%s", WiFi.localIP().toString().c_str());
    snprintf(gw, sizeof(gw), "%s", WiFi.gatewayIP().toString().c_str());
    // What the access point is actually running, which is not always what the
    // box was configured for - a WPA2/WPA3 transition network is the common
    // case where the two differ.
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) auth = authmodeStr(ap.authmode);
  } else if (wifiMode == WIFI_MODE_AP) {
    ssid = WiFi.softAPSSID();
    snprintf(ip, sizeof(ip), "%s", WiFi.softAPIP().toString().c_str());
  }

  char qssid[80];
  emit("OK STATUS state=%s ssid=%s ip=%s gw=%s bssid=%s ch=%d rssi=%d "
       "auth=%s mode=%s batt=%u uptime=%u heap=%u",
       state, qstr(qssid, sizeof(qssid), ssid.c_str()),
       ip, gw, bssid, channel, rssi,
       auth, configuredModeStr(),
       (unsigned)sercmdBatteryMillivolts(), uptimeSeconds(),
       (unsigned)ESP.getFreeHeap());
}

// A blocking all-channel scan, leaving the radio as it found it. The station
// interface has to exist for the scan to run, so a scan with WiFi off brings
// the interface up for the duration and puts it back afterwards.
static void cmdScan() {
  wifi_mode_t entryMode = wifiMode;
  uint32_t entryCpu = getCpuFrequencyMhz();

  if (entryMode == WIFI_MODE_NULL) {
    setCpuFrequencyMhz(240);              // the driver is unhappy at 80 MHz
    WiFi.mode(WIFI_STA);
  }

  // The dashboard's WiFi page keeps a scan running in the background. Adopt
  // one that is still in flight rather than racing it - but never a completed
  // one, because its results may be minutes old and the operator is standing
  // in front of the access point they expect to see.
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    uint32_t deadline = millis() + 10000;
    while ((n = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && millis() < deadline) {
      delay(50);
    }
  } else {
    WiFi.scanDelete();
    n = WiFi.scanNetworks(false, false);
  }

  if (n < 0) {
    WiFi.scanDelete();
    if (entryMode == WIFI_MODE_NULL) { WiFi.mode(WIFI_MODE_NULL); setCpuFrequencyMhz(entryCpu); }
    err("SCAN", "ESTATE", "the scan could not be started");
    return;
  }

  // Strongest first, so a client that shows the top few shows the ones worth
  // joining. Insertion sort over indices: n is small and the records stay in
  // the driver's own list.
  uint8_t order[SCAN_MAX_RESULTS];
  uint8_t count = (n > SCAN_MAX_RESULTS) ? SCAN_MAX_RESULTS : (uint8_t)n;
  for (uint8_t i = 0; i < count; i++) order[i] = i;
  for (uint8_t i = 1; i < count; i++) {
    uint8_t key = order[i];
    int j = (int)i - 1;
    while (j >= 0 && WiFi.RSSI(order[j]) < WiFi.RSSI(key)) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = key;
  }

  unsigned listed = 0;
  for (uint8_t i = 0; i < count; i++) {
    int idx = order[i];
    String ssid = WiFi.SSID(idx);
    if (ssid.length() == 0) continue;     // hidden: nothing to connect to
    listed++;

    wifi_auth_mode_t auth = WiFi.encryptionType(idx);
    char q[80];
    emit("DAT SCAN ssid=%s bssid=%s ch=%d rssi=%d auth=%s secure=%d enterprise=%d",
         qstr(q, sizeof(q), ssid.c_str()),
         WiFi.BSSIDstr(idx).c_str(),
         WiFi.channel(idx), WiFi.RSSI(idx),
         authmodeStr(auth), apNeedsPassword(auth) ? 1 : 0,
         apIsEnterprise(auth) ? 1 : 0);
  }

  WiFi.scanDelete();
  if (entryMode == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_MODE_NULL);
    setCpuFrequencyMhz(entryCpu);
  }

  // count is the number of DAT rows, so a client can size its list from it;
  // found also counts the hidden networks that were skipped.
  emit("OK SCAN count=%u found=%d", listed, n);
}

// The password is never reported, on either path. Everything else is: an
// operator debugging a failed 802.1X join needs to see which identity went in.
static void cmdWifiGet() {
  char qssid[80];
  if (cfg.wl2g4.eap == EAP_NONE) {
    emit("OK WIFI configured=%d ssid=%s mode=psk autostart=%d state=%s",
         wifiIsProvisioned() ? 1 : 0,
         qstr(qssid, sizeof(qssid), cfg.wl2g4.ssid.c_str()),
         cfg.wl2g4.autoStart ? 1 : 0,
         netStateStr());
    return;
  }
  char quser[272], qident[272];
  emit("OK WIFI configured=%d ssid=%s mode=enterprise eap=%s phase2=%s user=%s "
       "identity=%s ca=%s autostart=%d state=%s",
       wifiIsProvisioned() ? 1 : 0,
       qstr(qssid, sizeof(qssid), cfg.wl2g4.ssid.c_str()),
       eapStr(cfg.wl2g4.eap),
       cfg.wl2g4.eap == EAP_TTLS ? phase2Str(cfg.wl2g4.phase2) : "MSCHAPV2",
       qstr(quser, sizeof(quser), cfg.wl2g4.user.c_str()),
       qstr(qident, sizeof(qident), cfg.wl2g4.identity.c_str()),
       cfg.wl2g4.ca == CA_BUNDLE ? "bundle" : "none",
       cfg.wl2g4.autoStart ? 1 : 0,
       netStateStr());
}

// Everything the two WIFI SET commands share once their arguments are parsed:
// store, associate, report. Ownership of the reply is here so that the two
// cannot drift into reporting the same outcome differently.
static void wifiApplyAndReport(const String &ssid, unsigned long timeout) {
  WifiCredentials cred = wifiStoredCredentials();
  int verdict = wifiJoin(cred, (uint32_t)timeout);
  long reason = (long)wifiLastDisconnectReason();

  char q[80];
  switch (verdict) {
    case WIFI_JOIN_OK:
      // Someone who just typed credentials over the cable wants the link to
      // survive the next reboot; without this the unit comes back with the
      // radio off and a client's post-restart check reads as a failure.
      configMgr.set("autowifi", "1");
      sercmdWifiUp();
      emit("OK WIFI ssid=%s ip=%s mode=%s restart=0",
           qstr(q, sizeof(q), ssid.c_str()),
           WiFi.localIP().toString().c_str(), configuredModeStr());
      break;
    case WIFI_JOIN_AUTH:
      errKv("WIFI", "EAUTH",
            cred.eap == EAP_NONE ? "access point refused the passphrase"
                                 : "the 802.1X exchange was refused - check the username, password and EAP method",
            "reason", reason);
      break;
    case WIFI_JOIN_NOTFOUND:
      errKv("WIFI", "ENOTFOUND", "ssid not found on any channel", "reason", reason);
      break;
    case WIFI_JOIN_BUSY:
      err("WIFI", "EBUSY", "another credential change is already running");
      break;
    case WIFI_JOIN_TIMEOUT:
    default:
      errKv("WIFI", "ETIMEOUT", "no association within the timeout", "reason", reason);
      break;
  }
}

// WIFI SET <ssid> [<pass>] [<timeout_ms>]
//
// Open, WPA2-Personal and WPA3-Personal (SAE), including WPA2/WPA3 transition
// mode: the station advertises PMF and offers both SAE password-derivation
// methods, so a WPA3-only access point needs nothing extra asked of the
// operator. For 802.1X networks, WIFI SET-ENT below.
static void cmdWifiSet(int argc, char **argv) {
  if (argc < 3) {
    err("WIFI", "EARG", "usage: WIFI SET <ssid> [<pass>] [<timeout_ms>]");
    return;
  }
  if (sercmdDeviceBusy()) {
    err("WIFI", "EBUSY", "an uplink is in flight; try again in a moment");
    return;
  }

  String ssid = argv[2];
  String pass = (argc >= 4) ? String(argv[3]) : String("");
  unsigned long timeout = WIFI_SET_TIMEOUT_DEFAULT_MS;

  if (argc >= 5 && argv[4][0] != '\0') {
    if (!parseMillis(argv[4], timeout)) {
      err("WIFI", "EARG", "timeout must be a whole number of milliseconds");
      return;
    }
    if (timeout < WIFI_SET_TIMEOUT_MIN_MS) timeout = WIFI_SET_TIMEOUT_MIN_MS;
    if (timeout > WIFI_SET_TIMEOUT_MAX_MS) timeout = WIFI_SET_TIMEOUT_MAX_MS;
  }

  if (ssid.length() < 1 || ssid.length() > 32) {
    err("WIFI", "EARG", "ssid must be 1 to 32 characters");
    return;
  }
  if (pass.length() > 0 && (pass.length() < 8 || pass.length() > 63)) {
    err("WIFI", "EARG", "passphrase must be empty (open network) or 8 to 63 characters");
    return;
  }

  // Announced before the attempt, because the attempt blocks for as long as
  // the association takes: without this a client cannot tell a device that is
  // working on it from one that never received the command.
  char q[80];
  emit("DAT WIFI applying=1 ssid=%s mode=psk timeout=%lu",
       qstr(q, sizeof(q), ssid.c_str()), timeout);

  // Stored before the attempt, deliberately: failed credentials are not rolled
  // back. The operator holding the cable would rather correct them than have
  // the device revert to the network they were trying to leave.
  //
  // The 802.1X fields go back to their defaults, because this command says the
  // network is a personal one; leaving a previous network's identity in place
  // would turn the next reconnect into a silent EAP attempt.
  int error = configMgr.set("ssid", ssid);
  if (error == noError) error = configMgr.set("pass", pass);
  if (error == noError) error = configMgr.set("eap", "none");
  if (error == noError) error = configMgr.reset("user");
  if (error == noError) error = configMgr.reset("identity");
  if (error == noError) error = configMgr.reset("phase2");
  if (error == noError) error = configMgr.reset("ca");
  if (error != noError) {
    err("WIFI", settingErrCode(error), parseError(error).c_str());
    return;
  }

  wifiApplyAndReport(ssid, timeout);
}

// WIFI SET-ENT <ssid> <key>=<value> ...
//
// Named rather than positional options, because an 802.1X join needs up to
// seven of them and most are optional. Order does not matter.
static void cmdWifiSetEnt(int argc, char **argv) {
  if (argc < 3) {
    err("WIFI", "EARG", "usage: WIFI SET-ENT <ssid> eap=peap|ttls user=<name> pass=<password> "
                        "[identity=<outer>] [phase2=<method>] [ca=none|bundle] [timeout=<ms>]");
    return;
  }
  if (sercmdDeviceBusy()) {
    err("WIFI", "EBUSY", "an uplink is in flight; try again in a moment");
    return;
  }

  String ssid = argv[2];
  String eap, user, pass, identity, phase2, ca;
  unsigned long timeout = WIFI_SET_TIMEOUT_DEFAULT_MS;
  bool haveEap = false, haveUser = false, havePass = false;

  for (int i = 3; i < argc; i++) {
    char *eq = strchr(argv[i], '=');
    if (!eq) {
      err("WIFI", "EARG", "options are <key>=<value>; quote a value containing spaces");
      return;
    }
    *eq = '\0';
    const char *key = argv[i];
    String value = eq + 1;

    if (strcasecmp(key, "eap") == 0)            { eap = value;      haveEap = true;  }
    else if (strcasecmp(key, "user") == 0)      { user = value;     haveUser = true; }
    else if (strcasecmp(key, "pass") == 0)      { pass = value;     havePass = true; }
    else if (strcasecmp(key, "identity") == 0)  { identity = value; }
    else if (strcasecmp(key, "phase2") == 0)    { phase2 = value;   }
    else if (strcasecmp(key, "ca") == 0)        { ca = value;       }
    else if (strcasecmp(key, "domain") == 0) {
      // Accepted by the grammar and refused on purpose. This IDF's EAP client
      // has no name-matching call, so honouring it is impossible and ignoring
      // it would quietly hand back a weaker check than the operator asked for.
      err("WIFI", "EUNSUP", "domain= is not supported on this device: the EAP client "
                            "cannot check the certificate's name. Use ca=bundle alone, or omit it.");
      return;
    }
    else if (strcasecmp(key, "timeout") == 0) {
      if (!parseMillis(value.c_str(), timeout)) {
        err("WIFI", "EARG", "timeout must be a whole number of milliseconds");
        return;
      }
      if (timeout < WIFI_SET_TIMEOUT_MIN_MS) timeout = WIFI_SET_TIMEOUT_MIN_MS;
      if (timeout > WIFI_SET_TIMEOUT_MAX_MS) timeout = WIFI_SET_TIMEOUT_MAX_MS;
    }
    else {
      err("WIFI", "EARG", "unknown option; expected eap, user, pass, identity, phase2, ca or timeout");
      return;
    }
  }

  if (ssid.length() < 1 || ssid.length() > 32) {
    err("WIFI", "EARG", "ssid must be 1 to 32 characters");
    return;
  }
  String eapLower = eap;
  eapLower.toLowerCase();
  if (eapLower == "tls") {
    err("WIFI", "EUNSUP", "eap=tls needs a client certificate and private key, which do not fit "
                          "through a line-oriented console");
    return;
  }
  if (!haveEap || !haveUser || !havePass) {
    err("WIFI", "EARG", "eap, user and pass are all required");
    return;
  }
  if (eapLower != "peap" && eapLower != "ttls") {
    err("WIFI", "EARG", "eap must be peap or ttls");
    return;
  }

  // Validated before anything is stored, so a typo in the last option cannot
  // leave the box half-configured for a network it will never reach.
  const char *check[][2] = {
    { "eap", eapLower.c_str() }, { "user", user.c_str() }, { "pass", pass.c_str() },
    { "identity", identity.c_str() },
  };
  for (auto &c : check) {
    int error = configMgr.validate(c[0], String(c[1]));
    if (error != noError) {
      char q[192];
      snprintf(q, sizeof(q), "%s: %s", c[0], parseError(error).c_str());
      err("WIFI", settingErrCode(error), q);
      return;
    }
  }
  if (phase2.length() && configMgr.validate("phase2", phase2) != noError) {
    err("WIFI", "EARG", "phase2 must be mschapv2, mschap, pap, chap or eap");
    return;
  }
  if (ca.length() && configMgr.validate("ca", ca) != noError) {
    err("WIFI", "EARG", "ca must be none or bundle");
    return;
  }

  char q[80];
  emit("DAT WIFI applying=1 ssid=%s mode=enterprise eap=%s timeout=%lu",
       qstr(q, sizeof(q), ssid.c_str()), eapLower == "peap" ? "PEAP" : "TTLS", timeout);

  int error = configMgr.set("ssid", ssid);
  if (error == noError) error = configMgr.set("eap", eapLower);
  if (error == noError) error = configMgr.set("user", user);
  if (error == noError) error = configMgr.set("pass", pass);
  if (error == noError) error = configMgr.set("identity", identity);
  if (error == noError) error = phase2.length() ? configMgr.set("phase2", phase2)
                                                : configMgr.reset("phase2");
  if (error == noError) error = ca.length() ? configMgr.set("ca", ca) : configMgr.reset("ca");
  if (error != noError) {
    err("WIFI", settingErrCode(error), parseError(error).c_str());
    return;
  }

  wifiApplyAndReport(ssid, timeout);
}

// Erases both kinds of credential. There is no "nothing stored" state to
// return to: the stored SSID and passphrase double as the fallback access
// point's own, so clearing them means restoring the built-in pair rather than
// emptying them.
static void cmdWifiClear() {
  static const char *const keys[] = { "ssid", "pass", "eap", "user", "identity", "phase2", "ca" };
  int error = noError;
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]) && error == noError; i++) {
    error = configMgr.reset(keys[i]);
  }
  if (error == noError) error = configMgr.set("autowifi", "0");
  if (error != noError) {
    err("WIFI", settingErrCode(error), parseError(error).c_str());
    return;
  }

  sercmdWifiDown();

  char q[80];
  emit("OK WIFI cleared=1 restart=0 ssid=%s", qstr(q, sizeof(q), cfg.wl2g4.ssid.c_str()));
}

static void cmdWifi(int argc, char **argv) {
  if (argc < 2) {
    err("WIFI", "EARG", "usage: WIFI GET | SET | SET-ENT | CLEAR");
    return;
  }
  upcase(argv[1]);
  if (strcmp(argv[1], "GET") == 0)          cmdWifiGet();
  else if (strcmp(argv[1], "SET") == 0)     cmdWifiSet(argc, argv);
  else if (strcmp(argv[1], "SET-ENT") == 0) cmdWifiSetEnt(argc, argv);
  else if (strcmp(argv[1], "CLEAR") == 0)   cmdWifiClear();
  else err("WIFI", "EARG", "unknown subcommand: expected GET, SET, SET-ENT or CLEAR");
}

// ---- Settings ----

// One row per setting. A secret's value is withheld here and reported only by
// an explicit CFG GET: a listing is the thing that ends up on a screen or in a
// log, and nobody reading it asked for the AppKey.
static void cfgRow(uint16_t idx) {
  const SettingMetadata *meta = configMgr.metaByIndex(idx);
  String value = configMgr.getByIndex(idx);

  char qname[32], qvalue[272], qdefault[48], qhint[144];
  emit("DAT CFG key=%s name=%s group=%s type=%s secret=%d set=%d value=%s default=%s hint=%s",
       meta->key,
       qstr(qname, sizeof(qname), meta->displayName),
       settingGroupNames[meta->group],
       settingTypeNames[meta->type],
       meta->secret ? 1 : 0,
       value.length() ? 1 : 0,
       qstr(qvalue, sizeof(qvalue), meta->secret ? "" : value.c_str()),
       qstr(qdefault, sizeof(qdefault), meta->secret ? "" : meta->defaultValue),
       qstr(qhint, sizeof(qhint), meta->hint));
}

static void cmdCfg(int argc, char **argv) {
  if (argc < 2) {
    err("CFG", "EARG", "usage: CFG LIST [group] | GET <key> | SET <key> <value> | RESET [key]");
    return;
  }
  char sub[16];
  snprintf(sub, sizeof(sub), "%s", argv[1]);
  upcase(sub);

  if (strcmp(sub, "LIST") == 0) {
    int group = -1;
    if (argc >= 3) {
      for (int i = 0; i < GROUP_COUNT; i++) {
        if (strcasecmp(settingGroupNames[i], argv[2]) == 0) { group = i; break; }
      }
      if (group < 0) {
        err("CFG", "EARG", "unknown group; see the group field of an unfiltered CFG LIST");
        return;
      }
    }
    unsigned listed = 0;
    for (uint16_t i = 0; i < configMgr.count(); i++) {
      if (group >= 0 && configMgr.metaByIndex(i)->group != group) continue;
      cfgRow(i);
      listed++;
    }
    emit("OK CFG count=%u total=%u", listed, (unsigned)configMgr.count());
    return;
  }

  if (strcmp(sub, "GET") == 0) {
    if (argc < 3) { err("CFG", "EARG", "usage: CFG GET <key>"); return; }
    const SettingMetadata *meta = configMgr.getMetadata(argv[2]);
    if (!meta) { err("CFG", "ENOKEY", "no setting by that name"); return; }
    char q[96];
    emit("OK CFG key=%s value=%s", meta->key,
         qstr(q, sizeof(q), configMgr.get(meta->key).c_str()));
    return;
  }

  if (strcmp(sub, "SET") == 0) {
    if (argc < 3) { err("CFG", "EARG", "usage: CFG SET <key> <value>"); return; }
    if (sercmdDeviceBusy()) {
      err("CFG", "EBUSY", "an uplink is in flight; try again in a moment");
      return;
    }
    const SettingMetadata *meta = configMgr.getMetadata(argv[2]);
    if (!meta) { err("CFG", "ENOKEY", "no setting by that name"); return; }

    // An omitted value means the default, matching '+key=' on the legacy
    // console; an explicit "" is a real empty string where the setting allows
    // one. The two are different and the tokeniser keeps them apart.
    String value = (argc >= 4) ? String(argv[3]) : String(meta->defaultValue);
    int error = configMgr.set(meta->key, value);
    if (error != noError) {
      char q[192];
      snprintf(q, sizeof(q), "%s (%s)", parseError(error).c_str(), meta->hint);
      err("CFG", settingErrCode(error), q);
      return;
    }
    char q[96];
    emit("OK CFG key=%s value=%s", meta->key,
         qstr(q, sizeof(q), meta->secret ? "" : value.c_str()));
    return;
  }

  if (strcmp(sub, "RESET") == 0) {
    if (sercmdDeviceBusy()) {
      err("CFG", "EBUSY", "an uplink is in flight; try again in a moment");
      return;
    }
    if (argc >= 3) {
      const SettingMetadata *meta = configMgr.getMetadata(argv[2]);
      if (!meta) { err("CFG", "ENOKEY", "no setting by that name"); return; }
      int error = configMgr.reset(meta->key);
      if (error != noError) { err("CFG", settingErrCode(error), parseError(error).c_str()); return; }
      emit("OK CFG key=%s reset=1", meta->key);
      return;
    }
    int error = configMgr.resetAll();
    if (error != noError) { err("CFG", settingErrCode(error), parseError(error).c_str()); return; }
    emit("OK CFG reset=%u", (unsigned)configMgr.count());
    return;
  }

  err("CFG", "EARG", "unknown subcommand: expected LIST, GET, SET or RESET");
}

// ---- LoRaWAN ----

static void cmdLora() {
  emit("OK LORA activated=%d devaddr=%08X method=%s version=%s dr=%u dbm=%d "
       "adr=%u confirmed=%d otaa=%d abp=%d rssi=%.1f snr=%.1f next=%u",
       sercmdActivated() ? 1 : 0, (unsigned)sercmdDevAddr(),
       cfg.actvn.method == OTAA ? "OTAA" : "ABP",
       cfg.actvn.version == v11 ? "1.1" : "1.0.4",
       cfg.uplink.dr, cfg.uplink.dbm, cfg.uplink.adr,
       cfg.uplink.confirmed ? 1 : 0,
       isValidGroupOTAA() ? 1 : 0, isValidGroupABP() ? 1 : 0,
       rssi, snr, (unsigned)nextUplink);
}

// ---- Filesystem ----
//
// Listing and deleting only. Reading a file means raw bytes on the wire, which
// does not fit a grammar where every line starts with '@' - that stays on the
// legacy '+cat' channel, which already has its own framing and its own client.

static bool safeName(const String &n) {
  return n.length() > 0 && n.length() < 64 &&
         n.indexOf('/') < 0 && n.indexOf('\\') < 0 && n.indexOf("..") < 0;
}

static void cmdFs(int argc, char **argv) {
  if (argc < 2) {
    err("FS", "EARG", "usage: FS LIST | DELETE <name>");
    return;
  }
  char sub[16];
  snprintf(sub, sizeof(sub), "%s", argv[1]);
  upcase(sub);

  if (strcmp(sub, "LIST") == 0) {
    File root = LittleFS.open("/");
    if (!root) { err("FS", "ESTATE", "the filesystem could not be opened"); return; }
    unsigned count = 0;
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        const char *raw = f.name();
        const char *name = (raw[0] == '/') ? raw + 1 : raw;
        char q[96];
        emit("DAT FS name=%s size=%u", qstr(q, sizeof(q), name), (unsigned)f.size());
        count++;
      }
      f.close();
      f = root.openNextFile();
    }
    root.close();
    emit("OK FS count=%u used=%u total=%u", count,
         (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    return;
  }

  if (strcmp(sub, "DELETE") == 0) {
    if (argc < 3) { err("FS", "EARG", "usage: FS DELETE <name>"); return; }
    String name = argv[2];
    if (!safeName(name)) { err("FS", "EARG", "not a valid file name"); return; }
    String path = "/" + name;
    if (!LittleFS.exists(path)) { err("FS", "ENOTFOUND", "no such file"); return; }
    if (!LittleFS.remove(path)) { err("FS", "ESTATE", "the file could not be removed"); return; }
    char q[96];
    emit("OK FS deleted=%s", qstr(q, sizeof(q), name.c_str()));
    return;
  }

  err("FS", "EARG", "unknown subcommand: expected LIST or DELETE");
}

// ---- Maintenance ----

static void cmdWipe(int argc, char **argv) {
  if (argc < 2) {
    err("WIPE", "EARG", "usage: WIPE CFG | LORA");
    return;
  }
  if (sercmdDeviceBusy()) {
    err("WIPE", "EBUSY", "an uplink is in flight; try again in a moment");
    return;
  }
  upcase(argv[1]);

  if (strcmp(argv[1], "CFG") == 0) {
    int error = configMgr.resetAll();
    if (error != noError) { err("WIPE", settingErrCode(error), parseError(error).c_str()); return; }
    sercmdRequestJoin();
    // The session is authenticated against the web password, which lives
    // outside the settings table and is therefore still whatever it was.
    emit("OK WIPE cfg=%u", (unsigned)configMgr.count());
    return;
  }
  if (strcmp(argv[1], "LORA") == 0) {
    sercmdWipeLoRaWAN();
    emit("OK WIPE lora=1");
    return;
  }
  err("WIPE", "EARG", "unknown subcommand: expected CFG or LORA");
}

static void cmdPass(int argc, char **argv) {
  if (argc < 2 || strcasecmp(argv[1], "RESET") != 0) {
    err("PASS", "EARG", "usage: PASS RESET");
    return;
  }
  webResetPassword();
  // The password this session authenticated against no longer exists, so the
  // session goes with it rather than outliving the credential.
  s_authed = false;
  emit("OK PASS reset=1 authed=0");
}

static void cmdLog(int argc, char **argv) {
  if (argc < 2) {
    err("LOG", "EARG", "usage: LOG NONE|ERROR|WARN|INFO|DEBUG|VERBOSE");
    return;
  }
  upcase(argv[1]);
  uint8_t level;
  if      (strcmp(argv[1], "NONE") == 0)    level = SERLOG_NONE;
  else if (strcmp(argv[1], "ERROR") == 0)   level = SERLOG_ERROR;
  else if (strcmp(argv[1], "WARN") == 0)    level = SERLOG_WARN;
  else if (strcmp(argv[1], "INFO") == 0)    level = SERLOG_INFO;
  else if (strcmp(argv[1], "DEBUG") == 0)   level = SERLOG_DEBUG;
  else if (strcmp(argv[1], "VERBOSE") == 0) level = SERLOG_VERBOSE;
  else { err("LOG", "EARG", "unknown level"); return; }

  serialLogLevel = level;
  // Honest about its reach: only PRINTF output obeys this, so a client still
  // has to ignore every line that does not start with '@'.
  emit("OK LOG level=%s partial=1", argv[1]);
}

static void cmdRestart(int argc, char **argv) {
  unsigned long delayMs = 500;
  if (argc >= 2) {
    if (!parseMillis(argv[1], delayMs)) {
      err("RESTART", "EARG", "delay must be a whole number of milliseconds");
      return;
    }
    if (delayMs < 100) delayMs = 100;
    if (delayMs > 10000) delayMs = 10000;
  }
  // Reply first, then wait it out: the delay exists so the reply has left the
  // port (and the client has read it) before the reset.
  emit("OK RESTART delay=%lu", delayMs);
  Serial.flush();
  delay(delayMs);
  ESP.restart();
}

static void cmdSleep() {
  emit("OK SLEEP delay=500");
  Serial.flush();
  delay(500);
  sercmdRequestSleep();
}

// ============================================================
// Dispatch
// ============================================================

// Commands usable without authenticating: enough to find the device, learn
// that it wants a password, and supply one. Nothing that reads device state or
// changes anything.
static bool cmdIsOpen(const char *verb) {
  return strcmp(verb, "PING") == 0 ||
         strcmp(verb, "HELP") == 0 ||
         strcmp(verb, "AUTH") == 0 ||
         strcmp(verb, "LOGOUT") == 0;
}

// Sessions expire on idle. A serial port has no hang-up to notice, so a timer
// is the only thing that ends a session when the operator walks away.
static void expireIdleSession() {
  uint32_t now = millis();
  if (s_authed && (now - s_lastCmdMs) > (uint32_t)AUTH_IDLE_TIMEOUT_S * 1000) {
    s_authed = false;
  }
  s_lastCmdMs = now;
}

static void dispatch(char *line) {
  char *argv[SERCMD_MAX_ARGS];
  int argc = tokenize(line, argv, SERCMD_MAX_ARGS);

  if (argc < 0) {
    err("?", "EARG", "unterminated quoted argument");
    return;
  }
  if (argc == 0) return;                   // blank line: no reply, no error

  upcase(argv[0]);

  // The verb is echoed in every reply so a client that pipelined several
  // commands can tell which answer belongs to which - but only when it is
  // plain ASCII, so a line of line noise cannot inject protocol characters
  // into a reply.
  const char *verb = argv[0];
  for (const char *p = argv[0]; *p != '\0'; p++) {
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) {
      verb = "?";
      break;
    }
  }

  expireIdleSession();

  if (!s_authed && !cmdIsOpen(argv[0])) {
    err(verb, "EAUTHREQ", "authentication required; send AUTH <password>");
    return;
  }

  if      (strcmp(argv[0], "PING") == 0)    emit("OK PING authed=%d", s_authed ? 1 : 0);
  else if (strcmp(argv[0], "HELP") == 0)    cmdHelp();
  else if (strcmp(argv[0], "AUTH") == 0)    cmdAuth(argc, argv);
  else if (strcmp(argv[0], "LOGOUT") == 0)  { s_authed = false; emit("OK LOGOUT authed=0"); }
  else if (strcmp(argv[0], "INFO") == 0)    cmdInfo();
  else if (strcmp(argv[0], "STATUS") == 0)  cmdStatus();
  else if (strcmp(argv[0], "SCAN") == 0)    cmdScan();
  else if (strcmp(argv[0], "WIFI") == 0)    cmdWifi(argc, argv);
  else if (strcmp(argv[0], "CFG") == 0)     cmdCfg(argc, argv);
  else if (strcmp(argv[0], "LORA") == 0)    cmdLora();
  else if (strcmp(argv[0], "UPLINK") == 0)  { sercmdRequestUplink(); emit("OK UPLINK scheduled=1"); }
  else if (strcmp(argv[0], "JOIN") == 0)    { sercmdRequestJoin();   emit("OK JOIN rejoin=1"); }
  else if (strcmp(argv[0], "WIPE") == 0)    cmdWipe(argc, argv);
  else if (strcmp(argv[0], "FS") == 0)      cmdFs(argc, argv);
  else if (strcmp(argv[0], "PASS") == 0)    cmdPass(argc, argv);
  else if (strcmp(argv[0], "LOG") == 0)     cmdLog(argc, argv);
  else if (strcmp(argv[0], "SLEEP") == 0)   cmdSleep();
  else if (strcmp(argv[0], "RESTART") == 0) cmdRestart(argc, argv);
  else                                      err(verb, "EUNKNOWN", "unknown command; send HELP");
}

// ============================================================
// Reader
// ============================================================

// The legacy console, character for character as serial.h used to run it:
// echoed while typing, backspace erases, Escape clears the line, and the
// answer is execCommand()'s error string.
static void runLegacy(char *line) {
  Serial.println();
#if SERCMD_GATE_LEGACY
  expireIdleSession();
  if (!s_authed) {
    Serial.println("Locked - send AUTH <password> first (see serial-provisioning.md)");
    return;
  }
#endif
  String command = line;
  int errorCode = execCommand(command);
  Serial.println(parseError(errorCode));
  Serial.flush();
}

void sercmdBegin() {
  char qmodel[48];
  emit("EVT READY proto=%d model=%s fw=%s auth=required",
       SERCMD_PROTO, qstr(qmodel, sizeof(qmodel), SERCMD_MODEL), MJLO_VERSION);
  s_lastCmdMs = millis();
}

void sercmdPoll() {
  static char line[SERCMD_MAX_LINE + 1];
  static size_t len = 0;
  static bool overflow = false;

  while (Serial.available()) {
    int got = Serial.read();
    if (got < 0) break;                    // raced by the driver; try next loop
    char c = (char)got;

    // Only the legacy console echoes, and only from its leading '+'. A
    // protocol line is never echoed, which is what keeps 'AUTH <password>' out
    // of the terminal and out of the monitor log file.
    bool echo = (len > 0 && line[0] == '+');

    if (c == '\r') continue;               // CRLF and bare LF both end a line

    if (c == '\n') {
      if (overflow) {
        // Answered on the channel the line belonged to: a '@ERR' at someone
        // typing an over-long '+cat=' would be the wrong language entirely.
        if (len > 0 && line[0] == '+') {
          Serial.printf("\r\nMaximum input length exceeded (%d characters)\r\n", SERCMD_MAX_LINE);
          Serial.flush();
        } else {
          err("?", "ELINE", "command line too long");
        }
      } else if (len > 0) {
        line[len] = '\0';
        if (line[0] == '+') runLegacy(line);
        else                dispatch(line);
      }
      len = 0;
      overflow = false;
      continue;
    }

    if (c == 8 || c == 127) {              // backspace
      if (len > 0) {
        len--;
        if (echo) Serial.print("\b \b");
      }
      continue;
    }

    if (c == 27) {                         // escape: abandon the line
      if (echo) {
        for (size_t i = 0; i < len; i++) Serial.print("\b \b");
      }
      len = 0;
      overflow = false;
      continue;
    }

    // Other control bytes cannot appear in a valid command and would otherwise
    // survive into an argument.
    if ((unsigned char)c < 0x20) continue;

    if (len >= SERCMD_MAX_LINE) {
      // Keep consuming to the line end, so an overlong line is reported once
      // instead of being split into fragments that each parse as a command.
      overflow = true;
      continue;
    }

    line[len++] = c;
    if (len == 1 ? (c == '+') : echo) Serial.print(c);
  }
}
