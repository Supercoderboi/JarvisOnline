#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <BluetoothSerial.h>

#define JOY_BTN 32
#define LED_PIN 2 // Built-in LED

Preferences prefs;
WebServer server(80);
BluetoothSerial SerialBT;

bool otaModeActive = false;

// --- OTA Config ---
#define FW_VERSION "1.0.1" // bumped version so it knows to update
const char* MANIFEST_URL = "https://raw.githubusercontent.com/Supercoderboi/JarvisOnline/master/firmware/manifest.json";

void setup() {
  Serial.begin(115200);
  pinMode(JOY_BTN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED off

  SerialBT.begin("GhostBoard");

  // ========== RESCUE MODE: ALWAYS START AP ==========
  ledBlink(3); // 3 blinks = AP mode
  startConfigPortal(); // SKIP WIFI COMPLETELY
}

void loop() {
  if(otaModeActive) {
    server.handleClient(); // OTA webserver
    ledBlink(4); // 4 blinks = OTA mode
  } else {
    handleButton();
    // your BT, joystick code goes here
  }
  delay(10);
}

// ========== LED STATUS ==========
void ledBlink(int times) {
  for(int i=0; i<times; i++) {
    digitalWrite(LED_PIN, LOW); delay(200);
    digitalWrite(LED_PIN, HIGH); delay(200);
  }
  delay(1000); // pause
}

// 3 blinks = AP Config Portal
// 4 blinks = OTA Mode

// ========== BUTTON LOGIC ==========
void handleButton() {
  int btnVal = digitalRead(JOY_BTN);
  static bool btnWasDown = false;
  static unsigned long btnDownTime = 0;

  if (btnVal == LOW) {
    if (!btnWasDown) btnDownTime = millis();
    btnWasDown = true;
    unsigned long held = millis() - btnDownTime;

    if (held > 5000) { // 5s HOLD = WI-FI RESET
      prefs.begin("wifi-creds", false);
      prefs.clear();
      prefs.end();
      digitalWrite(LED_PIN, LOW); // solid on
      delay(3000);
      ESP.restart();
    }
  } else {
    btnWasDown = false;
  }
}

// ========== WIFI SAVE + PORTAL ==========
bool loadWiFi(String &s, String &p) {
  prefs.begin("wifi-creds", true);
  s = prefs.getString("ssid", "");
  p = prefs.getString("pass", "");
  prefs.end();
  return s.length() > 0;
}

void saveWiFi(String s, String p) {
  prefs.begin("wifi-creds", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  prefs.end();
}

void startConfigPortal() {
  otaModeActive = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("GhostBoard-Setup", "12345678");

  server.on("/", HTTP_GET, [](){
    server.send(200, "text/html",
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<h2>GhostBoard WiFi Setup</h2>"
      "<form method='POST' action='/save'>"
      "SSID: <input name='ssid'><br><br>"
      "Pass: <input name='pass' type='password'><br><br>"
      "<input type='submit' value='Save & Reboot'></form>"
      "<hr><h3>Manual OTA</h3>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='update'><input type='submit' value='Update'></form>");
  });

  server.on("/save", HTTP_POST, [](){
    saveWiFi(server.arg("ssid"), server.arg("pass"));
    server.send(200, "text/plain", "Saved! Rebooting...");
    delay(2000);
    ESP.restart();
  });

  server.on("/update", HTTP_POST, [](){
    server.send(200, "text/plain", "Update Done! Rebooting...");
    delay(1000);
    ESP.restart();
  }, [](){
    HTTPUpload& upload = server.upload();
    if(upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
    else if(upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
    else if(upload.status == UPLOAD_FILE_END) Update.end(true);
  });

  server.begin();
  Serial.println("AP Started. Go to 192.168.4.1");
}

// ========== GITHUB OTA ==========
void checkForGitHubUpdate() {
  Serial.println("Checking GitHub for update...");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, MANIFEST_URL);
  http.setTimeout(10000);

  int httpCode = http.GET();
  if(httpCode!= 200) {
    Serial.println("Manifest fetch failed");
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<512> doc;
  deserializeJson(doc, payload);

  String remoteVer = doc["version"] | "";
  String binUrl = doc["bin_url"] | "";

  if(remoteVer!= FW_VERSION && binUrl.length() > 0) {
    Serial.println("New version found: " + remoteVer);
    ledBlink(4);
    performOTA(binUrl);
  } else {
    Serial.println("Already up to date");
  }
}

void performOTA(String binUrl) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, binUrl);

  int httpCode = http.GET();
  if(httpCode!= 200) return;

  int len = http.getSize();
  if(!Update.begin(len)) return;

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buff[128] = {0};
  while(http.connected()) {
    int c = stream->readBytes(buff, sizeof(buff));
    if(c <= 0) break;
    Update.write(buff, c);
  }

  if(Update.end()) {
    Serial.println("OTA Done. Rebooting");
    ESP.restart();
  }
}