// ============================================================================
//  Theme.h  --  Light / dark palette. Colors are resolved at call time from
//  the current settings.darkTheme flag, so a theme switch is instant.
// ============================================================================
#pragma once
#include <lvgl.h>

lv_color_t th_bg();        // screen background
lv_color_t th_card();      // panel / card surface
lv_color_t th_text();      // primary text
lv_color_t th_text_dim();  // secondary text
lv_color_t th_accent();    // RPM / primary accent
lv_color_t th_track();     // gauge track (unfilled arc)
lv_color_t th_temp();      // temperature accent
lv_color_t th_danger();    // errors / warnings
lv_color_t th_ok();        // green (low-rev / safe zone)

// (Re)apply the base LVGL theme (buttons, switches, lists) for current mode.
void theme_apply(lv_disp_t* disp);
