#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <BluetoothSerial.h>

#define JOY_BTN 32
#define LED_PIN 2 // Built-in LED. Active LOW

Preferences prefs;
WebServer server(80);
BluetoothSerial SerialBT;

bool otaModeActive = false;
bool controlModeActive = false;

// --- OTA Config ---
#define FW_VERSION "1.0.3"
const char* MANIFEST_URL = "https://raw.githubusercontent.com/Supercoderboi/JarvisOnline/master/firmware/manifest.json";

// Touch pins
const int touchPins[] = {4, 0, 2, 15, 13, 12, 14, 27, 33, 32};
const char* touchNames[] = {"T0-G4", "T1-G0", "T2-G2", "T3-G15", "T4-G13", "T5-G12", "T6-G14", "T7-G27", "T8-G33", "T9-G32"};
int touchValues[10];

// Double click vars
unsigned long lastClickTime = 0;
int clickCount = 0;
unsigned long lastTouchRead = 0;

void setup() {
  Serial.begin(115200);
  pinMode(JOY_BTN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  SerialBT.begin("GhostBoard");

  String s,p;
  if(loadWiFi(s,p)) {
    WiFi.begin(s.c_str(), p.c_str());
    ledBlink(2);
    unsigned long start = millis();
    while(WiFi.status()!= WL_CONNECTED && millis() - start < 8000) delay(500);

    if(WiFi.status() == WL_CONNECTED) {
      ledBlink(1);
      Serial.println("IP: " + WiFi.localIP().toString());
      startControlServer();
      checkForGitHubUpdate();
    } else {
      startConfigPortal();
    }
  } else {
    startConfigPortal();
  }
}

void loop() {
  if(otaModeActive || controlModeActive) {
    server.handleClient();
    if(controlModeActive && millis() - lastTouchRead > 500) {
      readTouch();
      lastTouchRead = millis();
    }
    if(controlModeActive) ledBlinkSlow(1);
    if(otaModeActive) ledBlink(4);
  } else {
    handleButton();
  }
  delay(10);
}

// ========== LED STATUS ==========
void ledBlink(int times) {
  for(int i=0; i<times; i++) {
    digitalWrite(LED_PIN, LOW); delay(150);
    digitalWrite(LED_PIN, HIGH); delay(150);
  }
  delay(500);
}
void ledBlinkSlow(int times) {
  for(int i=0; i<times; i++) {
    digitalWrite(LED_PIN, LOW); delay(400);
    digitalWrite(LED_PIN, HIGH); delay(400);
  }
  delay(1000);
}

// ========== BUTTON LOGIC WITH DOUBLE CLICK ==========
void handleButton() {
  int btnVal = digitalRead(JOY_BTN);
  static bool btnWasDown = false;
  static unsigned long btnDownTime = 0;

  if (btnVal == LOW) {
    if (!btnWasDown) btnDownTime = millis();
    btnWasDown = true;
    unsigned long held = millis() - btnDownTime;

    if (held > 3000 && held < 3200) {
      ledBlink(4);
      startConfigPortal();
    }
    if (held > 5000) {
      prefs.begin("wifi-creds", false);
      prefs.clear();
      prefs.end();
      digitalWrite(LED_PIN, LOW);
      delay(3000);
      ESP.restart();
    }
  } else {
    if(btnWasDown) {
      unsigned long pressDuration = millis() - btnDownTime;
      if(pressDuration < 400) {
        if(millis() - lastClickTime < 400) {
          clickCount++;
          if(clickCount >= 2) {
            Serial.println("Double Click! Starting Control UI");
            if(WiFi.status() == WL_CONNECTED) startControlServer();
            else startConfigPortal();
            clickCount = 0;
          }
        } else {
          clickCount = 1;
        }
        lastClickTime = millis();
      }
    }
    btnWasDown = false;
  }
}

// ========== TOUCH READ ==========
void readTouch() {
  for(int i=0; i<10; i++) {
    touchValues[i] = touchRead(touchPins[i]);
  }
}

// ========== CONTROL WEB UI ==========
void startControlServer() {
  controlModeActive = true;
  otaModeActive = false;
  server.stop();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/touch", HTTP_GET, handleTouchJSON);
  server.on("/led", HTTP_GET, handleLED);
  server.on("/config", HTTP_GET, [](){ startConfigPortal(); });
  server.on("/update", HTTP_POST, handleUpdateFinish, handleUpdateUpload);

  server.begin();
  Serial.println("Control UI Started at: http://" + WiFi.localIP().toString());
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>GhostBoard Touch</title>";
  html += "<style>body{font-family:Arial;text-align:center;padding:20px;background:#111;color:#eee} ";
  html += "button{padding:15px 30px;font-size:18px;margin:10px;border:none;border-radius:8px;background:#00aaff;color:white} ";
  html += ".touch{border:1px solid #333;padding:10px;margin:5px;border-radius:8px;background:#222} ";
  html += ".bar{height:20px;background:#00ff88;border-radius:4px}</style></head><body>";

  html += "<h2>GhostBoard Touch Monitor</h2>";
  html += "<p>IP: " + WiFi.localIP().toString() + " | FW: " + FW_VERSION + "</p><hr>";

  html += "<h3>Touch Pins Live</h3><div id='touchbox'>";
  for(int i=0; i<10; i++) {
    html += "<div class='touch'>" + String(touchNames[i]) + ": <span id='t" + String(i) + "'>0</span><div class='bar' id='b" + String(i) + "' style='width:0%'></div></div>";
  }
  html += "</div>";

  html += "<hr><h3>LED Test</h3>";
  html += "<button onclick=\"fetch('/led?state=1')\">LED ON</button>";
  html += "<button onclick=\"fetch('/led?state=0')\">LED OFF</button>";

  html += "<hr><a href='/config'><button>WiFi Setup</button></a>";
  html += "<a href='/update'><button>Manual OTA</button></a>";

  html += "<script>";
  html += "function updateTouch(){";
  html += "fetch('/touch').then(r=>r.json()).then(d=>{";
  html += "for(let i=0;i<10;i++){";
  html += "document.getElementById('t'+i).innerText=d[i];";
  html += "let w = Math.max(0, 100 - d[i]/10);"; // lower value = more touch
  html += "document.getElementById('b'+i).style.width = w + '%';";
  html += "}})}";
  html += "setInterval(updateTouch, 300); updateTouch();";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

void handleTouchJSON() {
  String json = "[";
  for(int i=0; i<10; i++) {
    json += String(touchValues[i]);
    if(i < 9) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleLED() {
  bool state = server.arg("state").toInt();
  digitalWrite(LED_PIN,!state);
  server.send(200, "text/plain", state? "LED ON" : "LED OFF");
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
  controlModeActive = false;
  server.stop();
  WiFi.mode(WIFI_AP);
  WiFi.softAP("GhostBoard-Setup", "12345678");
  ledBlink(3);

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
  server.on("/save", HTTP_POST, [](){ saveWiFi(server.arg("ssid"), server.arg("pass")); server.send(200, "text/plain", "Saved! Rebooting..."); delay(2000); ESP.restart(); });
  server.on("/update", HTTP_POST, handleUpdateFinish, handleUpdateUpload);
  server.begin();
  Serial.println("AP Started. Go to 192.168.4.1");
}

void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if(upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
  else if(upload.status == UPLOAD_FILE_END) Update.end(true);
}
void handleUpdateFinish() {
  server.send(200, "text/plain", "Update Done! Rebooting...");
  delay(1000);
  ESP.restart();
}

// ========== GITHUB OTA ==========
void checkForGitHubUpdate() {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.begin(client, MANIFEST_URL); http.setTimeout(10000);
  int httpCode = http.GET();
  if(httpCode!= 200) { http.end(); return; }
  String payload = http.getString(); http.end();
  StaticJsonDocument<512> doc; deserializeJson(doc, payload);
  String remoteVer = doc["version"] | ""; String binUrl = doc["bin_url"] | "";
  if(remoteVer!= FW_VERSION && binUrl.length() > 0) performOTA(binUrl);
}
void performOTA(String binUrl) {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.begin(client, binUrl);
  int httpCode = http.GET(); if(httpCode!= 200) return;
  int len = http.getSize(); if(!Update.begin(len)) return;
  WiFiClient* stream = http.getStreamPtr(); uint8_t buff[128] = {0};
  while(http.connected()) { int c = stream->readBytes(buff, sizeof(buff)); if(c <= 0) break; Update.write(buff, c); }
  if(Update.end()) ESP.restart();
}