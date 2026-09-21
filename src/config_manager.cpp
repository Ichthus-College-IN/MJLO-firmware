#include "config_manager.h"
#include "config.h"

// NVS namespace shared with the web password (see webserver.cpp). Only the
// keys in the metadata table are touched here, so the two cannot collide.
#define CONFIG_NAMESPACE "config"

const char *const settingGroupNames[GROUP_COUNT] = {
  "LoRaWAN",
  "Uplink",
  "OTAA",
  "ABP",
  "WiFi",
  "Time"
};

const char *const settingTypeNames[] = {
  "enum", "bool", "int", "text", "hex", "list"
};

// ============= ConfigManager Implementation =============

ConfigManager::ConfigManager(const SettingMetadata *meta, uint16_t count)
  : _meta(meta), _count(count), _value(new String[count]) {
  // Start from the defaults, so getByIndex() is answerable before load() has
  // run. The old code guarded every read with a "is this cache slot valid"
  // flag and a lazy NVS re-read to cover the same window; seeding the cache
  // removes the window instead of handling it.
  for (uint16_t i = 0; i < _count; i++) {
    _value[i] = _meta[i].defaultValue;
  }
}

ConfigManager::~ConfigManager() {
  delete[] _value;
}

int ConfigManager::load() {
  Preferences nvs;
  if (!nvs.begin(CONFIG_NAMESPACE, true)) {
    PRINTF("[Config] Could not open NVS namespace '%s'", CONFIG_NAMESPACE);
    return busyError;
  }

  for (uint16_t i = 0; i < _count; i++) {
    const SettingMetadata &meta = _meta[i];
    String value = nvs.getString(meta.key, meta.defaultValue);

    // A value that no longer validates is a stored value from an older
    // firmware, or one a tightened range has left behind. Falling back to the
    // default keeps `cfg` and the cache agreeing with each other; silently
    // keeping the bad string would leave `cfg` holding whatever the previous
    // setting had put there.
    if (meta.parse(value, &cfg) != noError) {
      PRINTF("[Config] Rejected stored value for '%s'; using the default", meta.key);
      value = meta.defaultValue;
      meta.parse(value, &cfg);
    }
    _value[i] = value;
  }

  nvs.end();
  return noError;
}

int ConfigManager::store(uint16_t idx, const String &value) {
  Preferences nvs;
  if (!nvs.begin(CONFIG_NAMESPACE, false)) {
    PRINTF("[Config] Could not open NVS namespace '%s' for writing", CONFIG_NAMESPACE);
    return busyError;
  }
  bool ok = nvs.putString(_meta[idx].key, value) > 0 || value.length() == 0;
  nvs.end();
  return ok ? noError : fileError;
}

int ConfigManager::set(const char *key, const String &value) {
  uint16_t idx = getMetadataIndex(key);
  if (idx >= _count) return keyError;

  const SettingMetadata &meta = _meta[idx];

  if (meta.maxLength > 0 && value.length() > meta.maxLength) return lengthError;

  // One pass decides both questions: is this acceptable, and what does it mean?
  int error = meta.parse(value, &cfg);
  if (error != noError) return error;

  if (_value[idx] == value) return noError;   // nothing to write

  _value[idx] = value;
  return store(idx, value);
}

int ConfigManager::setByIndex(uint16_t idx, const String &value) {
  if (idx >= _count) return keyError;
  return set(_meta[idx].key, value);
}

String ConfigManager::get(const char *key) const {
  return getByIndex(getMetadataIndex(key));
}

String ConfigManager::getByIndex(uint16_t idx) const {
  if (idx >= _count) return String();
  return _value[idx];
}

bool ConfigManager::has(const char *key) const {
  return get(key).length() > 0;
}

int ConfigManager::validate(const char *key, const String &value) const {
  const SettingMetadata *meta = getMetadata(key);
  if (!meta) return keyError;
  if (meta->maxLength > 0 && value.length() > meta->maxLength) return lengthError;
  return meta->parse(value, nullptr);
}

int ConfigManager::reset(const char *key) {
  const SettingMetadata *meta = getMetadata(key);
  if (!meta) return keyError;
  return set(key, String(meta->defaultValue));
}

int ConfigManager::resetAll() {
  Preferences nvs;
  if (!nvs.begin(CONFIG_NAMESPACE, false)) return busyError;

  for (uint16_t i = 0; i < _count; i++) {
    _value[i] = _meta[i].defaultValue;
    _meta[i].parse(_value[i], &cfg);
    nvs.putString(_meta[i].key, _value[i]);
  }

  nvs.end();
  return noError;
}

String ConfigManager::printSettings(int group) const {
  String result = "";
  for (uint16_t i = 0; i < _count; i++) {
    if (group < 0 || _meta[i].group == group) {
      result += "\r\n+" + String(_meta[i].displayName) + "=" + _value[i];
    }
  }
  return result;
}

const SettingMetadata *ConfigManager::metaByIndex(uint16_t idx) const {
  if (idx >= _count) return nullptr;
  return &_meta[idx];
}

const SettingMetadata *ConfigManager::getMetadata(const char *key) const {
  uint16_t idx = getMetadataIndex(key);
  if (idx >= _count) return nullptr;
  return &_meta[idx];
}

uint16_t ConfigManager::getMetadataIndex(const char *key) const {
  if (!key) return _count;
  for (uint16_t i = 0; i < _count; i++) {
    if (strcasecmp(_meta[i].key, key) == 0) return i;
  }
  return _count;  // not found
}
