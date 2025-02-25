#include <Arduino.h>

#include <Wire.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2591.h>
#include <Adafruit_VEML6070.h>
#include <Adafruit_BME680.h>

Adafruit_TSL2591 tsl = Adafruit_TSL2591();
Adafruit_VEML6070 veml = Adafruit_VEML6070();
Adafruit_BME680 bme = Adafruit_BME680(&Wire);

uint32_t full, lumi, uva;

void Scanner()
{
  Serial.println();
  Serial.println("I2C scanner. Scanning ...");
  uint8_t address = 0;

  for (uint8_t i = 8; i < 120; i++)
  {
    Wire.beginTransmission(i);          // Begin I2C transmission Address (i)
    if (Wire.endTransmission() == 0)  // Receive 0 = success (ACK response) 
    {
      Serial.print("Found address: ");
      Serial.print(i, DEC);
      Serial.print(" (0x");
      Serial.print(i, HEX);     // PCF8574 7 bit address
      Serial.println(")");
      address = i;
    }
  }

  switch(address) {
    case(41):
      // TSL2591
      tsl.begin(&Wire, 41);
      full = tsl.getFullLuminosity();
      lumi = tsl.calculateLux(full & 0xFFFF, full >> 16);
      Serial.println(lumi);
      break;
    case(56):
    case(57):
      // VEML6070
      veml.begin(VEML6070_1_T, &Wire);
      delay(100);
      uva = veml.readUV();
      veml.sleep(true);
      Serial.println(uva);
      break;
    case(118):
    case(119):
      bme.begin();
      bme.performReading();
      Serial.println(bme.temperature);
      Serial.println(bme.pressure / 100.0);
      Serial.println(bme.humidity);
      break;
    default:
      Serial.println("No I2C device found");
      break;
  }

}

bool btnFlag = false;

IRAM_ATTR void btnCallback() {
  btnFlag = true;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(16, 15);
  pinMode(35, OUTPUT);

  attachInterrupt(0, btnCallback, FALLING);
  delay(2000);
  Serial.println("Test");
}

void loop() {
  if(btnFlag) {
    Scanner();
    btnFlag = false;
  }
  delay(100);
}