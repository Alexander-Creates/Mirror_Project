#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>



// ── OLED config ───────────────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C
#define SDA_PIN       8
#define SCL_PIN       9

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// LED configurations
// Pin2 = Green
// Pin3 = Yellow
// Pin4 = Red

int greenLed = 2;
int yellowLed = 3;
int redLed = 4;


// ── BLE config ────────────────────────────────────────────────────────────────
#define NOTIFY_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

static constexpr uint32_t scanTimeMs = 10 * 1000;

NimBLEAddress foundAddress("", 0);
bool deviceFound = false;

// ── Telemetry ─────────────────────────────────────────────────────────────────
struct {
  float   speed      = 0;
  uint8_t safety     = 0;
  bool    speed_valid  = false;
  bool    safety_valid = false;
  bool    connected    = false;
  bool    fresh        = false;
} euc;

// ── Helpers ───────────────────────────────────────────────────────────────────
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

// ── Frame parsers ─────────────────────────────────────────────────────────────
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

// ── OLED renderer ─────────────────────────────────────────────────────────────
void draw_bar(int x, int y, int w, int h, uint8_t pct, bool valid) {
  display.drawRect(x, y, w, h, SSD1306_WHITE);
  if (valid && pct > 0) {
    int fill = (int)((pct / 100.0f) * (w - 2));
    if (fill > w - 2) fill = w - 2;
    display.fillRect(x + 1, y + 1, fill, h - 2, SSD1306_WHITE);
  }
}

void render() {
  display.clearDisplay();

  if (!euc.connected) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 28);
    display.print("Scanning for EUC...");
    display.display();
    return;
  }

  if (!euc.fresh) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 28);
    display.print("Waiting for data");
    display.display();
    return;
  }

  // ── Speed (large, top half) ───────────────────────────────────────────────
  char spd_buf[8];
  snprintf(spd_buf, sizeof(spd_buf), euc.speed_valid ? "%.1f" : "--.-", euc.speed);

  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  int spd_w = strlen(spd_buf) * 18;
  display.setCursor((SCREEN_WIDTH - spd_w) / 2, 2);
  display.print(spd_buf);

  display.setTextSize(1);
  display.setCursor(104, 14);
  display.print("km/h");

  // ── Divider ───────────────────────────────────────────────────────────────
  display.drawFastHLine(0, 28, SCREEN_WIDTH, SSD1306_WHITE);

  // ── Safety margin (bottom half) ───────────────────────────────────────────
  display.setTextSize(1);
  display.setCursor(0, 33);
  display.print("SAFETY");

  char saf_buf[5];
  snprintf(saf_buf, sizeof(saf_buf), euc.safety_valid ? "%3d%%" : " --%%", euc.safety);
  display.setCursor(0, 43);
  display.print(saf_buf);

  // Warning: invert block when safety < 20%
  bool warn = euc.safety_valid && euc.safety < 20;
  bool warn2 = euc.safety_valid && euc.safety < 40;

if (warn) {
    display.fillRect(70, 33, 58, 20, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(2);
    char w_buf[6];
    snprintf(w_buf, sizeof(w_buf), "%d%%!", euc.safety);
    display.setCursor(72, 37);
    display.print(w_buf);
    display.setTextColor(SSD1306_WHITE);
    digitalWrite(redLed,    HIGH);
    digitalWrite(yellowLed, LOW);
    digitalWrite(greenLed,  LOW);
  
  } else if (warn2) {
    digitalWrite(redLed,    LOW);
    digitalWrite(yellowLed, HIGH);
    digitalWrite(greenLed,  LOW);  

  } else {
    digitalWrite(redLed,    LOW);
    digitalWrite(yellowLed, LOW);
    digitalWrite(greenLed,  HIGH);

    // Numeric value large on the right
    display.setTextSize(2);
    char big_saf[5];
    snprintf(big_saf, sizeof(big_saf), euc.safety_valid ? "%3d%%" : "--%%", euc.safety);
    display.setCursor(64, 35);
    display.print(big_saf);
  }

  // Progress bar full width at bottom
  draw_bar(0, 54, SCREEN_WIDTH, 10, euc.safety, euc.safety_valid);

  display.display();
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


// ── Arduino setup / loop ──────────────────────────────────────────────────────
void setup() {
  
pinMode(greenLed, OUTPUT);
pinMode(yellowLed, OUTPUT);
pinMode(redLed, OUTPUT);
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("SSD1306 init failed - check wiring!");
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.display();

  NimBLEDevice::init("");
  startScan();
}

void loop() {
  render();

  //

  if (deviceFound && !NimBLEDevice::getScan()->isScanning()) {
    connectToEUC();
    if (!deviceFound) {
      delay(2000);
      startScan();
    }
  }
  delay(100);
}