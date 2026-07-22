// ============================================================================
//  ESP32 WiFi OBD-II Gauge
//  Board : ESP32-2424S012 (ESP32-C3 + GC9A01 240x240 round + CST816D touch)
//  Link  : ELM327 WiFi V1.5 (PIC18F25K80) @ 192.168.0.10:35000
//
//  Screens: connect  ->  swipe between  RPM | Coolant | Trouble codes | Settings
//  Features: dark/light theme, metric/imperial units (persisted to NVS).
//
//  Everything runs single-threaded in loop() -- the ESP32-C3 has one core and
//  LVGL is not thread-safe, so we cooperatively drive OBD polling + the UI.
//  See README.md for library versions and lv_conf.h setup.
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "Settings.h"
#include "Display.h"
#include "Theme.h"
#include "ObdClient.h"
#include "Ui.h"

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n[OBD Gauge] boot");

  settings_load();     // theme + units + saved SSID from NVS
  display_init();      // GC9A01 + CST816D + LVGL
  ui_init();           // build screens; shows WiFi picker + starts a scan
  // obd.begin() is called by the UI once a network is chosen.
}

void loop() {
  obd.loop();          // non-blocking OBD state machine
  ui_tick();           // screen switching + value refresh
  display_tick();      // feed LVGL its tick
  lv_timer_handler();  // render
  delay(5);
}
