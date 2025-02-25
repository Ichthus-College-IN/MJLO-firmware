#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    pinMode(47, INPUT_PULLUP);
    pinMode(48, INPUT_PULLUP);
    pinMode(26, INPUT_PULLUP);
    pinMode(3, INPUT_PULLUP);
}

void loop() {
    bool dip1 = digitalRead(47);
    bool dip2 = digitalRead(48);
    bool dip3 = digitalRead(26);
    Serial.printf("%d %d %d \r\n", dip1, dip2, dip3);

    delay(2000);
}