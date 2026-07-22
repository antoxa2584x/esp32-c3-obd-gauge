#include "Theme.h"
#include "Settings.h"

// Small helper
static inline lv_color_t C(uint32_t hex) { return lv_color_hex(hex); }

// --- Palette --------------------------------------------------------------
//            DARK                 LIGHT
lv_color_t th_bg()       { return settings.darkTheme ? C(0x0E1116) : C(0xF4F6F8); }
lv_color_t th_card()     { return settings.darkTheme ? C(0x1A1F27) : C(0xFFFFFF); }
lv_color_t th_text()     { return settings.darkTheme ? C(0xF2F5F8) : C(0x1A1F27); }
lv_color_t th_text_dim() { return settings.darkTheme ? C(0x8A93A0) : C(0x6B7480); }
lv_color_t th_accent()   { return C(0x3B82F6); }   // blue-500 (RPM)
lv_color_t th_track()    { return settings.darkTheme ? C(0x272E38) : C(0xE2E6EB); }
lv_color_t th_temp()     { return C(0xF59E0B); }   // amber-500 (temp)
lv_color_t th_danger()   { return C(0xEF4444); }   // red-500 (DTC / over-limit)
lv_color_t th_ok()       { return C(0x22C55E); }   // green-500 (safe rev zone)

void theme_apply(lv_disp_t* disp) {
  lv_theme_t* t = lv_theme_default_init(
      disp,
      th_accent(),                 // primary
      th_temp(),                   // secondary
      settings.darkTheme,          // dark flag
      LV_FONT_DEFAULT);
  lv_disp_set_theme(disp, t);
}
