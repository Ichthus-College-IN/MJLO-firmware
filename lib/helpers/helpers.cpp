#include <Arduino.h>

#include "helpers.h"

bool isHexString(const String &str) {
    for (char c : str) {
        if (!isHexadecimalDigit(c))
            return false;
    }
    return true;
}

void hexStringToByteArray(const char* hexString, uint8_t* byteArray, size_t byteArraySize) {
    for (size_t i = 0; i < byteArraySize / 2; i++) {
        sscanf(hexString + i * 2, "%2hhx", &byteArray[i]);
    }
}

String byteArrayToHexString(const uint8_t* byteArray, size_t byteArraySize) {
    String result = "";
    for (size_t i = 0; i < byteArraySize; i++) {
        char hexBuffer[3];
        snprintf(hexBuffer, sizeof(hexBuffer), "%02X", byteArray[i]);
        result += hexBuffer;
    }
    return result;
}

uint32_t hexStringToUint32(const char* hexString) {
    return strtoul(hexString, NULL, 16);
}

uint64_t hexStringToUint64(const char* hexString) {
    return strtoull(hexString, NULL, 16);
}