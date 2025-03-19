#ifndef _GNSS_H
#define _GNSS_H

#include <TinyGPSPlus.h>
#include <time.h>
#include <sys/time.h>

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

void setSystemTimeFromGPS() {
	if (gps.date.isValid() && gps.time.isValid()) {
		struct tm t;
		t.tm_year = gps.date.year() - 1900;  	// years since 1900
		t.tm_mon  = gps.date.month() - 1;   	// months are 0-based in struct tm
		t.tm_mday = gps.date.day();
		t.tm_hour = gps.time.hour();
		t.tm_min  = gps.time.minute();
		t.tm_sec  = gps.time.second();
		t.tm_isdst = 0;  								// ignore daylight saving

		time_t epochTime = mktime(&t);  // convert to epoch time
		
		struct timeval tv;
		tv.tv_sec = epochTime;
		tv.tv_usec = 0;
		settimeofday(&tv, NULL);  			// set system time
		
		Serial.println("System time updated from GNSS");
	} else {
		Serial.println("GNSS time not available yet");
	}
}

#endif