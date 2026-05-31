#define ENCODER_DO_NOT_USE_INTERRUPTS 
#include <Encoder.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <WebServer.h>
#include <Update.h>
#include <BleKeyboard.h>
#include <NimBLEDevice.h>
#include <Preferences.h>  // Handles isolated boot-state tracking safely

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#ifndef FW_BUILD_ID
#define FW_BUILD_ID "dev"
#endif

#define VERSION "2.2.0-pure-remote"

// --- Wi-Fi Configuration ---
const char* ssid = "Ethria2.4";
const char* password = "PalmDale007";

// --- Bluetooth HID Configuration ---
BleKeyboard bleKeyboard("Jarvis Remote", "ESP32", 100);
bool bleInitialized = false;

// --- Pin Layout ---
const int homeBtn = 32;
const int backBtn = 33;
const int joyX = 34;
const int joyY = 35;
const int joySW = 25;
const int encCLK = 26;
const int encDT = 27;
const int encSW = 14;

// --- Input Tuning & States ---
unsigned long lastBtnCheck = 0;
const int debounceDelay = 300; 

const int JOY_CENTER = 2048;
const int JOY_THRESHOLD = 1200; 
int lastJoyDir = 0;             
unsigned long lastJoyMove = 0;
const int joyDebounce = 200;    

Encoder myEnc(encCLK, encDT);
long oldPosition = -999;

// Map standard Android Consumer Home key for FireTV systems
#define KEY_FIRE_HOME 0xEA
// --- Nokia 5110 Display Layout ---
#define NOKIA_CLK 18
#define NOKIA_DIN 19
#define NOKIA_DC 21
#define NOKIA_CE 5
#define NOKIA_RST 15

Adafruit_PCD8544 display = Adafruit_PCD8544(NOKIA_CLK, NOKIA_DIN, NOKIA_DC, NOKIA_CE, NOKIA_RST);
WebServer server(80);
Preferences preferences;

// --- Operational State Variables ---
bool otaModeActive = false;
bool otaServerRunning = false;

const char* CURRENT_BUILD_ID = FW_BUILD_ID;
const char* UPDATE_MANIFEST_URLS[] = {
  "https://jarvisupload.netlify.app/firmware/manifest.json",
  "https://raw.githubusercontent.com/Supercoderboi/JarvisOnline/master/firmware/manifest.json"
};
const size_t UPDATE_MANIFEST_URL_COUNT = sizeof(UPDATE_MANIFEST_URLS) / sizeof(UPDATE_MANIFEST_URLS[0]);

const char* serverIndex =
  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
  "<h2 style='font-family:sans-serif;'>Jarvis System Update</h2>"
  "<p style='font-family:sans-serif;'>Select the new .bin file from your phone to flash.</p>"
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='update' accept='.bin' style='margin-bottom:20px;'><br>"
  "<input type='submit' value='Update Firmware' style='padding:10px 20px; background:#007BFF; color:white; border:none; border-radius:5px;'>"
  "</form>";

// --- Forward Declarations ---
bool connectToWiFi(unsigned long timeoutMs);
void initializeDisplay();
void updateDisplay(const String& line1, const String& line2 = "", const String& line3 = "", const String& line4 = "", const String& line5 = "", const String& line6 = "");
void runOtaMode();
void showOtaMessage(const String& line1, const String& line2 = "", const String& line3 = "");
bool ensureWiFiForOta();
String fetchRemoteBuildId(String& binUrl, String& errorMessage);
bool isRemoteBuildNewer(const String& remoteBuildId);
bool performFirmwareUpdate(const String& binUrl);
bool checkForGitHubUpdate();
void startManualOtaServer();

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  
  pinMode(homeBtn, INPUT_PULLUP);
  pinMode(backBtn, INPUT_PULLUP);
  pinMode(joySW, INPUT_PULLUP);
  pinMode(encSW, INPUT_PULLUP);
  analogSetWidth(12);

  initializeDisplay();
  updateDisplay("JARVIS NODE", "Booting...", "Verifying Mode");

  // 1. Memory Check: Did the user request an update cycle?
  preferences.begin("system", false);
  otaModeActive = preferences.getBool("ota_boot", false);
  
  if (otaModeActive) {
    // Drop flag immediately so a bad loop won't loop the boot state sequence
    preferences.putBool("ota_boot", false);
    preferences.end();
    
    updateDisplay("JARVIS NODE", "BOOT MODE: OTA", "Connecting WiFi");
    if (connectToWiFi(20000)) {
      runOtaMode(); // Executes direct GitHub check completely clear of Bluetooth stack allocations
    } else {
      updateDisplay("WiFi Timeout", "Rebooting to", "Normal Remote");
      delay(2000);
      ESP.restart();
    }
    return; 
  }
  preferences.end();

  // 2. Normal Boot: Start BLE stack directly
  updateDisplay("JARVIS", "Initializing", "BLE Stack", "Version:", VERSION);
  bleKeyboard.begin();
  bleInitialized = true;
  
  delay(500);
  updateDisplay("JARVIS REMOTE", "Ready for Pair", "Go to FireTV", "Settings");
}

void loop() {
  // If we are locked inside clean OTA sequence, bypass standard remote configurations
  if (otaModeActive) {
    if (otaServerRunning) {
      server.handleClient();
    }
    delay(10);
    return;
  }

  bool homePressed = (digitalRead(homeBtn) == LOW);
  bool backPressed = (digitalRead(backBtn) == LOW);

  // Catch user holding Down Combo to update remote firmware via background preferences flag
  if (homePressed && backPressed) {
    if (millis() - lastBtnCheck > 1500) { 
      updateDisplay("JARVIS SYSTEM", "Update Selected", "Rebooting cleanly", "Please Wait...");
      delay(1500);
      
      preferences.begin("system", false);
      preferences.putBool("ota_boot", true);
      preferences.end();
      
      ESP.restart(); // Triggers a hardware reset directly into a safe OTA container
    }
  }

  // Handle standard Fire TV Input Processing loops
  if (bleInitialized && bleKeyboard.isConnected()) {
    
    // --- HOME Button Processing (Fixed for Fire TV / Android) ---
    if (homePressed && !backPressed && (millis() - lastBtnCheck > debounceDelay)) {
      const uint8_t homeKeyBuffer[2] = {0xEA, 0}; // 0xEA maps directly to Android's Consumer Home Key
      bleKeyboard.write(homeKeyBuffer); 
      updateDisplay("FireTV HID", "HOME Triggered");
      lastBtnCheck = millis();
    }
    
    // --- BACK Button Processing ---
    if (backPressed && !homePressed && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_ESC); 
      updateDisplay("FireTV HID", "BACK Triggered");
      lastBtnCheck = millis();
    }

    // --- Process Analog Navigation Joystick ---
    int xVal = analogRead(joyX);
    int yVal = analogRead(joyY);
    int currentDir = 0; 

    if (yVal > JOY_CENTER + JOY_THRESHOLD)       currentDir = 2; // DOWN
    else if (yVal < JOY_CENTER - JOY_THRESHOLD)  currentDir = 1; // UP
    else if (xVal > JOY_CENTER + JOY_THRESHOLD)  currentDir = 4; // RIGHT
    else if (xVal < JOY_CENTER - JOY_THRESHOLD)  currentDir = 3; // LEFT

    if (currentDir != 0 && (currentDir != lastJoyDir || (millis() - lastJoyMove > joyDebounce))) {
      switch(currentDir) {
        case 1: bleKeyboard.write(KEY_UP_ARROW);    updateDisplay("Navigate", "UP"); break;
        case 2: bleKeyboard.write(KEY_DOWN_ARROW);  updateDisplay("Navigate", "DOWN"); break;
        case 3: bleKeyboard.write(KEY_LEFT_ARROW);  updateDisplay("Navigate", "LEFT"); break;
        case 4: bleKeyboard.write(KEY_RIGHT_ARROW); updateDisplay("Navigate", "RIGHT"); break;
      }
      lastJoyMove = millis();
    }
    lastJoyDir = currentDir; 

    // --- Joystick Center Click Selection Processing ---
    if (digitalRead(joySW) == LOW && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_RETURN); 
      updateDisplay("Navigate", "SELECT / ENTER");
      lastBtnCheck = millis();
    }

    // --- Rotary Encoder Volume Ticks (Fixed Media Buffering) ---
    long newPosition = myEnc.read() / 4;
    if (newPosition != oldPosition) {
      if (newPosition > oldPosition) {
        const uint8_t volUpBuffer[2] = {KEY_MEDIA_VOLUME_UP, 0};
        bleKeyboard.write(volUpBuffer);
        updateDisplay("System Audio", "VOLUME UP");
      } else {
        const uint8_t volDownBuffer[2] = {KEY_MEDIA_VOLUME_DOWN, 0};
        bleKeyboard.write(volDownBuffer);
        updateDisplay("System Audio", "VOLUME DOWN");
      }
      oldPosition = newPosition;
    }

    // --- Rotary Click - Play/Pause media mapping toggle ---
    if (digitalRead(encSW) == LOW && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
      updateDisplay("Media Engine", "PLAY / PAUSE");
      lastBtnCheck = millis();
    }
  }
  delay(10);
}

// --- Display Mechanics & Drivers ---
void initializeDisplay() {
  display.begin();
  display.setContrast(58);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.display();
}

void updateDisplay(const String& line1, const String& line2, const String& line3, const String& line4, const String& line5, const String& line6) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(line1);
  display.println(line2);
  display.println(line3);
  display.println(line4);
  display.println(line5);
  display.println(line6);
  display.display();
}

void showOtaMessage(const String& line1, const String& line2, const String& line3) {
  updateDisplay(line1, line2, line3);
}

// --- Wi-Fi Connections ---
bool connectToWiFi(unsigned long timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeoutMs) {
    delay(500);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool ensureWiFiForOta() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect(false, false);
  WiFi.reconnect();
  unsigned long reconnectStarted = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - reconnectStarted < 10000) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

// --- Core 2.0.17 Compiling Firmware Updater Setup ---
void runOtaMode() {
  bool updateApplied = checkForGitHubUpdate();
  if (!updateApplied) {
    startManualOtaServer();
    showOtaMessage("MANUAL OTA MODE", "URL: " + WiFi.localIP().toString(), "Upload .bin asset");
  }
}

String fetchRemoteBuildId(String& binUrl, String& errorMessage) {
  for (size_t urlIndex = 0; urlIndex < UPDATE_MANIFEST_URL_COUNT; urlIndex++) {
    const char* manifestUrl = UPDATE_MANIFEST_URLS[urlIndex];
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
      errorMessage = "HTTP Error " + String(httpCode);
      http.end();
      continue;
    }
    
    String payload = http.getString();
    http.end();
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      errorMessage = String("JSON parse fail: ") + error.c_str();
      continue;
    }
    
    binUrl = doc["bin_url"] | "";
    String buildId = doc["build_id"] | "";
    if (buildId.length() == 0 || binUrl.length() == 0) {
      errorMessage = "Missing keys in JSON";
      continue;
    }
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
  http.setTimeout(30000); 
  http.setUserAgent("ESP32-OTA-Remote");

  showOtaMessage("Downloading", "firmware binary...");
  
  if (!http.begin(client, binUrl)) {
    showOtaMessage("Update Error", "Bad Target Target");
    delay(2000);
    return false;
  }
  
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    showOtaMessage("Update Error", "HTTP Code: " + String(httpCode));
    http.end();
    delay(3000);
    return false;
  }
  
  int contentLength = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  size_t updateSize = (contentLength > 0) ? contentLength : UPDATE_SIZE_UNKNOWN;
  
  if (!Update.begin(updateSize)) {
    http.end();
    showOtaMessage("Update Error", "Out of flash space");
    delay(3000);
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
          showOtaMessage("Flashing...", String(percent) + "%", String(written / 1024) + "KB");
        } else {
          showOtaMessage("Flashing...", String(written / 1024) + " KB");
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
    showOtaMessage("Update Success", "Rebooting nodes...");
    delay(1500);
    ESP.restart();
    return true;
  }
  return false;
}

bool checkForGitHubUpdate() {
  if (!ensureWiFiForOta()) {
    showOtaMessage("OTA Error", "Network Drop");
    delay(2000);
    return false;
  }

  String binUrl = "";
  String manifestError = "";
  String remoteBuildId = fetchRemoteBuildId(binUrl, manifestError);

  if (remoteBuildId.length() == 0 || binUrl.length() == 0) {
    showOtaMessage("OTA Error", manifestError.length() == 0 ? "Bad Manifest File" : manifestError);
    delay(2000);
    return false;
  }

  if (!isRemoteBuildNewer(remoteBuildId)) {
    showOtaMessage("Jarvis Remote", "Firmware up to date");
    delay(2000);
    return false;
  }

  showOtaMessage("Update Detected", remoteBuildId);
  delay(1200);
  return performFirmwareUpdate(binUrl);
}

void startManualOtaServer() {
  if (otaServerRunning) return;

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });

  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", Update.hasError() ? "FLASH DECK ERROR! System Resetting." : "FLASH SUCCESSFUL! Launching Jarvis...");
    delay(2000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      showOtaMessage("Receiving local", "firmware file...");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        showOtaMessage("Upload Done!", "Flashing complete");
      }
    }
  });

  server.begin();
  otaServerRunning = true;
}