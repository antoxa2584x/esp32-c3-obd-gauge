// ============================================================================
//  lv_conf.h  --  LVGL 8.x configuration for the ESP32 WiFi OBD Gauge.
//
//  IMPORTANT (Arduino IDE):
//    LVGL only finds this file if it sits NEXT TO the lvgl library folder, i.e.
//        Arduino/libraries/lv_conf.h        (one level ABOVE the lvgl/ folder)
//    Copy this file there, OR add  -DLV_CONF_PATH=".../lv_conf.h"  build flag.
//    Anything not set here falls back to LVGL's built-in defaults.
// ============================================================================
#ifndef LV_CONF_H
#define LV_CONF_H
#include <stdint.h>

// --- Color ---
#define LV_COLOR_DEPTH      16
#define LV_COLOR_16_SWAP    0        // byte-swap done in the flush callback

// --- Memory (no PSRAM on ESP32-C3; keep modest) ---
#define LV_MEM_CUSTOM       0
#define LV_MEM_SIZE         (48U * 1024U)

// --- Tick: we call lv_tick_inc() ourselves from display_tick() ---
#define LV_TICK_CUSTOM      0

#define LV_DPI_DEF          130
#define LV_USE_LOG          0
#define LV_USE_ASSERT_NULL  1

// --- Fonts used by the UI ---
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_28   1
#define LV_FONT_MONTSERRAT_48   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14

// --- Widgets (explicitly on; most default on in v8) ---
#define LV_USE_ARC          1
#define LV_USE_LABEL        1
#define LV_USE_BTN          1
#define LV_USE_LIST         1
#define LV_USE_SWITCH       1
#define LV_USE_TILEVIEW     1
#define LV_USE_SPINNER      1
#define LV_USE_MSGBOX       1
#define LV_USE_ROLLER       1

// --- Theme ---
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_GROW   1
#define LV_THEME_DEFAULT_TRANSITION_TIME  80

#endif // LV_CONF_H
