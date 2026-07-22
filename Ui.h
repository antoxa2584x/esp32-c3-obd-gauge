// ============================================================================
//  Ui.h  --  LVGL screens: connect, RPM gauge, temp gauge, error scan,
//            settings (dark/light theme + metric/imperial units).
// ============================================================================
#pragma once

void ui_init();          // build all screens; show the connect screen
void ui_tick();          // call every loop: screen switching + value refresh
void ui_apply_theme();   // re-skin everything for the current settings
