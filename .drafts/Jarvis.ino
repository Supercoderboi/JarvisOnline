#define ENCODER_DO_NOT_USE_INTERRUPTS 
#include <Encoder.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <BleKeyboard.h>
#include <NimBLEDevice.h>
#include <Preferences.h>  // Used to store the isolated boot state flag

#ifndef FW_BUILD_ID
#define FW_BUILD_ID "dev"
#endif

#define VERSION "2.0.0-ota"

const char* ssid = "Ethria2.4";
const char* password = "PalmDale007";

// Configure BleKeyboard
BleKeyboard bleKeyboard("Jarvis Remote", "ESP32", 100);
bool bleInitialized = false;

// Pin Definitions
const int homeBtn = 32;
const int backBtn = 33;
const int joyX = 34;
const int joyY = 35;
const int joySW = 25;
const int encCLK = 26;
const int encDT = 27;
const int encSW = 14;

// Operational States
bool otaModeActive = false;
bool otaServerRunning = false;
WebServer server(80);
Preferences preferences;

// Input State Tracking
unsigned long lastBtnCheck = 0;
const int debounceDelay = 300;

// Joystick Tuning
const int JOY_CENTER = 2048;
const int JOY_THRESHOLD = 1200; 
int lastJoyDir = 0;             
unsigned long lastJoyMove = 0;
const int joyDebounce = 200;    

// Rotary Encoder Tuning
Encoder myEnc(encCLK, encDT);
long oldPosition = -999;

// Nokia Display Setup
#define NOKIA_CLK 18
#define NOKIA_DIN 19
#define NOKIA_DC 21
#define NOKIA_CE 5
#define NOKIA_RST 15
Adafruit_PCD8544 display = Adafruit_PCD8544(NOKIA_CLK, NOKIA_DIN, NOKIA_DC, NOKIA_CE, NOKIA_RST);

const char* CURRENT_BUILD_ID = FW_BUILD_ID;
const char* UPDATE_MANIFEST_URLS[] = {
  "https://jarvisupload.netlify.app/firmware/manifest.json",
  "https://raw.githubusercontent.com/Supercoderboi/JarvisOnline/master/firmware/manifest.json"
};
const size_t UPDATE_MANIFEST_URL_COUNT = sizeof(UPDATE_MANIFEST_URLS) / sizeof(UPDATE_MANIFEST_URLS[0]);

const char* serverIndex =
  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
  "<h2 style='font-family:sans-serif;'>Jarvis System Update</h2>"
  "<p style='font-family:sans-serif;'>Select the new .bin file to flash.</p>"
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='update' accept='.bin' style='margin-bottom:20px;'><br>"
  "<input type='submit' value='Update Firmware' style='padding:10px 20px; background:#007BFF; color:white; border:none; border-radius:5px;'>"
  "</form>";

void showMessage(const String& line1, const String& line2 = "", const String& line3 = "", const String& line4 = "");
bool connectToWiFi(unsigned long timeoutMs);
bool checkForGitHubUpdate();
void startManualOtaServer();
void runOtaMode();

void setup() {
  Serial.begin(115200);
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

  // Check if we need to boot directly into clean OTA mode
  preferences.begin("system", false);
  otaModeActive = preferences.getBool("ota_boot", false);
  
  if (otaModeActive) {
    // Clear the flag immediately so if things crash, next boot is normal
    preferences.putBool("ota_boot", false);
    preferences.end();
    
    showMessage("JARVIS REMOTE", "Mode: OTA Update", "Initializing WiFi");
    if (connectToWiFi(20000)) {
      showMessage("WiFi OK", "Checking updates");
      runOtaMode(); // Runs updates cleanly before Bluetooth touches memory
    } else {
      showMessage("WiFi Timeout", "Rebooting normal");
      delay(2000);
      ESP.restart();
    }
    return; // Stop standard initialization
  }
  preferences.end();

  // Standard Operation Mode Initialization
  showMessage("Jarvis Remote", VERSION, "Booting...");
  delay(500);

  bleKeyboard.begin();
  bleInitialized = true; 
  showMessage("Jarvis Remote", "BLE Operational", "System Ready");
}

void loop() {
  if (otaModeActive) {
    if (otaServerRunning) {
      server.handleClient();
    }
    delay(10);
    return;
  }

  bool homePressed = (digitalRead(homeBtn) == LOW);
  bool backPressed = (digitalRead(backBtn) == LOW);

  // Trigger OTA Update Routine via Button Combination
  if (homePressed && backPressed) {
    if (millis() - lastBtnCheck > 1500) { 
      showMessage("Update Request", "Setting Flag", "Rebooting isolated");
      delay(1000);
      
      // Save memory boot flag and restart cleanly
      preferences.begin("system", false);
      preferences.putBool("ota_boot", true);
      preferences.end();
      
      ESP.restart();
    }
  }

  // Handle standard Fire TV input mappings when Bluetooth is connected
  if (bleInitialized && bleKeyboard.isConnected()) {
    
    if (homePressed && !backPressed && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_HOME); 
      showMessage("Jarvis Remote", "HOME pressed");
      lastBtnCheck = millis();
    }
    
    if (backPressed && !homePressed && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_ESC); 
      showMessage("Jarvis Remote", "BACK pressed");
      lastBtnCheck = millis();
    }

    int xVal = analogRead(joyX);
    int yVal = analogRead(joyY);
    int currentDir = 0; 

    if (yVal > JOY_CENTER + JOY_THRESHOLD)       currentDir = 2; // DOWN
    else if (yVal < JOY_CENTER - JOY_THRESHOLD)  currentDir = 1; // UP
    else if (xVal > JOY_CENTER + JOY_THRESHOLD)  currentDir = 4; // RIGHT
    else if (xVal < JOY_CENTER - JOY_THRESHOLD)  currentDir = 3; // LEFT

    if (currentDir != 0 && (currentDir != lastJoyDir || (millis() - lastJoyMove > joyDebounce))) {
      switch(currentDir) {
        case 1: bleKeyboard.write(KEY_UP_ARROW);    showMessage("Navigating", "UP"); break;
        case 2: bleKeyboard.write(KEY_DOWN_ARROW);  showMessage("Navigating", "DOWN"); break;
        case 3: bleKeyboard.write(KEY_LEFT_ARROW);  showMessage("Navigating", "LEFT"); break;
        case 4: bleKeyboard.write(KEY_RIGHT_ARROW); showMessage("Navigating", "RIGHT"); break;
      }
      lastJoyMove = millis();
    }
    lastJoyDir = currentDir; 

    if (digitalRead(joySW) == LOW && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_RETURN); 
      showMessage("Selection", "SELECT / OK");
      lastBtnCheck = millis();
    }

    long newPosition = myEnc.read() / 4;
    if (newPosition != oldPosition) {
      if (newPosition > oldPosition) {
        bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
        showMessage("Volume", "VOLUME UP");
      } else {
        bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
        showMessage("Volume", "VOLUME DOWN");
      }
      oldPosition = newPosition;
    }

    if (digitalRead(encSW) == LOW && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
      showMessage("Media Control", "PLAY / PAUSE");
      lastBtnCheck = millis();
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
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeoutMs) {
    delay(500);
  }
  return WiFi.status() == WL_CONNECTED;
}

void runOtaMode() {
  bool updateApplied = checkForGitHubUpdate();
  if (!updateApplied) {
    startManualOtaServer();
    showMessage("MANUAL OTA MODE", "Open URL:", WiFi.localIP().toString(), "Upload .bin");
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
      errorMessage = "HTTP " + String(httpCode);
      http.end();
      continue;
    }
    
    String payload = http.getString();
    http.end();
    
    StaticJsonDocument<512> doc;
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
    return buildId;
  }
  return "";
}

bool performFirmwareUpdate(const String& binUrl) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSize(1024); // Optimize network cache overhead

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(30000);
  http.setUserAgent("ESP32-OTA-Remote");

  if (!http.begin(client, binUrl)) {
    showMessage("Update Error", "Bad Target URL");
    delay(2000);
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    showMessage("Update Error", "HTTP: " + String(httpCode));
    http.end();
    delay(2000);
    return false;
  }

  int contentLength = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  size_t updateSize = (contentLength > 0) ? contentLength : UPDATE_SIZE_UNKNOWN;

  if (!Update.begin(updateSize)) {
    http.end();
    showMessage("Update Error", "No space");
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
          showMessage("Flashing...", String(percent) + "%", String(written / 1024) + "KB");
        } else {
          showMessage("Flashing...", String(written / 1024) + " KB");
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
  return false;
}

bool checkForGitHubUpdate() {
  String binUrl = "";
  String manifestError = "";
  String remoteBuildId = fetchRemoteBuildId(binUrl, manifestError);

  if (remoteBuildId.length() == 0 || binUrl.length() == 0) {
    showMessage("Manifest Error", manifestError);
    delay(2000);
    return false;
  }

  if (remoteBuildId == String(CURRENT_BUILD_ID)) {
    showMessage("System Status", "Up To Date");
    delay(2000);
    return false;
  }

  showMessage("Update Found", remoteBuildId);
  delay(1000);
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
    server.send(200, "text/plain", Update.hasError() ? "FAILED" : "SUCCESS! Restarting...");
    delay(2000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      showMessage("Receiving File", "Flashing...");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        showMessage("Done!", "Rebooting...");
      }
    }
  });

  server.begin();
  otaServerRunning = true;
}