#include "Settings.h"
#include "config.h"
#include <Preferences.h>

AppSettings settings;
static Preferences prefs;

void settings_load() {
  prefs.begin("obdgauge", true);            // read-only
  settings.darkTheme = prefs.getBool("dark", true);
  settings.metric    = prefs.getBool("metric", true);
  settings.rpmMax    = prefs.getInt("rpmmax", 8000);
  settings.warnTempC = prefs.getInt("warnc", 110);
  settings.brightness = prefs.getInt("bri", 70);
  settings.ssid      = prefs.getString("ssid", OBD_WIFI_SSID);
  settings.pass      = prefs.getString("pass", OBD_WIFI_PASS);
  prefs.end();
}

void settings_save() {
  prefs.begin("obdgauge", false);           // read-write
  prefs.putBool("dark", settings.darkTheme);
  prefs.putBool("metric", settings.metric);
  prefs.putInt("rpmmax", settings.rpmMax);
  prefs.putInt("warnc", settings.warnTempC);
  prefs.putInt("bri", settings.brightness);
  prefs.putString("ssid", settings.ssid);
  prefs.putString("pass", settings.pass);
  prefs.end();
}

float temp_display(int celsius) {
  return settings.metric ? (float)celsius : (celsius * 9.0f / 5.0f) + 32.0f;
}
const char* temp_unit()  { return settings.metric ? "\xC2\xB0""C" : "\xC2\xB0""F"; }
const char* speed_unit() { return settings.metric ? "km/h" : "mph"; }

float speed_display(int kmh) {
  return settings.metric ? (float)kmh : kmh * 0.621371f;
}
