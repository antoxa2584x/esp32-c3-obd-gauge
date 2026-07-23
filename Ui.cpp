#include <lvgl.h>
#include <math.h>
#include <WiFi.h>
#include "Ui.h"
#include "Theme.h"
#include "Settings.h"
#include "ObdClient.h"
#include "Display.h"
#include "config.h"

// ---- screen / widget handles ---------------------------------------------
static lv_obj_t* s_boot;     // startup splash (animated checkered flag)
static lv_obj_t* s_pick;     // WiFi network picker
static lv_obj_t* s_connect;
static lv_obj_t* s_screens[5];   // 0=RPM 1=Temp 2=Volt 3=Errors 4=Settings
static const int SCREEN_COUNT = 5;
static int       s_scr = 0;

static lv_obj_t* bright_toast;         // brief "Brightness NN%" popup
static uint32_t  bright_shown_at = 0;
static bool      s_gestured = false;   // a swipe happened during this touch

static lv_obj_t* pick_list;   // scanned networks
static lv_obj_t* pick_status; // "Scanning..." / results

static lv_obj_t* c_status;   // connect status label
static lv_obj_t* c_ssid;     // connect ssid label
static lv_obj_t* c_spin;     // spinner
static lv_obj_t* c_demo;     // demo button

// gauge tiles
static lv_obj_t* t_rpm;  static lv_obj_t* arc_rpm;  static lv_obj_t* lbl_rpm;  static lv_obj_t* lbl_speed;  static lv_obj_t* ttl_rpm;
static lv_obj_t* t_temp; static lv_obj_t* arc_temp; static lv_obj_t* lbl_temp; static lv_obj_t* ttl_temp;
static lv_obj_t* t_volt; static lv_obj_t* arc_volt; static lv_obj_t* lbl_volt; static lv_obj_t* ttl_volt;

// error tile
static lv_obj_t* t_err;  static lv_obj_t* err_list; static lv_obj_t* err_status; static lv_obj_t* ttl_err;
static lv_obj_t* btn_scan; static lv_obj_t* btn_clear;

// settings tile
static lv_obj_t* t_set;  static lv_obj_t* sw_theme; static lv_obj_t* sw_units; static lv_obj_t* ttl_set;
static lv_obj_t* set_conn; static lv_obj_t* lbl_theme; static lv_obj_t* lbl_units;

// overheat overlay (lives on the top layer so it covers every screen) + modals
static lv_obj_t* w_overlay; static lv_obj_t* w_title; static lv_obj_t* w_sub;
static lv_obj_t* rpm_modal = NULL;  static lv_obj_t* rpm_roller;
static lv_obj_t* temp_modal = NULL; static lv_obj_t* temp_roller;

static const int RPM_OPTS[]  = { 6000, 7000, 8000, 9000, 10000 };
static const int TEMP_OPTS[] = { 95, 100, 105, 110, 115, 120 };   // °C (native)

// state
static bool     s_mainShown = false;
static bool     s_dtcWaiting = false;
static bool     s_demo = false;          // true = simulate data, no real OBD
static bool     s_scanning = false;      // WiFi scan in progress
static uint32_t s_lastRefresh = 0;

// boot splash + auto-connect
static bool     s_bootDone   = false;
static uint32_t s_bootStart  = 0;
static bool     s_scanDone   = false;    // a WiFi scan has finished
static bool     s_savedFound = false;    // saved SSID was seen in that scan
#define BOOT_MIN_MS   2400               // minimum splash time
#define BOOT_MAX_MS   6000               // don't wait past this for the scan

// checkered-flag splash geometry (kept modest: tiles are LVGL objects and this
// board's LVGL heap is small; the whole splash is freed after boot).
#define FLAG_COLS  6
#define FLAG_ROWS  6
#define FLAG_CELL  48
static lv_obj_t* s_flagTiles[FLAG_ROWS][FLAG_COLS];

// collect themable text labels so we can recolor on theme switch
static lv_obj_t* s_textLabels[24]; static int s_nText = 0;
static lv_obj_t* s_dimLabels[24];  static int s_nDim  = 0;
static void reg_text(lv_obj_t* l) { if (s_nText < 24) s_textLabels[s_nText++] = l; }
static void reg_dim (lv_obj_t* l) { if (s_nDim  < 24) s_dimLabels[s_nDim++]  = l; }

// ---------------------------------------------------------------------------
//  builders
// ---------------------------------------------------------------------------
static lv_obj_t* make_title(lv_obj_t* parent, const char* txt) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 26);
  reg_dim(l);
  return l;
}

// arc-style gauge; returns the arc, out-param gives the centre value label
static lv_obj_t* make_gauge(lv_obj_t* parent, int lo, int hi, lv_obj_t** valLabel) {
  lv_obj_t* arc = lv_arc_create(parent);
  lv_obj_set_size(arc, 210, 210);
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, lo, hi);
  lv_arc_set_value(arc, lo);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);          // indicator only
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);         // arc is display-only
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_arc_width(arc, 16, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 16, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

  lv_obj_t* v = lv_label_create(parent);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_48, 0);
  lv_label_set_text(v, "--");
  lv_obj_center(v);
  reg_text(v);
  *valLabel = v;
  return arc;
}

// RPM indicator color by fraction of full-scale: green -> amber -> red.
static lv_color_t rpm_zone_color(int rpm, int maxr) {
  float f = maxr > 0 ? (float)rpm / maxr : 0.0f;
  if (f >= 0.88f) return th_danger();
  if (f >= 0.70f) return th_temp();
  return th_ok();
}

// Battery voltage color: red when flat/overcharging, green while charging.
static lv_color_t volt_zone_color(float v) {
  if (v < 11.8f || v > 14.8f) return th_danger();   // discharged / overcharge
  if (v >= 13.0f)             return th_ok();        // alternator charging
  return th_temp();                                  // resting / low-ish
}

// ---------------------------------------------------------------------------
//  event callbacks
// ---------------------------------------------------------------------------
static void ev_theme(lv_event_t* e) {
  settings.darkTheme = lv_obj_has_state(sw_theme, LV_STATE_CHECKED);
  settings_save();
  ui_apply_theme();
}
static void ev_units(lv_event_t* e) {
  settings.metric = lv_obj_has_state(sw_units, LV_STATE_CHECKED);
  settings_save();
  s_lastRefresh = 0;   // force immediate value refresh
}
static void ev_scan(lv_event_t* e) {
  if (s_demo) {
    lv_obj_clean(err_list);
    const char* fake[] = { "P0301", "P0420", "P0171" };
    for (auto c : fake) {
      lv_obj_t* b = lv_list_add_btn(err_list, LV_SYMBOL_WARNING, c);
      lv_obj_set_style_text_color(b, th_danger(), 0);
    }
    lv_label_set_text(err_status, "");
    return;
  }
  obd.requestDtcScan();
  s_dtcWaiting = true;
  lv_obj_clean(err_list);
  lv_label_set_text(err_status, "Scanning for codes...");
}
static void ev_clear(lv_event_t* e) {
  if (!s_demo) obd.requestDtcClear();
  lv_obj_clean(err_list);
  lv_label_set_text(err_status, "Codes cleared. Re-scan to verify.");
}
static void ev_demo(lv_event_t* e) {
  s_demo = true;
  s_mainShown = true;
  s_scr = 0;
  lv_scr_load(s_screens[0]);
}

static void ui_start_scan();          // fwd decl

static void ev_rescan(lv_event_t* e) { ui_start_scan(); }

static void ev_pick_net(lv_event_t* e) {
  lv_obj_t* btn = lv_event_get_target(e);
  const char* ssid = lv_list_get_btn_text(pick_list, btn);
  if (!ssid || !ssid[0]) return;
  settings.ssid = ssid;
  settings.pass = OBD_WIFI_PASS;   // "" = open (typical); set in config.h if secured
  settings.wifiConfigured = true;  // remember it -> auto-connect on next boot
  settings_save();
  lv_label_set_text(c_ssid, settings.ssid.c_str());
  obd.begin();                     // start joining the chosen adapter
  lv_scr_load_anim(s_connect, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
}

static void ui_start_scan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.scanDelete();
  lv_obj_clean(pick_list);
  lv_label_set_text(pick_status, "Scanning...");
  WiFi.scanNetworks(true /* async */);
  s_scanning = true;
}

// ---- circular modal: roller in the middle, discard | confirm on the sides -
static lv_obj_t* make_modal(const char* title, lv_obj_t** roller,
                            const char* opts, int selIdx,
                            lv_event_cb_t onSave, lv_event_cb_t onCancel) {
  lv_obj_t* m = lv_obj_create(lv_layer_top());
  lv_obj_set_size(m, 220, 220);
  lv_obj_center(m);
  lv_obj_set_style_radius(m, 110, 0);
  lv_obj_set_style_bg_color(m, th_card(), 0);
  lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_clear_flag(m, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* t = lv_label_create(m);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(t, th_text(), 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 20);

  *roller = lv_roller_create(m);
  lv_roller_set_options(*roller, opts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(*roller, 3);
  lv_obj_set_width(*roller, 84);
  lv_obj_align(*roller, LV_ALIGN_CENTER, 0, 8);
  lv_roller_set_selected(*roller, selIdx, LV_ANIM_OFF);

  // discard (left) and confirm (right) -- round, same size, flanking the list
  const int BTN = 46;
  lv_obj_t* ca = lv_btn_create(m);        // discard
  lv_obj_set_size(ca, BTN, BTN);
  lv_obj_set_style_radius(ca, BTN / 2, 0);
  lv_obj_set_style_bg_color(ca, th_track(), 0);
  lv_obj_align(ca, LV_ALIGN_LEFT_MID, 8, 8);
  lv_obj_t* cal = lv_label_create(ca); lv_label_set_text(cal, LV_SYMBOL_CLOSE); lv_obj_center(cal);
  lv_obj_add_event_cb(ca, onCancel, LV_EVENT_CLICKED, NULL);

  lv_obj_t* ok = lv_btn_create(m);        // confirm
  lv_obj_set_size(ok, BTN, BTN);
  lv_obj_set_style_radius(ok, BTN / 2, 0);
  lv_obj_align(ok, LV_ALIGN_RIGHT_MID, -8, 8);
  lv_obj_t* okl = lv_label_create(ok); lv_label_set_text(okl, LV_SYMBOL_OK); lv_obj_center(okl);
  lv_obj_add_event_cb(ok, onSave, LV_EVENT_CLICKED, NULL);
  return m;
}

// ---- RPM max picker -------------------------------------------------------
static void ev_rpm_save(lv_event_t* e) {
  settings.rpmMax = RPM_OPTS[lv_roller_get_selected(rpm_roller)];
  settings_save();
  lv_arc_set_range(arc_rpm, 0, settings.rpmMax);
  lv_obj_del(rpm_modal); rpm_modal = NULL;
}
static void ev_rpm_cancel(lv_event_t* e) { lv_obj_del(rpm_modal); rpm_modal = NULL; }

static void ev_rpm_tile(lv_event_t* e) {
  if (s_gestured) return;    // this "tap" was really a swipe
  if (rpm_modal) return;
  int sel = 0;
  for (int i = 0; i < 5; i++) if (RPM_OPTS[i] == settings.rpmMax) sel = i;
  rpm_modal = make_modal("Max RPM", &rpm_roller,
                         "6000\n7000\n8000\n9000\n10000", sel,
                         ev_rpm_save, ev_rpm_cancel);
}

// ---- Temp warning picker --------------------------------------------------
static void ev_temp_save(lv_event_t* e) {
  settings.warnTempC = TEMP_OPTS[lv_roller_get_selected(temp_roller)];
  settings_save();
  lv_obj_del(temp_modal); temp_modal = NULL;
}
static void ev_temp_cancel(lv_event_t* e) { lv_obj_del(temp_modal); temp_modal = NULL; }

static void ev_temp_tile(lv_event_t* e) {
  if (s_gestured) return;    // this "tap" was really a swipe
  if (temp_modal) return;
  String opts; int sel = 0;
  for (int i = 0; i < 6; i++) {
    opts += String((int)temp_display(TEMP_OPTS[i]));
    if (i < 5) opts += "\n";
    if (TEMP_OPTS[i] == settings.warnTempC) sel = i;
  }
  char title[24];
  snprintf(title, sizeof(title), "Warn (%s)", temp_unit());
  temp_modal = make_modal(title, &temp_roller, opts.c_str(), sel,
                          ev_temp_save, ev_temp_cancel);
}

// ---- overheat overlay -----------------------------------------------------
// brightness popup on the top layer (shown briefly on a vertical swipe)
static void build_bright() {
  bright_toast = lv_label_create(lv_layer_top());
  lv_label_set_text(bright_toast, "");
  lv_obj_set_style_text_font(bright_toast, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(bright_toast, lv_color_white(), 0);
  lv_obj_set_style_bg_color(bright_toast, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(bright_toast, LV_OPA_70, 0);
  lv_obj_set_style_pad_all(bright_toast, 12, 0);
  lv_obj_set_style_radius(bright_toast, 10, 0);
  lv_obj_clear_flag(bright_toast, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(bright_toast, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(bright_toast, LV_OBJ_FLAG_HIDDEN);
}

// Warning overlay lives ON the coolant screen (not the global top layer), so
// it never covers the nav/gear buttons -- those are added later => drawn on top.
static void build_warn(lv_obj_t* parent) {
  w_overlay = lv_obj_create(parent);
  lv_obj_set_size(w_overlay, SCREEN_W, SCREEN_H);
  lv_obj_center(w_overlay);
  lv_obj_set_style_radius(w_overlay, 0, 0);
  lv_obj_set_style_border_width(w_overlay, 0, 0);
  lv_obj_set_style_bg_color(w_overlay, th_danger(), 0);
  lv_obj_clear_flag(w_overlay, LV_OBJ_FLAG_CLICKABLE);   // never intercept touch
  lv_obj_clear_flag(w_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(w_overlay, LV_OBJ_FLAG_HIDDEN);

  w_title = lv_label_create(w_overlay);
  lv_label_set_text(w_title, LV_SYMBOL_WARNING);
  lv_obj_set_style_text_font(w_title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(w_title, lv_color_white(), 0);
  lv_obj_align(w_title, LV_ALIGN_CENTER, 0, -18);

  w_sub = lv_label_create(w_overlay);
  lv_label_set_text(w_sub, "");
  lv_obj_set_style_text_font(w_sub, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(w_sub, lv_color_white(), 0);
  lv_obj_align(w_sub, LV_ALIGN_CENTER, 0, 28);
}

static void update_temp_warn(int coolant) {
  const int warn = settings.warnTempC;
  const bool over = coolant >= warn;
  const bool warming = !over && coolant >= warn - 4;

  if (over) {                                            // solid warning
    lv_obj_clear_flag(w_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(w_overlay, LV_OPA_90, 0);
    lv_label_set_text(w_title, LV_SYMBOL_WARNING " OVERHEAT");
    lv_label_set_text_fmt(w_sub, "%d%s", (int)temp_display(coolant), temp_unit());
  } else if (warming && ((millis() / 300) % 2)) {        // flashing
    lv_obj_clear_flag(w_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(w_overlay, LV_OPA_50, 0);
    lv_label_set_text(w_title, LV_SYMBOL_WARNING " TEMP HIGH");
    lv_label_set_text_fmt(w_sub, "%d%s", (int)temp_display(coolant), temp_unit());
  } else {
    lv_obj_add_flag(w_overlay, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---- screen navigation (swipe-driven) -------------------------------------
static void go_prev() { s_scr = (s_scr + SCREEN_COUNT - 1) % SCREEN_COUNT; lv_scr_load(s_screens[s_scr]); }
static void go_next() { s_scr = (s_scr + 1) % SCREEN_COUNT; lv_scr_load(s_screens[s_scr]); }

// change backlight, persist, and flash a small popup
static void adjust_brightness(int delta) {
  int b = settings.brightness + delta;
  if (b < 10)  b = 10;
  if (b > 100) b = 100;
  if (b == settings.brightness) return;
  settings.brightness = b;
  display_backlight(b);
  settings_save();
  lv_label_set_text_fmt(bright_toast, "Brightness %d%%", b);
  lv_obj_clear_flag(bright_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(bright_toast);
  bright_shown_at = millis();
}

// reset the swipe flag at the start of every touch
static void ev_press_reset(lv_event_t* e) { s_gestured = false; }

// horizontal swipe = change screen; vertical swipe = brightness
static void ev_gesture(lv_event_t* e) {
  s_gestured = true;         // mark this touch as a swipe, so CLICKED is ignored
  lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
  if      (d == LV_DIR_LEFT)   go_next();
  else if (d == LV_DIR_RIGHT)  go_prev();
  else if (d == LV_DIR_TOP)    adjust_brightness(+15);   // swipe up -> brighter
  else if (d == LV_DIR_BOTTOM) adjust_brightness(-15);   // swipe down -> dimmer
}

// ---------------------------------------------------------------------------
//  boot splash: a waving checkered flag behind centered "OBD / Dash"
// ---------------------------------------------------------------------------
static void build_boot() {
  s_boot = lv_obj_create(NULL);
  lv_obj_clear_flag(s_boot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(s_boot, 0, 0);
  lv_obj_set_style_border_width(s_boot, 0, 0);
  lv_obj_set_style_bg_color(s_boot, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_boot, LV_OPA_COVER, 0);

  // grid of tiles (a little larger than the screen so the wave never bares gaps)
  const int startX = (SCREEN_W - FLAG_COLS * FLAG_CELL) / 2;
  const int startY = (SCREEN_H - FLAG_ROWS * FLAG_CELL) / 2;
  for (int r = 0; r < FLAG_ROWS; r++) {
    for (int c = 0; c < FLAG_COLS; c++) {
      lv_obj_t* tile = lv_obj_create(s_boot);
      lv_obj_remove_style_all(tile);
      lv_obj_set_size(tile, FLAG_CELL, FLAG_CELL);
      lv_obj_set_pos(tile, startX + c * FLAG_CELL, startY + r * FLAG_CELL);
      lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(tile,
          ((r + c) & 1) ? lv_color_white() : lv_color_black(), 0);
      s_flagTiles[r][c] = tile;
    }
  }

  // Wordmark with a black outline so it stays legible over the checker.
  // LVGL has no text stroke, so draw black copies offset in 8 directions
  // behind the white text.
  const int OFF = 3;
  const int dx[8] = { -OFF, 0, OFF, -OFF, OFF, -OFF, 0, OFF };
  const int dy[8] = { -OFF, -OFF, -OFF, 0, 0, OFF, OFF, OFF };
  for (int k = 0; k < 8; k++) {
    lv_obj_t* o = lv_label_create(s_boot);
    lv_label_set_text(o, "OBD\nDash");
    lv_obj_set_style_text_font(o, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(o, lv_color_black(), 0);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(o, LV_ALIGN_CENTER, dx[k], dy[k]);
  }

  lv_obj_t* wm = lv_label_create(s_boot);
  lv_label_set_text(wm, "OBD\nDash");
  lv_obj_set_style_text_font(wm, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(wm, lv_color_white(), 0);
  lv_obj_set_style_text_align(wm, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(wm, LV_ALIGN_CENTER, 0, 0);
}

// Ripple the flag columns each frame (fixed at the left "mast", free on the right).
static void boot_anim_tick() {
  if (s_bootDone || !s_boot) return;
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 33) return;           // ~30 fps
  last = now;
  const int startY = (SCREEN_H - FLAG_ROWS * FLAG_CELL) / 2;
  float t = (now - s_bootStart) / 1000.0f;
  for (int c = 0; c < FLAG_COLS; c++) {
    float amp = 12.0f * (float)c / (FLAG_COLS - 1);     // 0 at mast -> max at fly
    int   dy  = (int)(amp * sinf(t * 4.5f - c * 0.7f));
    for (int r = 0; r < FLAG_ROWS; r++)
      lv_obj_set_y(s_flagTiles[r][c], startY + r * FLAG_CELL + dy);
  }
}

// ---------------------------------------------------------------------------
//  screen construction
// ---------------------------------------------------------------------------
static void build_pick() {
  s_pick = lv_obj_create(NULL);
  lv_obj_clear_flag(s_pick, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* ttl = lv_label_create(s_pick);
  lv_label_set_text(ttl, "SELECT OBD WIFI");
  lv_obj_set_style_text_font(ttl, &lv_font_montserrat_16, 0);
  lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 22);
  reg_dim(ttl);

  pick_list = lv_list_create(s_pick);
  lv_obj_set_size(pick_list, 192, 116);
  lv_obj_align(pick_list, LV_ALIGN_CENTER, 0, -4);

  pick_status = lv_label_create(s_pick);
  lv_label_set_text(pick_status, "");
  lv_obj_set_style_text_font(pick_status, &lv_font_montserrat_14, 0);
  lv_obj_align(pick_status, LV_ALIGN_CENTER, 0, -4);
  reg_dim(pick_status);

  lv_obj_t* br = lv_btn_create(s_pick);      // rescan
  lv_obj_align(br, LV_ALIGN_BOTTOM_MID, -44, -12);
  lv_obj_t* brl = lv_label_create(br);
  lv_label_set_text(brl, LV_SYMBOL_REFRESH); lv_obj_center(brl);
  lv_obj_add_event_cb(br, ev_rescan, LV_EVENT_CLICKED, NULL);

  lv_obj_t* bd = lv_btn_create(s_pick);      // demo
  lv_obj_align(bd, LV_ALIGN_BOTTOM_MID, 44, -12);
  lv_obj_t* bdl = lv_label_create(bd);
  lv_label_set_text(bdl, LV_SYMBOL_EYE_OPEN); lv_obj_center(bdl);
  lv_obj_add_event_cb(bd, ev_demo, LV_EVENT_CLICKED, NULL);
}

static void build_connect() {
  s_connect = lv_obj_create(NULL);
  lv_obj_clear_flag(s_connect, LV_OBJ_FLAG_SCROLLABLE);

  c_spin = lv_spinner_create(s_connect, 1000, 60);
  lv_obj_set_size(c_spin, 70, 70);
  lv_obj_align(c_spin, LV_ALIGN_CENTER, 0, -26);

  c_status = lv_label_create(s_connect);
  lv_label_set_text(c_status, "Starting...");
  lv_obj_set_style_text_font(c_status, &lv_font_montserrat_16, 0);
  lv_obj_align(c_status, LV_ALIGN_CENTER, 0, 40);
  reg_text(c_status);

  c_ssid = lv_label_create(s_connect);
  lv_label_set_text(c_ssid, OBD_WIFI_SSID);
  lv_obj_set_style_text_font(c_ssid, &lv_font_montserrat_14, 0);
  lv_obj_align(c_ssid, LV_ALIGN_CENTER, 0, 66);
  reg_dim(c_ssid);

  // Demo button: skip the adapter and drive the UI with simulated data.
  c_demo = lv_btn_create(s_connect);
  lv_obj_align(c_demo, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_t* dl = lv_label_create(c_demo);
  lv_label_set_text(dl, LV_SYMBOL_EYE_OPEN " Demo");
  lv_obj_center(dl);
  lv_obj_add_event_cb(c_demo, ev_demo, LV_EVENT_CLICKED, NULL);
}

static lv_obj_t* new_screen() {
  lv_obj_t* s = lv_obj_create(NULL);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(s, 0, 0);
  lv_obj_set_style_border_width(s, 0, 0);
  lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);                  // enable tap + swipe target
  lv_obj_add_event_cb(s, ev_press_reset, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(s, ev_gesture, LV_EVENT_GESTURE, NULL);
  return s;
}

static void build_main() {
  // Each page is its own full screen. (Buttons inside an LVGL tileview were
  // unreliable on this touch panel; plain screens + nav buttons are solid.)

  // --- screen 0: RPM ---  (title sits INSIDE the ring, above the value)
  t_rpm = new_screen(); s_screens[0] = t_rpm;
  arc_rpm = make_gauge(t_rpm, 0, settings.rpmMax, &lbl_rpm);
  ttl_rpm = lv_label_create(t_rpm);
  lv_label_set_text(ttl_rpm, "ENGINE RPM");
  lv_obj_set_style_text_font(ttl_rpm, &lv_font_montserrat_14, 0);
  lv_obj_align(ttl_rpm, LV_ALIGN_CENTER, 0, -42);
  reg_dim(ttl_rpm);
  lbl_speed = lv_label_create(t_rpm);
  lv_label_set_text(lbl_speed, "-- km/h");
  lv_obj_set_style_text_font(lbl_speed, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl_speed, LV_ALIGN_CENTER, 0, 44);
  reg_dim(lbl_speed);

  // --- screen 1: Temp ---
  t_temp = new_screen(); s_screens[1] = t_temp;
  arc_temp = make_gauge(t_temp, TEMP_MIN_C, TEMP_MAX_C, &lbl_temp);
  ttl_temp = lv_label_create(t_temp);
  lv_label_set_text(ttl_temp, "COOLANT");
  lv_obj_set_style_text_font(ttl_temp, &lv_font_montserrat_14, 0);
  lv_obj_align(ttl_temp, LV_ALIGN_CENTER, 0, -42);
  reg_dim(ttl_temp);
  build_warn(t_temp);   // overheat overlay on this screen (below the buttons)

  // --- screen 2: Battery voltage ---
  t_volt = new_screen(); s_screens[2] = t_volt;
  arc_volt = make_gauge(t_volt, 80, 160, &lbl_volt);   // decivolts: 8.0 .. 16.0 V (font 48, like the other gauges)
  ttl_volt = lv_label_create(t_volt);
  lv_label_set_text(ttl_volt, "BATTERY");
  lv_obj_set_style_text_font(ttl_volt, &lv_font_montserrat_14, 0);
  lv_obj_align(ttl_volt, LV_ALIGN_CENTER, 0, -42);
  reg_dim(ttl_volt);

  // --- screen 3: Errors ---
  t_err = new_screen(); s_screens[3] = t_err;
  ttl_err = make_title(t_err, "TROUBLE CODES");
  btn_scan = lv_btn_create(t_err);
  lv_obj_align(btn_scan, LV_ALIGN_TOP_MID, 0, 58);
  lv_obj_t* bl = lv_label_create(btn_scan); lv_label_set_text(bl, LV_SYMBOL_REFRESH " Scan");
  lv_obj_center(bl);
  lv_obj_add_event_cb(btn_scan, ev_scan, LV_EVENT_CLICKED, NULL);

  err_list = lv_list_create(t_err);
  lv_obj_set_size(err_list, 168, 78);
  lv_obj_align(err_list, LV_ALIGN_CENTER, 0, 18);

  err_status = lv_label_create(t_err);
  lv_label_set_text(err_status, "Tap Scan to read codes");
  lv_obj_set_style_text_font(err_status, &lv_font_montserrat_14, 0);
  lv_obj_align(err_status, LV_ALIGN_CENTER, 0, 18);
  reg_dim(err_status);

  btn_clear = lv_btn_create(t_err);
  lv_obj_align(btn_clear, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_obj_t* cl = lv_label_create(btn_clear); lv_label_set_text(cl, LV_SYMBOL_TRASH " Clear");
  lv_obj_center(cl);
  lv_obj_add_event_cb(btn_clear, ev_clear, LV_EVENT_CLICKED, NULL);

  // --- screen 4: Settings ---
  t_set = new_screen(); s_screens[4] = t_set;
  ttl_set = make_title(t_set, "SETTINGS");

  lbl_theme = lv_label_create(t_set);
  lv_label_set_text(lbl_theme, "Dark theme");
  lv_obj_align(lbl_theme, LV_ALIGN_CENTER, -20, -34);
  reg_text(lbl_theme);
  sw_theme = lv_switch_create(t_set);
  lv_obj_align(sw_theme, LV_ALIGN_CENTER, 58, -34);
  lv_obj_add_event_cb(sw_theme, ev_theme, LV_EVENT_VALUE_CHANGED, NULL);

  lbl_units = lv_label_create(t_set);
  lv_label_set_text(lbl_units, "Metric units");
  lv_obj_align(lbl_units, LV_ALIGN_CENTER, -20, 12);
  reg_text(lbl_units);
  sw_units = lv_switch_create(t_set);
  lv_obj_align(sw_units, LV_ALIGN_CENTER, 58, 12);
  lv_obj_add_event_cb(sw_units, ev_units, LV_EVENT_VALUE_CHANGED, NULL);

  set_conn = lv_label_create(t_set);
  lv_label_set_text(set_conn, "");
  lv_obj_set_style_text_font(set_conn, &lv_font_montserrat_14, 0);
  lv_obj_align(set_conn, LV_ALIGN_BOTTOM_MID, 0, -34);
  reg_dim(set_conn);

  // hold a gauge to open its picker (a swipe won't trigger it -- see s_gestured)
  lv_obj_add_event_cb(t_rpm,  ev_rpm_tile,  LV_EVENT_LONG_PRESSED, NULL);
  lv_obj_add_event_cb(t_temp, ev_temp_tile, LV_EVENT_LONG_PRESSED, NULL);
}

// ---------------------------------------------------------------------------
void ui_init() {
  build_boot();
  build_pick();
  build_connect();
  build_main();
  build_bright();
  ui_apply_theme();
  display_backlight(settings.brightness);   // apply saved brightness

  // Show the splash and start scanning immediately, so by the time the logo
  // clears we already know whether the last-used adapter is in range.
  s_bootStart = millis();
  lv_scr_load(s_boot);
  ui_start_scan();
}

// ---------------------------------------------------------------------------
void ui_apply_theme() {
  theme_apply(display_lv_disp());

  lv_obj_t* bgs[] = { s_pick, s_connect, t_rpm, t_temp, t_volt, t_err, t_set };
  for (auto o : bgs) if (o) {
    lv_obj_set_style_bg_color(o, th_bg(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
  }
  for (int i = 0; i < s_nText; i++) lv_obj_set_style_text_color(s_textLabels[i], th_text(), 0);
  for (int i = 0; i < s_nDim;  i++) lv_obj_set_style_text_color(s_dimLabels[i],  th_text_dim(), 0);

  // gauges
  if (arc_rpm) {
    lv_obj_set_style_arc_color(arc_rpm, th_track(),  LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_rpm, th_accent(), LV_PART_INDICATOR);
  }
  if (arc_temp) {
    lv_obj_set_style_arc_color(arc_temp, th_track(), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_temp, th_temp(),  LV_PART_INDICATOR);
  }
  if (arc_volt) {
    lv_obj_set_style_arc_color(arc_volt, th_track(), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_volt, th_ok(),    LV_PART_INDICATOR);
  }
  // spinner accent
  if (c_spin) lv_obj_set_style_arc_color(c_spin, th_accent(), LV_PART_INDICATOR);

  // keep switches reflecting current settings
  if (sw_theme) { settings.darkTheme ? lv_obj_add_state(sw_theme, LV_STATE_CHECKED)
                                      : lv_obj_clear_state(sw_theme, LV_STATE_CHECKED); }
  if (sw_units) { settings.metric    ? lv_obj_add_state(sw_units, LV_STATE_CHECKED)
                                      : lv_obj_clear_state(sw_units, LV_STATE_CHECKED); }
}

// ---------------------------------------------------------------------------
void ui_tick() {
  // auto-hide the brightness popup ~1.2s after the last change
  if (bright_shown_at && millis() - bright_shown_at > 1200) {
    lv_obj_add_flag(bright_toast, LV_OBJ_FLAG_HIDDEN);
    bright_shown_at = 0;
  }

  // WiFi picker: collect async scan results
  if (s_scanning) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      s_scanning = false;
      s_scanDone = true;
      s_savedFound = false;
      lv_obj_clean(pick_list);
      if (n == 0) {
        lv_label_set_text(pick_status, "No networks - tap " LV_SYMBOL_REFRESH);
      } else {
        lv_label_set_text(pick_status, "");
        for (int i = 0; i < n; i++) {
          String ss = WiFi.SSID(i);
          if (ss.length() == 0) continue;
          if (ss == settings.ssid) s_savedFound = true;
          lv_obj_t* b = lv_list_add_btn(pick_list, LV_SYMBOL_WIFI, ss.c_str());
          lv_obj_add_event_cb(b, ev_pick_net, LV_EVENT_CLICKED, NULL);
        }
      }
      WiFi.scanDelete();
    } else if (n == WIFI_SCAN_FAILED) {
      s_scanning = false;
      s_scanDone = true;
      lv_label_set_text(pick_status, "Scan failed - tap " LV_SYMBOL_REFRESH);
    }
  }

  // Boot splash: animate the flag, then either auto-connect to the last-used
  // adapter (if it's in range) or fall back to the network picker.
  if (!s_bootDone) {
    boot_anim_tick();
    uint32_t el = millis() - s_bootStart;
    bool ready = s_scanDone || el >= BOOT_MAX_MS;   // don't wait forever
    if (el >= BOOT_MIN_MS && ready) {
      s_bootDone = true;
      if (settings.wifiConfigured && s_savedFound) {
        lv_label_set_text(c_ssid, settings.ssid.c_str());
        lv_label_set_text(c_status, "Connecting...");
        obd.begin();                                // join the saved adapter
        lv_scr_load_anim(s_connect, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, true);
      } else {
        lv_scr_load_anim(s_pick, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, true);
      }
    }
    return;   // nothing else runs while the splash is up
  }

  // screen switching
  if (!s_mainShown) {
    lv_label_set_text(c_status, obd.stateText());
    if (obd.ready()) {
      s_mainShown = true;
      s_scr = 0;
      lv_scr_load(s_screens[0]);
    }
    return;
  }

  // populate DTC list once a scan completes
  if (s_dtcWaiting && obd.dtcReady() && !obd.dtcBusy()) {
    s_dtcWaiting = false;
    const auto& codes = obd.dtcCodes();
    if (codes.empty()) {
      lv_label_set_text(err_status, LV_SYMBOL_OK " No stored codes");
    } else {
      lv_label_set_text(err_status, "");
      for (auto& c : codes) {
        lv_obj_t* b = lv_list_add_btn(err_list, LV_SYMBOL_WARNING, c.c_str());
        lv_obj_set_style_text_color(b, th_danger(), 0);
      }
    }
  }

  // throttle live value refresh to ~10 Hz
  if (millis() - s_lastRefresh < 100) return;
  s_lastRefresh = millis();

  int rpm, coolant, speed;
  float volt;
  if (s_demo) {                                   // synthetic sweeps
    float ph = millis() / 1000.0f;
    rpm     = (int)(1500 + (sinf(ph * 1.7f) * 0.5f + 0.5f) * 4500);   // 1500..6000
    speed   = (int)((sinf(ph * 0.5f) * 0.5f + 0.5f) * 120);          // 0..120 km/h
    uint32_t warm = millis() / 600;                                   // warm-up ramp
    // ramp to ~105 then oscillate +/-12 so it crosses a ~110 warning for the demo
    coolant = 40 + (int)(warm > 65 ? 65 : warm) + (int)(sinf(ph) * 12);
    volt    = 13.8f + sinf(ph * 0.4f) * 0.6f;                         // ~13.2..14.4 V
  } else {
    rpm = obd.rpm; coolant = obd.coolantC; speed = obd.speedKmh; volt = obd.voltage;
  }

  lv_arc_set_value(arc_rpm, rpm);
  lv_obj_set_style_arc_color(arc_rpm, rpm_zone_color(rpm, settings.rpmMax), LV_PART_INDICATOR);
  lv_label_set_text_fmt(lbl_rpm, "%d", rpm);
  lv_label_set_text_fmt(lbl_speed, "%d %s", (int)speed_display(speed), speed_unit());

  lv_arc_set_value(arc_temp, coolant);
  lv_label_set_text_fmt(lbl_temp, "%d%s", (int)temp_display(coolant), temp_unit());

  update_temp_warn(coolant);   // flash / solid overheat overlay

  int dvolt = (int)(volt * 10.0f + 0.5f);   // decivolts (LVGL's printf has no %f)
  lv_arc_set_value(arc_volt, dvolt);
  lv_obj_set_style_arc_color(arc_volt, volt_zone_color(volt), LV_PART_INDICATOR);
  if (volt > 0.5f) lv_label_set_text_fmt(lbl_volt, "%d.%dV", dvolt / 10, dvolt % 10);
  else             lv_label_set_text(lbl_volt, "--");

  lv_label_set_text(set_conn, s_demo ? (LV_SYMBOL_EYE_OPEN " Demo mode")
                     : obd.ready() ? (LV_SYMBOL_WIFI " Connected")
                                   : obd.stateText());
}
