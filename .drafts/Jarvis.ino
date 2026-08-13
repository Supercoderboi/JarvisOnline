#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <BluetoothSerial.h>

#define JOY_BTN 32
#define LED_PIN 2 // Built-in LED

Preferences prefs;
WebServer server(80);
BluetoothSerial SerialBT;

bool otaModeActive = false;
unsigned long btnDownTime = 0;
bool btnWasDown = false;
unsigned long lastBlink = 0;
int blinkState = 0;

void setup() {
  Serial.begin(115200);
  pinMode(JOY_BTN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED off

  SerialBT.begin("GhostBoard");

  String s,p;
  if(loadWiFi(s,p)) {
    WiFi.begin(s.c_str(), p.c_str());
    ledPattern(2); // 2 blinks = trying wifi
  } else {
    startConfigPortal(); // 3 fast blinks = AP mode
  }
}

void loop() {
  if(otaModeActive) {
    server.handleClient(); // OTA webserver
    ledPattern(4); // 4 blinks = OTA mode
  } else {
    handleButton();
    // your other code: BT, joystick etc goes here
  }
  delay(10);
}

// ========== LED STATUS CODES ==========
void ledPattern(int times) {
  if(millis() - lastBlink > 300) {
    lastBlink = millis();
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    blinkState++;
    if(blinkState >= times*2) {
      digitalWrite(LED_PIN, HIGH); // off
      blinkState = 0;
      delay(1000); // pause between patterns
    }
  }
}
// 1 blink = Booted OK
// 2 blinks = Connecting WiFi
// 3 fast blinks = AP Config Portal
// 4 blinks = OTA Mode
// Solid ON 3s = WiFi Reset done

// ========== BUTTON LOGIC ==========
void handleButton() {
  int btnVal = digitalRead(JOY_BTN);
  if (btnVal == LOW) {
    if (!btnWasDown) btnDownTime = millis();
    btnWasDown = true;
    unsigned long held = millis() - btnDownTime;

    // 3s HOLD = OTA MODE
    if (held > 3000 && held < 3100) { 
      ledPattern(4);
      String s,p;
      if (!loadWiFi(s,p)) startConfigPortal();
      else if (WiFi.status() != WL_CONNECTED) WiFi.begin(s.c_str(), p.c_str());
      startOtaServer();
    }
    
    // 5s HOLD = WI-FI RESET
    if (held > 5000) { 
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

// ========== OTA SERVER ==========
void startOtaServer() {
  otaModeActive = true;
  server.on("/", HTTP_GET, [](){
    server.send(200, "text/html", "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
  });
  server.on("/update", HTTP_POST, [](){
    server.send(200, "text/plain", "Rebooting...");
    delay(1000);
    ESP.restart();
  }, [](){
    HTTPUpload& upload = server.upload();
    if(upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
    else if(upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
    else if(upload.status == UPLOAD_FILE_END) Update.end(true);
  });
  server.begin();
  Serial.println("OTA Started. Go to http://192.168.4.1");
}

bool loadWiFi(String &s, String &p) {
  prefs.begin("wifi-creds", true);
  s = prefs.getString("ssid", "");
  p = prefs.getString("pass", "");
  prefs.end();
  return s.length() > 0;
}

void startConfigPortal() {
  WiFi.softAP("GhostBoard-Setup", "12345678");
  startOtaServer(); // reuse same page
}