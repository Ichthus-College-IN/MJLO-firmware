#ifndef _FLASH_H
#define _FLASH_H

#include <Arduino.h>
#include "config.h"
#include <LittleFS.h>

void checkAvailableStorage(const char *dateBuf) {
    File root = LittleFS.open("/");
    File file = root.openNextFile();

    while (LittleFS.usedBytes() > 0.8 * LittleFS.totalBytes()) {
        if(!file)
            break;

        String path = file.path();
        file.close();

        Serial.printf("Removing file %s (%d)\r\n", path.c_str(), path.length());
        if(!LittleFS.remove(path)) {
            Serial.printf("Failed to remove file\r\n");
        }
        file = root.openNextFile();
    }
    
    char fileBuf[16];
    sprintf(fileBuf, "/%s.csv", dateBuf);
    Serial.printf("Opening file %s for appending\r\n", fileBuf);
    file = LittleFS.open(fileBuf, "a");
    if (!file.size()) {
        Serial.printf("Created new file\r\n");
        file.println("time,dev_eui,f_port,PayloadHex");
    }
    file.close();
}

#endif