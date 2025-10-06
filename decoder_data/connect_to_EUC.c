#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define SERVICE_UUID "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHAR_UUID    "0000ffe1-0000-1000-8000-00805f9b34fb"

#define SCALE_FACTOR (134.4 / 67.2)

// --- Data structures ---
std::vector<uint8_t> buffer;

struct FrameA {
  float voltage;
  float speed;
  float current;
  float temp;
  uint32_t distance;
} latestA;

struct FrameB {
  float total_distance;
  uint8_t pedal_speed;
  uint8_t led_mode;
} latestB;

// --- Utility functions ---
float scaleVoltage(uint16_t raw_v) { return raw_v * SCALE_FACTOR / 100.0; }
float scaleSpeed(int16_t raw_s)    { return abs(raw_s) / 100.0; }
float scaleCurrent(int16_t raw_c)  { return raw_c / 100.0; }
float scaleTemp(uint16_t raw_t)    { return raw_t / 100.0; }

std::vector<std::vector<uint8_t>> extractFrames(std::vector<uint8_t>& buf) {
  std::vector<std::vector<uint8_t>> frames;
  for (size_t i = 0; i + 24 <= buf.size(); i++) {
    if (buf[i] == 0x55 && buf[i + 1] == 0xAA) {
      std::vector<uint8_t> candidate(buf.begin() + i, buf.begin() + i + 24);
      if (candidate[20] == 0x5A && candidate[21] == 0x5A &&
          candidate[22] == 0x5A && candidate[23] == 0x5A) {
        frames.push_back(candidate);
        i += 23;
      }
    }
  }
  return frames;
}

bool parseFrame(const std::vector<uint8_t>& frame) {
  if (frame.size() != 24 || frame[0] != 0x55 || frame[1] != 0xAA)
    return false;

  uint8_t type = frame[18];
  if (type == 0x00) {  // Frame A
    uint16_t voltage_raw = (frame[2] << 8) | frame[3];
    int16_t speed_raw    = (frame[4] << 8) | frame[5];
    uint32_t distance    = (frame[6] << 24) | (frame[7] << 16) | (frame[8] << 8) | frame[9];
    int16_t current_raw  = (frame[10] << 8) | frame[11];
    uint16_t temp_raw    = (frame[12] << 8) | frame[13];

    latestA.voltage  = roundf(scaleVoltage(voltage_raw) * 100) / 100.0;
    latestA.speed    = roundf(scaleSpeed(speed_raw) * 100) / 100.0;
    latestA.current  = roundf(scaleCurrent(current_raw) * 100) / 100.0;
    latestA.temp     = roundf(scaleTemp(temp_raw) * 100) / 100.0;
    latestA.distance = distance;
    return true;
  } else if (type == 0x04) {  // Frame B
    uint32_t total_distance_raw = (frame[2] << 24) | (frame[3] << 16) | (frame[4] << 8) | frame[5];
    latestB.total_distance = total_distance_raw / 1000.0;
    latestB.pedal_speed = frame[6];
    latestB.led_mode = frame[13];
    return true;
  }
  return false;
}

// --- BLE globals ---
static boolean connected = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData, size_t length, bool isNotify) {

  for (size_t i = 0; i < length; i++) buffer.push_back(pData[i]);

  auto frames = extractFrames(buffer);
  for (auto& f : frames) parseFrame(f);

  if (buffer.size() > 2000)
    buffer.erase(buffer.begin(), buffer.end() - 2000);

  Serial.println("\n=== EUC Live Summary ===");
  Serial.printf("Frame A: Voltage = %.2f V (scaled 134V)\n", latestA.voltage);
  Serial.printf("         Speed   = %.2f km/h\n", latestA.speed);
  Serial.printf("         Current = %.2f A\n", latestA.current);
  Serial.printf("         Temp    = %.2f °C\n", latestA.temp);
  Serial.printf("         Distance= %lu m\n", latestA.distance);
  Serial.printf("Frame B: Total Distance = %.2f km\n", latestB.total_distance);
  Serial.printf("         Pedal/Alarm    = %d\n", latestB.pedal_speed);
  Serial.printf("         LED Mode       = %d\n", latestB.led_mode);
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    connected = true;
    Serial.println("Connected!");
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("Disconnected.");
  }
};

bool connectToServer() {
  BLEClient* pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  if (!pClient->connect(myDevice)) {
    Serial.println("Failed to connect.");
    return false;
  }

  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println("Service not found!");
    pClient->disconnect();
    return false;
  }

  pRemoteCharacteristic = pRemoteService->getCharacteristic(CHAR_UUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.println("Characteristic not found!");
    pClient->disconnect();
    return false;
  }

  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(notifyCallback);
  }

  return true;
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveServiceUUID() &&
        advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
      Serial.println("Found target device!");
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Scanning for EUC...");

  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);

  if (myDevice == nullptr) {
    Serial.println("EUC not found. Restart to try again.");
    return;
  }

  connectToServer();
}

void loop() {
  if (!connected) {
    delay(2000);
    return;
  }
  delay(1000);
}
