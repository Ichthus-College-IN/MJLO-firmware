#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2CScd4x.h>
#include "pins.h"

SensirionI2cScd4x scd4x;
uint16_t co2;
float scd_temp, scd_hum;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Wire.begin(SDA0, SCL0);

  scd4x.begin(Wire, 0x62);
  scd4x.wakeUp();
  scd4x.stopPeriodicMeasurement();
  // scd4x.setAutomaticSelfCalibrationEnabled(1);
  // scd4x.setAutomaticSelfCalibrationTarget((uint16_t)(400 + 25));
  // scd4x.setAutomaticSelfCalibrationStandardPeriod((uint16_t)56);
  // scd4x.persistSettings();
}

void loop() {
  scd4x.begin(Wire, 0x62);
  scd4x.wakeUp();
  uint32_t start = millis();
  scd4x.stopPeriodicMeasurement();
  uint32_t stop = millis();
  scd4x.setAmbientPressureRaw(1016);
  uint16_t en = false;
  scd4x.getAutomaticSelfCalibrationEnabled(en);
  Serial.printf("ASC: %s\n", en ? "Y" : "N");

  // manually call the measureSingleShot() registers as the library does a blocking call
  uint8_t buffer_ptr[9] = { 0 };
  SensirionI2CTxFrame txFrame =
      SensirionI2CTxFrame::createWithUInt16Command(0x219d, buffer_ptr, 2);
  (void)SensirionI2CCommunication::sendFrame(0x62, txFrame, Wire);
  

  bool co2_ready = false;
  while(!co2_ready) {
    (void)scd4x.getDataReadyStatus(co2_ready);
  }


  if(co2_ready) {
    (void)scd4x.readMeasurement(co2, scd_temp, scd_hum);
    // (void)scd4x.powerDown();
    Serial.printf("CO2: %d\r\n", co2);
  } else {
    Serial.println("Nack");
  }
  Serial.printf("Duration: %d ms\n", stop - start);

  delay(30000);
}