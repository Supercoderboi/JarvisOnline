#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

#ifndef FW_BUILD_ID
#define FW_BUILD_ID "dev"
#endif

#define VERSION "1.3.2 cont"

// --- Wi-Fi Configuration ---
const char* ssid = "Ethria2.4";
const char* password = "PalmDale007";

// --- OTA Buttons ---
const int homeBtn = 32;
const int backBtn = 33;

unsigned long lastBtnCheck = 0;
const int debounceDelay = 1000;

// --- WiFi retry ---
unsigned long lastWiFiAttempt = 0;
const unsigned long WIFI_RETRY_INTERVAL = 30000; // 30s
bool wifiConnecting = false;

// --- Nokia 5110 Wiring ---
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

bool connectToWiFi(unsigned long timeoutMs);
bool ensureWiFiForOta();
String fetchRemoteBuildId(String& binUrl, String& errorMessage);
bool isRemoteBuildNewer(const String& remoteBuildId);
bool performFirmwareUpdate(const String& binUrl);
bool checkForGitHubUpdate();
void showOtaMessage(const String& line1, const String& line2 = "", const String& line3 = "", const String& line4 = "");

void setup() {
  Serial.begin(115200);

  pinMode(homeBtn, INPUT_PULLUP);
  pinMode(backBtn, INPUT_PULLUP);

  display.begin();
  display.setContrast(58);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.display();

  showOtaMessage("OTA MODE", "Booting...", VERSION);

  if (connectToWiFi(20000)) {
    showOtaMessage("WiFi OK", WiFi.localIP().toString(), "Hold HOME+BACK", "to update");
  } else {
    showOtaMessage("WiFi FAIL", "Retrying 30s", "Hold HOME+BACK", "to update");
    lastWiFiAttempt = millis();
  }
}

void loop() {
  // --- Auto-retry WiFi every 30s if disconnected ---
  if (WiFi.status()!= WL_CONNECTED && millis() - lastWiFiAttempt > WIFI_RETRY_INTERVAL) {
    if (!wifiConnecting) {
      wifiConnecting = true;
      showOtaMessage("WiFi lost", "Retrying...", "Please wait");
      Serial.println("WiFi disconnected. Retrying...");
    }

    if (connectToWiFi(10000)) {
      showOtaMessage("WiFi OK", WiFi.localIP().toString(), "Hold HOME+BACK", "to update");
      Serial.println("WiFi reconnected");
      wifiConnecting = false;
    } else {
      showOtaMessage("WiFi FAIL", "Retrying 30s", "Hold HOME+BACK", "to update");
      wifiConnecting = false;
    }
    lastWiFiAttempt = millis();
  }

  // --- Update trigger: Home + Back ---
  bool homePressed = digitalRead(homeBtn) == LOW;
  bool backPressed = digitalRead(backBtn) == LOW;

  if(homePressed && backPressed && millis() - lastBtnCheck > debounceDelay) {
    showOtaMessage("Update combo", "detected...");
    delay(500);
    checkForGitHubUpdate();
    lastBtnCheck = millis();
  }

  delay(50);
}

void showOtaMessage(const String& line1, const String& line2, const String& line3, const String& line4) {
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
  Serial.print("Connecting to Wi-Fi");

  unsigned long startTime = millis();
  while (WiFi.status()!= WL_CONNECTED && millis() - startTime < timeoutMs) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

bool ensureWiFiForOta() {
  if (WiFi.status() == WL_CONNECTED) return true;

  showOtaMessage("WiFi lost", "Reconnecting");
  WiFi.disconnect(false, false);
  WiFi.reconnect();
  unsigned long reconnectStarted = millis();
  while (WiFi.status()!= WL_CONNECTED && millis() - reconnectStarted < 10000) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

String fetchRemoteBuildId(String& binUrl, String& errorMessage) {
  for (size_t urlIndex = 0; urlIndex < UPDATE_MANIFEST_URL_COUNT; urlIndex++) {
    const char* manifestUrl = UPDATE_MANIFEST_URLS[urlIndex];
    showOtaMessage("Checking", "manifest...", String(urlIndex + 1) + "/" + String(UPDATE_MANIFEST_URL_COUNT));

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
      errorMessage = http.errorToString(httpCode);
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
    showOtaMessage("Update Error", http.errorToString(httpCode));
    http.end();
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

  uint8_t buffer[1024];
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
          showOtaMessage("Updating...", String(percent) + "%", String(written/1024) + "KB");
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
    showOtaMessage("OTA Error", "No WiFi", "Retrying 30s");
    lastWiFiAttempt = millis() - WIFI_RETRY_INTERVAL + 2000; // retry soon
    return false;
  }

  String binUrl = "";
  String manifestError = "";
  String remoteBuildId = fetchRemoteBuildId(binUrl, manifestError);

  if (remoteBuildId.length() == 0 || binUrl.length() == 0) {
    if (manifestError.length() == 0) manifestError = "Bad manifest";
    showOtaMessage("OTA Error", manifestError);
    delay(2000);
    showOtaMessage("Ready", WiFi.localIP().toString(), "Hold HOME+BACK");
    return false;
  }

  showOtaMessage("Current:", CURRENT_BUILD_ID, "Remote:", remoteBuildId);
  delay(1500);

  if (!isRemoteBuildNewer(remoteBuildId)) {
    showOtaMessage("Device", "Up To Date", CURRENT_BUILD_ID);
    delay(2000);
    showOtaMessage("Ready", WiFi.localIP().toString(), "Hold HOME+BACK");
    return false;
  }

  showOtaMessage("Update Found", remoteBuildId);
  delay(1200);
  bool result = performFirmwareUpdate(binUrl);

  if (!result) {
    showOtaMessage("Update Failed", "Hold HOME+BACK", "to retry");
  }
  return result;
}