#include <fabgl.h>

using namespace fabgl;

// VGA pins
#define VGA_RED    GPIO_NUM_25
#define VGA_GREEN  GPIO_NUM_26
#define VGA_BLUE   GPIO_NUM_27
#define VGA_HSYNC  GPIO_NUM_18
#define VGA_VSYNC  GPIO_NUM_19

VGAController DisplayController;
Terminal Terminal(&DisplayController);

void setup()
{
  // Serial input/output
  Serial.begin(115200);
  delay(500);

  // Start VGA
  DisplayController.begin(
    VGA_RED,
    VGA_GREEN,
    VGA_BLUE,
    VGA_HSYNC,
    VGA_VSYNC
  );

  // 640x480 @ 60 Hz
  DisplayController.setResolution(VGA_640x480_60Hz);

  // Start text terminal
  Terminal.begin();

  Terminal.write("\033[2J");   // Clear screen
  Terminal.write("\033[H");    // Cursor home

  Terminal.write("================================\r\n");
  Terminal.write("       ESP32 COMPUTER BOOT\r\n");
  Terminal.write("================================\r\n\r\n");

  Terminal.write("VGA: OK\r\n");
  Terminal.write("SERIAL: OK\r\n");
  Terminal.write("CPU: ESP32\r\n");
  Terminal.write("MEMORY: OK\r\n\r\n");

  Terminal.write("ESP32 COMPUTER READY\r\n");
  Terminal.write("> ");
}

void loop()
{
  // Anything typed into Serial Monitor
  // gets displayed on the VGA screen.

  while (Serial.available())
  {
    char c = Serial.read();

    // Send character to VGA terminal
    Terminal.write(c);

    // Echo it back to Serial Monitor too
    Serial.write(c);
  }
}