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

/*

...                       K

000 = 10m + OTAA + GNSS | uplink
100 = 30s + OTAA + GNSS | uplink
010 = 10m +  ABP + GNSS | uplink
110 = 30s +  ABP + GNSS | uplink
001 = 10m + OTAA + _    | uplink
101 = 30s + OTAA + _    | uplink
011 = 30s + WiFi + _    | WiFi
111 =  1s + WiFi + _    | WiFi

*/