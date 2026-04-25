#include <TFT_eSPI.h>
#include <lvgl.h>
#include "ui.h"
#include "vars.h"
#include "structs.h"
#include "screens.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#define TFT_BL 40

#define NOTIFY_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

// Setting the BLE configs
static constexpr uint32_t scanTimeMs = 10 * 1000;
NimBLEAddress foundAddress("", 0);
bool deviceFound = false;

//setting the telemetry structure
struct {
  float   speed      = 0;
  uint8_t safety     = 0;
  uint8_t euc_battery  = 0;
  uint8_t dev_battery  = 0;
  uint8_t esp_battery  = 0;
  bool    speed_valid  = false;
  bool    safety_valid = false;
  bool    battery_valid = false;
  bool    dev_bat_valid = false;
  bool    connected    = false;
  bool    fresh        = false;
} euc;



// helper functions
static inline uint16_t u16le(const uint8_t* d, int i) {
  return (uint16_t)d[i] | ((uint16_t)d[i+1] << 8);
}
static inline int16_t i16le(const uint8_t* d, int i) {
  return (int16_t)u16le(d, i);
}
static inline uint32_t u32le(const uint8_t* d, int i) {
  return (uint32_t)d[i]            |
         ((uint32_t)d[i+1] << 8)   |
         ((uint32_t)d[i+2] << 16)  |
         ((uint32_t)d[i+3] << 24);
}

void print_alarms(uint16_t flags) {
  if (flags == 0) { Serial.print("  Alarms:    None\n"); return; }
  const char* names[] = {
    "1st speed", "2nd speed", "3rd speed",
    "Current (peak)", "Current (sustained)", "Temperature",
    "Overvoltage", "(reserved)", "Safety margin",
    "Speed limit", "Undervoltage", "Tire pressure"
  };
  Serial.print("  Alarms:   ");
  for (int i = 0; i < 12; i++) {
    if (flags & (1 << i)) Serial.printf(" [%s]", names[i]);
  }
  Serial.println();
}

// Frame parsing
void parse_frame_02(const uint8_t* d, uint8_t idx) {
  uint16_t alarms  = u16le(d, 1);
  uint16_t config  = u16le(d, 3);
  uint8_t  vf      = d[5];
  uint32_t dist    = u32le(d, 6);
  float    speed   = u16le(d, 10) * 0.1f;
  float    cellv   = u16le(d, 12) * 0.01f;
  float    energy  = i16le(d, 14) * 0.1f;
  uint8_t  safety  = d[16];
  uint8_t  load    = d[17];
  uint8_t  battery = d[18];
  int      temp    = (int)d[19] - 40;

  // Update display state
  euc.speed        = speed;
  euc.safety       = safety;
  euc.speed_valid  = (vf >> 1) & 1;
  euc.safety_valid = (vf >> 4) & 1;
  euc.fresh        = true;
  euc.euc_battery   = battery;
  euc.battery_valid = (vf >> 6) & 1;


  Serial.printf("\n=== Frame %d ===\n", idx);
  print_alarms(alarms);
  Serial.printf("  Config flags:   0x%04X\n",   config);
  Serial.printf("  Distance:       %lu m         [valid=%d]\n", dist,    (vf>>0)&1);
  Serial.printf("  Speed:          %.1f km/h     [valid=%d]\n", speed,   (vf>>1)&1);
  Serial.printf("  Avg cell V:     %.2f V        [valid=%d]\n", cellv,   (vf>>2)&1);
  Serial.printf("  Energy cons:    %.1f Wh/km    [valid=%d]\n", energy,  (vf>>3)&1);
  Serial.printf("  Safety margin:  %d %%          [valid=%d]\n", safety, (vf>>4)&1);
  Serial.printf("  Relative load:  %d %%          [valid=%d]\n", load,   (vf>>5)&1);
  Serial.printf("  Battery:        %d %%          [valid=%d]\n", battery,(vf>>6)&1);
  Serial.printf("  Temperature:    %d C          [valid=%d]\n",  temp,   (vf>>7)&1);
}

void parse_frame_1(const uint8_t* d) {
  uint16_t alarms   = u16le(d, 1);
  uint8_t  spd_rng  = d[3];
  uint8_t  phone_bt = d[4];
  uint16_t vf       = u16le(d, 5);
  float    avg_spd  = u16le(d,  7) * 0.1f;
  float    avg_ride = u16le(d,  9) * 0.1f;
  float    max_spd  = u16le(d, 11) * 0.1f;
  float    avg_eng  = i16le(d, 13) * 0.1f;
  uint8_t  min_saf  = d[15];
  uint8_t  min_load = d[16];
  uint8_t  max_load = d[17];
  uint8_t  min_bat  = d[18];
  uint8_t  max_bat  = d[19];

  Serial.println("\n=== Frame 1 ===");
  print_alarms(alarms);
  Serial.printf("  Speedo range:   %d km/h\n",    spd_rng);
  Serial.printf("  Phone battery:  %d %%\n",       phone_bt);
  Serial.printf("  Avg speed:      %.1f km/h     [valid=%d]\n", avg_spd,  (vf>>0)&1);
  Serial.printf("  Avg ride speed: %.1f km/h     [valid=%d]\n", avg_ride, (vf>>1)&1);
  Serial.printf("  Max speed:      %.1f km/h     [valid=%d]\n", max_spd,  (vf>>2)&1);
  Serial.printf("  Avg energy:     %.1f Wh/km    [valid=%d]\n", avg_eng,  (vf>>3)&1);
  Serial.printf("  Min safety:     %d %%          [valid=%d]\n", min_saf,  (vf>>4)&1);
  Serial.printf("  Min rel load:   %d %%          [valid=%d]\n", min_load, (vf>>5)&1);
  Serial.printf("  Max rel load:   %d %%          [valid=%d]\n", max_load, (vf>>6)&1);
  Serial.printf("  Min battery:    %d %%          [valid=%d]\n", min_bat,  (vf>>7)&1);
  Serial.printf("  Max battery:    %d %%          [valid=%d]\n", max_bat,  (vf>>8)&1);

  euc.dev_battery  = phone_bt;
  euc.dev_bat_valid = true;  // phone_bt has no validity flag in frame 1
}

void parse_frame_3(const uint8_t* d) {
  uint16_t alarms   = u16le(d, 1);
  uint16_t vf       = u16le(d, 3);
  int      min_temp = (int)d[5] - 40;
  int      max_temp = (int)d[6] - 40;

  print_alarms(alarms);
  Serial.printf("  Min temp:       %d C          [valid=%d]\n", min_temp, (vf>>0)&1);
  Serial.printf("  Max temp:       %d C          [valid=%d]\n", max_temp, (vf>>1)&1);
}

// ── Notify handler ────────────────────────────────────────────────────────────
static void notifyHandler(NimBLERemoteCharacteristic* pChar,
                          uint8_t* data, size_t len, bool isNotify) {
  char hex[len * 2 + 1];
  for (size_t i = 0; i < len; i++) sprintf(&hex[i * 2], "%02x", data[i]);
  hex[len * 2] = '\0';
  Serial.printf("RAW: %s\n", hex);

  if (len < 20) { Serial.println("  [short frame, skipping]"); return; }

  switch (data[0]) {
    case 0: case 2: parse_frame_02(data, data[0]); break;
    case 1:         parse_frame_1(data);            break;
    case 3:         parse_frame_3(data);            break;
    default: Serial.printf("  [unknown frame index: %d]\n", data[0]); break;
  }
}

// ── NimBLE client callbacks ───────────────────────────────────────────────────
class ClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient* client, int reason) override {
    Serial.printf("Disconnected (reason=%d) - rescanning...\n", reason);
    euc.connected = false;
    euc.fresh     = false;
    deviceFound   = false;
  }
};
ClientCallbacks clientCB;

void connectToEUC() {
  Serial.println("Connecting...");
  NimBLEClient* pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(&clientCB, false);

  if (!pClient->connect(foundAddress)) {
    Serial.println("Connection failed - rescanning...");
    NimBLEDevice::deleteClient(pClient);
    deviceFound = false;
    return;
  }

  NimBLERemoteCharacteristic* pChar = nullptr;
  const std::vector<NimBLERemoteService*> services = pClient->getServices(true);
  for (NimBLERemoteService* svc : services) {
    pChar = svc->getCharacteristic(NOTIFY_UUID);
    if (pChar) break;
  }

  if (!pChar || !pChar->canNotify()) {
    Serial.println("Notify characteristic not found - rescanning...");
    pClient->disconnect();
    NimBLEDevice::deleteClient(pClient);
    deviceFound = false;
    return;
  }

  pChar->subscribe(true, notifyHandler);
  euc.connected = true;
  Serial.println("Subscribed - receiving data:");
}

// ── NimBLE scan callbacks ─────────────────────────────────────────────────────
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    String name = dev->getName().c_str();
    name.trim();
    bool matchName    = name.indexOf("EUC World 4FDE22") >= 0;
    bool matchService = dev->isAdvertisingService(NimBLEUUID("ffe0"));

    if (matchName || matchService) {
      Serial.println("EUC World 4FDE22 FOUND");
      foundAddress = dev->getAddress();
      deviceFound  = true;
      NimBLEDevice::getScan()->stop();
    }
  }

  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    if (!deviceFound) {
      Serial.println("EUC World 4FDE22 NOT found - restarting scan");
      NimBLEDevice::getScan()->start(scanTimeMs, false, true);
    }
  }
} scanCB;

void startScan() {
  deviceFound = false;
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCB, false);
  pScan->setActiveScan(true);
  pScan->setMaxResults(0);
  pScan->start(scanTimeMs, false, true);
  Serial.println("Scanning...");
}


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

//color gradient interpolation
lv_color_t arcColor(int val) {
  uint8_t r, g;
  if (val <= 50) {
    // Green to Yellow (0 to 50): red increases 0 to 255, green stays 255
    r = (uint8_t)(val * 255 / 50);
    g = 255;
  } else {
    // Yellow to Red (50 to 80): green decreases 255 to 0, red stays 255
    // Clamp anything above 80 to full red
    int v = (val >= 80) ? 80 : val;
    r = 255;
    g = (uint8_t)((80 - v) * 255 / 30);
  }
  return lv_color_make(r, g, 0);
}


lv_color_t arcColorPWM(int val) {
  uint8_t r, g;
  if (val <= 50) {
    // Green to Yellow (0 to 50): red increases 0 to 255, green stays 255
    r = (uint8_t)(val * 255 / 50);
    g = 255;
  } else {
    // Yellow to Red (50 to 80): green decreases 255 to 0, red stays 255
    // Clamp anything above 80 to full red
    int v = (val >= 80) ? 80 : val;
    r = 255;
    g = (uint8_t)((80 - v) * 255 / 30);
  }
  return lv_color_make(g, r, 0);
}





//function to set the actual color
void setPWMArcColor(int val) {
  lv_obj_set_style_arc_color(objects.pwm_arc, arcColorPWM(val), LV_PART_INDICATOR | LV_STATE_DEFAULT);
}

void setSpeedArcColor(int val) {
  lv_obj_set_style_arc_color(objects.speed_arc, arcColor(val), LV_PART_INDICATOR | LV_STATE_DEFAULT);
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
  Serial.begin(115200);
  NimBLEDevice::init("");
  startScan();


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

  // set static color for speed arc
  lv_obj_set_style_arc_color(objects.speed_arc, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
  
  // set static color for LED widget
  lv_led_set_color(objects.euc_connected_status, lv_color_hex(0xFFFFFF));  //set LED widget to white
  analogReadResolution(12);
  pinMode(1, INPUT);
}

void setEUCBattery(int val) {
  flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_EUC_BATTERY_VALUE, Value(val));
}

void setDEVBattery(int val) {
  flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_DEVICE_BATTERY_VALUE, Value(val));
}

// reading the battery of the euc display
const float conversion_factor = 3.3f / (1 << 12) * 3;
static constexpr float BAT_MAX_V = 3.3f;  // ADC reading at full charge (~4.2V actual)
static constexpr float BAT_MIN_V = 2.5f;  // ADC reading at cutoff (~3.0V actual)


int voltageToPercent(float v) {
  if (v >= BAT_MAX_V) return 100;
  if (v <= BAT_MIN_V) return 0;
  return (int)((v - BAT_MIN_V) / (BAT_MAX_V - BAT_MIN_V) * 100.0f);
}

float mini_batt;
float get_var_mini_batt() {
    return mini_batt;
}

void set_var_mini_batt(float value) {
    mini_batt = value;
}




void loop() {
  float voltage = (analogReadMilliVolts(1) * conversion_factor );
  set_var_mini_batt( voltageToPercent(voltage) );



  if (deviceFound && !NimBLEDevice::getScan()->isScanning()) {
    connectToEUC();
    if (!deviceFound) {
      delay(2000);
      startScan();
    }
  }

  if (euc.fresh) {
    euc.fresh = false;  // consume the fresh flag

    int speed  = euc.speed_valid  ? (int)(euc.speed * 0.621371f)  : 0; //mph conversion
    //int speed  = euc.speed_valid  ? (int)euc.speed : 0; //mph conversion
    
    int safety = euc.safety_valid ? (int)euc.safety : 0;
    int EUC_battery = euc.battery_valid ? (int)euc.euc_battery : 0;
    int DEV_battery = euc.dev_bat_valid ? (int)euc.dev_battery : 0;
    setEUCBattery(EUC_battery);
    setDEVBattery(DEV_battery);
    // add EUC_battery and DEV_battery 

    setSpeed(speed);
    setPWM(safety);
    setSpeedArcColor(speed);
    setPWMArcColor(safety);
  }

  lv_timer_handler();
  ui_tick();
  delay(5);
}