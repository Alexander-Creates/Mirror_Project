#define TFT_BL 40
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "ui.h"
#include "vars.h"
#include "structs.h"
#include "screens.h"

int speedVar = 0;

// Read
// int getSpeed() {
//   return flow::getGlobalVariable(FLOW_GLOBAL_VARIABLE_SPEED_VALUE).getInt();
// }
// Write LVGL labels bound to these variables auto-update
void setSpeed(int val) {
  flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SPEED_VALUE, Value(val));
}

// Read
// int getPWM() {
//   return flow::getGlobalVariable(FLOW_GLOBAL_VARIABLE_PWM_VALUE).getInt();
// }
void setPWM(int val) {
  flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PWM_VALUE, Value(val));
}

TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[240 * 20];
static lv_color_t buf2[240 * 20];

void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.setRotation(135);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

void setup() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); // Backlight on
  tft.init();
  tft.setRotation(0);
  lv_init();  
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 240 * 20);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = 240;
  disp_drv.ver_res  = 240;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  ui_init();          // EEZ-generated — initializes all screens and widgets
}

void loop() {
  lv_led_set_color(objects.euc_connected_status, lv_color_hex(0xFF0000));
  // Count up 0 to 50
  for (int i = 0; i <= 50; i++) {
    setPWM(i);          // All widgets bound to FLOW_GLOBAL_VARIABLE_SPEED_VALUE
                          // update automatically — arc, label, meter, etc.
    lv_timer_handler();
    ui_tick();
    delay(5);
  }

  lv_led_set_color(objects.euc_connected_status, lv_color_hex(0x00FF00));
  // Count down 50 to 0
  for (int i = 50; i >= 0; i--) {
    setPWM(i);
    lv_timer_handler();
    ui_tick();
    delay(5);
  }
}