#include <fabgl.h>

using namespace fabgl;

// VGA pins
#define VGA_RED    GPIO_NUM_25
#define VGA_GREEN  GPIO_NUM_26
#define VGA_BLUE   GPIO_NUM_27
#define VGA_HSYNC  GPIO_NUM_18
#define VGA_VSYNC  GPIO_NUM_19

VGAController DisplayController;
Terminal VGATerminal;

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

  // Attach terminal to VGA controller
  VGATerminal.begin(&DisplayController);

  VGATerminal.write("\033[2J");
  VGATerminal.write("\033[H");

  VGATerminal.write("================================\r\n");
  VGATerminal.write("       ESP32 COMPUTER BOOT\r\n");
  VGATerminal.write("================================\r\n\r\n");

  VGATerminal.write("VGA: OK\r\n");
  VGATerminal.write("SERIAL: OK\r\n");
  VGATerminal.write("CPU: ESP32\r\n");
  VGATerminal.write("MEMORY: OK\r\n\r\n");

  VGATerminal.write("ESP32 COMPUTER READY\r\n");
  VGATerminal.write("> ");
}

void loop()
{
  while (Serial.available())
  {
    char c = Serial.read();

    // Send character to VGA terminal
    VGATerminal.write(c);

    // Echo back to Serial
    Serial.write(c);
  }
}