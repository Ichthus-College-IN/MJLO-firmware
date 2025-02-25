#include <Arduino.h>
#include <Wire.h>
// #include <BMI160.h>

// BMI160Class bmi;

bool isMotion = false;
IRAM_ATTR void onMotion(void) {
  isMotion = true;
}

void setup()
{
  Serial.begin(115200);  
  Wire.begin(43, 44);
  delay(5000);
}
   
void Scanner()
{
  Serial.println();
  Serial.println("I2C scanner. Scanning ...");
  byte count = 0;

  for (byte i = 8; i < 120; i++)
  {
    Wire.beginTransmission(i);          // Begin I2C transmission Address (i)
    if (Wire.endTransmission() == 0)  // Receive 0 = success (ACK response) 
    {
      Serial.print("Found address: ");
      Serial.print(i, DEC);
      Serial.print(" (0x");
      Serial.print(i, HEX);     // PCF8574 7 bit address
      Serial.println(")");
      count++;
    }
  }
  Serial.print("Found ");      
  Serial.print(count, DEC);        // numbers of devices
  Serial.println(" device(s).");
}

void loop()
{
    Scanner();
    // bmi.initialize();
    // // bmi.accelStepDetect();
    // bmi.accelAnyMotion();
    // attachInterrupt(5, onMotion, RISING);           // accelerometer
    // while(1) {
    //     if(isMotion) {
    //         Serial.println("Bump");
    //         isMotion = false;
    //     }
    //     if(Serial.available()) {
    //         (void)Serial.read();
    //         Serial.flush();
    //         break;
    //     }
    // }
    // esp_sleep_enable_timer_wakeup(600 * 1000 * 1000);
    // esp_deep_sleep_start();
    delay(5000);
}