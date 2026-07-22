// ============================================================================
//  Display.h  --  GC9A01 + CST816D bring-up and LVGL driver registration.
// ============================================================================
#pragma once
#include <lvgl.h>

void display_init();               // init panel, touch, LVGL buffers/drivers
void display_tick();               // feed LVGL its millisecond tick
lv_disp_t* display_lv_disp();      // the registered LVGL display
void display_backlight(uint8_t pct);  // 0..100
