#ifndef _CONFIG_MANAGER_H
#define _CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "helpers.h"

// ============= Error Codes =============
enum ErrorCode {
  noError = 0,
  lengthError = 1,
  formatError = 2,
  commandError = 3,
  keyError = 4,
  valueError = 5,
  busyError = 6,
  NUM_ERRORS = 7
};

// ============= Setting Metadata =============

enum SettingGroup {
  GROUP_LORAWAN = 0,
  GROUP_UPLINK = 1,
  GROUP_ACTIVATION_OTAA = 2,
  GROUP_ACTIVATION_ABP = 3,
  GROUP_WIFI_2G4 = 4,
  GROUP_TIME = 5,
  GROUP_COUNT = 6
};

// Validator function type: returns error code (0 = success)
typedef int (*ValidatorFn)(const String& value);

struct SettingMetadata {
  const char* key;           // e.g., "version", "dr", "timezone"
  const char* displayName;   // e.g., "Version", "Data Rate"
  SettingGroup group;
  const char* defaultValue;  // String representation
  ValidatorFn validator;     // nullptr = no validation
  uint16_t maxLength;        // For strings, 0 = no limit
};

// ============= Configuration Manager Class =============

class ConfigManager {
private:
  Preferences nvs;
  const SettingMetadata* metadata;
  uint16_t metadataCount;
  bool isDirty;
  
  // Dirty tracking per setting (assuming max 50 settings)
  bool settingDirty[50];
  
  // Cache for string values
  String valueCache[50];
  bool cacheValid[50];
  
  // Transaction support
  bool inTransaction;
  
public:
  ConfigManager(const SettingMetadata* meta, uint16_t count);
  
  // Load all settings from flash
  int load();
  
  // Save only changed settings to flash
  int save();
  
  // Force save a specific setting
  int saveSetting(const char* key);
  
  // Set a single setting (validated, marked dirty)
  int set(const char* key, const String& value);
  
  // Get a setting value (from cache if available)
  String get(const char* key);
  
  // Get setting by index
  int setByIndex(uint16_t idx, const String& value);
  String getByIndex(uint16_t idx);
  
  // Print all settings (optionally for a group)
  String printSettings(int group = -1);
  
  // Reset to defaults (without writing to flash)
  int resetToDefaults();
  
  // Reset and save
  int resetToDefaultsAndSave();
  
  // Transaction support
  void beginTransaction();
  int commitTransaction();
  void cancelTransaction();
  
  // Validate a setting without applying
  int validate(const char* key, const String& value);
  
  // Get metadata for a setting
  const SettingMetadata* getMetadata(const char* key);
  uint16_t getMetadataIndex(const char* key);
  
  // Check if dirty
  bool isDirtyFlag() const { return isDirty; }
  bool isSettingDirty(const char* key);
  
private:
  void invalidateCache(uint16_t idx);
  void invalidateAllCache();
};

// ============= Validators (Reusable) =============

int validateVersion(const String& val);
int validateMethod(const String& val);
int validateRelay(const String& val);
int validateADR(const String& val);
int validateDataRate(const String& val);
int validateDBm(const String& val);
int validateBoolean(const String& val);
int validateInterval(const String& val);
int validateOperation(const String& val);
int validateTimeout(const String& val);
int validateTimezone(const String& val);
int validateDST(const String& val);
int validateName(const String& val);
int validateSSID(const String& val);
int validatePassword(const String& val);
int validateUser(const String& val);

// Hex validators for keys (with length validation)
int validateHex8(const String& val);      // 8 hex characters (4 bytes)
int validateHex16(const String& val);     // 16 hex characters (8 bytes)
int validateHex32(const String& val);     // 32 hex characters (16 bytes)

// Global config manager instance
extern ConfigManager configMgr;

#endif
