// ============================================================================
//  Settings.h  --  Persistent user preferences (theme, units) via NVS.
// ============================================================================
#pragma once
#include <Arduino.h>

struct AppSettings {
  bool   darkTheme = true;   // true = dark, false = light
  bool   metric    = true;   // true = km/h,°C ; false = mph,°F
  int    rpmMax    = 8000;   // RPM gauge full-scale
  int    warnTempC = 110;    // coolant warning threshold (°C, native)
  int    brightness = 70;    // backlight %  (10..100)
  String ssid;               // chosen OBD adapter SSID
  String pass;               // its password ("" = open)
};

extern AppSettings settings;

void settings_load();      // read from NVS (call in setup)
void settings_save();      // persist current values

// Unit-conversion helpers (input is always the native OBD unit)
float temp_display(int celsius);       // -> °C or °F depending on setting
const char* temp_unit();               // "°C" / "°F"
const char* speed_unit();              // "km/h" / "mph"
float speed_display(int kmh);          // -> km/h or mph
