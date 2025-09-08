/*--------------------------------------------------------------------
  This file is originally part of the TTN-Apeldoorn Sound Sensor,
  developed by Marcel Meek and Remko Welling.
  It is modified and updated for MJLO by Steven Boonstoppel.

  This code is free software:
  you can redistribute it and/or modify it under the terms of a Creative
  Commons Attribution-NonCommercial 4.0 International License
  (http://creativecommons.org/licenses/by-nc/4.0/) by
  TTN-Apeldoorn (https://www.thethingsnetwork.org/community/apeldoorn/) 

  The program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  --------------------------------------------------------------------*/

#include "soundsensor.h"
#include "arduinoFFT.h"

const i2s_port_t I2S_PORT = I2S_NUM_0;
i2s_chan_handle_t rx_chan = NULL;


SoundSensor::SoundSensor() {
  _fft = new ArduinoFFT<float>(_real, _imag, SAMPLES, SAMPLES);
  _runningDC = 0.0;
  _runningN = 0;
  offset( 0.0);
}

SoundSensor::~SoundSensor() {
  delete _fft;
}

void SoundSensor::begin(int bclk, int lrclk, int din){
  // https://esp32.com/viewtopic.php?f=18&t=35402
  i2s_chan_config_t rx_chan_cfg = { 
    .id = I2S_PORT, 
    .role = I2S_ROLE_MASTER, 
    .dma_desc_num = 8, 
    .dma_frame_num = 1024, 
    .auto_clear_after_cb = false, 
    .auto_clear_before_cb = false, 
    .intr_priority = 0
  };
  rx_chan_cfg.auto_clear = true;
  ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));
  
  i2s_std_config_t rx_std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_FREQ),
    .slot_cfg = { 
      .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT, 
      .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO, 
      .slot_mode = I2S_SLOT_MODE_MONO, 
      .slot_mask = I2S_STD_SLOT_LEFT, 
      .ws_width = I2S_DATA_BIT_WIDTH_32BIT, 
      .ws_pol = false, 
      .bit_shift = true, 
      .left_align = true, 
      .big_endian = false, 
      .bit_order_lsb = false
    },
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)bclk,
      .ws = (gpio_num_t)lrclk,
      .dout = I2S_GPIO_UNUSED,
      .din = (gpio_num_t)din,
      .invert_flags = {
          .mclk_inv = false,
          .bclk_inv = false,
          .ws_inv = false,
      },
    },
  };
  
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

  printf("I2S driver installed.\n");
}

void SoundSensor::disable() {
  _err = i2s_channel_disable(rx_chan);
  if (_err != ESP_OK) {
      printf("Failed uninstalling I2S driver: %d\n", _err);
      while (true);
  }
  printf("I2S driver uninstalled.\n");
}

float* SoundSensor::readSamples() {
  // Read multiple samples at once and calculate the sound pressure
  
  size_t num_bytes_read;
  _err = i2s_channel_read(rx_chan, (uint8_t *)_samples, BLOCK_SIZE * sizeof(int32_t), &num_bytes_read, portMAX_DELAY);
  
  if(_err != ESP_OK){
      printf("%d err\n",_err);
  }

  integerToFloat(_samples, _real, _imag, SAMPLES);

  // apply HANN window, optimal for energy calculations
  _fft->windowing(FFT_WIN_TYP_HANN, FFT_FORWARD);
  
  // do FFT processing
  _fft->compute(FFT_FORWARD);

  // calculate energy in each bin
  calculateEnergy(_real, _imag, SAMPLES);

  // sum up energy in bin for each octave
  sumEnergy(_real, _energy);

  return _energy;
}

// convert WAV integers to float
// convert 24 High bits from I2S buffer to float and divide * 256 
// remove DC offset, necessary for some MEMS microphones 
/*void SoundSensor::integerToFloat(int32_t * samples, float *vReal, float *vImag, uint16_t size) {
  float sum = 0.0;
  for (uint16_t i = 0; i < size; i++) {
    int32_t val = (samples[i] >> 8);            // move 24 value bits on the correct place in a long
    sum += (float)val;
    samples[i] = (val - _offset ) << 8;         // DC component removed, and move back to original buffer
    vReal[i] = (float)val / (256.0 * FACTOR);   // adjustment
    vImag[i] = 0.0;
  }
  _offset = sum / size;   //dc component
  //printf("DC offset %d\n", offset);
}*/

void SoundSensor::integerToFloat(int32_t * samples, float *vReal, float *vImag, uint16_t size) {
    float sum = 0.0;
    // calculate offset
    for (uint16_t i = 0; i < size; i++) {
        int32_t val = (samples[i] >> 8);            // move 24 value bits on the correct place in a long
        vReal[i] = (float)val;
        sum += vReal[i];
    }
    float offs = sum / (float)size;   //dc component
    if( _runningN < 100)
        _runningN++;
    float newDC = _runningDC + (offs - _runningDC)/_runningN;
    _runningDC = newDC;

    for (uint16_t i = 0; i < size; i++) {
        vReal[i] = (vReal[i] - newDC) / (256.0 * FACTOR / _factor);   // 30.0 adjustment
        vImag[i] = 0.0;
    }
    //printf("DC offset %f\n", newDC);
}


// calculates energy from Re and Im parts and places it back in the Re part (Im part is zeroed)
void SoundSensor::calculateEnergy(float *vReal, float *vImag, uint16_t samples)
{
    for (uint16_t i = 0; i < samples; i++) {
        vReal[i] = sq(vReal[i]) + sq(vImag[i]);
        vImag[i] = 0.0;
    }
}

// convert dB offset to factor
void SoundSensor::offset( float dB) {
    _factor = pow(10, dB / 20.0);    // convert dB to factor 
}

// sums up energy in whole octave bins
void SoundSensor::sumEnergy(const float *samples, float *energies) {

    // skip the first two bins
    int bin_size = 2;
    int bin = bin_size;
    for (int octave = 0; octave < OCTAVES; octave++){
        float sum = 0.0;
        for (int i = 0; i < bin_size; i++){
        sum += samples[bin++];
        }
        energies[octave] = sum;
        bin_size *= 2;
        //printf("octaaf=%d, bin=%d, sum=%f\n", octave, bin-1, sum);
    }
}