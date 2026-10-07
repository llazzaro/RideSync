#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  Serial.println("RideSync: repository bring-up only; camera control is not implemented.");
}

void loop() {
  // Yield to the Arduino/FreeRTOS scheduler. No peripheral pins are driven yet.
  delay(1);
}
