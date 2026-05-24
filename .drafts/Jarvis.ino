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
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#ifndef FW_BUILD_ID
#define FW_BUILD_ID "dev"
#endif

#define VERSION "  1.3.1  "

// --- Wi-Fi Configuration ---
const char* ssid = "Ethria2.4";
const char* password = "PalmDale007";

// --- Server Configuration ---
const char* host = "jarvisEp.pythonanywhere.com";
const char* chunkServerPath = "/voice-command/chunk";
const uint16_t serverPort = 80;

// --- Audio Hardware Pin Layout ---
const int AO_PIN = 34;
const int DO_PIN = 12;
const int SAMPLE_RATE = 8000;
const unsigned long SILENCE_TIMEOUT_MS = 1500;

// --- Nokia 5110 Wiring ---
#define NOKIA_CLK 18
#define NOKIA_DIN 19
#define NOKIA_DC 21
#define NOKIA_CE 5
#define NOKIA_RST 15

Adafruit_PCD8544 display = Adafruit_PCD8544(
  NOKIA_CLK,
  NOKIA_DIN,
  NOKIA_DC,
  NOKIA_CE,
  NOKIA_RST
);
WebServer server(80);

// --- Local Recording Buffers ---
const char* RECORDING_FILE_PATH = "/recording.pcm";
const size_t SAMPLE_BUFFER_SAMPLES = 512;
const size_t UPLOAD_CHUNK_BYTES = 2048;
int16_t sampleBuffer[SAMPLE_BUFFER_SAMPLES];
uint8_t uploadBuffer[UPLOAD_CHUNK_BYTES];

bool isRecording = false;
unsigned long lastSoundTime = 0;
unsigned long nextSampleTime = 0;
size_t sampleBufferCount = 0;
size_t totalRecordedBytes = 0;
int uploadChunkIndex = 0;
String activeSessionId;
File recordingFile;
bool otaModeActive = false;
bool otaServerRunning = false;

// --- Scroll State Variables ---
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

bool connectToWiFi(unsigned long timeoutMs);
bool beginRecording();
bool flushSampleBufferToFile();
void finishRecordingAndUpload();
bool uploadRecordingFile();
String sendChunkToServer(const uint8_t* chunkData, size_t chunkSize, bool isFinalChunk, bool resetSession);
void initializeDisplay();
void updateDisplay(const String& line1, const String& line2 = "", const String& line3 = "", const String& line4 = "", const String& line5 = "", const String& line6 = "");
void startScrollingMessage(const String& title, const String& message);
void handleDisplayScrolling();
String readHttpResponseBody(WiFiClient& client);
String extractJsonField(const String& json, const char* key);
String decodeJsonString(const String& encoded);
String toLowerCaseCopy(String text);
bool isSoftwareUpdateCommand(const String& transcription);
void openOtaMode();
void runOtaMode();
void showOtaMessage(const String& line1, const String& line2 = "", const String& line3 = "");
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
  pinMode(DO_PIN, INPUT);
  analogSetWidth(12);

  initializeDisplay();
  updateDisplay("JARVIS", "Booting...", "Mounting FS");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed.");
    updateDisplay("JARVIS", "SPIFFS fail");
    while (1) {
      delay(100);
    }
  }

  updateDisplay("JARVIS", "Connecting", "to WiFi", "S.H.I.E.L.D", VERSION);
  if (connectToWiFi(20000)) {
    Serial.println("Connected! JARVIS hardware node online.");
    updateDisplay("JARVIS", "WiFi OK", "Awaiting", "voice");
  } else {
    Serial.println("Wi-Fi connection timed out. Retrying in loop.");
    updateDisplay("JARVIS", "WiFi fail", "Retry loop");
  }
}

void loop() {
  if (otaModeActive) {
    runOtaMode();
    delay(10);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt >= 5000) {
      lastReconnectAttempt = millis();
      updateDisplay("JARVIS", "WiFi lost", "Reconnecting");
      connectToWiFi(5000);
    }
  }

  // Handle display scrolling asynchronously without blocking sound detection
  handleDisplayScrolling();

  if (digitalRead(DO_PIN) == HIGH) {
    lastSoundTime = millis();

    if (!isRecording) {
      if (beginRecording()) {
        Serial.println("Sound detected. Recording to SPIFFS...");
      }
    }
  }

  if (!isRecording) {
    return;
  }

  const unsigned long interval = 1000000UL / SAMPLE_RATE;
  if (micros() >= nextSampleTime) {
    int analogVal = analogRead(AO_PIN);
    int16_t sample = (analogVal - 2048) << 4;

    sampleBuffer[sampleBufferCount++] = sample;
    totalRecordedBytes += sizeof(int16_t);
    nextSampleTime += interval;

    if (sampleBufferCount >= SAMPLE_BUFFER_SAMPLES) {
      if (!flushSampleBufferToFile()) {
        return;
      }
    }
  }

  if (millis() - lastSoundTime > SILENCE_TIMEOUT_MS) {
    Serial.println("Speech ended. Uploading saved audio in chunks...");
    finishRecordingAndUpload();
  }
}

bool beginRecording() {
  if (recordingFile) {
    recordingFile.close();
  }

  // Interrupt scrolling visually when a new recording begins
  isScrollingActive = false;

  SPIFFS.remove(RECORDING_FILE_PATH);
  recordingFile = SPIFFS.open(RECORDING_FILE_PATH, FILE_WRITE);
  if (!recordingFile) {
    Serial.println("Could not open SPIFFS recording file.");
    updateDisplay("Record fail", "File open");
    return false;
  }

  isRecording = true;
  sampleBufferCount = 0;
  totalRecordedBytes = 0;
  uploadChunkIndex = 0;
  activeSessionId = String((uint32_t)millis()) + "-" + String((uint32_t)esp_random(), HEX);
  nextSampleTime = micros();
  updateDisplay("Listening...", "Saving", "to flash");
  return true;
}

bool flushSampleBufferToFile() {
  if (!recordingFile || sampleBufferCount == 0) {
    return true;
  }

  size_t bytesToWrite = sampleBufferCount * sizeof(int16_t);
  size_t written = recordingFile.write((const uint8_t*)sampleBuffer, bytesToWrite);
  sampleBufferCount = 0;

  if (written != bytesToWrite) {
    Serial.println("SPIFFS write failed.");
    updateDisplay("Record fail", "Write error");
    recordingFile.close();
    isRecording = false;
    return false;
  }

  return true;
}

void finishRecordingAndUpload() {
  if (!flushSampleBufferToFile()) {
    return;
  }

  if (recordingFile) {
    recordingFile.close();
  }

  isRecording = false;

  if (totalRecordedBytes == 0) {
    Serial.println("No audio captured. Skipping upload.");
    updateDisplay("No audio", "Nothing sent");
    delay(1500);
    updateDisplay("JARVIS", "Awaiting", "voice");
    return;
  }

  updateDisplay("Uploading...", String(totalRecordedBytes) + " bytes");
  if (!uploadRecordingFile()) {
    updateDisplay("Upload fail", "Chunk send", "stopped");
    delay(1500);
    updateDisplay("JARVIS", "Awaiting", "voice");
    return;
  }

  // REMOVED: Immediate override back to "JARVIS Awaiting voice" so text keeps scrolling.
}

bool uploadRecordingFile() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected. Cannot upload.");
    return false;
  }

  File pcmFile = SPIFFS.open(RECORDING_FILE_PATH, FILE_READ);
  if (!pcmFile) {
    Serial.println("Could not reopen recording file for upload.");
    return false;
  }

  size_t totalBytes = pcmFile.size();
  bool resetSession = true;
  String finalResponse;

  while (pcmFile.available()) {
    size_t bytesRead = pcmFile.read(uploadBuffer, UPLOAD_CHUNK_BYTES);
    bool isFinalChunk = pcmFile.position() >= totalBytes;

    Serial.printf("Uploading chunk %d (%u bytes)%s\n", uploadChunkIndex, (unsigned int)bytesRead, isFinalChunk ? " FINAL" : "");
    updateDisplay("Uploading...", "Chunk " + String(uploadChunkIndex), String((unsigned int)bytesRead) + " bytes");

    finalResponse = sendChunkToServer(uploadBuffer, bytesRead, isFinalChunk, resetSession);
    if (finalResponse.length() == 0) {
      pcmFile.close();
      return false;
    }

    resetSession = false;
    uploadChunkIndex++;
  }

  pcmFile.close();
  SPIFFS.remove(RECORDING_FILE_PATH);

  Serial.println("\n--- JARVIS SYSTEM REPLY ---");
  Serial.println(finalResponse);
  Serial.println("----------------------------\n");

  String transcription = extractJsonField(finalResponse, "transcription");
  String reply = extractJsonField(finalResponse, "response");

  if (reply.length() == 0) {
    reply = finalResponse;
  }

  if (isSoftwareUpdateCommand(transcription)) {
    updateDisplay("Heard:", transcription);
    delay(1500);
    openOtaMode();
    return true;
  }

  // Construct a single combined message to scroll smoothly
  String completeOutput = "";
  if (transcription.length() > 0) {
    completeOutput += "Heard: " + transcription + " | ";
  }
  completeOutput += "Jarvis: " + reply;

  // Initialize the non-blocking scroll view
  startScrollingMessage("SYSTEM OUTPUT", completeOutput);
  
  return true;
}

String sendChunkToServer(const uint8_t* chunkData, size_t chunkSize, bool isFinalChunk, bool resetSession) {
  WiFiClient client;
  if (!client.connect(host, serverPort)) {
    Serial.println("Could not connect to PythonAnywhere.");
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
    "Content-Disposition: form-data; name=\"final\"\r\n\r\n" + String(isFinalChunk ? "1" : "0") + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"reset\"\r\n\r\n" + String(resetSession ? "1" : "0") + "\r\n"
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
  if (chunkSize > 0) {
    client.write(chunkData, chunkSize);
  }
  client.print(footerPart);

  unsigned long responseDeadline = millis() + 15000;
  while (!client.available() && client.connected() && millis() < responseDeadline) {
    delay(10);
  }

  if (!client.available()) {
    Serial.println("Chunk upload timed out waiting for response.");
    client.stop();
    return "";
  }

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  Serial.println(statusLine);

  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) {
      break;
    }
  }

  String body = readHttpResponseBody(client);
  client.stop();
  return body;
}

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

// Converts the message into separate formatted lines and resets the background timer variables
void startScrollingMessage(const String& title, const String& message) {
  const int charsPerLine = 14;
  totalScrollLines = 0;
  currentScrollIndex = 0;
  
  // Set the structural first line header
  scrollLines[totalScrollLines++] = title;
  
  int start = 0;
  while (start < message.length() && totalScrollLines < MAX_SCROLL_LINES) {
    int remaining = message.length() - start;
    int take = remaining < charsPerLine ? remaining : charsPerLine;

    if (start + take < message.length()) {
      int split = message.lastIndexOf(' ', start + take - 1);
      if (split >= start) {
        take = split - start;
      }
    }

    if (take <= 0) {
      take = remaining < charsPerLine ? remaining : charsPerLine;
    }

    scrollLines[totalScrollLines] = message.substring(start, start + take);
    scrollLines[totalScrollLines].trim();
    start += take;

    while (start < message.length() && message.charAt(start) == ' ') {
      start++;
    }
    totalScrollLines++;
  }

  isScrollingActive = true;
  lastScrollTime = millis();
  
  // Instantly draw initial parsed frame
  handleDisplayScrolling();
}

// Non-blocking engine executed inside the main processing loop 
void handleDisplayScrolling() {
  if (!isScrollingActive) return;

  // Check if 2 seconds have ticked by
  if (millis() - lastScrollTime >= SCROLL_INTERVAL_MS) {
    lastScrollTime = millis();
    
    // Scroll down by 1 line if there is unread text leftover
    if (currentScrollIndex + 5 < totalScrollLines) {
      currentScrollIndex++;
    } else {
      // Loop text back to the top window view frame when done
      currentScrollIndex = 0;
    }
  }

  // Dynamic composition array generation
  display.clearDisplay();
  display.setCursor(0, 0);
  
  // Draw current snapshot window view layer (up to 6 lines max on PCD8544 display)
  for (int i = 0; i < 6; i++) {
    int lineIndex = currentScrollIndex + i;
    if (lineIndex < totalScrollLines) {
      display.println(scrollLines[lineIndex]);
    }
  }
  display.display();
}

String readHttpResponseBody(WiFiClient& client) {
  String body;
  unsigned long deadline = millis() + 5000;

  while ((client.connected() || client.available()) && millis() < deadline) {
    while (client.available()) {
      body += (char)client.read();
    }
    delay(5);
  }

  body.trim();
  return body;
}

String extractJsonField(const String& json, const char* key) {
  String pattern = "\"" + String(key) + "\":";
  int keyPos = json.indexOf(pattern);
  if (keyPos < 0) {
    return "";
  }

  int quoteStart = json.indexOf('"', keyPos + pattern.length());
  if (quoteStart < 0) {
    return "";
  }

  int i = quoteStart + 1;
  while (i < json.length()) {
    char c = json.charAt(i);
    if (c == '"' && json.charAt(i - 1) != '\\') {
      break;
    }
    i++;
  }

  if (i >= json.length()) {
    return "";
  }

  return decodeJsonString(json.substring(quoteStart + 1, i));
}

String decodeJsonString(const String& encoded) {
  String decoded;
  decoded.reserve(encoded.length());

  for (int i = 0; i < encoded.length(); i++) {
    char c = encoded.charAt(i);
    if (c == '\\' && i + 1 < encoded.length()) {
      char next = encoded.charAt(i + 1);
      switch (next) {
        case 'n':
        case 't':
          decoded += ' ';
          break;
        case 'r':
          break;
        case '"':
        case '\\':
        case '/':
          decoded += next;
          break;
        default:
          decoded += next;
          break;
      }
      i++;
    } else {
      decoded += c;
    }
  }

  decoded.trim();
  return decoded;
}

String toLowerCaseCopy(String text) {
  text.toLowerCase();
  return text;
}

bool isSoftwareUpdateCommand(const String& transcription) {
  String lowered = toLowerCaseCopy(transcription);
  return lowered.indexOf("software update") >= 0 || lowered.indexOf("system update") >= 0;
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
    if (!updateApplied || !otaServerRunning) {
      startManualOtaServer();
      showOtaMessage("OTA MODE ON", "Open browser:", WiFi.localIP().toString());
    }
  }

  if (otaServerRunning) {
    server.handleClient();
  }
}

void showOtaMessage(const String& line1, const String& line2, const String& line3) {
  updateDisplay(line1, line2, line3);
}

String httpStatusText(HTTPClient& http, int httpCode) {
  if (httpCode >= 0) {
    return "HTTP " + String(httpCode);
  }
  return http.errorToString(httpCode);
}

bool connectToWiFi(unsigned long timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeoutMs) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  return WiFi.status() == WL_CONNECTED;
}

bool ensureWiFiForOta() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

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
  return remoteBuildId.length() > 0 && remoteBuildId != String(CURRENT_BUILD_ID);
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
  if (httpCode != HTTP_CODE_OK) {
    String status = httpStatusText(http, httpCode);
    http.end();
    showOtaMessage("Update Error", status);
    delay(2000);
    return false;
  }

  int contentLength = http.getSize();
  WiFiClient* stream = http.getStreamPtr();

  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    Update.printError(Serial);
    http.end();
    showOtaMessage("Update Error", "No space");
    delay(2000);
    return false;
  }

  uint8_t buffer[1024];
  int written = 0;
  unsigned long lastDataAt = millis();
  bool streamFailed = false;

  while (http.connected()) {
    int availableSize = stream->available();
    if (availableSize > 0) {
      size_t chunkSize = (size_t)availableSize;
      if (chunkSize > sizeof(buffer)) {
        chunkSize = sizeof(buffer);
      }

      int readLen = stream->readBytes(buffer, chunkSize);
      if (readLen > 0) {
        if (Update.write(buffer, readLen) != (size_t)readLen) {
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
          if (written >= contentLength) {
            break;
          }
        } else {
          showOtaMessage("Updating...", String(written / 1024) + " KB");
        }
      }
    } else {
      if (!http.connected()) {
        break;
      }
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
    if (manifestError.length() == 0) {
      manifestError = "Bad manifest";
    }
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
  if (otaServerRunning) {
    return;
  }

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });

  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", Update.hasError() ? "UPDATE FAILED! Rebooting..." : "SUCCESS! Restarting Jarvis...");
    delay(2000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
      showOtaMessage("Receiving...", "Manual upload");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        showOtaMessage("DONE!", "Rebooting...");
      }
    }
  });

  server.begin();
  otaServerRunning = true;
}