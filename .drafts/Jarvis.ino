#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <BluetoothA2DPSink.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#ifndef FW_BUILD_ID
#define FW_BUILD_ID "dev"
#endif
#define VERSION "1.5.1"

// --- Joystick pins ---
#define JOY_X 34
#define JOY_Y 35
#define JOY_BTN 33 // Add 10k pullup to 3.3V

// --- Nokia 5110 ---
#define NOKIA_CLK 18
#define NOKIA_DIN 19
#define NOKIA_DC 21
#define NOKIA_CE 5
#define NOKIA_RST 15

Adafruit_PCD8544 display = Adafruit_PCD8544(NOKIA_CLK, NOKIA_DIN, NOKIA_DC, NOKIA_CE, NOKIA_RST);
Preferences prefs;

// --- Bluetooth ---
BluetoothA2DPSink a2dp_sink;
bool btConnected = false;
String btDeviceName = "None";

// --- Web + OTA ---
WebServer server(80);
WebServer configServer(80);
bool otaModeActive = false;
bool otaServerRunning = false;
bool configMode = false;

// --- Menu ---
enum MenuState { MENU_MAIN, MENU_BT, MENU_OTA, MENU_WIFI_RESET };
MenuState menuState = MENU_MAIN;
int menuIndex = 0;
const char* mainMenuItems[] = {"BT Remote", "OTA Update", "Reset WiFi"};
const int menuCount = 3;

unsigned long lastJoyRead = 0;
unsigned long lastBtnPress = 0;
int lastBtnState = HIGH;
unsigned long btnDownTime = 0;
bool isPaused = true; // track play/pause state

// --- OTA stuff from mic code ---
const char* CURRENT_BUILD_ID = FW_BUILD_ID;
const char* UPDATE_MANIFEST_URLS[] = {
  "https://jarvisupload.netlify.app/firmware/manifest.json",
  "https://raw.githubusercontent.com/Supercoderboi/JarvisOnline/master/firmware/manifest.json"
};
const size_t UPDATE_MANIFEST_URL_COUNT = sizeof(UPDATE_MANIFEST_URLS) / sizeof(UPDATE_MANIFEST_URLS[0]);

const char* serverIndex =
  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
  "<h2>Jarvis System Update</h2>"
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='update' accept='.bin'><br>"
  "<input type='submit' value='Update Firmware'></form>";

const char* wifiConfigPage =
  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
  "<h2>Jarvis WiFi Setup</h2>"
  "<form method='POST' action='/save'>"
  "SSID: <input name='ssid' length=32><br><br>"
  "Password: <input name='pass' type='password' length=64><br><br>"
  "<input type='submit' value='Save & Reboot'></form>";

// Prototypes
void saveWiFiCreds(String ssid, String pass);
bool loadWiFiCreds(String &ssid, String &pass);
void startConfigPortal();
bool connectToWiFi(unsigned long timeoutMs);
void initializeDisplay();
void updateDisplay(const String& l1, const String& l2="", const String& l3="", const String& l4="", const String& l5="", const String& l6="");
void drawMenu();
void handleMenu();
void handleJoystick();
void updateBTDisplay();
void onBTConnected(esp_a2d_connection_state_t state, void* ptr);
void openOtaMode();
void runOtaMode();
void showOtaMessage(const String& l1, const String& l2="", const String& l3="");
String httpStatusText(HTTPClient& http, int httpCode);
bool ensureWiFiForOta();
String fetchRemoteBuildId(String& binUrl, String& errorMessage);
bool isRemoteBuildNewer(const String& remoteBuildId);
bool performFirmwareUpdate(const String& binUrl);
bool checkForGitHubUpdate();
void startManualOtaServer();

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

  initializeDisplay();
  updateDisplay("BT REMOTE", "Booting...", VERSION);

  pinMode(JOY_BTN, INPUT_PULLUP);

  // Bluetooth callbacks - v1.8.11 only has connection callback
  a2dp_sink.set_on_connection_state_changed(onBTConnected);

  drawMenu();
}

void loop() {
  if (configMode) {
    configServer.handleClient();
    updateDisplay("AP Mode", "SSID: Jarvis-Setup", "IP: 192.168.4.1");
    delay(10);
    return;
  }

  if (otaModeActive) {
    runOtaMode();
    delay(10);
    return;
  }

  // 5s hold = reset WiFi from any menu
  int btnVal = digitalRead(JOY_BTN);
  if (btnVal == LOW) {
    if (btnDownTime == 0) btnDownTime = millis();
    if (millis() - btnDownTime > 5000) {
      prefs.begin("wifi-creds", false);
      prefs.clear();
      prefs.end();
      updateDisplay("WiFi Cleared", "Rebooting...");
      delay(1500);
      ESP.restart();
    }
  } else {
    btnDownTime = 0;
  }

  handleJoystick();
  handleMenu();
}

// --- Menu Logic ---
void drawMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(">> MAIN MENU <<");

  for (int i = 0; i < menuCount; i++) {
    if (i == menuIndex) display.print("> ");
    else display.print(" ");
    display.println(mainMenuItems[i]);
  }

  if (menuState == MENU_BT && btConnected) {
    display.println(btDeviceName.substring(0,14));
  } else if (menuState == MENU_BT) {
    display.println("Waiting BT...");
  }

  display.display();
}

void handleMenu() {
  if (menuState!= MENU_MAIN) return;
  if (millis() - lastJoyRead < 200) return;

  int xVal = analogRead(JOY_X);
  int btnVal = digitalRead(JOY_BTN);

  // Left/Right to move cursor
  if (xVal < 1000) {
    menuIndex--;
    if (menuIndex < 0) menuIndex = menuCount - 1;
    lastJoyRead = millis();
    drawMenu();
  } else if (xVal > 3000) {
    menuIndex++;
    if (menuIndex >= menuCount) menuIndex = 0;
    lastJoyRead = millis();
    drawMenu();
  }

  // Button press = select
  if (btnVal == LOW && lastBtnState == HIGH && millis() - lastBtnPress > 300) {
    lastBtnPress = millis();
    lastBtnState = btnVal;

    if (menuIndex == 0) { // BT Remote
      menuState = MENU_BT;
      String s,p;
      if (loadWiFiCreds(s,p)) connectToWiFi(5000);
      a2dp_sink.start("ESP32-Remote");
      updateDisplay("BT Remote", "Pair: ESP32-Remote");
    }
    else if (menuIndex == 1) { // OTA Update
      menuState = MENU_OTA;
      String s,p;
      if (!loadWiFiCreds(s,p)) startConfigPortal();
      else if (!connectToWiFi(10000)) startConfigPortal();
      openOtaMode();
    }
    else if (menuIndex == 2) { // Reset WiFi
      prefs.begin("wifi-creds", false);
      prefs.clear();
      prefs.end();
      updateDisplay("WiFi Reset", "Rebooting...");
      delay(1500);
      ESP.restart();
    }
  }
  if (btnVal == HIGH) lastBtnState = HIGH;
}

// --- BT Remote Logic ---
void handleJoystick() {
  if (menuState!= MENU_BT ||!btConnected) return;
  if (millis() - lastJoyRead < 150) return;
  lastJoyRead = millis();

  int xVal = analogRead(JOY_X);
  int yVal = analogRead(JOY_Y);
  int btnVal = digitalRead(JOY_BTN);

  // Button = Play/Pause toggle, Long press 1s = back to menu
  if (btnVal == LOW && lastBtnState == HIGH && millis() - lastBtnPress > 300) {
    if (millis() - btnDownTime > 1000) {
      a2dp_sink.end();
      menuState = MENU_MAIN;
      menuIndex = 0;
      drawMenu();
    } else {
      if (isPaused) {
        a2dp_sink.play();
        isPaused = false;
      } else {
        a2dp_sink.pause();
        isPaused = true;
      }
      updateBTDisplay();
    }
    lastBtnPress = millis();
  }
  lastBtnState = btnVal;

  // Y = Volume
  if (yVal < 1000) {
    a2dp_sink.volume_up();
    updateBTDisplay();
    delay(150);
  } else if (yVal > 3000) {
    a2dp_sink.volume_down();
    updateBTDisplay();
    delay(150);
  }

  // X = Next/Prev
  if (xVal < 1000) {
    a2dp_sink.previous();
    updateBTDisplay();
    delay(250);
  } else if (xVal > 3000) {
    a2dp_sink.next();
    updateBTDisplay();
    delay(250);
  }
}

void updateBTDisplay() {
  String l1 = btConnected? "BT Connected" : "Pairing...";
  String l2 = btDeviceName.substring(0,14);
  String l3 = isPaused? "Btn: Play" : "Btn: Pause";
  String l4 = "Up/Down: Vol";
  String l5 = "L/R: Next/Prev";
  String l6 = "Hold 1s: Back";
  updateDisplay(l1, l2, l3, l4, l5, l6);
}

void onBTConnected(esp_a2d_connection_state_t state, void* ptr) {
  btConnected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
  btDeviceName = btConnected? a2dp_sink.get_peer_name() : "None";
  if (btConnected) isPaused = false;
  if (menuState == MENU_BT) updateBTDisplay();
}

// --- WiFi NVS ---
void saveWiFiCreds(String ssid, String pass) {
  prefs.begin("wifi-creds", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

bool loadWiFiCreds(String &ssid, String &pass) {
  prefs.begin("wifi-creds", true);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  prefs.end();
  return ssid.length() > 0;
}

void startConfigPortal() {
  configMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Jarvis-Setup", "12345678");
  configServer.on("/", []() { configServer.send(200, "text/html", wifiConfigPage); });
  configServer.on("/save", HTTP_POST, []() {
    saveWiFiCreds(configServer.arg("ssid"), configServer.arg("pass"));
    configServer.send(200, "text/html", "Saved! Rebooting...");
    delay(2000);
    ESP.restart();
  });
  configServer.begin();
}

bool connectToWiFi(unsigned long timeoutMs) {
  String ssid, pass;
  if(!loadWiFiCreds(ssid, pass)) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  while (WiFi.status()!= WL_CONNECTED && millis() - start < timeoutMs) delay(500);
  return WiFi.status() == WL_CONNECTED;
}

// --- Display ---
void initializeDisplay() {
  display.begin();
  display.setContrast(58);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.display();
}

void updateDisplay(const String& l1, const String& l2, const String& l3, const String& l4, const String& l5, const String& l6) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(l1);
  display.println(l2);
  display.println(l3);
  display.println(l4);
  display.println(l5);
  display.println(l6);
  display.display();
}

// --- OTA FUNCTIONS FROM MIC CODE ---
void openOtaMode() {
  otaModeActive = true;
  showOtaMessage("Opening...", "Update mode");
}

void runOtaMode() {
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    bool updateApplied = checkForGitHubUpdate();
    if (!updateApplied ||!otaServerRunning) {
      startManualOtaServer();
      showOtaMessage("OTA MODE ON", "IP:", WiFi.localIP().toString());
    }
  }
  if (otaServerRunning) server.handleClient();
}

void showOtaMessage(const String& line1, const String& line2, const String& line3) {
  updateDisplay(line1, line2, line3);
}

String httpStatusText(HTTPClient& http, int httpCode) {
  if (httpCode >= 0) return "HTTP " + String(httpCode);
  return http.errorToString(httpCode);
}

bool ensureWiFiForOta() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect(false, false);
  WiFi.reconnect();
  unsigned long reconnectStarted = millis();
  while (WiFi.status()!= WL_CONNECTED && millis() - reconnectStarted < 10000) delay(250);
  return WiFi.status() == WL_CONNECTED;
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
    if (httpCode!= HTTP_CODE_OK) {
      errorMessage = httpStatusText(http, httpCode);
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

    errorMessage = "";
    return buildId;
  }
  return "";
}

bool isRemoteBuildNewer(const String& remoteBuildId) {
  return remoteBuildId.length() > 0 && remoteBuildId!= String(CURRENT_BUILD_ID);
}

bool performFirmwareUpdate(const String& binUrl) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);

  showOtaMessage("Downloading", "firmware...");

  if (!http.begin(client, binUrl)) {
    showOtaMessage("Update Error", "Bad BIN URL");
    delay(2000);
    return false;
  }

  int httpCode = http.GET();
  if (httpCode!= HTTP_CODE_OK) {
    String status = httpStatusText(http, httpCode);
    http.end();
    showOtaMessage("Update Error", status);
    delay(2000);
    return false;
  }

  int contentLength = http.getSize();
  WiFiClient* stream = http.getStreamPtr();

  if (!Update.begin(contentLength > 0? contentLength : UPDATE_SIZE_UNKNOWN)) {
    Update.printError(Serial);
    http.end();
    showOtaMessage("Update Error", "No space");
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
        if (Update.write(buffer, readLen)!= (size_t)readLen) {
          Update.printError(Serial);
          Update.abort();
          streamFailed = true;
          break;
        }
        written += readLen;
        lastDataAt = millis();

        if (contentLength > 0) {
          int percent = (written * 100) / contentLength;
          showOtaMessage("Updating...", String(percent) + "%");
          if (written >= contentLength) break;
        } else {
          showOtaMessage("Updating...", String(written / 1024) + " KB");
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

  bool success =!streamFailed && Update.end(true);
  http.end();

  if (success && Update.isFinished()) {
    showOtaMessage("Update Done!", "Rebooting...");
    delay(1500);
    ESP.restart();
    return true;
  }

  showOtaMessage("Update Error", "Finalize fail");
  delay(2000);
  return false;
}

bool checkForGitHubUpdate() {
  if (!ensureWiFiForOta()) {
    showOtaMessage("OTA Error", "No WiFi");
    delay(2000);
    return false;
  }

  String binUrl = "";
  String manifestError = "";
  String remoteBuildId = fetchRemoteBuildId(binUrl, manifestError);

  if (remoteBuildId.length() == 0 || binUrl.length() == 0) {
    if (manifestError.length() == 0) manifestError = "Bad manifest";
    showOtaMessage("OTA Error", manifestError);
    delay(2000);
    return false;
  }

  if (!isRemoteBuildNewer(remoteBuildId)) {
    showOtaMessage("Device Software", "Up To Date");
    delay(2000);
    return false;
  }

  showOtaMessage("Update Found", remoteBuildId);
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
    server.send(200, "text/plain", Update.hasError()? "UPDATE FAILED! Rebooting..." : "SUCCESS! Restarting Jarvis...");
    delay(2000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      showOtaMessage("Receiving...", "Manual upload");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize)!= upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) showOtaMessage("DONE!", "Rebooting...");
    }
  });

  server.begin();
  otaServerRunning = true;
}