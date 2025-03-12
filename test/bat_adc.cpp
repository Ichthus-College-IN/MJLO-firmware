#include <Arduino.h>
#include "pins.h"

void setup() {
    Serial.begin(115200);
    pinMode(BAT_ADC, INPUT);
    pinMode(BAT_CTRL, INPUT_PULLUP);
}

void loop() {
    uint16_t battMillivolts = analogReadMilliVolts(BAT_ADC) * 4.9f;
    Serial.printf("Battery: %d mV\r\n", battMillivolts);
    delay(1000);
}