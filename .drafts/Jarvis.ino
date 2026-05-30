#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

#include <BleKeyboard.h>

#ifndef KEY_UP_ARROW
#define KEY_UP_ARROW    0xDA
#endif

#ifndef KEY_DOWN_ARROW
#define KEY_DOWN_ARROW  0xD9
#endif

#ifndef KEY_LEFT_ARROW
#define KEY_LEFT_ARROW  0xD8
#endif

#ifndef KEY_RIGHT_ARROW
#define KEY_RIGHT_ARROW 0xD7
#endif

#ifndef KEY_RETURN
#define KEY_RETURN      0xB0
#endif

#ifndef KEY_ESC
#define KEY_ESC         0xB1
#endif

#include <Encoder.h>

#ifndef FW_BUILD_ID
#define FW_BUILD_ID "dev"
#endif

#define VERSION "1.3.4-slim"

const char* ssid = "Ethria2.4";
const char* password = "PalmDale007";

// Keep this as BleKeyboard. The global compiler flag handles the magic now!
BleKeyboard bleKeyboard("Jarvis Remote", "ESP32", 100);

const int homeBtn = 32;
const int backBtn = 33;
unsigned long lastBtnCheck = 0;
const int debounceDelay = 1000;

const int joyX = 34;
const int joyY = 35;
const int joySW = 25;
int lastJoyDir = 0;
unsigned long lastJoyMove = 0;
const int joyThreshold = 800;
const int joyDebounce = 250;

const int encCLK = 26;
const int encDT = 27;
const int encSW = 14;
Encoder myEnc(encCLK, encDT);
long oldPosition = -999;

#define NOKIA_CLK 18
#define NOKIA_DIN 19
#define NOKIA_DC 21
#define NOKIA_CE 5
#define NOKIA_RST 15
Adafruit_PCD8544 display = Adafruit_PCD8544(NOKIA_CLK, NOKIA_DIN, NOKIA_DC, NOKIA_CE, NOKIA_RST);

unsigned long lastWiFiAttempt = 0;
const unsigned long WIFI_RETRY_INTERVAL = 30000;
bool wifiConnecting = false;

const char* CURRENT_BUILD_ID = FW_BUILD_ID;
const char* UPDATE_MANIFEST_URLS[] = {
  "https://jarvisupload.netlify.app/firmware/manifest.json",
  "https://raw.githubusercontent.com/Supercoderboi/JarvisOnline/master/firmware/manifest.json"
};
const size_t UPDATE_MANIFEST_URL_COUNT = sizeof(UPDATE_MANIFEST_URLS) / sizeof(UPDATE_MANIFEST_URLS[0]);

void showMessage(const String& line1, const String& line2 = "", const String& line3 = "", const String& line4 = "");
bool connectToWiFi(unsigned long timeoutMs);
bool checkForGitHubUpdate();

void setup() {
  pinMode(homeBtn, INPUT_PULLUP);
  pinMode(backBtn, INPUT_PULLUP);
  pinMode(joySW, INPUT_PULLUP);
  pinMode(encSW, INPUT_PULLUP);

  display.begin();
  display.setContrast(58);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.display();

  showMessage("Jarvis Remote", VERSION, "Booting...");

  bleKeyboard.begin();
  delay(500);

  if (connectToWiFi(20000)) {
    showMessage("WiFi OK", WiFi.localIP().toString(), "BLE Starting", "H+B = Update");
  } else {
    showMessage("WiFi FAIL", "BLE Starting", "Retrying 30s");
    lastWiFiAttempt = millis();
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED && millis() - lastWiFiAttempt > WIFI_RETRY_INTERVAL) {
    if (!wifiConnecting) {
      wifiConnecting = true;
      showMessage("WiFi lost", "Retrying...");
    }
    if (connectToWiFi(10000)) {
      showMessage("WiFi OK", bleKeyboard.isConnected() ? "BLE OK" : "BLE Wait");
      wifiConnecting = false;
    } else {
      showMessage("WiFi FAIL", "Retrying 30s");
      wifiConnecting = false;
    }
    lastWiFiAttempt = millis();
  }

  bool homePressed = digitalRead(homeBtn) == LOW;
  bool backPressed = digitalRead(backBtn) == LOW;
  if(homePressed && backPressed && millis() - lastBtnCheck > debounceDelay) {
    showMessage("Update combo", "detected...");
    delay(500);
    checkForGitHubUpdate();
    lastBtnCheck = millis();
    return;
  }

  if(bleKeyboard.isConnected()) {
    int xVal = analogRead(joyX);
    int yVal = analogRead(joyY);
    int currentDir = 0;

    if (yVal < 4095 - joyThreshold) currentDir = 1;
    else if (yVal > joyThreshold) currentDir = 2;
    else if (xVal < 4095 - joyThreshold) currentDir = 3;
    else if (xVal > joyThreshold) currentDir = 4;

    if (currentDir != 0 && currentDir != lastJoyDir && millis() - lastJoyMove > joyDebounce) {
      switch(currentDir) {
        case 1: bleKeyboard.write(KEY_UP_ARROW); showMessage("BLE OK", "UP"); break;
        case 2: bleKeyboard.write(KEY_DOWN_ARROW); showMessage("BLE OK", "DOWN"); break;
        case 3: bleKeyboard.write(KEY_LEFT_ARROW); showMessage("BLE OK", "LEFT"); break;
        case 4: bleKeyboard.write(KEY_RIGHT_ARROW); showMessage("BLE OK", "RIGHT"); break;
      }
      lastJoyMove = millis();
    }
    lastJoyDir = currentDir;

    if(digitalRead(joySW) == LOW) {
      bleKeyboard.write(KEY_RETURN);
      showMessage("BLE OK", "SELECT");
      delay(300);
    }

    long newPosition = myEnc.read() / 4;
    if (newPosition != oldPosition) {
      if(newPosition > oldPosition) {
        bleKeyboard.write(KEY_UP_ARROW);
        showMessage("BLE OK", "ENC UP");
      } else {
        bleKeyboard.write(KEY_DOWN_ARROW);
        showMessage("BLE OK", "ENC DOWN");
      }
      oldPosition = newPosition;
    }

    if(digitalRead(encSW) == LOW) {
      bleKeyboard.write(KEY_ESC);
      showMessage("BLE OK", "BACK");
      delay(300);
    }

  } else {
    static unsigned long lastBleMsg = 0;
    if (millis() - lastBleMsg > 2000) {
      showMessage("BLE", "Not Paired", "Fire TV", "Settings>BT");
      lastBleMsg = millis();
    }
  }

  delay(10);
}

void showMessage(const String& line1, const String& line2, const String& line3, const String& line4) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(line1);
  display.println(line2);
  display.println(line3);
  display.println(line4);
  display.display();
}

bool connectToWiFi(unsigned long timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeoutMs) {
    delay(500);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool ensureWiFiForOta() {
  if (WiFi.status() == WL_CONNECTED) return true;
  showMessage("WiFi lost", "Reconnecting");
  WiFi.disconnect(false, false);
  WiFi.reconnect();
  unsigned long reconnectStarted = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - reconnectStarted < 10000) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

String fetchRemoteBuildId(String& binUrl, String& errorMessage) {
  for (size_t urlIndex = 0; urlIndex < UPDATE_MANIFEST_URL_COUNT; urlIndex++) {
    const char* manifestUrl = UPDATE_MANIFEST_URLS[urlIndex];
    showMessage("Checking", "manifest...", String(urlIndex + 1) + "/" + String(UPDATE_MANIFEST_URL_COUNT));
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, manifestUrl)) {
      errorMessage = "begin() failed";
      continue;
    }
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      errorMessage = http.errorToString(httpCode);
      http.end();
      continue;
    }
    String payload = http.getString();
    http.end();
    StaticJsonDocument<384> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      errorMessage = String("JSON ") + error.c_str();
      continue;
    }
    binUrl = doc["bin_url"] | "";
    String buildId = doc["build_id"] | "";
    if (buildId.length() == 0 || binUrl.length() == 0) {
      errorMessage = "Missing fields";
      continue;
    }
    errorMessage = "";
    return buildId;
  }
  return "";
}

bool isRemoteBuildNewer(const String& remoteBuildId) {
  return remoteBuildId.length() > 0 && remoteBuildId != String(CURRENT_BUILD_ID);
}

bool performFirmwareUpdate(const String& binUrl) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  showMessage("Downloading", "firmware...");
  if (!http.begin(client, binUrl)) {
    showMessage("Update Error", "Bad BIN URL");
    delay(2000);
    return false;
  }
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    showMessage("Update Error", http.errorToString(httpCode));
    http.end();
    delay(2000);
    return false;
  }
  int contentLength = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    http.end();
    showMessage("Update Error", "No space");
    delay(2000);
    return false;
  }
  uint8_t buffer[128];
  int written = 0;
  unsigned long lastDataAt = millis();
  bool streamFailed = false;
  while (http.connected()) {
    int availableSize = stream->available();
    if (availableSize > 0) {
      size_t chunkSize = (size_t)availableSize;
      if (chunkSize > sizeof(buffer)) chunkSize = sizeof(buffer);
      int readLen = stream->readBytes(buffer, chunkSize);
      if (readLen > 0) {
        if (Update.write(buffer, readLen) != (size_t)readLen) {
          Update.abort();
          streamFailed = true;
          break;
        }
        written += readLen;
        lastDataAt = millis();
        if (contentLength > 0) {
          int percent = (written * 100) / contentLength;
          showMessage("Updating...", String(percent) + "%", String(written/1024) + "KB");
          if (written >= contentLength) break;
        } else {
          showMessage("Updating...", String(written / 1024) + " KB");
        }
      }
    } else {
      if (!http.connected()) break;
      if (millis() - lastDataAt > 15000) {
        Update.abort();
        streamFailed = true;
        break;
      }
      delay(1);
    }
  }
  bool success = !streamFailed && Update.end(true);
  http.end();
  if (success && Update.isFinished()) {
    showMessage("Update Done!", "Rebooting...");
    delay(1500);
    ESP.restart();
    return true;
  }
  showMessage("Update Error", "Finalize fail");
  delay(2000);
  return false;
}

bool checkForGitHubUpdate() {
  if (!ensureWiFiForOta()) {
    showMessage("OTA Error", "No WiFi", "Retrying 30s");
    lastWiFiAttempt = millis() - WIFI_RETRY_INTERVAL + 2000;
    return false;
  }
  String binUrl = "";
  String manifestError = "";
  String remoteBuildId = fetchRemoteBuildId(binUrl, manifestError);
  if (remoteBuildId.length() == 0 || binUrl.length() == 0) {
    if (manifestError.length() == 0) manifestError = "Bad manifest";
    showMessage("OTA Error", manifestError);
    delay(2000);
    showMessage("Ready", "H+B = Update");
    return false;
  }
  showMessage("Current:", CURRENT_BUILD_ID, "Remote:", remoteBuildId);
  delay(1500);
  if (!isRemoteBuildNewer(remoteBuildId)) {
    showMessage("Device", "Up To Date", CURRENT_BUILD_ID);
    delay(2000);
    showMessage("Ready", "H+B = Update");
    return false;
  }
  showMessage("Update Found", remoteBuildId);
  delay(1200);
  bool result = performFirmwareUpdate(binUrl);
  if (!result) {
    showMessage("Update Failed", "Hold H+B", "to retry");
  }
  return result;
}