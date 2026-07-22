#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Wire.h>
#include "Display.h"
#include "config.h"

// --- LovyanGFX device definition for ESP32-2424S012 (display only) ----------
// Touch is driven directly over I2C below, NOT via LovyanGFX, so we can disable
// the CST816's auto-sleep (the cause of "touch works once then dies / breaks").
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01   _panel;
  lgfx::Bus_SPI        _bus;
  lgfx::Light_PWM      _light;

 public:
  LGFX() {
    { // SPI bus
      auto c = _bus.config();
      c.spi_host   = SPI2_HOST;
      c.spi_mode   = 0;
      c.freq_write = 40000000;
      c.freq_read  = 16000000;
      c.pin_sclk   = PIN_LCD_SCLK;
      c.pin_mosi   = PIN_LCD_MOSI;
      c.pin_miso   = PIN_LCD_MISO;
      c.pin_dc     = PIN_LCD_DC;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    { // panel
      auto c = _panel.config();
      c.pin_cs    = PIN_LCD_CS;
      c.pin_rst   = PIN_LCD_RST;
      c.panel_width  = SCREEN_W;
      c.panel_height = SCREEN_H;
      c.offset_x  = 0;
      c.offset_y  = 0;
      c.readable  = false;
      c.invert    = true;      // GC9A01 typically needs inversion
      c.rgb_order = false;
      _panel.config(c);
    }
    { // backlight
      auto c = _light.config();
      c.pin_bl = PIN_LCD_BL;
      c.freq   = 12000;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

static LGFX gfx;

// LVGL draw buffers (partial — no PSRAM on this board)
static lv_disp_draw_buf_t s_drawbuf;
static lv_color_t s_buf1[SCREEN_W * LVGL_BUF_LINES];
static lv_color_t s_buf2[SCREEN_W * LVGL_BUF_LINES];
static lv_disp_drv_t   s_disp_drv;
static lv_indev_drv_t  s_indev_drv;
static lv_disp_t*      s_disp = nullptr;
static uint32_t        s_last_tick = 0;

static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx.startWrite();
  gfx.setAddrWindow(area->x1, area->y1, w, h);
  gfx.writePixels((uint16_t*)px, w * h, true);   // true = swap bytes
  gfx.endWrite();
  lv_disp_flush_ready(drv);
}

// --- CST816 capacitive touch (direct I2C) ----------------------------------
static void cst816_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TP_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static bool cst816_read(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(TP_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)TP_I2C_ADDR, (int)len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void cst816_init() {
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);  delay(10);
  digitalWrite(PIN_TP_RST, HIGH); delay(60);       // chip boots ~50ms after reset
  Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);
  delay(5);
  cst816_write(0xFE, 0x01);   // DisAutoSleep = 1  -> never sleep (keeps touch alive)
}

static void touch_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
  uint8_t d[6];
  // regs: 0x02=finger count, 0x03/04=X hi/lo, 0x05/06=Y hi/lo
  if (cst816_read(0x01, d, 6) && (d[1] & 0x0F) > 0) {
    int x = ((d[2] & 0x0F) << 8) | d[3];
    int y = ((d[4] & 0x0F) << 8) | d[5];
    if (x >= SCREEN_W) x = SCREEN_W - 1;
    if (y >= SCREEN_H) y = SCREEN_H - 1;
    data->state   = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void display_init() {
  gfx.init();
  gfx.setRotation(0);
  gfx.setBrightness(180);

  cst816_init();          // bring up touch over I2C, auto-sleep disabled

  lv_init();
  lv_disp_draw_buf_init(&s_drawbuf, s_buf1, s_buf2, SCREEN_W * LVGL_BUF_LINES);

  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res  = SCREEN_W;
  s_disp_drv.ver_res  = SCREEN_H;
  s_disp_drv.flush_cb = flush_cb;
  s_disp_drv.draw_buf = &s_drawbuf;
  s_disp = lv_disp_drv_register(&s_disp_drv);

  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = touch_cb;
  lv_indev_drv_register(&s_indev_drv);

  s_last_tick = millis();
}

void display_tick() {
  uint32_t now = millis();
  lv_tick_inc(now - s_last_tick);
  s_last_tick = now;
}

lv_disp_t* display_lv_disp() { return s_disp; }

void display_backlight(uint8_t pct) {
  gfx.setBrightness((uint8_t)(pct * 255 / 100));
}
