#include <fabgl.h>
using namespace fabgl;

#define VGA_RED    GPIO_NUM_25
#define VGA_GREEN  GPIO_NUM_26
#define VGA_BLUE   GPIO_NUM_27
#define VGA_HSYNC  GPIO_NUM_18
#define VGA_VSYNC  GPIO_NUM_19

VGAController DisplayController;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("       ESP32 VGA DIAGNOSTIC");
  Serial.println("================================");

  Serial.println("BOOT");
  delay(500);

  Serial.println("BEFORE VGA BEGIN");
  delay(500);

  DisplayController.begin(
    VGA_RED,
    VGA_GREEN,
    VGA_BLUE,
    VGA_HSYNC,
    VGA_VSYNC
  );

  Serial.println("AFTER VGA BEGIN");
  delay(1000);

  DisplayController.setResolution(VGA_640x480_60Hz);

  Serial.println("AFTER RESOLUTION");
  delay(1000);

  Serial.println("VGA INITIALIZATION COMPLETE");
  Serial.println("ESP32 IS RUNNING");
}

void loop() {
  Serial.println("ALIVE");
  delay(1000);
}