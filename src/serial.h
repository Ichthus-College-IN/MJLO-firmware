#ifndef _SERIAL_H
#define _SERIAL_H

#include <Arduino.h>

#include "config.h"
#include "ble.h"

String cmdSerial = "";

void handleSerialUSB() {
  while (Serial.available()) {
    // read a single character
    char input = Serial.read();

    // limit to 71 characters input, flush everything if exceeded
    if (cmdSerial.length() > 71) {
      Serial.printf("\r\nMaximum input length exceeded (71 characters)\r\n");
      Serial.flush();
      cmdSerial = "";
      continue;
    }

    bool complete = false;

    // arrow keys:
    // up 91 65
    // dn 91 99
    // rt 91 67
    // lt 91 68

    switch(input) {
      case 0 ... 7:
        break;
      case 8:   // backspace
        if(cmdSerial.length() > 0) {
          cmdSerial.remove(cmdSerial.length() - 1);
          Serial.print("\b");   // backspace
          Serial.print(" ");    // overwrite
          Serial.print("\b");   // backspace
        }
        break;
      case 9:   // tab
        break;
      case 10:  // newline
        if(cmdSerial.length() > 0) {
          complete = true;
        }
        break;
      case 11 ... 26:
        break;
      case 27:
        for(int i = 0; i < cmdSerial.length(); i++) {
          Serial.print('\b');
        }
        for(int i = 0; i < cmdSerial.length(); i++) {
          Serial.print(' ');
        }
        for(int i = 0; i < cmdSerial.length(); i++) {
          Serial.print('\b');
        }
        Serial.println();
        cmdSerial = "";
        break;
      case 28 ... 31:
        break;
      default:
        cmdSerial += input;
        Serial.print(input);
        break;
    }

    if(complete) {
      Serial.println();

      int errorCode = execCommand(cmdSerial);
      String errorString = parseError(errorCode);
      Serial.println(errorString);
      Serial.flush();
      
      cmdSerial = "";
    }
  }
}

#endif