#ifndef _ACCELEROMETER_H
#define _ACCELEROMETER_H

#include <LSM6DSRSensor.h>
#include <Wire.h>

LSM6DSRSensor lsm6dsr(&Wire, LSM6DSR_I2C_ADD_H);

void accelSetup() {
  int status = lsm6dsr.begin();
  if(status != 0) {
    Serial.printf("Failed to initalize LSM6DSR: status %d\r\n", status);
    return;
  }
  // 1270 uA default
  lsm6dsr.Enable_X();

  // 372 uA
  lsm6dsr.Disable_G();

  // 20 uA
  lsm6dsr.Set_X_ODR(12.5);

  // 12 uA
  // lsm6dsr.Disable_X();
}

void accelAnyMotion() {
  lsm6dsr_wkup_ths_weight_set(&(lsm6dsr.reg_ctx), LSM6DSR_LSb_FS_DIV_64);   // FS = 2g
  lsm6dsr_wkup_threshold_set(&(lsm6dsr.reg_ctx), 0b00001100);               // 12/64 * 2g = 0.375g
  lsm6dsr_wkup_dur_set(&(lsm6dsr.reg_ctx), 0);
  lsm6dsr_act_sleep_dur_set(&(lsm6dsr.reg_ctx), 0);

  lsm6dsr_pin_int1_route_t int1val;
  int1val.md1_cfg.int1_wu = PROPERTY_ENABLE;                // enable wake-up source on INT1 pin
  lsm6dsr_pin_int1_route_set(&(lsm6dsr.reg_ctx), &int1val); // configure the selected wake-up sources on INT1
}

#endif