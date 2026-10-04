#include <fabgl.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("================================");
  Serial.println("FABGL TEST");
  Serial.println("================================");
  Serial.println("FabGL library loaded");
}

void loop() {
  Serial.println("ALIVE");
  delay(1000);
}