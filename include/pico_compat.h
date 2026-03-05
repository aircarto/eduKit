/**
 * Pico Compatibility Layer
 * Emule la classe Preferences de l'ESP32 avec LittleFS
 */

#ifndef PICO_COMPAT_H
#define PICO_COMPAT_H

#include <Arduino.h>
#include <LittleFS.h>

static bool _littlefs_started = false;

static void ensureLittleFS() {
  if (!_littlefs_started) {
    _littlefs_started = LittleFS.begin();
  }
}

class Preferences {
private:
  String _namespace;
  bool _readOnly;

  String makePath(const char* key) {
    return "/" + _namespace + "_" + String(key);
  }

public:
  Preferences() : _readOnly(false) {}

  bool begin(const char* name, bool readOnly = false) {
    _namespace = String(name);
    _readOnly = readOnly;
    ensureLittleFS();
    return _littlefs_started;
  }

  void end() {}

  String getString(const char* key, const String& defaultValue = "") {
    String path = makePath(key);
    if (!LittleFS.exists(path)) return defaultValue;

    File f = LittleFS.open(path, "r");
    if (!f) return defaultValue;

    String value = f.readString();
    f.close();
    return value;
  }

  int getInt(const char* key, int defaultValue = 0) {
    String val = getString(key, String(defaultValue));
    return val.toInt();
  }

  bool putString(const char* key, const String& value) {
    if (_readOnly) return false;
    String path = makePath(key);
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print(value);
    f.close();
    return true;
  }

  bool putInt(const char* key, int value) {
    return putString(key, String(value));
  }

  void clear() {
    if (_readOnly) return;
  }
};

#endif // PICO_COMPAT_H
