#define ENCODER_DO_NOT_USE_INTERRUPTS 
#include <Encoder.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
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

#define VERSION "2.1.0-fixed"

// --- Wi-Fi Configuration ---
const char* ssid = "Ethria2.4";
const char* password = "PalmDale007";

// --- Server Configuration (From Voice Module) ---
const char* host = "jarvisEp.pythonanywhere.com";
const char* chunkServerPath = "/voice-command/chunk";
const uint16_t serverPort = 80;

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
const int AO_PIN = 34; // Shared/Legacy pin from voice code
const int DO_PIN = 12;

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

// --- Nokia 5110 Display Layout ---
#define NOKIA_CLK 18
#define NOKIA_DIN 19
#define NOKIA_DC 21
#define NOKIA_CE 5
#define NOKIA_RST 15

Adafruit_PCD8544 display = Adafruit_PCD8544(NOKIA_CLK, NOKIA_DIN, NOKIA_DC, NOKIA_CE, NOKIA_RST);
WebServer server(80);
Preferences preferences;

// --- Audio & Operational State Variables ---
bool isRecording = false;
unsigned long lastSoundTime = 0;
unsigned long nextSampleTime = 0;
bool otaModeActive = false;
bool otaServerRunning = false;

// --- Scroll Text Configurations ---
#define MAX_SCROLL_LINES 20
String scrollLines[MAX_SCROLL_LINES];
int totalScrollLines = 0;
int currentScrollIndex = 0;
unsigned long lastScrollTime = 0;
const unsigned long SCROLL_INTERVAL_MS = 2000;
bool isScrollingActive = false;

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
void startScrollingMessage(const String& title, const String& message);
void handleDisplayScrolling();
void openOtaMode();
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
  pinMode(DO_PIN, INPUT);
  analogSetWidth(12);

  initializeDisplay();
  updateDisplay("JARVIS NODE", "Booting...", "Verifying Mode");

  // 1. Memory Check: Did the user request an update cycle?
  preferences.begin("system", false);
  otaModeActive = preferences.getBool("ota_boot", false);
  
  if (otaModeActive) {
    // Drop flag immediately so a bad loop won't brick the device sequence
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

  // 2. Normal Boot: Mount File System and start BLE stack
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed.");
    updateDisplay("JARVIS", "SPIFFS fail");
  }

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

  // Background display ticker
  handleDisplayScrolling();

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
    
    if (homePressed && !backPressed && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_HOME); 
      updateDisplay("FireTV HID", "HOME Triggered");
      lastBtnCheck = millis();
    }
    
    if (backPressed && !homePressed && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_ESC); 
      updateDisplay("FireTV HID", "BACK Triggered");
      lastBtnCheck = millis();
    }

    // Process Analog Navigation Joystick
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

    // Joystick Center Click Selection Processing
    if (digitalRead(joySW) == LOW && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_RETURN); 
      updateDisplay("Navigate", "SELECT / ENTER");
      lastBtnCheck = millis();
    }

    // Rotary Encoder Volume Ticks
    long newPosition = myEnc.read() / 4;
    if (newPosition != oldPosition) {
      if (newPosition > oldPosition) {
        bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
        updateDisplay("System Audio", "VOLUME UP");
      } else {
        bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
        updateDisplay("System Audio", "VOLUME DOWN");
      }
      oldPosition = newPosition;
    }

    // Rotary Click - Play/Pause media mapping toggle
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

void startScrollingMessage(const String& title, const String& message) {
  const int charsPerLine = 14;
  totalScrollLines = 0;
  currentScrollIndex = 0;
  scrollLines[totalScrollLines++] = title;
  
  int start = 0;
  while (start < message.length() && totalScrollLines < MAX_SCROLL_LINES) {
    int remaining = message.length() - start;
    int take = remaining < charsPerLine ? remaining : charsPerLine;

    if (start + take < message.length()) {
      int split = message.lastIndexOf(' ', start + take - 1);
      if (split >= start) take = split - start;
    }
    if (take <= 0) take = remaining < charsPerLine ? remaining : charsPerLine;

    scrollLines[totalScrollLines] = message.substring(start, start + take);
    scrollLines[totalScrollLines].trim();
    start += take;

    while (start < message.length() && message.charAt(start) == ' ') start++;
    totalScrollLines++;
  }
  isScrollingActive = true;
  lastScrollTime = millis();
  handleDisplayScrolling();
}

void handleDisplayScrolling() {
  if (!isScrollingActive) return;

  if (millis() - lastScrollTime >= SCROLL_INTERVAL_MS) {
    lastScrollTime = millis();
    if (currentScrollIndex + 5 < totalScrollLines) {
      currentScrollIndex++;
    } else {
      currentScrollIndex = 0;
    }
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  for (int i = 0; i < 6; i++) {
    int lineIndex = currentScrollIndex + i;
    if (lineIndex < totalScrollLines) {
      display.println(scrollLines[lineIndex]);
    }
  }
  display.display();
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

// --- Clean, Core 2.0.17 Compiling Firmware Updater Setup ---
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