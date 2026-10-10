#ifndef _ACCELEROMETER_H
#define _ACCELEROMETER_H

#include <Wire.h>
#include <SPI.h>

// the libraries do not expose the register context that we need for motion detection
#define private public
#include <LSM6DSRSensor.h>
#include <LSM6DSV16XSensor.h>
#undef private

LSM6DSRSensor lsm6dsr(&Wire, LSM6DSR_I2C_ADD_H);
LSM6DSV16XSensor lsm6dsv(&Wire, LSM6DSV16X_I2C_ADD_H);
bool accelV = false;  // LSM6DSV (true) or LSM6DSR (false), detected at runtime

template <class T> void accelInit(T &s) {
  // 1270 uA default
  s.Enable_X();

  // 372 uA
  s.Disable_G();

  // 20 uA
  s.Set_X_ODR(12.5);

  // 12 uA
  // s.Disable_X();
}

void accelSetup() {
  // both share the same I2C address, so tell them apart by WHO_AM_I
  Wire.beginTransmission(LSM6DSR_I2C_ADD_H >> 1);
  Wire.write(0x0F);
  Wire.endTransmission(false);
  Wire.requestFrom(LSM6DSR_I2C_ADD_H >> 1, 1);
  accelV = Wire.read() == LSM6DSV16X_ID;

  int status = accelV ? (int)lsm6dsv.begin() : (int)lsm6dsr.begin();
  if(status != 0) {
    Serial.printf("Failed to initalize %s: status %d\r\n", accelV ? "LSM6DSV" : "LSM6DSR", status);
    return;
  }
  if(accelV) accelInit(lsm6dsv);
  else       accelInit(lsm6dsr);
}

void accelAnyMotionV() {
  lsm6dsv16x_ctx_t *ctx = &lsm6dsv.reg_ctx;

  lsm6dsv16x_act_thresholds_t ths = {};
  ths.inactivity_cfg.wu_inact_ths_w = 2;    // 31.25 mg / LSb
  ths.threshold = 12;                       // 12 * 31.25 mg = 0.375g
  lsm6dsv16x_act_thresholds_set(ctx, &ths);

  // pulse INT per event rather than latching it, and enable the basic interrupts
  lsm6dsv16x_interrupt_mode_t mode = { .enable = 1, .lir = 0 };
  lsm6dsv16x_interrupt_enable_set(ctx, mode);

  // ACC_INT follows INT1, so unroute INT2
  lsm6dsv16x_pin_int_route_t route = {};
  lsm6dsv16x_pin_int2_route_set(ctx, &route);
  route.wakeup = PROPERTY_ENABLE;
  lsm6dsv16x_pin_int1_route_set(ctx, &route);
}

void accelAnyMotion() {
  if(accelV) return accelAnyMotionV();

  lsm6dsr_wkup_ths_weight_set(&(lsm6dsr.reg_ctx), LSM6DSR_LSb_FS_DIV_64);   // FS = 2g
  lsm6dsr_wkup_threshold_set(&(lsm6dsr.reg_ctx), 0b00001100);               // 12/64 * 2g = 0.375g
  lsm6dsr_wkup_dur_set(&(lsm6dsr.reg_ctx), 0);
  lsm6dsr_act_sleep_dur_set(&(lsm6dsr.reg_ctx), 0);

  // pulse INT per event rather than latching it until the source register is read
  lsm6dsr_int_notification_set(&(lsm6dsr.reg_ctx), LSM6DSR_ALL_INT_PULSED);

  // ACC_INT follows INT1, so unroute INT2 - this has to happen before
  // arming INT1, because both setters rewrite the shared TAP_CFG2 interrupts_enable bit from
  // their own pin only, and clearing INT2 last would switch all interrupts back off again
  lsm6dsr_pin_int2_route_t int2val;
  memset(&int2val, 0, sizeof(int2val));
  lsm6dsr_pin_int2_route_set(&(lsm6dsr.reg_ctx), &int2val);

  // the setter writes INT1_CTRL, MD1_CFG, EMB_FUNC_INT1 and FSM_INT1_A/B
  lsm6dsr_pin_int1_route_t int1val;
  memset(&int1val, 0, sizeof(int1val));
  int1val.md1_cfg.int1_wu = PROPERTY_ENABLE;                // enable wake-up source on INT1 pin
  lsm6dsr_pin_int1_route_set(&(lsm6dsr.reg_ctx), &int1val); // configure the selected wake-up sources on INT1
}

#endif
