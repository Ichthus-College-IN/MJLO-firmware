#ifndef _GNSS_H
#define _GNSS_H

#include <TinyGPSPlus.h>

// GPS class
TinyGPSPlus gps;

enum GpsFixLevel {
    GPS_NO_FIX,
    GPS_BAD_FIX,
    GPS_GOOD_FIX
};

int gpsFixLevel = GPS_NO_FIX;
int prevFixLevel = GPS_NO_FIX;

char dateBuf[11], newDateBuf[11], timeBuf[9];
int numConsecutiveFix = 0;

#endif