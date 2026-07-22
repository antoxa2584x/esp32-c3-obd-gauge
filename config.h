// ============================================================================
//  config.h  --  Board pins, WiFi/OBD endpoint and app-wide constants
//  Target board: ESP32-2424S012 (ESP32-C3 + GC9A01 240x240 round + CST816D)
// ============================================================================
#pragma once

// ---------------------------------------------------------------------------
//  ELM327 WiFi adapter (V1.5 / PIC18F25K80) network endpoint
//  The adapter is a WiFi Access Point; this board joins it as a station.
//  Adjust SSID/PASS below to match YOUR adapter (check the label / manual).
//  Common SSIDs: "WiFi_OBDII", "WiFi327", "V-LINK", "OBDII".
//  Most V1.5 adapters run an OPEN network (empty password).
// ---------------------------------------------------------------------------
#define OBD_WIFI_SSID      "WiFi_OBDII"
#define OBD_WIFI_PASS      ""                 // "" for open networks
#define OBD_HOST           "192.168.0.10"     // ELM327 default IP
#define OBD_PORT           35000              // ELM327 default TCP port

// Timeouts (ms)
#define OBD_WIFI_TIMEOUT   15000              // give up joining AP after this
#define OBD_TCP_TIMEOUT    8000               // socket connect timeout
#define OBD_RSP_TIMEOUT    5000               // wait for '>' after a command
#define OBD_POLL_INTERVAL  60                 // ms between live PID polls

// ---------------------------------------------------------------------------
//  Display (GC9A01) SPI pins
// ---------------------------------------------------------------------------
#define PIN_LCD_SCLK   6
#define PIN_LCD_MOSI   7
#define PIN_LCD_MISO  -1     // not used by GC9A01
#define PIN_LCD_DC     2
#define PIN_LCD_CS    10
#define PIN_LCD_RST   -1     // tied on this board
#define PIN_LCD_BL     3     // backlight (active high)

// ---------------------------------------------------------------------------
//  Touch (CST816D) I2C pins
// ---------------------------------------------------------------------------
#define PIN_TP_SDA     4
#define PIN_TP_SCL     5
#define PIN_TP_INT     0
#define PIN_TP_RST     1
#define TP_I2C_ADDR    0x15

// Panel geometry
#define SCREEN_W      240
#define SCREEN_H      240

// LVGL draw buffer height (lines). 40 lines * 240 * 2B * 2 buffers ~= 38 KB.
// No PSRAM on this board, so keep it small.
#define LVGL_BUF_LINES 40

// Gauge ranges
#define RPM_MAX        8000
#define TEMP_MIN_C      40
#define TEMP_MAX_C     130
