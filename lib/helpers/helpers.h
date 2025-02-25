#ifndef _HELPERS_H
#define _HELPERS_H

#include <Arduino.h>

bool isHexString(const String &str);

void hexStringToByteArray(const char* hexString, uint8_t* byteArray, size_t byteArraySize);

String byteArrayToHexString(const uint8_t* byteArray, size_t byteArraySize);

uint32_t hexStringToUint32(const char* hexString);

uint64_t hexStringToUint64(const char* hexString);

#endif