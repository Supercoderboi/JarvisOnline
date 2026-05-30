#define ENCODER_DO_NOT_USE_INTERRUPTS 
#include <Encoder.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <BleKeyboard.h>
#include <NimBLEDevice.h>

#ifndef FW_BUILD_ID
#define FW_BUILD_ID "dev"
#endif

#define VERSION "1.4.0-hid"

const char* ssid = "Ethria2.4";
const char* password = "PalmDale007";

// Configure BleKeyboard to present itself clearly to the Fire TV
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

// Input State Tracking
unsigned long lastBtnCheck = 0;
const int debounceDelay = 300; // Snappier button response

// Joystick Tuning
// ESP32 ADC goes from 0 to 4095. Center is roughly 2048.
const int JOY_CENTER = 2048;
const int JOY_THRESHOLD = 1200; // Distance from center to trigger movement
int lastJoyDir = 0;             // 0=Center, 1=UP, 2=DOWN, 3=LEFT, 4=RIGHT
unsigned long lastJoyMove = 0;
const int joyDebounce = 200;    // Delay between repeated directional swiping

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
  delay(1000);

  bleKeyboard.begin();
  bleInitialized = true; 
  delay(1000);

  if (connectToWiFi(20000)) {
    showMessage("WiFi OK", WiFi.localIP().toString(), "BLE Ready", "H+B = Update");
  } else {
    showMessage("WiFi FAIL", "BLE Ready", "Retrying 30s");
    lastWiFiAttempt = millis();
  }
}

void loop() {
  // Handle Wi-Fi background monitoring reconnects
  if (WiFi.status() != WL_CONNECTED && millis() - lastWiFiAttempt > WIFI_RETRY_INTERVAL) {
    if (!wifiConnecting) {
      wifiConnecting = true;
      showMessage("WiFi lost", "Retrying...");
    }
    if (connectToWiFi(10000)) {
      showMessage("WiFi OK", (bleInitialized && bleKeyboard.isConnected()) ? "BLE OK" : "BLE Wait");
      wifiConnecting = false;
    } else {
      showMessage("WiFi FAIL", "Retrying 30s");
      wifiConnecting = false;
    }
    lastWiFiAttempt = millis();
  }

  // Read raw Button States (LOW when pressed)
  bool homePressed = (digitalRead(homeBtn) == LOW);
  bool backPressed = (digitalRead(backBtn) == LOW);

  // 1. CRITICAL: Check for OTA Update Combo Command First
  if (homePressed && backPressed) {
    if (millis() - lastBtnCheck > 1500) { // Require a clear deliberate hold
      showMessage("Update combo", "detected...");
      delay(500);
      checkForGitHubUpdate();
      lastBtnCheck = millis();
      return;
    }
  }

  // Process HID commands only if Bluetooth is connected to Fire TV
  if (bleInitialized && bleKeyboard.isConnected()) {
    
    // 2. Individual Dedicated Home & Back Button Processing
    if (homePressed && !backPressed && (millis() - lastBtnCheck > debounceDelay)) {
      // Send standard HID Home key sequence to drop back to FireOS launcher dashboard
      bleKeyboard.write(KEY_HOME); 
      showMessage("Jarvis Remote", "HOME pressed");
      lastBtnCheck = millis();
    }
    
    if (backPressed && !homePressed && (millis() - lastBtnCheck > debounceDelay)) {
      // Send standard HID Escape key layout which maps natively to "Back" on Android/FireOS
      bleKeyboard.write(KEY_ESC); 
      showMessage("Jarvis Remote", "BACK pressed");
      lastBtnCheck = millis();
    }

    // 3. Robust Joystick Navigation Processing
    int xVal = analogRead(joyX);
    int yVal = analogRead(joyY);
    int currentDir = 0; // 0=Center, 1=UP, 2=DOWN, 3=LEFT, 4=RIGHT

    // Determine direction based on deviation thresholds from the analog center point
    if (yVal > JOY_CENTER + JOY_THRESHOLD)       currentDir = 2; // DOWN
    else if (yVal < JOY_CENTER - JOY_THRESHOLD)  currentDir = 1; // UP
    else if (xVal > JOY_CENTER + JOY_THRESHOLD)  currentDir = 4; // RIGHT
    else if (xVal < JOY_CENTER - JOY_THRESHOLD)  currentDir = 3; // LEFT

    // Fire the command if moved to a new direction or if holding down past debounce timeout
    if (currentDir != 0 && (currentDir != lastJoyDir || (millis() - lastJoyMove > joyDebounce))) {
      switch(currentDir) {
        case 1: bleKeyboard.write(KEY_UP_ARROW);    showMessage("Navigating", "UP"); break;
        case 2: bleKeyboard.write(KEY_DOWN_ARROW);  showMessage("Navigating", "DOWN"); break;
        case 3: bleKeyboard.write(KEY_LEFT_ARROW);  showMessage("Navigating", "LEFT"); break;
        case 4: bleKeyboard.write(KEY_RIGHT_ARROW); showMessage("Navigating", "RIGHT"); break;
      }
      lastJoyMove = millis();
    }
    lastJoyDir = currentDir; // Updates state machinery to ensure snap response

    // 4. Joystick Click -> Natively Selects/Enters
    if (digitalRead(joySW) == LOW && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_RETURN); // Select / OK item
      showMessage("Selection", "SELECT / OK");
      lastBtnCheck = millis();
    }

    // 5. Rotary Encoder -> Precise TV System Volume Manipulation
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

    // 6. Rotary Encoder Click Switch -> Universal Video Play/Pause Toggle
    if (digitalRead(encSW) == LOW && (millis() - lastBtnCheck > debounceDelay)) {
      bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
      showMessage("Media Control", "PLAY / PAUSE");
      lastBtnCheck = millis();
    }

  } else {
    // Bluetooth Connection status reporting ticker
    if (bleInitialized) {
      static unsigned long lastBleMsg = 0;
      if (millis() - lastBleMsg > 3000) {
        showMessage("BLE STATUS", "Not Connected", "Go to Fire TV", "Settings > BT");
        lastBleMsg = millis();
      }
    }
  }

  delay(10);
}

// Display & OTA helper tasks remain unchanged below here
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
  String finalDownloadUrl = binUrl; 
  
  // STEP 1: Resolve the 302 redirect safely
  {
    WiFiClientSecure initialClient;
    initialClient.setInsecure();
    
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    http.setUserAgent("ESP32-OTA-Remote");
    
    const char* headerKeys[] = {"Location"};
    http.collectHeaders(headerKeys, 1);
    
    showMessage("Resolving URL...", "Checking route");
    
    if (http.begin(initialClient, binUrl)) {
      int httpCode = http.GET();
      
      if (httpCode == 301 || httpCode == 302 || httpCode == 303 || httpCode == 307) {
        String redirectedUrl = http.header("Location");
        if (redirectedUrl.length() > 0) {
          finalDownloadUrl = redirectedUrl;
        }
      }
      http.end();
    }
  }

  // STEP 2: Initiate a completely fresh download client
  WiFiClientSecure downloadClient;
  downloadClient.setInsecure(); 
  
  // CRITICAL SNI FIX: Parse out the new host domain name for the secure handshake
  // e.g., Extracting "objects.githubusercontent.com" from the final redirect URL
  if (finalDownloadUrl.startsWith("https://")) {
    String remaining = finalDownloadUrl.substring(8);
    int slashIndex = remaining.indexOf('/');
    if (slashIndex > 0) {
      String hostName = remaining.substring(0, slashIndex);
      // Force the secure engine to expect the redirected host name
      downloadClient.setPeerName(hostName.c_str());
    }
  }

  HTTPClient http;
  http.setTimeout(30000); 
  http.setUserAgent("ESP32-OTA-Remote");
  http.setReuse(false);   

  showMessage("Downloading", "firmware...");
  
  if (!http.begin(downloadClient, finalDownloadUrl)) {
    showMessage("Update Error", "Bad Target URL");
    delay(2000);
    return false;
  }
  
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    showMessage("Update Error", "HTTP Code: " + String(httpCode));
    http.end();
    delay(3000);
    return false;
  }
  
  int contentLength = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  size_t updateSize = (contentLength > 0) ? contentLength : UPDATE_SIZE_UNKNOWN;
  
  if (!Update.begin(updateSize)) {
    http.end();
    showMessage("Update Error", "No space");
    display.println("Err Code: " + String(Update.getError()));
    display.display();
    delay(4000);
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
  if (bleInitialized) {
    showMessage("Stopping BLE...", "Freeing RAM");
    delay(500);
    NimBLEDevice::deinit(true); 
    bleInitialized = false; 
    delay(500);
  }

  if (!ensureWiFiForOta()) {
    showMessage("OTA Error", "No WiFi", "Rebooting remote");
    delay(2000);
    ESP.restart(); 
    return false;
  }
  
  String binUrl = "";
  String manifestError = "";
  String remoteBuildId = fetchRemoteBuildId(binUrl, manifestError);
  if (remoteBuildId.length() == 0 || binUrl.length() == 0) {
    if (manifestError.length() == 0) manifestError = "Bad manifest";
    showMessage("OTA Error", manifestError, "Rebooting...");
    delay(2000);
    ESP.restart(); 
    return false;
  }
  
  showMessage("Current:", CURRENT_BUILD_ID, "Remote:", remoteBuildId);
  delay(1500);
  if (!isRemoteBuildNewer(remoteBuildId)) {
    showMessage("Device", "Up To Date", "Rebooting...");
    delay(2000);
    ESP.restart(); 
    return false;
  }
  
  showMessage("Update Found", remoteBuildId);
  delay(1200);
  bool result = performFirmwareUpdate(binUrl);
  if (!result) {
    showMessage("Update Failed", "Rebooting device");
    delay(2000);
    ESP.restart();
  }
  return result;
}