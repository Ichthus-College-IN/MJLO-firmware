#include <Arduino.h>
#include "soundsensor.h"
#include "measurement.h"
#include "pins.h"

SoundSensor mic;

static float zweighting[] = Z_WEIGHTING;		// weighting lists
static Measurement zMeasurement( zweighting);	// measurement buffers

float db_min, db_avg, db_max;
long startMic, lastUpdate;
bool reset = false;

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("Starting microphone test");

  mic.offset(-1.8);		// for SPH0645
	mic.begin(BCLK, LRCLK, DIN);
	startMic = millis();
}

void loop() {

  float* energy = mic.readSamples();
  zMeasurement.update(energy);

  if(millis() - lastUpdate > 1000) {
    zMeasurement.calculate();
    db_min = zMeasurement.min;
    db_avg = zMeasurement.avg;
    db_max = zMeasurement.max;
    Serial.printf("dB: [%.1f | %.1f | %.1f]\r\n", db_min, db_avg, db_max);
    lastUpdate = millis();
  }

  // if(millis() - startMic > 3000) {
  //   if(!reset) {
  //     zMeasurement.reset();
  //     reset = true;
  //   }
  // }
}
