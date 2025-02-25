#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_BMI270_Arduino_Library.h>

#include "pins.h"

BMI270 bmi;
bool isMotion = false;

IRAM_ATTR void onMotion(void) {
  isMotion = true;
}

void setup() {
    Serial.begin(115200);
    Wire.begin(SDA0, SCL0);

    bmi.beginI2C(0x68);
    bmi.enableFeature(BMI2_ANY_MOTION);
    bmi.mapInterruptToPin(BMI2_ANY_MOTION_INT, BMI2_INT1);

    bmi2_sens_config anyMotionConfig;
    anyMotionConfig.type = BMI2_ANY_MOTION;
    anyMotionConfig.cfg.any_motion.duration = 1;
    anyMotionConfig.cfg.any_motion.threshold = 150;
    anyMotionConfig.cfg.any_motion.select_x = BMI2_ENABLE;
    anyMotionConfig.cfg.any_motion.select_y = BMI2_ENABLE;
    anyMotionConfig.cfg.any_motion.select_z = BMI2_ENABLE;
    bmi.setConfig(anyMotionConfig);
    
    bmi2_int_pin_config intPinConfig;
    intPinConfig.pin_type = BMI2_INT1;
    intPinConfig.int_latch = BMI2_INT_NON_LATCH;
    intPinConfig.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
    intPinConfig.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
    intPinConfig.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    intPinConfig.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;
    bmi.setInterruptPinConfig(intPinConfig);

    attachInterrupt(ACC_INT, onMotion, RISING);       // accelerometer
}

void loop() {
    if(isMotion) {
        Serial.println("Motion");
        isMotion = false;
    }
}