#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include "DHT.h"
#include <BleKeyboard.h>
#include <time.h>
#include <Preferences.h>
#include <WebServer.h>
#include <Update.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#ifndef FW_BUILD_ID
#define FW_BUILD_ID "dev"
#endif

#define NOKIA_CLK 18
#define NOKIA_DIN 19
#define NOKIA_DC 21
#define NOKIA_CE 5
#define NOKIA_RST 15

#define ENC_CLK 32
#define ENC_DT 33
#define ENC_SW 25

#define DHTPIN 26
#define DHTTYPE DHT11

Adafruit_PCD8544 display = Adafruit_PCD8544(NOKIA_CLK, NOKIA_DIN, NOKIA_DC, NOKIA_CE, NOKIA_RST);
DHT dht(DHTPIN, DHTTYPE);
BleKeyboard bleKeyboard("Jarvis Remote", "ESP32", 100);
Preferences preferences;
WebServer server(80);

String wifi_ssid = "Airtel_Ethria2.4";
String wifi_pass = "PalmDale007";
const char *JARVIS_URL = "http://jarvisep.pythonanywhere.com/command";

const long GMT_OFFSET_SEC = 19800;
const int DAYLIGHT_OFFSET_SEC = 0;

const char *CURRENT_BUILD_ID = FW_BUILD_ID;
const char *UPDATE_MANIFEST_URL = "https://jarvisupload.netlify.app/firmware/manifest.json";

String httpStatusText(HTTPClient &http, int httpCode) {
  if (httpCode >= 0) return "HTTP " + String(httpCode);
  return http.errorToString(httpCode).c_str();
}

const uint8_t shield_width = 48;
const uint8_t shield_height = 48;
const uint8_t PROGMEM shield_bitmap[] = {
  0xff, 0xff, 0xf0, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xfc, 0x01, 0x80,
  0x3f, 0xff, 0xff, 0xf0, 0x7f, 0xfe, 0x0f, 0xff, 0xff, 0xc1, 0xff, 0xff, 0x83, 0xff, 0xff, 0x87,
  0xff, 0xff, 0xe1, 0xff, 0xff, 0x1f, 0xff, 0xff, 0xf0, 0xff, 0xfe, 0x3f, 0xff, 0xff, 0xfc, 0x7f,
  0xfc, 0x7f, 0xff, 0xff, 0xfe, 0x3f, 0xf8, 0xbf, 0xfe, 0x07, 0xfd, 0x1f, 0xf1, 0x9f, 0xfc, 0x0f,
  0xf9, 0x8f, 0xf3, 0x07, 0xfc, 0x1f, 0xe0, 0xcf, 0xe2, 0x03, 0xf8, 0x1f, 0xc0, 0x47, 0xe6, 0x01,
  0xf8, 0x1f, 0x80, 0x67, 0xcf, 0x00, 0xf0, 0x0f, 0x00, 0xe3, 0xc9, 0x80, 0x30, 0x0c, 0x01, 0x93,
  0x88, 0xc0, 0x00, 0x00, 0x03, 0x11, 0x98, 0x40, 0x00, 0x00, 0x06, 0x19, 0x90, 0x60, 0x00, 0x00,
  0x04, 0x09, 0xb0, 0xf0, 0x00, 0x00, 0x0e, 0x09, 0x31, 0x98, 0x00, 0x00, 0x1b, 0x08, 0x33, 0x0c,
  0x00, 0x00, 0x30, 0x88, 0x36, 0x06, 0x00, 0x00, 0x60, 0x64, 0x3c, 0x07, 0x00, 0x00, 0xe0, 0x34,
  0x38, 0x0d, 0x80, 0x01, 0xa0, 0x1c, 0x30, 0x18, 0xc0, 0x03, 0x18, 0x08, 0x30, 0x30, 0xc0, 0x03,
  0x08, 0x00, 0x10, 0x70, 0x60, 0x06, 0x04, 0x08, 0x90, 0xc0, 0x70, 0x0e, 0x02, 0x09, 0x91, 0xc0,
  0xf8, 0x1f, 0x03, 0x09, 0x9b, 0x80, 0xe4, 0x27, 0x01, 0xc9, 0x8e, 0x01, 0xc2, 0x43, 0x80, 0x51,
  0xcc, 0x03, 0xc1, 0x83, 0xc0, 0x73, 0xc4, 0x07, 0x80, 0x01, 0xe0, 0x33, 0xe4, 0x0f, 0x00, 0x00,
  0xf0, 0x27, 0xe2, 0x0f, 0x00, 0x00, 0xf0, 0x47, 0xf3, 0x1e, 0x00, 0x00, 0x78, 0xcf, 0xf1, 0x3c,
  0x00, 0x00, 0x3c, 0x8f, 0xf8, 0xfc, 0x00, 0x00, 0x3f, 0x1f, 0xfc, 0x78, 0x00, 0x00, 0x1e, 0x3f,
  0xfe, 0x30, 0x00, 0x00, 0x0c, 0x7f, 0xff, 0x18, 0x00, 0x00, 0x18, 0xff, 0xff, 0x86, 0x00, 0x00,
  0x61, 0xff, 0xff, 0xc1, 0x80, 0x01, 0x83, 0xff, 0xff, 0xf0, 0x78, 0x1e, 0x0f, 0xff, 0xff, 0xfc,
  0x03, 0xc0, 0x3f, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x0f, 0xff, 0xff
};

String correctPIN = "XR2896";
String enteredPIN = "";
int authCharIndex = 0;
const char authChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const int numAuthChars = 36;

enum ScreenState {
  HOME, MENU, JARVIS, SENSORS, TIMER, MUSIC, SOCIAL, CAMERA,
  SETTINGS, SETTINGS_CONTRAST, SETTINGS_WIFI_SSID, SETTINGS_WIFI_PASS,
  JARVIS_RESPONSE, OTA_UPDATE, TIMER_ALARM, SCREENSAVER
};
ScreenState currentState = HOME;

int displayContrast = 55;

volatile int encoderCount = 0;
int lastEncoderCount = 0;

bool btnState = false;
bool lastBtnState = false;
unsigned long btnPressTime = 0;
unsigned long btnReleaseTime = 0;
int tapCount = 0;
int registeredTaps = 0;
bool longPress = false;
const unsigned long TAP_TIMEOUT = 350;

const char *menuItems[] = {"Jarvis", "Sensors", "Timer", "Music", "Social", "Camera", "Assistant", "Settings", "System Update"};
const int numMenuItems = 9;
int menuIndex = 0;

const char *settingsItems[] = {"Contrast", "WiFi SSID", "WiFi Pass", "Back"};
const int numSettingsItems = 4;
int settingsIndex = 0;

unsigned long lastActivityTime = 0;
const unsigned long SCREENSAVER_TIMEOUT = 60000;

const char charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,!?<*>";
int charIndex = 0;
String jarvisMessage = "";
String predictedWord = "";
String jarvisReply = "";
int jarvisScrollY = 0;
String tempTypingString = "";

int timerHours = 0;
int timerMinutes = 0;
int timerSeconds = 0;
int timerSetupStage = 0;
unsigned long timerEndTime = 0;
bool timerRunning = false;

bool otaStarted = false;
bool otaServerRunning = false;

const char *serverIndex =
  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
  "<h2 style='font-family:sans-serif;'>Jarvis System Update</h2>"
  "<p style='font-family:sans-serif;'>Select the new .bin file from your phone to flash.</p>"
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='update' accept='.bin' style='margin-bottom:20px;'><br>"
  "<input type='submit' value='Update Firmware' style='padding:10px 20px; background:#007BFF; color:white; border:none; border-radius:5px;'>"
  "</form>";

void handleButton();
int getEncoderDelta();
void verifyClearanceLevel();
void runHome();
void runMenu();
void runSettings();
void runSettingsContrast();
void runSettingsWifiSsid();
void runSettingsWifiPass();
void runWifiInput(bool isSsid);
void runJarvis();
void runJarvisResponse();
void runSensors();
void runTimer();
void runTimerAlarm();
void runMusic();
void runSocial();
void runCamera();
void runScreensaver();
void runOtaMode();
void fetchPrediction(String input);
void sendToJarvis(String msg);

void showOtaMessage(const String &line1, const String &line2 = "", const String &line3 = "");
bool checkForGitHubUpdate();
bool performFirmwareUpdate(const String &binUrl);
String fetchRemoteBuildId(String &binUrl, String &errorMessage);
bool isRemoteBuildNewer(const String &remoteBuildId);
void startManualOtaServer();
void stopManualOtaServer();

void IRAM_ATTR readEncoder() {
  static uint8_t old_AB = 3;
  static int8_t encval = 0;
  static const int8_t enc_states[] = {
    0, -1, 1, 0, 1, 0, 0, -1,
    -1, 0, 0, 1, 0, 1, -1, 0
  };

  old_AB <<= 2;
  old_AB |= ((digitalRead(ENC_DT) << 1) | digitalRead(ENC_CLK));
  encval += enc_states[(old_AB & 0x0f)];

  if (encval > 3) {
    encoderCount++;
    encval = 0;
    lastActivityTime = millis();
  } else if (encval < -3) {
    encoderCount--;
    encval = 0;
    lastActivityTime = millis();
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

  preferences.begin("jarvis", false);
  displayContrast = preferences.getInt("contrast", 55);
  wifi_ssid = preferences.getString("ssid", "Airtel_Ethria2.4");
  wifi_pass = preferences.getString("pass", "PalmDale007");

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), readEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT), readEncoder, CHANGE);

  display.begin();
  display.setContrast(displayContrast);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);

  display.drawBitmap(18, 0, shield_bitmap, shield_width, shield_height, WHITE, BLACK);
  display.display();

  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    wifiAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org");
    struct tm timeinfo;
    int ntpAttempts = 0;
    while (!getLocalTime(&timeinfo) && ntpAttempts < 10) {
      delay(500);
      ntpAttempts++;
    }
  }

  dht.begin();
  bleKeyboard.begin();

  verifyClearanceLevel();

  encoderCount = 0;
  lastEncoderCount = 0;
  lastActivityTime = millis();
}

void verifyClearanceLevel() {
  bool accessGranted = false;
  enteredPIN = "";
  authCharIndex = 0;
  encoderCount = 0;
  lastEncoderCount = 0;

  while (!accessGranted) {
    handleButton();
    int delta = getEncoderDelta();

    if (delta > 0) {
      authCharIndex++;
      if (authCharIndex >= numAuthChars) authCharIndex = 0;
    } else if (delta < 0) {
      authCharIndex--;
      if (authCharIndex < 0) authCharIndex = numAuthChars - 1;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(12, 0);
    display.print("S.H.I.E.L.D");
    display.setCursor(0, 10);
    display.print("LEVEL 8 ACCESS");
    display.setCursor(0, 20);
    display.print("VERIFY ACCESS:");
    display.setCursor(0, 30);

    for (int i = 0; i < correctPIN.length(); i++) {
      if (i < enteredPIN.length()) display.print(enteredPIN[i]);
      else display.print("_");
      display.print(" ");
    }

    display.setCursor(24, 40);
    display.print("[ ");
    display.print(authChars[authCharIndex]);
    display.print(" ]");
    display.display();

    if (registeredTaps > 0) {
      enteredPIN += authChars[authCharIndex];
      registeredTaps = 0;

      if (enteredPIN.length() == correctPIN.length()) {
        if (enteredPIN == correctPIN) {
          display.clearDisplay();
          display.drawBitmap(18, 0, shield_bitmap, shield_width, shield_height, WHITE, BLACK);
          display.display();
          delay(1500);
          accessGranted = true;
        } else {
          while (true) {
            display.clearDisplay();
            display.drawBitmap(18, 0, shield_bitmap, shield_width, shield_height, WHITE, BLACK);
            display.display();
            delay(2000);

            display.clearDisplay();
            display.setCursor(18, 20);
            display.print("LOCKDOWN");
            display.display();
            delay(2000);
          }
        }
      }
    }
    delay(30);
  }
}

void loop() {
  handleButton();

  if (timerRunning && millis() >= timerEndTime) {
    timerRunning = false;
    currentState = TIMER_ALARM;
  }

  if (millis() - lastActivityTime > SCREENSAVER_TIMEOUT &&
      currentState != SCREENSAVER &&
      currentState != TIMER_ALARM &&
      currentState != OTA_UPDATE &&
      currentState != MUSIC &&
      currentState != SOCIAL) {
    currentState = SCREENSAVER;
    display.clearDisplay();
  }

  switch (currentState) {
    case HOME: runHome(); break;
    case MENU: runMenu(); break;
    case JARVIS: runJarvis(); break;
    case JARVIS_RESPONSE: runJarvisResponse(); break;
    case SENSORS: runSensors(); break;
    case TIMER: runTimer(); break;
    case MUSIC: runMusic(); break;
    case SOCIAL: runSocial(); break;
    case CAMERA: runCamera(); break;
    case SETTINGS: runSettings(); break;
    case SETTINGS_CONTRAST: runSettingsContrast(); break;
    case SETTINGS_WIFI_SSID: runSettingsWifiSsid(); break;
    case SETTINGS_WIFI_PASS: runSettingsWifiPass(); break;
    case OTA_UPDATE: runOtaMode(); break;
    case TIMER_ALARM: runTimerAlarm(); break;
    case SCREENSAVER: runScreensaver(); break;
  }

  registeredTaps = 0;
  longPress = false;

  if (currentState != OTA_UPDATE) delay(30);
}

void handleButton() {
  btnState = !digitalRead(ENC_SW);
  unsigned long now = millis();

  if (btnState && !lastBtnState) {
    btnPressTime = now;
    lastActivityTime = now;
  }

  if (!btnState && lastBtnState) {
    unsigned long duration = now - btnPressTime;
    if (duration > 30 && duration < 600) {
      tapCount++;
      btnReleaseTime = now;
    } else if (duration >= 600) {
      longPress = true;
    }
    lastActivityTime = now;
  }

  if (tapCount > 0 && (now - btnReleaseTime) > TAP_TIMEOUT) {
    registeredTaps = tapCount;
    tapCount = 0;
  }

  lastBtnState = btnState;
}

int getEncoderDelta() {
  int delta = encoderCount - lastEncoderCount;
  lastEncoderCount = encoderCount;
  return delta;
}

void runHome() {
  display.clearDisplay();
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[10];
    char dayStr[15];
    char dateStr[15];

    strftime(timeStr, sizeof(timeStr), "%I:%M %p", &timeinfo);
    strftime(dayStr, sizeof(dayStr), "%A", &timeinfo);
    strftime(dateStr, sizeof(dateStr), "%d %b %Y", &timeinfo);

    int timeX = (84 - (strlen(timeStr) * 6)) / 2;
    int dayX = (84 - (strlen(dayStr) * 6)) / 2;
    int dateX = (84 - (strlen(dateStr) * 6)) / 2;

    if (timeX < 0) timeX = 0;
    if (dayX < 0) dayX = 0;
    if (dateX < 0) dateX = 0;

    display.setCursor(timeX, 5);
    display.print(timeStr);
    display.setCursor(dayX, 18);
    display.print(dayStr);
    display.setCursor(dateX, 31);
    display.print(dateStr);
  } else {
    display.setCursor(0, 10);
    display.println("No WiFi / Time");
  }
  display.display();

  if (registeredTaps == 1) {
    encoderCount = 0;
    lastEncoderCount = 0;
    currentState = MENU;
  }
}

void runMenu() {
  int delta = getEncoderDelta();
  if (delta > 0) menuIndex = (menuIndex + 1) % numMenuItems;
  if (delta < 0) {
    menuIndex--;
    if (menuIndex < 0) menuIndex = numMenuItems - 1;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("--- MENU ---");

  int maxVisible = 4;
  int startIndex = menuIndex - 1;
  if (startIndex < 0) startIndex = 0;
  if (startIndex > numMenuItems - maxVisible) startIndex = numMenuItems - maxVisible;

  for (int i = 0; i < maxVisible; i++) {
    int currentI = startIndex + i;
    if (currentI >= numMenuItems) break;

    display.setCursor(0, 10 + (i * 9));
    if (currentI == menuIndex) display.print(">");
    else display.print(" ");

    if (currentI == menuIndex && strlen(menuItems[currentI]) > 12) {
      display.print(String(menuItems[currentI]).substring(0, 12));
    } else {
      display.print(menuItems[currentI]);
    }
  }

  display.drawLine(82, 10, 82, 48, BLACK);
  int barY = map(menuIndex, 0, numMenuItems - 1, 10, 40);
  display.fillRect(80, barY, 4, 8, BLACK);
  display.display();

  if (registeredTaps == 1) {
    encoderCount = 0;
    lastEncoderCount = 0;
    if (menuIndex == 0) currentState = JARVIS;
    else if (menuIndex == 1) currentState = SENSORS;
    else if (menuIndex == 2) currentState = TIMER;
    else if (menuIndex == 3) currentState = MUSIC;
    else if (menuIndex == 4) currentState = SOCIAL;
    else if (menuIndex == 5) currentState = CAMERA;
    else if (menuIndex == 6) {
      bleKeyboard.write(KEY_MEDIA_WWW_SEARCH);
      currentState = HOME;
    } else if (menuIndex == 7) currentState = SETTINGS;
    else if (menuIndex == 8) currentState = OTA_UPDATE;
  }

  if (longPress) {
    encoderCount = 0;
    lastEncoderCount = 0;
    currentState = HOME;
  }
}

void runSettings() {
  int delta = getEncoderDelta();
  if (delta > 0) settingsIndex = (settingsIndex + 1) % numSettingsItems;
  if (delta < 0) {
    settingsIndex--;
    if (settingsIndex < 0) settingsIndex = numSettingsItems - 1;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("- SETTINGS -");

  int maxVisible = 3;
  int startIndex = settingsIndex - 1;
  if (startIndex < 0) startIndex = 0;
  if (startIndex > numSettingsItems - maxVisible) startIndex = numSettingsItems - maxVisible;

  for (int i = 0; i < maxVisible; i++) {
    int currentI = startIndex + i;
    if (currentI >= numSettingsItems) break;
    display.setCursor(0, 15 + (i * 10));
    if (currentI == settingsIndex) display.print(">");
    else display.print(" ");
    display.print(settingsItems[currentI]);
  }
  display.display();

  if (registeredTaps == 1) {
    encoderCount = 0;
    lastEncoderCount = 0;
    if (settingsIndex == 0) currentState = SETTINGS_CONTRAST;
    else if (settingsIndex == 1) {
      tempTypingString = wifi_ssid;
      charIndex = 0;
      currentState = SETTINGS_WIFI_SSID;
    } else if (settingsIndex == 2) {
      tempTypingString = wifi_pass;
      charIndex = 0;
      currentState = SETTINGS_WIFI_PASS;
    } else if (settingsIndex == 3) currentState = MENU;
  }

  if (longPress) currentState = MENU;
}

void runSettingsContrast() {
  int delta = getEncoderDelta();

  if (delta != 0) {
    displayContrast += (delta * 2);
    if (displayContrast < 0) displayContrast = 0;
    if (displayContrast > 100) displayContrast = 100;
    display.setContrast(displayContrast);
    preferences.putInt("contrast", displayContrast);
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("- CONTRAST -");
  display.setCursor(15, 20);
  display.print("< ");
  display.print(displayContrast);
  display.println(" >");
  display.setCursor(0, 40);
  display.println("(Tap to Exit)");
  display.display();

  if (registeredTaps > 0 || longPress) currentState = SETTINGS;
}

void runWifiInput(bool isSsid) {
  int delta = getEncoderDelta();
  int charsetLen = strlen(charset);

  if (delta > 0) charIndex = (charIndex + 1) % charsetLen;
  if (delta < 0) {
    charIndex--;
    if (charIndex < 0) charIndex = charsetLen - 1;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  if (isSsid) display.println("Edit SSID:");
  else display.println("Edit PASS:");

  if (tempTypingString.length() > 14) display.println(tempTypingString.substring(tempTypingString.length() - 14));
  else display.println(tempTypingString);

  char selectedChar = charset[charIndex];
  display.setCursor(0, 30);
  display.print("Char: [ ");
  display.print(selectedChar);
  display.println(" ]");

  display.setCursor(0, 40);
  display.print("<:Del  >:Save");
  display.display();

  if (registeredTaps == 1) {
    if (selectedChar == '<') {
      if (tempTypingString.length() > 0) tempTypingString.remove(tempTypingString.length() - 1);
    } else if (selectedChar == '>') {
      if (isSsid) {
        wifi_ssid = tempTypingString;
        preferences.putString("ssid", wifi_ssid);
      } else {
        wifi_pass = tempTypingString;
        preferences.putString("pass", wifi_pass);
      }

      display.clearDisplay();
      display.setCursor(0, 10);
      display.println("Saved!");
      display.println("Reboot to");
      display.println("apply.");
      display.display();
      delay(2000);

      tempTypingString = "";
      currentState = SETTINGS;
    } else if (selectedChar != '*') {
      tempTypingString += selectedChar;
    }
  }

  if (longPress) {
    tempTypingString = "";
    currentState = SETTINGS;
  }
}

void runSettingsWifiSsid() { runWifiInput(true); }
void runSettingsWifiPass() { runWifiInput(false); }

void fetchPrediction(String input) {
  if (input.length() == 0) {
    predictedWord = "";
    return;
  }

  display.setCursor(75, 20);
  display.print("...");
  display.display();

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = "https://api.datamuse.com/sug?s=" + input + "&max=1";
    http.begin(client, url);

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error && doc.size() > 0) {
        predictedWord = doc[0]["word"].as<String>();
        predictedWord.toUpperCase();
      } else {
        predictedWord = "";
      }
    }
    http.end();
  }
}

void runJarvis() {
  int delta = getEncoderDelta();
  int charsetLen = strlen(charset);

  if (delta > 0) charIndex = (charIndex + 1) % charsetLen;
  if (delta < 0) {
    charIndex--;
    if (charIndex < 0) charIndex = charsetLen - 1;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("JARVIS>");

  if (jarvisMessage.length() > 14) display.println(jarvisMessage.substring(jarvisMessage.length() - 14));
  else display.println(jarvisMessage);

  display.setCursor(0, 20);
  if (predictedWord != "") {
    display.print("~");
    display.print(predictedWord);
  }

  char selectedChar = charset[charIndex];
  display.setCursor(0, 35);
  display.print("Char: [ ");
  display.print(selectedChar);
  display.println(" ]");
  display.display();

  if (registeredTaps == 1) {
    if (selectedChar == '<') {
      if (jarvisMessage.length() > 0) {
        jarvisMessage.remove(jarvisMessage.length() - 1);
        fetchPrediction(jarvisMessage);
      }
    } else if (selectedChar == '*') {
      if (predictedWord != "") {
        jarvisMessage = predictedWord;
        predictedWord = "";
      }
    } else if (selectedChar == '>') {
      sendToJarvis(jarvisMessage);
      jarvisMessage = "";
      predictedWord = "";
      currentState = JARVIS_RESPONSE;
    } else {
      jarvisMessage += selectedChar;
      fetchPrediction(jarvisMessage);
    }
  }

  if (longPress) currentState = MENU;
}

void sendToJarvis(String msg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting...");
  display.display();

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(20000);

    http.begin(client, JARVIS_URL);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc;
    doc["text"] = msg;
    String requestBody;
    serializeJson(doc, requestBody);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Sending...");
    display.display();

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      String responseStr = http.getString();
      StaticJsonDocument<500> respDoc;
      DeserializationError error = deserializeJson(respDoc, responseStr);

      if (!error) {
        jarvisReply = respDoc["response"].as<String>();
        jarvisScrollY = 0;
      } else {
        jarvisReply = "JSON Error";
      }
    } else {
      jarvisReply = "Err: " + http.errorToString(httpResponseCode);
    }

    http.end();
  } else {
    jarvisReply = "No WiFi!";
  }
}

void runJarvisResponse() {
  int delta = getEncoderDelta();

  int totalLines = (jarvisReply.length() / 12) + 2;
  int maxScroll = (totalLines * 8) - 48;
  if (maxScroll < 0) maxScroll = 0;

  if (delta > 0) jarvisScrollY += 8;
  if (delta < 0) jarvisScrollY -= 8;

  if (jarvisScrollY < 0) jarvisScrollY = 0;
  if (jarvisScrollY > maxScroll) jarvisScrollY = maxScroll;

  display.clearDisplay();
  display.setCursor(0, -jarvisScrollY);
  display.print(jarvisReply);
  display.display();

  if (registeredTaps > 0 || longPress) currentState = MENU;
}

void runSensors() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("- SENSORS -");

  if (isnan(h) || isnan(t)) {
    display.println("Failed read");
  } else {
    display.print("Temp: ");
    display.print(t, 1);
    display.println("C");
    display.print("Hum:  ");
    display.print(h, 1);
    display.println("%");
  }
  display.display();

  if (registeredTaps > 0 || longPress) currentState = MENU;
}

void runTimer() {
  int delta = getEncoderDelta();

  if (!timerRunning) {
    if (timerSetupStage == 0) {
      if (delta > 0) timerHours++;
      if (delta < 0 && timerHours > 0) timerHours--;
    } else if (timerSetupStage == 1) {
      if (delta > 0) timerMinutes++;
      if (delta < 0 && timerMinutes > 0) timerMinutes--;
      if (timerMinutes > 59) timerMinutes = 0;
    } else if (timerSetupStage == 2) {
      if (delta > 0) timerSeconds++;
      if (delta < 0 && timerSeconds > 0) timerSeconds--;
      if (timerSeconds > 59) timerSeconds = 0;
    }

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Set Timer:");

    display.setCursor(0, 15);
    if (timerSetupStage == 0) display.print("["); else display.print(" ");
    display.print(timerHours);
    if (timerSetupStage == 0) display.print("]h "); else display.print(" h ");

    if (timerSetupStage == 1) display.print("["); else display.print(" ");
    display.print(timerMinutes);
    if (timerSetupStage == 1) display.print("]m "); else display.print(" m ");

    display.setCursor(0, 25);
    if (timerSetupStage == 2) display.print("["); else display.print(" ");
    display.print(timerSeconds);
    if (timerSetupStage == 2) display.println("]s"); else display.println(" s");

    display.setCursor(0, 38);
    display.println("(Tap to next)");
    display.display();

    if (registeredTaps == 1) {
      timerSetupStage++;
      if (timerSetupStage > 2) {
        unsigned long totalMs = (timerHours * 3600000UL) + (timerMinutes * 60000UL) + (timerSeconds * 1000UL);
        if (totalMs > 0) {
          timerEndTime = millis() + totalMs;
          timerRunning = true;
        }
        timerSetupStage = 0;
        currentState = HOME;
      }
    }

    if (longPress) {
      timerSetupStage = 0;
      currentState = MENU;
    }
  } else {
    unsigned long now = millis();
    unsigned long timeLeft = timerEndTime - now;

    int h = (timeLeft / 3600000UL);
    int m = ((timeLeft % 3600000UL) / 60000UL);
    int s = ((timeLeft % 60000UL) / 1000UL);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("- RUNNING -");
    display.print(h); display.print("h ");
    display.print(m); display.print("m ");
    display.print(s); display.println("s");
    display.println();
    display.println("(Hold=Stop)");
    display.display();

    if (longPress) {
      timerRunning = false;
      timerSetupStage = 0;
      currentState = MENU;
    }
  }
}

void runTimerAlarm() {
  if ((millis() / 500) % 2 == 0) display.setTextColor(WHITE, BLACK);
  else display.setTextColor(BLACK, WHITE);

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 15);
  display.println("TIME");
  display.setCursor(25, 30);
  display.println("UP!");
  display.display();

  if (registeredTaps > 0 || longPress) {
    display.setTextColor(BLACK);
    display.setTextSize(1);
    currentState = HOME;
  }
}

void runMusic() {
  int delta = getEncoderDelta();

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(12, 16);
  display.print("MUSIC");
  display.setTextSize(1);
  display.display();

  if (delta > 0) bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
  if (delta < 0) bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);

  if (registeredTaps == 1) bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
  if (registeredTaps == 2) bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
  if (registeredTaps == 3) bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);

  if (longPress) currentState = MENU;
}

void runSocial() {
  int delta = getEncoderDelta();

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(15, 0);
  display.println("- SOCIAL -");
  display.setCursor(0, 15);
  display.println("Turn = Scroll");
  display.println("Tap  = Pause");
  display.println("(Hold to Exit)");
  display.display();

  if (delta > 0) bleKeyboard.write(KEY_DOWN_ARROW);
  if (delta < 0) bleKeyboard.write(KEY_UP_ARROW);
  if (registeredTaps == 1) bleKeyboard.write(' ');

  if (longPress) currentState = MENU;
}

void runCamera() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(15, 0);
  display.println("- CAMERA -");
  display.setCursor(0, 20);
  display.println("Tap to Snap!");
  display.setCursor(0, 38);
  display.println("(Hold to Exit)");
  display.display();

  if (registeredTaps > 0) bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
  if (longPress) currentState = MENU;
}

void runScreensaver() {
  display.clearDisplay();
  display.drawBitmap(18, 0, shield_bitmap, shield_width, shield_height, WHITE, BLACK);
  display.display();

  if (encoderCount != lastEncoderCount || registeredTaps > 0 || longPress) {
    lastActivityTime = millis();
    encoderCount = 0;
    lastEncoderCount = 0;
    currentState = HOME;
    display.clearDisplay();
  }
}

void showOtaMessage(const String &line1, const String &line2, const String &line3) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(line1);
  if (line2.length()) display.println(line2);
  if (line3.length()) display.println(line3);
  display.display();
}

bool isRemoteBuildNewer(const String &remoteBuildId) {
  return remoteBuildId.length() > 0 && remoteBuildId != String(CURRENT_BUILD_ID);
}

String fetchRemoteBuildId(String &binUrl, String &errorMessage) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);

  if (!http.begin(client, UPDATE_MANIFEST_URL)) {
    errorMessage = "begin() failed";
    return "";
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    errorMessage = httpStatusText(http, httpCode);
    http.end();
    return "";
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    errorMessage = String("JSON ") + error.c_str();
    return "";
  }

  binUrl = doc["bin_url"] | "";
  String buildId = doc["build_id"] | "";
  if (buildId.length() == 0 || binUrl.length() == 0) {
    errorMessage = "Missing fields";
    return "";
  }

  errorMessage = "";
  return buildId;
}

bool performFirmwareUpdate(const String &binUrl) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);

  showOtaMessage("Downloading...", "Firmware");

  if (!http.begin(client, binUrl)) {
    showOtaMessage("Update Error", "Bad BIN URL");
    delay(2000);
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    String status = httpStatusText(http, httpCode);
    Serial.println("[OTA] Firmware GET failed: " + status);
    showOtaMessage("Update Error", status);
    http.end();
    delay(2000);
    return false;
  }

  int contentLength = http.getSize();
  WiFiClient *stream = http.getStreamPtr();

  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    showOtaMessage("Update Error", "Not enough", "space");
    http.end();
    delay(2000);
    return false;
  }

  uint8_t buffer[1024];
  int written = 0;
  unsigned long lastDataAt = millis();

  while (http.connected()) {
    int availableSize = stream->available();
    if (availableSize > 0) {
      size_t chunkSize = (size_t)availableSize;
      if (chunkSize > sizeof(buffer)) chunkSize = sizeof(buffer);

      int readLen = stream->readBytes(buffer, chunkSize);
      if (readLen > 0) {
        if (Update.write(buffer, readLen) != (size_t)readLen) {
          Update.abort();
          http.end();
          showOtaMessage("Update Error", "Write failed");
          delay(2000);
          return false;
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
        http.end();
        showOtaMessage("Update Error", "Download timeout");
        delay(2000);
        return false;
      }
      delay(1);
    }
  }

  bool success = Update.end(true);
  http.end();

  if (!success || !Update.isFinished()) {
    showOtaMessage("Update Error", "Finalize fail");
    delay(2000);
    return false;
  }

  showOtaMessage("Update Done!", "Rebooting...");
  delay(1500);
  ESP.restart();
  return true;
}

void startManualOtaServer() {
  if (otaServerRunning) return;

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
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
      showOtaMessage("Receiving...", "Manual upload");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) showOtaMessage("DONE!", "Rebooting...");
    }
  });

  server.begin();
  otaServerRunning = true;
}

void stopManualOtaServer() {
  if (!otaServerRunning) return;
  server.stop();
  otaServerRunning = false;
}

bool checkForGitHubUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    showOtaMessage("OTA Error", "No WiFi");
    Serial.println("[OTA] WiFi not connected");
    delay(2000);
    return false;
  }

  Serial.println("[OTA] Free heap before manifest request: " + String(ESP.getFreeHeap()));
  showOtaMessage("Checking...", "GitHub");
  String binUrl = "";
  String manifestError = "";
  String remoteBuildId = fetchRemoteBuildId(binUrl, manifestError);

  if (remoteBuildId.length() == 0 || binUrl.length() == 0) {
    Serial.println("[OTA] Manifest URL: " + String(UPDATE_MANIFEST_URL));
    Serial.println("[OTA] Manifest error: " + manifestError);
    if (manifestError.length() == 0) manifestError = "Bad manifest";
    showOtaMessage("OTA Error", manifestError);
    delay(2000);
    return false;
  }

  Serial.println("[OTA] Current build: " + String(CURRENT_BUILD_ID));
  Serial.println("[OTA] Remote build: " + remoteBuildId);
  Serial.println("[OTA] BIN URL: " + binUrl);

  if (!isRemoteBuildNewer(remoteBuildId)) {
    showOtaMessage("Device Software", "Up To Date!");
    delay(2500);
    return true;
  }

  showOtaMessage("Update Found", remoteBuildId);
  delay(1200);
  return performFirmwareUpdate(binUrl);
}

void runOtaMode() {
  if (!otaStarted) {
    otaStarted = true;
    Serial.println("[OTA] Free heap before btStop(): " + String(ESP.getFreeHeap()));
    btStop();
    delay(500);
    Serial.println("[OTA] Free heap after btStop(): " + String(ESP.getFreeHeap()));

    bool handledOnline = checkForGitHubUpdate();
    if (handledOnline) {
      otaStarted = false;
      currentState = MENU;
      return;
    }

    startManualOtaServer();
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("OTA MODE ON");
    display.println("Open Safari:");
    display.println(WiFi.localIP().toString());
    display.display();
  }

  if (otaServerRunning) server.handleClient();

  if (longPress) {
    stopManualOtaServer();
    otaStarted = false;
    currentState = MENU;
  }
}
