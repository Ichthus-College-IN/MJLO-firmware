// #include <Arduino.h>
// #include <Adafruit_GFX.h>
// #include <Adafruit_ST7735.h>
// #include <TinyGPSPlus.h>

// SPIClass spiST(HSPI);
// Adafruit_ST7735 st7735 = Adafruit_ST7735(&spiST, 38, 40, 39);

// TinyGPSPlus gps;

// bool gpsFixCur = false;
// bool doGpsRst = false;


// void VextOn(void) {
//     // pinMode(35, OUTPUT);
//     // digitalWrite(35, HIGH);
//     pinMode(3, OUTPUT);
//     digitalWrite(3, HIGH);  // active HIGH
//     pinMode(21, OUTPUT);
//     digitalWrite(21, HIGH);  // active HIGH
// }

// void VextOff(void) {
//     // pinMode(35, OUTPUT);
//     // digitalWrite(35, LOW);
//     // pinMode(3, OUTPUT);
//     // digitalWrite(3, LOW);
//     // pinMode(21, OUTPUT);
//     // digitalWrite(21, LOW);
// }

// bool ledEnabled = false;
// long lastPPS = 0;
// void onPPS() {
//     ledEnabled = !ledEnabled;
//     digitalWrite(18, ledEnabled);
//     if (lastPPS < millis() - 1000) {
//         Serial.println(millis());
//     }
//     lastPPS = millis();
// }

// void setup() {
//     Serial.begin(115200);
//     delay(5000);

//     VextOn();
//     delay(50);
//     spiST.begin(41, -1, 42, 38);            // SCK/CLK, MISO, MOSI, NSS/CS
//     st7735.initR(INITR_MINI160x80_PLUGIN);  // initialize ST7735S chip, mini display
//     st7735.setRotation(2);
//     st7735.fillScreen(ST7735_BLACK);
//     st7735.drawFastHLine(0, 93, 80, ST7735_WHITE);

//     Serial1.begin(115200, SERIAL_8N1, 33, 34);  // open GPS comms
    
// }

// String types[] = {
//     "$GNGSA",
//     "$GPGSV",
//     "$GLGSV",
//     "$GBGS1",
//     "$GAGSV",
//     "$GQGSV",
//     "$GNTXT",
//     "$GNGGA",
//     "$GNRMC",
//     "$GBGSV"
// };
// //$GBGS,1,00,8*6C


// void displayGPS() {
//     st7735.setTextColor(ST7735_WHITE);
//     st7735.fillRect(30, 96, 50, 30, ST7735_BLACK);
//     st7735.setCursor(2,  96);
//     st7735.printf("Lat: %8.04f", gps.location.lat());
//     st7735.setCursor(2, 106);
//     st7735.printf("Lng: %8.04f", gps.location.lng());
//     st7735.setCursor(2, 116);
//     st7735.printf("Alt: %6.2f m", gps.altitude.meters());
//     st7735.fillRect(30, 126, 50, 20, ST7735_BLACK);
//     st7735.setCursor(2, 126);
//     st7735.printf("HDOP: %7.1f", gps.hdop.hdop());
//     st7735.setCursor(2, 136);
//     st7735.printf("Sats: %7d", gps.satellites.value());
//     st7735.fillRect(4, 146, 50, 8, ST7735_BLACK);
//     st7735.setCursor(5, 146);
//     st7735.printf("%02d:%02d:%02d UTC", gps.time.hour(), gps.time.minute(), gps.time.second());

//     Serial.printf("[%04d-%02d-%02d / %02d:%02d:%02d] ", 
//                     gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second());
//     Serial.printf("Lat: %8.5f | Lng: %8.5f | Alt: %5.2f | Sats: %d | HDOP: %5.2f\r\n", 
//                     gps.location.lat(), gps.location.lng(), gps.altitude.meters(), gps.satellites.value(), gps.hdop.hdop());

// }

// void loop() {
//     while (Serial1.available()) {
//         String input = Serial1.readStringUntil('\n');
//         for (char c : input) {
//             gps.encode(c);
//         }
//         // for (int i = 0; i < 10; i++) {
//         //     if (input.startsWith(types[i])) {
//         //         Serial.print(".");
//         //         return;
//         //     }
//         // }
//         Serial.println(input);
//     }
//     delay(1000);
//     displayGPS();

// }
