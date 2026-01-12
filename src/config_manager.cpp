#include "config_manager.h"
#include "config.h"

// ============= Validators Implementation =============

int validateVersion(const String& val) {
  String v = val;
  v.toLowerCase();
  if (v == "1.0.4" || v == "0" || v == "1.0.4r" || v == "0r" || 
      v == "1.1" || v == "1") {
    return noError;
  }
  return valueError;
}

int validateMethod(const String& val) {
  String v = val;
  v.toUpperCase();
  if (v == "ABP" || v == "0" || v == "OTAA" || v == "1") {
    return noError;
  }
  return valueError;
}

int validateRelay(const String& val) {
  String v = val;
  v.toUpperCase();
  if (v == "OFF" || v == "0") return noError;
  
  uint8_t mode, smartlevel, backoff;
  if (sscanf(val.c_str(), "%hhu,%hhu,%hhu", &mode, &smartlevel, &backoff) == 3) {
    return noError;
  }
  return valueError;
}

int validateADR(const String& val) {
  String v = val;
  v.toUpperCase();
  if (v == "N" || v == "NO" || v == "OFF" || v == "0" ||
      v == "Y" || v == "YES" || v == "ON" || v == "1") {
    return noError;
  }
  if (v.startsWith("DR,") || v.startsWith("DBM,")) {
    return noError;
  }
  return valueError;
}

int validateDataRate(const String& val) {
  String v = val;
  v.toUpperCase();
  if (v == "0" || v == "SF12" || v == "SF12BW125" ||
      v == "1" || v == "SF11" || v == "SF11BW125" ||
      v == "2" || v == "SF10" || v == "SF10BW125" ||
      v == "3" || v == "SF9" || v == "SF9BW125" ||
      v == "4" || v == "SF8" || v == "SF8BW125" ||
      v == "5" || v == "SF7" || v == "SF7BW125" ||
      v == "6" || v == "SF7BW250" ||
      v == "7" || v == "FSK") {
    return noError;
  }
  return valueError;
}

int validateDBm(const String& val) {
  int valInt = val.toInt();
  if (valInt >= -16 && valInt <= 16) {  // Default band is EU868 with max 16 dBm
    return noError;
  }
  return valueError;
}

int validateBoolean(const String& val) {
  String v = val;
  v.toUpperCase();
  if (v == "N" || v == "NO" || v == "OFF" || v == "0" ||
      v == "Y" || v == "YES" || v == "ON" || v == "1") {
    return noError;
  }
  return valueError;
}

int validateInterval(const String& val) {
  String v = val;
  v.toLowerCase();
  if (v.startsWith("dc,")) {
    String subval = v.substring(3);
    if (subval == "fup" || subval == "0.1%" || subval == "1%") {
      return noError;
    }
    int num = subval.toInt();
    if (num > 0 && num < 8640) return noError;
    return valueError;
  }
  if (v.startsWith("fixed,")) {
    String subval = v.substring(6);
    int num = subval.toInt();
    if (num >= 10 && num <= 65535) return noError;
    return valueError;
  }
  return valueError;
}

int validateOperation(const String& val) {
  String v = val;
  v.toLowerCase();
  if (v == "stationary" || v == "0") return noError;
  if (v == "mobile" || v == "1") return noError;
  if (v.startsWith("mobile,") || v.startsWith("1,")) {
    String subval = v.substring(v.indexOf(",") + 1);
    int num = subval.toInt();
    if (num > 0 && num < 256) return noError;
    return valueError;
  }
  return valueError;
}

int validateTimeout(const String& val) {
  if (val.length() == 0) return noError;
  int timeout = val.toInt();
  if (timeout < 0 || timeout > 3600) return valueError;
  return noError;
}

int validateHexString(const String& val, uint16_t expectedLength) {
  if (val.length() > 0 && val.length() != expectedLength) return valueError;
  if (val.length() > 0 && !isHexString(val)) return valueError;
  return noError;
}

int validateTimezone(const String& val) {
  String v = val;
  v.trim();
  if (v.length() == 0) return valueError;

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

  if (minutes < -720 || minutes > 840) return valueError;  // -12:00 .. +14:00
  return noError;
}

int validateDST(const String& val) {
  String v = val;
  v.trim();
  if (v.length() == 0) return valueError;
  int vInt = v.toInt();
  if (abs(vInt) <= 12) vInt = vInt * 60;
  if (vInt < 0 || vInt > 720) return valueError;
  return noError;
}

int validateName(const String& val) {
  if (val.length() >= 4 && val.length() <= 16) return noError;
  return valueError;
}

int validateSSID(const String& val) {
  if (val.length() >= 1 && val.length() <= 32) return noError;
  return valueError;
}

int validatePassword(const String& val) {
  if (val.length() >= 8 && val.length() <= 64) return noError;
  return valueError;
}

int validateUser(const String& val) {
  if (val.length() <= 64) return noError;
  return valueError;
}

// Hex validators - each checks format and expected length
int validateHex8(const String& val) {
  if (val.length() > 0 && val.length() != 8) return valueError;
  if (val.length() > 0 && !isHexString(val)) return valueError;
  return noError;
}

int validateHex16(const String& val) {
  if (val.length() > 0 && val.length() != 16) return valueError;
  if (val.length() > 0 && !isHexString(val)) return valueError;
  return noError;
}

int validateHex32(const String& val) {
  if (val.length() > 0 && val.length() != 32) return valueError;
  if (val.length() > 0 && !isHexString(val)) return valueError;
  return noError;
}

// ============= ConfigManager Implementation =============

ConfigManager::ConfigManager(const SettingMetadata* meta, uint16_t count)
  : metadata(meta), metadataCount(count), isDirty(false), inTransaction(false) {
  memset(settingDirty, 0, sizeof(settingDirty));
  memset(cacheValid, 0, sizeof(cacheValid));
}

int ConfigManager::load() {
  if (!nvs.begin("config", true)) {  // Read-only mode
    PRINTF("Failed to open NVS config namespace");
    return busyError;
  }

  invalidateAllCache();

  for (uint16_t i = 0; i < metadataCount; i++) {
    const SettingMetadata& meta = metadata[i];
    String value = nvs.getString(meta.key, meta.defaultValue);
    valueCache[i] = value;
    cacheValid[i] = true;
    settingDirty[i] = false;
  }

  nvs.end();
  isDirty = false;
  return noError;
}

int ConfigManager::save() {
  if (!isDirty && !inTransaction) return noError;
  
  if (!nvs.begin("config", false)) {  // Read-write mode
    PRINTF("Failed to open NVS config namespace for writing");
    return busyError;
  }
  
  for (uint16_t i = 0; i < metadataCount; i++) {
    if (settingDirty[i]) {
      const SettingMetadata& meta = metadata[i];
      nvs.putString(meta.key, valueCache[i]);
      settingDirty[i] = false;
    }
  }

  nvs.end();
  isDirty = false;
  return noError;
}

int ConfigManager::saveSetting(const char* key) {
  uint16_t idx = getMetadataIndex(key);
  if (idx >= metadataCount) return keyError;

  if (!nvs.begin("config", false)) {
    return busyError;
  }

  nvs.putString(metadata[idx].key, valueCache[idx]);
  settingDirty[idx] = false;

  nvs.end();
  return noError;
}

int ConfigManager::set(const char* key, const String& value) {
  uint16_t idx = getMetadataIndex(key);
  if (idx >= metadataCount) return keyError;

  const SettingMetadata& meta = metadata[idx];
  
  // Validate
  if (meta.validator) {
    int result = meta.validator(value);
    if (result != noError) return result;
  }

  // Check for length limits on strings
  if (meta.maxLength > 0 && value.length() > meta.maxLength) {
    return lengthError;
  }

  // Only mark dirty if value actually changed
  if (valueCache[idx] != value) {
    valueCache[idx] = value;
    settingDirty[idx] = true;
    isDirty = true;
  }

  cacheValid[idx] = true;
  
  // Auto-save if not in transaction
  if (!inTransaction) {
    return save();
  }

  return noError;
}

String ConfigManager::get(const char* key) {
  uint16_t idx = getMetadataIndex(key);
  if (idx >= metadataCount) return "";
  return getByIndex(idx);
}

int ConfigManager::setByIndex(uint16_t idx, const String& value) {
  if (idx >= metadataCount) return keyError;
  return set(metadata[idx].key, value);
}

String ConfigManager::getByIndex(uint16_t idx) {
  if (idx >= metadataCount) return "";
  if (!cacheValid[idx]) {
    // Load from NVS if not cached
    if (nvs.begin("config", true)) {
      valueCache[idx] = nvs.getString(metadata[idx].key, metadata[idx].defaultValue);
      nvs.end();
      cacheValid[idx] = true;
    }
  }
  return valueCache[idx];
}

String ConfigManager::printSettings(int group) {
  String result = "";
  for (uint16_t i = 0; i < metadataCount; i++) {
    if (group < 0 || metadata[i].group == group) {
      result += "\r\n+" + String(metadata[i].displayName) + "=" + getByIndex(i);
    }
  }
  return result;
}

int ConfigManager::resetToDefaults() {
  invalidateAllCache();
  for (uint16_t i = 0; i < metadataCount; i++) {
    valueCache[i] = String(metadata[i].defaultValue);
    settingDirty[i] = true;
    cacheValid[i] = true;
  }
  isDirty = true;
  return noError;
}

int ConfigManager::resetToDefaultsAndSave() {
  resetToDefaults();
  return save();
}

void ConfigManager::beginTransaction() {
  inTransaction = true;
  isDirty = false;
  memset(settingDirty, 0, sizeof(settingDirty));
}

int ConfigManager::commitTransaction() {
  inTransaction = false;
  return save();
}

void ConfigManager::cancelTransaction() {
  inTransaction = false;
  // Invalidate cache and reload from NVS
  invalidateAllCache();
  memset(settingDirty, 0, sizeof(settingDirty));
  isDirty = false;
}

int ConfigManager::validate(const char* key, const String& value) {
  const SettingMetadata* meta = getMetadata(key);
  if (!meta) return keyError;
  if (meta->validator) return meta->validator(value);
  return noError;
}

const SettingMetadata* ConfigManager::getMetadata(const char* key) {
  uint16_t idx = getMetadataIndex(key);
  if (idx >= metadataCount) return nullptr;
  return &metadata[idx];
}

uint16_t ConfigManager::getMetadataIndex(const char* key) {
  for (uint16_t i = 0; i < metadataCount; i++) {
    if (strcmp(metadata[i].key, key) == 0) return i;
  }
  return metadataCount;  // Not found
}

bool ConfigManager::isSettingDirty(const char* key) {
  uint16_t idx = getMetadataIndex(key);
  if (idx >= metadataCount) return false;
  return settingDirty[idx];
}

void ConfigManager::invalidateCache(uint16_t idx) {
  if (idx < 50) {
    cacheValid[idx] = false;
  }
}

void ConfigManager::invalidateAllCache() {
  memset(cacheValid, 0, sizeof(cacheValid));
}
