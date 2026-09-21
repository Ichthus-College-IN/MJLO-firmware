#ifndef _CONFIG_MANAGER_H
#define _CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "helpers.h"

// Config lives in config.h, which includes this header; a forward declaration
// is all the parse functions below need to write into it.
struct Config;

// ============= Error Codes =============
enum ErrorCode {
  noError = 0,
  lengthError = 1,
  formatError = 2,
  commandError = 3,
  keyError = 4,
  valueError = 5,
  busyError = 6,
  fileError = 7,
  NUM_ERRORS = 8
};

// ============= Setting Metadata =============

enum SettingGroup {
  GROUP_LORAWAN = 0,
  GROUP_UPLINK = 1,
  GROUP_ACTIVATION_OTAA = 2,
  GROUP_ACTIVATION_ABP = 3,
  // Deliberately absent from the web dashboard, in both directions: it is
  // neither listed nor writable there. A dashboard reachable only over WiFi
  // should not be able to move the box to a different WiFi network, because
  // the page that would let you correct a mistake sits on the far side of it.
  // These settings are the serial console's and the display menu's alone.
  GROUP_WIFI_2G4 = 4,
  GROUP_TIME = 5,
  GROUP_COUNT = 6
};

// Group labels, so a UI (web page, serial client) can render the same headings
// the firmware groups by instead of keeping its own copy of the grouping.
extern const char *const settingGroupNames[GROUP_COUNT];

// What a value looks like, for clients that want to pick an input widget.
// This is a hint about shape only - `parse` remains the authority on what is
// actually accepted.
enum SettingType {
  TYPE_ENUM = 0,    // one of a fixed set of words
  TYPE_BOOL = 1,    // on/off
  TYPE_INT = 2,     // a whole number
  TYPE_TEXT = 3,    // free text
  TYPE_HEX = 4,     // fixed-length hex string
  TYPE_LIST = 5     // comma-separated compound (e.g. "fixed,30")
};

extern const char *const settingTypeNames[];

// One function per setting, used for two jobs that used to be written twice:
//
//   parse(value, nullptr)  - validate only, returns an ErrorCode
//   parse(value, &cfg)     - validate and, on success, apply to `cfg`
//
// Keeping both in one function is the point of the rework: the old split
// between a validateX() and a matching branch of applySetting() meant every
// accepted format had to be spelled out twice, and the two drifted (ADR's
// "DR,..." form validated whatever followed the comma, then silently applied
// nothing if it did not parse). A single function cannot disagree with itself.
typedef int (*SettingFn)(const String &value, Config *out);

struct SettingMetadata {
  const char *key;           // NVS key and wire name, e.g. "dr" (<= 15 chars)
  const char *displayName;   // label as printConfig() and the UIs show it
  SettingGroup group;
  SettingType type;
  const char *defaultValue;  // applied when nothing is stored, or on reset
  SettingFn parse;           // validate (+ apply); never null
  uint16_t maxLength;        // hard cap on the stored string, 0 = none
  bool secret;               // a key or passphrase: do not show it unasked
  const char *hint;          // accepted values, in one line, for UIs and HELP
};

// ============= Configuration Manager =============
//
// Owns the string form of every setting: the copy in NVS, the cache in RAM
// and the parsed copy in `cfg` are kept in step by going through set().
//
// There is no dirty tracking and no transaction support any more. Both existed
// to batch NVS writes, but set() auto-saved on every call anyway, so the batch
// never happened and the bookkeeping only added ways to get out of step. A
// set() now writes exactly the one key it changed.
class ConfigManager {
  public:
    ConfigManager(const SettingMetadata *meta, uint16_t count);
    ~ConfigManager();

    // Read every setting from NVS into the cache and apply it to `cfg`.
    // A stored value that no longer validates (a downgrade, a tightened
    // range) falls back to the default rather than leaving `cfg` untouched.
    int load();

    // Validate, apply to `cfg`, cache and persist one setting.
    int set(const char *key, const String &value);
    int setByIndex(uint16_t idx, const String &value);

    // Current value; "" for an unknown key.
    String get(const char *key) const;
    String getByIndex(uint16_t idx) const;

    // Is a value stored (i.e. non-empty)? The activation-key checks want this
    // and nothing else, and it reads better than comparing lengths.
    bool has(const char *key) const;

    // Validate without applying or storing.
    int validate(const char *key, const String &value) const;

    // Back to the compiled-in default.
    int reset(const char *key);
    int resetAll();

    // "\r\n+<Name>=<value>" per setting, for one group or (group < 0) all.
    String printSettings(int group = -1) const;

    uint16_t count() const { return _count; }
    const SettingMetadata *metaByIndex(uint16_t idx) const;
    const SettingMetadata *getMetadata(const char *key) const;
    uint16_t getMetadataIndex(const char *key) const;

  private:
    int store(uint16_t idx, const String &value);

    const SettingMetadata *_meta;
    uint16_t _count;
    String *_value;            // one cached string per setting, never null
};

// Global config manager instance
extern ConfigManager configMgr;

#endif
