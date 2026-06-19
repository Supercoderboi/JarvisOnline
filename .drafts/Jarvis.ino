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
#include "driver/dac.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <FS.h>
#include <SPIFFS.h>

#ifndef FW_BUILD_ID
#define FW_BUILD_ID "dev"
#endif
#define VERSION "1.8.0-FULL"

// --- Joystick pins ---
#define JOY_X 34
#define JOY_Y 35
#define JOY_BTN 33

// --- Touch sensor + Mic pins for ESP32 WROOM-32C ---
#define TOUCH_PIN 27 // External touch sensor. HIGH when touched. Change to LOW if active-low
#define MIC_AO_PIN 32 // Mic analog out → GPIO32
#define SAMPLE_RATE 8000

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
enum MenuState { MENU_MAIN, MENU_BT, MENU_OTA, MENU_WIFI_RESET, MENU_AI };
MenuState menuState = MENU_MAIN;
int menuIndex = 0;
const char* mainMenuItems[] = {"BT Remote", "AI Voice", "OTA Update", "Reset WiFi"};
const int menuCount = 4;

unsigned long lastJoyRead = 0;
unsigned long lastBtnPress = 0;
int lastBtnState = HIGH;
unsigned long btnDownTime = 0;
bool isPaused = true;
bool aiWifiConnected = false;

// --- Voice recording vars ---
const char* RECORDING_FILE_PATH = "/recording.pcm";
const size_t SAMPLE_BUFFER_SAMPLES = 512;
const size_t UPLOAD_CHUNK_BYTES = 2048;
int16_t sampleBuffer[SAMPLE_BUFFER_SAMPLES];
uint8_t uploadBuffer[UPLOAD_CHUNK_BYTES];

bool isRecording = false;
unsigned long nextSampleTime = 0;
size_t sampleBufferCount = 0;
size_t totalRecordedBytes = 0;
int uploadChunkIndex = 0;
String activeSessionId;
File recordingFile;

// Server config
const char* host = "jarvisEp.pythonanywhere.com";
const char* chunkServerPath = "/voice-command/chunk";
const uint16_t serverPort = 80;

// --- OTA stuff ---
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
void dacTestTone();
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
void updateAIDisplay();
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

bool beginRecording();
bool flushSampleBufferToFile();
void finishRecordingAndUpload();
String uploadRecordingFile();
String sendChunkToServer(const uint8_t* chunkData, size_t chunkSize, bool isFinalChunk, bool resetSession);
String readHttpResponseBody(WiFiClient& client);
String extractJsonField(const String& json, const char* key);
String decodeJsonString(const String& encoded);
void scrollTextOnLCD(String text);

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 1);
  Serial.begin(115200);
  delay(500);

  initializeDisplay();
  updateDisplay("JARVIS REMOTE", "Booting...", VERSION);

  pinMode(JOY_BTN, INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT);
  analogSetWidth(12);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed.");
    updateDisplay("SPIFFS FAIL", "Rebooting...");
    delay(2000);
    ESP.restart();
  }

  a2dp_sink.set_on_connection_state_changed(onBTConnected);

  static const i2s_config_t i2s_config = {
   .mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
   .sample_rate = 44100,
   .bits_per_sample = (i2s_bits_per_sample_t) 16,
   .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
   .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_MSB,
   .intr_alloc_flags = 0,
   .dma_buf_count = 8,
   .dma_buf_len = 64,
   .use_apll = false,
   .tx_desc_auto_clear = true
  };
  a2dp_sink.set_i2s_config(i2s_config);

  dacTestTone();
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

  // 5s hold = reset WiFi
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

  // --- HOLD-TO-RECORD LOGIC - ONLY IN AI MENU ---
  if (menuState == MENU_AI && aiWifiConnected) {
    bool touchActive = digitalRead(TOUCH_PIN) == HIGH; // Change to LOW if sensor active-low

    if (touchActive) {
      if (!isRecording) {
        if (beginRecording()) {
          Serial.println("Touch pressed. Recording...");
          updateAIDisplay();
        }
      }
    } else {
      if (isRecording) {
        Serial.println("Touch released. Stopping...");
        finishRecordingAndUpload();
        updateAIDisplay();
      }
    }

    // Sample audio while recording
    if (isRecording) {
      const unsigned long interval = 1000000UL / SAMPLE_RATE;
      if (micros() >= nextSampleTime) {
        int analogVal = analogRead(MIC_AO_PIN);
        int16_t sample = (analogVal - 2048) << 4;
        sampleBuffer[sampleBufferCount++] = sample;
        totalRecordedBytes += sizeof(int16_t);
        nextSampleTime += interval;

        if (sampleBufferCount >= SAMPLE_BUFFER_SAMPLES) {
          if (!flushSampleBufferToFile()) {
            isRecording = false;
            return;
          }
        }
      }
    }
  }
}

// --- AI MENU DISPLAY ---
void updateAIDisplay() {
  if (!aiWifiConnected) {
    updateDisplay("AI Voice", "WiFi: Connecting", "Hold touch to talk");
    return;
  }
  if (isRecording) {
    updateDisplay("AI Voice", "Recording...", "Release to send");
  } else {
    updateDisplay("AI Voice", "WiFi: OK", "Hold touch to talk", "Hold BT 1s: Back");
  }
}

// --- SCROLL TEXT ON LCD ---
void scrollTextOnLCD(String text) {
  const int charsPerLine = 14;
  int len = text.length();

  for (int i = 0; i < len; i += charsPerLine) {
    display.clearDisplay();
    display.setCursor(0, 0);

    String line1 = text.substring(i, min(i + charsPerLine, len));
    display.println(line1);

    if (i + charsPerLine < len) {
      String line2 = text.substring(i + charsPerLine, min(i + charsPerLine*2, len));
      display.println(line2);
    }

    display.display();
    delay(2000); // 2s per screen. Change for speed

    // Exit scroll if user touches sensor again
    if (digitalRead(TOUCH_PIN) == HIGH) {
      delay(500);
      break;
    }
  }
}

// --- VOICE FUNCTIONS ---
bool beginRecording() {
  if (recordingFile) recordingFile.close();
  SPIFFS.remove(RECORDING_FILE_PATH);
  recordingFile = SPIFFS.open(RECORDING_FILE_PATH, FILE_WRITE);
  if (!recordingFile) {
    Serial.println("Could not open SPIFFS recording file.");
    updateDisplay("Error", "SPIFFS fail");
    delay(1500);
    return false;
  }
  isRecording = true;
  sampleBufferCount = 0;
  totalRecordedBytes = 0;
  uploadChunkIndex = 0;
  activeSessionId = String((uint32_t)millis()) + "-" + String((uint32_t)esp_random(), HEX);
  nextSampleTime = micros();
  return true;
}

bool flushSampleBufferToFile() {
  if (!recordingFile || sampleBufferCount == 0) return true;
  size_t bytesToWrite = sampleBufferCount * sizeof(int16_t);
  size_t written = recordingFile.write((const uint8_t*)sampleBuffer, bytesToWrite);
  sampleBufferCount = 0;
  if (written!= bytesToWrite) {
    Serial.println("SPIFFS write failed.");
    recordingFile.close();
    isRecording = false;
    updateDisplay("Error", "Write fail");
    delay(1500);
    return false;
  }
  return true;
}

void finishRecordingAndUpload() {
  if (!flushSampleBufferToFile()) return;
  if (recordingFile) recordingFile.close();
  isRecording = false;

  if (totalRecordedBytes == 0) {
    Serial.println("No audio captured.");
    updateDisplay("No audio", "Try again");
    delay(1500);
    return;
  }

  updateDisplay("Uploading...", String(totalRecordedBytes/1024) + "KB");

  if (WiFi.status()!= WL_CONNECTED) {
    updateDisplay("Upload fail", "WiFi lost", "Reconnecting...");
    delay(2000);
    aiWifiConnected = connectToWiFi(10000);
    if (!aiWifiConnected) {
      updateDisplay("Upload fail", "No WiFi");
      delay(1500);
      return;
    }
  }

  String serverResponse = uploadRecordingFile();
  if (serverResponse.length() == 0) {
    updateDisplay("Upload fail", "Server error");
    delay(1500);
    return;
  }

  // Extract response text from JSON
  String aiReply = extractJsonField(serverResponse, "response");
  if (aiReply.length() == 0) aiReply = serverResponse;

  updateDisplay("AI Reply:", aiReply.substring(0,14));
  delay(1500);

  scrollTextOnLCD("Jarvis: " + aiReply);
  updateAIDisplay();
}

String uploadRecordingFile() {
  File pcmFile = SPIFFS.open(RECORDING_FILE_PATH, FILE_READ);
  if (!pcmFile) {
    Serial.println("Can't open file for upload");
    return "";
  }

  size_t totalBytes = pcmFile.size();
  bool resetSession = true;
  String finalResponse;

  while (pcmFile.available()) {
    size_t bytesRead = pcmFile.read(uploadBuffer, UPLOAD_CHUNK_BYTES);
    bool isFinalChunk = pcmFile.position() >= totalBytes;

    Serial.printf("Chunk %d: %u bytes %s\n", uploadChunkIndex, (unsigned int)bytesRead, isFinalChunk? "FINAL" : "");
    finalResponse = sendChunkToServer(uploadBuffer, bytesRead, isFinalChunk, resetSession);

    if (finalResponse.length() == 0) {
      Serial.println("Server returned empty response");
      pcmFile.close();
      return "";
    }
    resetSession = false;
    uploadChunkIndex++;
  }

  pcmFile.close();
  SPIFFS.remove(RECORDING_FILE_PATH);
  Serial.println("Server reply: " + finalResponse);
  return finalResponse;
}

String sendChunkToServer(const uint8_t* chunkData, size_t chunkSize, bool isFinalChunk, bool resetSession) {
  WiFiClient client;
  if (!client.connect(host, serverPort)) {
    Serial.println("TCP connect failed");
    return "";
  }

  String boundary = "----ESP32Boundary";
  String headerPart =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"session_id\"\r\n\r\n" + activeSessionId + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"chunk_index\"\r\n\r\n" + String(uploadChunkIndex) + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"sample_rate\"\r\n\r\n" + String(SAMPLE_RATE) + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"final\"\r\n\r\n" + String(isFinalChunk? "1" : "0") + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"reset\"\r\n\r\n" + String(resetSession? "1" : "0") + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"chunk\"; filename=\"audio.pcm\"\r\n"
    "Content-Type: application/octet-stream\r\n\r\n";

  String footerPart = "\r\n--" + boundary + "--\r\n";
  size_t contentLength = headerPart.length() + chunkSize + footerPart.length();

  client.printf("POST %s HTTP/1.1\r\n", chunkServerPath);
  client.printf("Host: %s\r\n", host);
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.printf("Content-Length: %u\r\n", (unsigned int)contentLength);
  client.println("Connection: close");
  client.println();

  client.print(headerPart);
  if (chunkSize > 0) client.write(chunkData, chunkSize);
  client.print(footerPart);

  unsigned long responseDeadline = millis() + 20000;
  while (!client.available() && client.connected() && millis() < responseDeadline) delay(10);
  if (!client.available()) {
    Serial.println("No response from server");
    client.stop();
    return "";
  }

  String statusLine = client.readStringUntil('\n');
  Serial.println("Status: " + statusLine);

  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
  }
  String body = readHttpResponseBody(client);
  client.stop();
  return body;
}

String readHttpResponseBody(WiFiClient& client) {
  String body;
  unsigned long deadline = millis() + 5000;
  while ((client.connected() || client.available()) && millis() < deadline) {
    while (client.available()) body += (char)client.read();
    delay(5);
  }
  body.trim();
  return body;
}

String extractJsonField(const String& json, const char* key) {
  String pattern = "\"" + String(key) + "\":";
  int keyPos = json.indexOf(pattern);
  if (keyPos < 0) return "";
  int quoteStart = json.indexOf('"', keyPos + pattern.length());
  if (quoteStart < 0) return "";
  int i = quoteStart + 1;
  while (i < json.length()) {
    char c = json.charAt(i);
    if (c == '"' && json.charAt(i - 1)!= '\\') break;
    i++;
  }
  if (i >= json.length()) return "";
  return decodeJsonString(json.substring(quoteStart + 1, i));
}

String decodeJsonString(const String& encoded) {
  String decoded;
  decoded.reserve(encoded.length());
  for (int i = 0; i < encoded.length(); i++) {
    char c = encoded.charAt(i);
    if (c == '\\' && i + 1 < encoded.length()) {
      char next = encoded.charAt(i + 1);
      if (next == 'n' || next == 't') decoded += ' ';
      else if (next == 'r') {}
      else decoded += next;
      i++;
    } else decoded += c;
  }
  decoded.trim();
  return decoded;
}

// --- MENU LOGIC ---
void drawMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(">> MAIN MENU <<");
  for (int i = 0; i < menuCount; i++) {
    if (i == menuIndex) display.print("> ");
    else display.print(" ");
    display.println(mainMenuItems[i]);
  }
  if (menuState == MENU_BT && btConnected) display.println(btDeviceName.substring(0,14));
  else if (menuState == MENU_BT) display.println("Waiting BT...");
  display.display();
}

void handleMenu() {
  if (menuState!= MENU_MAIN) return;
  if (millis() - lastJoyRead < 200) return;
  int xVal = analogRead(JOY_X);
  int btnVal = digitalRead(JOY_BTN);

  if (xVal < 1000) { menuIndex--; if (menuIndex < 0) menuIndex = menuCount - 1; lastJoyRead = millis(); drawMenu(); }
  else if (xVal > 3000) { menuIndex++; if (menuIndex >= menuCount) menuIndex = 0; lastJoyRead = millis(); drawMenu(); }

  if (btnVal == LOW && lastBtnState == HIGH && millis() - lastBtnPress > 300) {
    lastBtnPress = millis();
    lastBtnState = btnVal;

    if (menuIndex == 0) {
      menuState = MENU_BT;
      String s,p;
      if (loadWiFiCreds(s,p)) connectToWiFi(5000);
      a2dp_sink.start("ESP32-Jack");
      updateBTDisplay();
    }
    else if (menuIndex == 1) {
      menuState = MENU_AI;
      updateDisplay("AI Voice", "WiFi: Loading...");

      String s,p;
      if (!loadWiFiCreds(s,p)) {
        startConfigPortal();
      } else {
        aiWifiConnected = connectToWiFi(10000);
        if (!aiWifiConnected) {
          updateDisplay("AI Voice", "WiFi Failed", "Check credentials");
          delay(2000);
          menuState = MENU_MAIN;
          drawMenu();
        } else {
          updateAIDisplay();
        }
      }
    }
    else if (menuIndex == 2) {
      menuState = MENU_OTA;
      String s,p;
      if (!loadWiFiCreds(s,p)) startConfigPortal();
      else if (!connectToWiFi(10000)) startConfigPortal();
      openOtaMode();
    }
    else if (menuIndex == 3) {
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

void handleJoystick() {
  if (menuState == MENU_BT) {
    if (millis() - lastJoyRead < 150) return;
    lastJoyRead = millis();
    int xVal = analogRead(JOY_X);
    int yVal = analogRead(JOY_Y);
    int btnVal = digitalRead(JOY_BTN);

    if (btnVal == LOW && lastBtnState == HIGH && millis() - lastBtnPress > 300) {
      if (millis() - btnDownTime > 1000) {
        a2dp_sink.end();
        menuState = MENU_MAIN;
        menuIndex = 0;
        drawMenu();
      } else {
        if (isPaused) { a2dp_sink.play(); isPaused = false; }
        else { a2dp_sink.pause(); isPaused = true; }
        updateBTDisplay();
      }
      lastBtnPress = millis();
    }
    lastBtnState = btnVal;

    if (btConnected) {
      if (yVal < 1000) { a2dp_sink.volume_up(); updateBTDisplay(); delay(150); }
      else if (yVal > 3000) { a2dp_sink.volume_down(); updateBTDisplay(); delay(150); }
      if (xVal < 1000) { a2dp_sink.previous(); updateBTDisplay(); delay(250); }
      else if (xVal > 3000) { a2dp_sink.next(); updateBTDisplay(); delay(250); }
    }
  }

  if (menuState == MENU_AI) {
    int btnVal = digitalRead(JOY_BTN);
    if (btnVal == LOW) {
      if (btnDownTime == 0) btnDownTime = millis();
      if (millis() - btnDownTime > 1000) {
        btnDownTime = 0;
        menuState = MENU_MAIN;
        aiWifiConnected = false;
        drawMenu();
      }
    } else {
      btnDownTime = 0;
    }
  }
}

void updateBTDisplay() {
  String l1 = btConnected? "BT Connected" : "Pairing...";
  String l2 = btConnected? btDeviceName.substring(0,14) : "ESP32-Jack";
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
  while (WiFi.status()!= WL_CONNECTED && millis() - start < timeoutMs) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

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
    if (!http.begin(client, manifestUrl)) { errorMessage = "begin() failed"; continue; }
    int httpCode = http.GET();
    if (httpCode!= HTTP_CODE_OK) { errorMessage = httpStatusText(http, httpCode); http.end(); continue; }
    String payload = http.getString();
    http.end();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) { errorMessage = String("JSON ") + error.c_str(); continue; }
    binUrl = doc["bin_url"] | "";
    String buildId = doc["build_id"] | "";
    if (buildId.length() == 0 || binUrl.length() == 0) { errorMessage = "Missing fields"; continue; }
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
  if (!http.begin(client, binUrl)) { showOtaMessage("Update Error", "Bad BIN URL"); delay(2000); return false; }
  int httpCode = http.GET();
  if (httpCode!= HTTP_CODE_OK) { String status = httpStatusText(http, httpCode); http.end(); showOtaMessage("Update Error", status); delay(2000); return false; }
  int contentLength = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  if (!Update.begin(contentLength > 0? contentLength : UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial); http.end(); showOtaMessage("Update Error", "No space"); delay(2000); return false; }
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
        if (Update.write(buffer, readLen)!= (size_t)readLen) { Update.printError(Serial); Update.abort(); streamFailed = true; break; }
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
      if (millis() - lastDataAt > 15000) { Update.abort(); streamFailed = true; break; }
      delay(1);
    }
  }
  bool success =!streamFailed && Update.end(true);
  http.end();
  if (success && Update.isFinished()) { showOtaMessage("Update Done!", "Rebooting..."); delay(1500); ESP.restart(); return true; }
  showOtaMessage("Update Error", "Finalize fail");
  delay(2000);
  return false;
}

bool checkForGitHubUpdate() {
  if (!ensureWiFiForOta()) { showOtaMessage("OTA Error", "No WiFi"); delay(2000); return false; }
  String binUrl = "";
  String manifestError = "";
  String remoteBuildId = fetchRemoteBuildId(binUrl, manifestError);
  if (remoteBuildId.length() == 0 || binUrl.length() == 0) {
    if (manifestError.length() == 0) manifestError = "Bad manifest";
    showOtaMessage("OTA Error", manifestError);
    delay(2000);
    return false;
  }
  if (!isRemoteBuildNewer(remoteBuildId)) { showOtaMessage("Device Software", "Up To Date"); delay(2000); return false; }
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

void dacTestTone() {
  updateDisplay("DAC TEST", "Listen...");
  dac_output_enable(DAC_CHANNEL_1);
  for(int i=0; i<255; i++) { dac_output_voltage(DAC_CHANNEL_1, i); delayMicroseconds(1000); }
  for(int i=255; i>=0; i--) { dac_output_voltage(DAC_CHANNEL_1, i); delayMicroseconds(1000); }
  dac_output_voltage(DAC_CHANNEL_1, 128);
  updateDisplay("DAC TEST", "Done");
  delay(1000);
}