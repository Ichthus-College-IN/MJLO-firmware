#ifndef _DISPLAY_H
#define _DISPLAY_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "config.h"
#include "lorawan.h"
#include "gnss.h"
#include "ble.h"
#include "fs_browser.h"
#include "Display_BMPs.h"

#define PRINTST7735(x, y, force, format, oldVal, newValFunc) \
  do { \
    auto newVar = newValFunc(); \
    if(newVar != oldVal || force) { \
      st7735.setCursor(x, y); \
      st7735.printf(format, newVar); \
      oldVal = newVar; \
    } \
  } while (0)

#define MAX_NUM_ROWS 16
#define X_ADVANCE     6
#define Y_ADVANCE     8

uint32_t displayTimout = 2500;

SPIClass spiST(HSPI);
Adafruit_ST7735 st7735 = Adafruit_ST7735(&spiST, 38, 40, 39);
uint32_t lastDisplayUpdate = 0;

double oldLat = -1, oldLng = -1, oldAlt = -1, oldHdop = -1;
uint32_t oldSats = 0xFFFF;
uint8_t oldH = 0xFF, oldM = 0xFF, oldS = 0xFF;

char devAddrText[15];

enum DisplaySymbols {
  SYMBOL_LORAWAN,
  SYMBOL_GPS,
  SYMBOL_CONN
};

enum DisplayStyles {
  DISPLAY_COMPACT,
  DISPLAY_LORAWAN,
  DISPLAY_SIGNAL,
  DISPLAY_GNSS,
  DISPLAY_2_4G,
  NUM_STYLES,
  DISPLAY_MENU  // not user accessible
};

RTC_DATA_ATTR int styleNum = DISPLAY_COMPACT;

class DisplayStyle {
  public:
    virtual int getStyleNum() const { return -1; }

    virtual void displayHeader(int symbol = 0) {
      st7735.setTextSize(1);

      // OTAA or ABP icon
      if(!symbol || symbol == SYMBOL_LORAWAN) {
        st7735.setTextColor(ST7735_BLACK);
        uint16_t bg;
        if(node.isActivated()) {
          bg = 0x97D2;  // mint green
        } else if((cfg.actvn.method == OTAA && isValidGroupOTAA()) || (cfg.actvn.method == ABP && isValidGroupABP())) {
          bg = 0x065F;  // vivid sky blue
        } else {
          bg = 0xFB0F;  // brink pink
        }
        st7735.fillRoundRect(3, 0, 27, 11, 2, bg);
        if (cfg.actvn.method == OTAA) {
          st7735.setCursor(5, 2);
          st7735.printf("OTAA");
        } else {
          st7735.setCursor(8, 2);
          st7735.printf("ABP");
        }
      }

      // GPS icon
      if(!symbol || symbol == SYMBOL_GPS) {
        st7735.setTextColor(ST7735_BLACK);
        uint16_t bg;
        if(gpsFixLevel == GPS_GOOD_FIX) {
          bg = 0x97D2;    // mint green
        } else if(gpsFixLevel == GPS_BAD_FIX) {
          bg = 0x065F;    // vivid sky blue
        } else {          // GPS_NO_FIX
          bg = 0xFB0F;    // brink pink
        }
        st7735.fillRoundRect(32, 0, 21, 11, 2, bg);
        st7735.setCursor(34, 2);
        st7735.printf("GPS");
      }

      // BLE, WiFi or USB icon
      if(!symbol || symbol == SYMBOL_CONN) {
        st7735.setTextColor(ST7735_BLACK);
        uint16_t bg = ST7735_BLACK;
        if(wifiMode != WIFI_MODE_NULL) {
          if(wifiMode == WIFI_MODE_STA && WiFi.isConnected()) {
            bg = 0x97D2;  // mint green
          } else if(wifiMode == WIFI_MODE_STA) {
            bg = 0xFB0F;  // brink pink
          } else {  // AP
            bg = 0x065F;  // vivid sky blue
          }
        } else if(ble.state > BLE_INACTIVE) {
          if(ble.state == BLE_ACTIVE) {
            bg = 0xFFFF;  // white
          } else {  // active connection
            bg = 0x065F;  // vivid sky blue
          }
        } else if(usbState) {
          bg = 0x97D2;    // mint green
        }
        // draw icon
        st7735.fillRoundRect(55, 0, 21, 11, 2, bg);

        if (ble.state > BLE_INACTIVE) {
          st7735.setCursor(57, 2);
          st7735.printf("BLE");
        } else 
        if (wifiMode != WIFI_MODE_NULL) {
          if (wifiMode == WIFI_MODE_STA) {
            st7735.setCursor(57, 2);
            st7735.printf("STA");
          } else {
            st7735.setCursor(60, 2);
            st7735.printf("AP");
          }
        } else
        if (usbState) {
          st7735.setCursor(57, 2);
          st7735.printf("USB");
        }
      }
    }

    virtual void displayBattery() {
      uint16_t battMillivolts = analogReadMilliVolts(1) * 4.9f;
      int width = map(battMillivolts, 2850, 4050, 0, 72);
      width = max(0, min(72, width));
      st7735.fillRect(4, 156, (int16_t)width, 4, ST7735_WHITE);
      st7735.fillRect(4 + (int16_t)width, 156, 72 - (int16_t)width, 4, ST7735_BLACK);
    }
    
    virtual void displayUplink() {
      st7735.fillRect(0, 12, 80, 142, ST7735_BLACK);
      st7735.drawBitmap(0, 12, Display_Uplink, 80, 142, ST7735_WHITE);
      this->displayHeader();
      this->displayBattery();
    }

    virtual void displayLoRaWAN() {}
    virtual void displayLast() {}
    virtual void displayGNSS(bool force = false) {}
    virtual void displayWiFi() {}
    virtual void displayWake() {}
    virtual void displayFull() {}
};

class DisplayCompact : public DisplayStyle {
  public:
    virtual int getStyleNum() const { return DISPLAY_COMPACT; }

    void displayFull() override {
      st7735.fillScreen(ST7735_BLACK);
      st7735.drawBitmap(0, 0, Display_Full, 80, 160, ST7735_WHITE);
      this->displayHeader();
      this->displayLoRaWAN();
      this->displayLast();
      this->displayGNSS(true);
      this->displayBattery();
    }
    
    void displayLoRaWAN() override {
      st7735.setTextSize(1);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);
      st7735.setCursor(38,  14);
      if(cfg.uplink.adr == ADR_ON) {
        st7735.printf("    ADR");
      } else if(cfg.uplink.adr == ADR_DR) {
        st7735.printf("%*d-%d", 6 - String(cfg.uplink.range[0]).length(), cfg.uplink.range[0], cfg.uplink.range[cfg.uplink.rangeLen - 1]);
      } else {
        st7735.printf("%7d", cfg.uplink.dr);
      }
      st7735.setCursor(38,  24);
      if(cfg.uplink.adr == ADR_ON) {
        st7735.printf("    ADR");
      } else if(cfg.uplink.adr == ADR_DBM) {
        st7735.printf("%*d-%d", 6 - String(cfg.uplink.range[0]).length(), cfg.uplink.range[0], cfg.uplink.range[cfg.uplink.rangeLen - 1]);
      } else {
        st7735.printf("%7d", cfg.uplink.dbm);
      }
      
      st7735.setCursor(38,  34);
      st7735.printf("%7s", cfg.uplink.confirmed ? "Yes" : "No");
      st7735.setCursor(38,  44);
      st7735.printf("%7d", node.getFCntUp());
      st7735.setCursor(38,  54);
      st7735.printf("%7d", node.getAFCntDown() + node.getNFCntDown());
      st7735.setCursor(38,  64);
      st7735.printf("%7.0f", wasDownlink ? rssi : 0);
      st7735.setCursor(2,  74);
      if(cfg.interval.fixed) {
        st7735.printf("Int:  %5d s", cfg.interval.period);
      } else {
        String limit = "Err";
        if (cfg.interval.dutycycle == 30)
          limit = "FUP";
        else if (cfg.interval.dutycycle == 86)
          limit = "0.1%";
        else if(cfg.interval.dutycycle == 864)
          limit = "1%";
        else
          limit = String(cfg.interval.dutycycle) + " s";
        st7735.printf("DC:   %7s", limit.c_str());
      }
    }

    void displayLast() override {
      st7735.setTextSize(1);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);
      st7735.setCursor(32, 84);
      st7735.printf("%6d s", nextUplink - time(NULL) + 1);
    }

    void displayGNSS(bool force = false) override {
      st7735.setTextSize(1);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);
      PRINTST7735(32,  96, force, "%8.04f", oldLat, gps.location.lat);
      PRINTST7735(32, 106, force, "%8.04f", oldLng, gps.location.lng);
      PRINTST7735(32, 116, force, "%6.0f m", oldAlt, gps.altitude.meters);
      if(gps.hdop.hdop() > 0 && gps.hdop.hdop() < 99) {
        PRINTST7735(32, 126, force, "%8.1f", oldHdop, gps.hdop.hdop);
      } else {
        st7735.setCursor(32, 126);
        st7735.printf("       x");
      }
      PRINTST7735(32, 136, force, "%8d", oldSats, gps.satellites.value);
      PRINTST7735( 6, 146, force, "%02d", oldH, gps.time.hour);
      PRINTST7735(24, 146, force, "%02d", oldM, gps.time.minute);
      PRINTST7735(42, 146, force, "%02d", oldS, gps.time.second);
    }

    void displayWake() override {
      st7735.fillRect(0, 0, 80, 154, ST7735_BLACK);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);
      st7735.setTextSize(2);
      st7735.setCursor( 1, 15); st7735.printf("S");
      st7735.setCursor(12, 15); st7735.printf("t");
      st7735.setCursor(23, 15); st7735.printf("a");
      st7735.setCursor(34, 15); st7735.printf("n");
      st7735.setCursor(45, 15); st7735.printf("d");
      st7735.setCursor(56, 15); st7735.printf("b");
      st7735.setCursor(67, 15); st7735.printf("y");

      st7735.setCursor(18, 38);
      st7735.printf("Next");
      st7735.setCursor(5, 54);
      st7735.printf("uplink");
      
      String remaining = String(nextUplink - time(NULL) + 1);
      int x = 0, y = 0;
      if(remaining.length() <= 4) {
        st7735.setTextSize(3);
        x = (80 - 18*remaining.length()) / 2 + 1;
        y = 76;
      } else {
        st7735.setTextSize(2);
        x = (80 - 12*remaining.length()) / 2 + 1;
        y = 80;
      }
      st7735.setCursor(x, y);
      st7735.printf("%s", remaining.c_str());
      
      st7735.setTextSize(1);
      st7735.setCursor(20, 102);
      st7735.printf("seconds");
      st7735.setCursor( 7, 122);
      st7735.printf("Press again");
      st7735.setCursor(10, 132);
      st7735.printf("to send an");
      st7735.setCursor(10, 142);
      st7735.printf("uplink now");
    }

    void displayCharging() {
      st7735.fillScreen(ST7735_BLACK);
      
      uint16_t battMillivolts = analogReadMilliVolts(1) * 4.9f;
      int16_t height = map(battMillivolts, 2850, 4050, 0, 100);
      height = max((int16_t)0, min((int16_t)100, height));

      st7735.fillRect(25, 32, 30, 8, ST7735_WHITE);
      st7735.fillRoundRect(10, 40, 60, 110, 4, ST7735_WHITE);
      st7735.fillRoundRect(13, 43, 54, 104, 3, ST7735_BLACK);
      st7735.fillRoundRect(15, 45, 50, 100 - height, 3, ST7735_BLACK);
      st7735.fillRoundRect(15, 145 - height, 50, height, 3, ST7735_WHITE);

      st7735.fillTriangle(40+12, 95-30, 40-0, 95+5, 40-17, 95+5, ST7735_BLACK);
      st7735.fillTriangle(40-12, 95+31, 40+0, 95-5, 40+17, 95-5, ST7735_BLACK);
      st7735.fillTriangle(40+8,  95-23, 40-2, 95+3, 40-12, 95+3, ST7735_WHITE);
      st7735.fillTriangle(40-8,  95+23, 40+2, 95-3, 40+12, 95-3, ST7735_WHITE);
      st7735.fillRect(40-2, 95-3, 4, 6, ST7735_WHITE);
      
      if(usbState) {
        st7735.setTextColor(ST7735_WHITE);
      } else {
        st7735.setTextColor(ST7735_BLACK);
      }
      st7735.setTextSize(3);
      st7735.setCursor(14, 3);
      st7735.print("USB");
    }
};

class DisplayLoRaWAN : public DisplayStyle {
  public:
    virtual int getStyleNum() const { return DISPLAY_LORAWAN; }

    void displayFull() override {
      st7735.fillScreen(ST7735_BLACK);
      st7735.drawBitmap(0, 0, Display_LW, 80, 160, ST7735_WHITE);
      this->displayHeader();
      this->displayLast();
      this->displayLoRaWAN();
      this->displayBattery();
    }

    void displayLoRaWAN() override {
      if(cfg.uplink.adr) {
        st7735.drawBitmap(62, 16, Display_ADR, 16, 12, ST7735_WHITE);
      } else {
        st7735.drawBitmap(62, 16, Display_Manual, 16, 12, ST7735_WHITE);
      }

      st7735.setRotation(3);
      st7735.setTextSize(2);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);
      st7735.setCursor(31, 3);
      if(cfg.uplink.adr == ADR_DR) {
        st7735.printf("%2d-%-2d|  %2d", cfg.uplink.range[0], cfg.uplink.range[cfg.uplink.rangeLen - 1], cfg.uplink.dbm);
      } else if(cfg.uplink.adr == ADR_DBM) {
        st7735.printf("  %2d|%2d-%-2d", cfg.uplink.dr, cfg.uplink.range[0], cfg.uplink.range[cfg.uplink.rangeLen - 1]);
      } else {
        st7735.printf("   %2d|  %2d", cfg.uplink.dr, cfg.uplink.dbm);
      }

      st7735.setCursor(31, 23);
      st7735.printf("%5d|%4d", node.getFCntUp(), node.getAFCntDown() + node.getNFCntDown());

      st7735.setCursor(31, 43);
      if(cfg.uplink.confirmed && wasDownlink) {
        st7735.printf("%4.0f | %4.0f", rssi, snr);
      } else {
        st7735.printf("    x|   x", rssi, snr);
      }
      
      st7735.setRotation(2);
    }

    void displayLast() override {
      st7735.setRotation(3);
      st7735.setTextSize(2);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);

      st7735.setCursor(31, 63);
      st7735.printf("%8d s", nextUplink - time(NULL) + 1);
      st7735.setRotation(2);
    }

    void displayWake() override {
      st7735.setRotation(3);
      st7735.fillRect(12, 0, 142, 80, ST7735_BLACK);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);

      st7735.setTextSize(2);
      st7735.setCursor(37, 8);
      st7735.printf("Sleeping");

      st7735.setTextSize(3);
      uint32_t remaining = nextUplink - time(NULL) + 1;
      st7735.setCursor(14 + (142 - 18*String(remaining).length()) / 2, 30);
      st7735.printf("%d", remaining);

      st7735.setTextSize(2);
      st7735.setCursor(43, 58);
      st7735.printf("seconds");

      st7735.setRotation(2);
    }
};

class DisplayGNSS : public DisplayStyle {
  public:
    virtual int getStyleNum() const { return DISPLAY_GNSS; }

    void displayFull() override {
      st7735.fillScreen(ST7735_BLACK);
      st7735.drawBitmap(0, 0, Display_GPS, 80, 160, ST7735_WHITE);
      this->displayHeader();
      this->displayGNSS(true);
      this->displayBattery();
    }

    void displayGNSS(bool force = false) override {
      if(gps.time.value()) {
        st7735.drawBitmap(2, 12 + 120, Display_Clock, 16, 16, ST7735_WHITE);
      } else {
        st7735.drawBitmap(2, 12 + 120, Display_Clock, 16, 16, 0x8410);
      }

      st7735.setRotation(3);
      st7735.setTextSize(2);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);
      PRINTST7735(31,  3, force, "%10.05f", oldLat, gps.location.lat);
      PRINTST7735(31, 23, force, "%10.05f", oldLng, gps.location.lng);
      PRINTST7735(31, 43, force, "% 8.0f",  oldAlt, gps.altitude.meters);
      if(gps.hdop.hdop() > 0 && gps.hdop.hdop() < 99) {
        PRINTST7735(31, 63, force, "%4.1f", oldHdop, gps.hdop.hdop);
      } else {
        st7735.setCursor(31, 63); st7735.printf("  x ");
      }
      PRINTST7735(103, 63, force, "%2d", oldSats, gps.satellites.value);
      st7735.setRotation(2);
    }

    void displayWake() override {
      st7735.setRotation(3);
      st7735.fillRect(12, 0, 142, 80, ST7735_BLACK);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);

      st7735.setTextSize(2);
      st7735.setCursor(37, 8);
      st7735.printf("Sleeping");

      st7735.setTextSize(3);
      uint32_t remaining = nextUplink - time(NULL) + 1;
      st7735.setCursor(14 + (142 - 18*String(remaining).length()) / 2, 30);
      st7735.printf("%d", remaining);

      st7735.setTextSize(2);
      st7735.setCursor(43, 58);
      st7735.printf("seconds");

      st7735.setRotation(2);
    }
};

class Display2_4G : public DisplayStyle {
  public:
    virtual int getStyleNum() const { return DISPLAY_2_4G; }

    void displayFull() override {
      st7735.fillRect(0, 0, 80, 160, ST7735_BLACK);
      st7735.drawBitmap(0, 0, Display_WiFi, 80, 160, ST7735_WHITE);
      this->displayHeader();
      this->displayWiFi();
      this->displayBattery();
    }

    void displayWiFi() override {
      if(wifiMode == WIFI_MODE_STA && WiFi.isConnected()) {
        int8_t rssi = WiFi.RSSI();
        st7735.drawBitmap(62, 16, Display_WiFi4, 16, 12, 0x8410);  // gray/grey
        if(rssi < -90) {
          st7735.drawBitmap(62, 16, Display_WiFi1, 16, 12, ST7735_GREEN);
        } else if(rssi < -67) {
          st7735.drawBitmap(62, 16, Display_WiFi2, 16, 12, ST7735_GREEN);
        } else if(rssi < 55) {
          st7735.drawBitmap(62, 16, Display_WiFi3, 16, 12, ST7735_GREEN);
        } else {
          st7735.drawBitmap(62, 16, Display_WiFi4, 16, 12, ST7735_GREEN);
        }
      } else if(wifiMode == WIFI_MODE_STA) {
        st7735.drawBitmap(62, 16, Display_WiFi4, 16, 12, ST7735_RED);  // gray/grey
      } else if(wifiMode == WIFI_MODE_AP) {
        st7735.drawBitmap(62, 16, Display_WiFi4, 16, 12, ST7735_BLUE);  // gray/grey
      } else {
        st7735.drawBitmap(62, 16, Display_WiFi4, 16, 12, 0x8410);  // gray/grey
        st7735.drawLine(62, 16, 78, 28, ST7735_RED);
        st7735.drawLine(78, 16, 62, 28, ST7735_RED);
      }

      st7735.setRotation(3);
      st7735.setTextSize(2);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);

      String start, end;
      st7735.setCursor(31, 3);
      if(cfg.wl2g4.ssid.length() <= 10) {
        st7735.printf("%10s", cfg.wl2g4.ssid);
      } else {
        start = cfg.wl2g4.ssid.substring(0, 4);
        end = cfg.wl2g4.ssid.substring(cfg.wl2g4.ssid.length() - 4, cfg.wl2g4.ssid.length());
        st7735.printf("%s__%s", start, end);
      }

      st7735.setCursor(31, 23);
      start = cfg.wl2g4.pass.substring(0, 1);
      end = cfg.wl2g4.pass.substring(cfg.wl2g4.pass.length() - 1, cfg.wl2g4.pass.length());
      st7735.printf("%s________%s", start, end);

      st7735.setCursor(31, 43);
      if(cfg.wl2g4.user.length() > 0) {
        start = cfg.wl2g4.user.begin();
        end = cfg.wl2g4.user.end();
        st7735.printf("%s________%s", start, end);
      } else {
        st7735.printf("          ");
      }

      st7735.setCursor(31, 63);
      if(String(IP).length() < 3) {
        st7735.printf("          ");
      }
      if(String(IP).length() <= 10) {
        st7735.printf("%s", String(IP));
      } else {
        st7735.setTextSize(1);
        st7735.printf("%d.", IP[0]);
        st7735.setCursor(31, 71);
        st7735.printf("%d.", IP[1]);
        st7735.setTextSize(2);
        st7735.setCursor(59, 63);
        st7735.printf("%d.%d", IP[2], IP[3]);
      }

      st7735.setRotation(2);
    }

    void displayWake() override {
      st7735.setRotation(3);
      st7735.fillRect(12, 0, 142, 80, ST7735_BLACK);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);

      st7735.setTextSize(2);
      st7735.setCursor(37, 8);
      st7735.printf("Sleeping");

      st7735.setTextSize(3);
      uint32_t remaining = nextUplink - time(NULL) + 1;
      st7735.setCursor(14 + (142 - 18*String(remaining).length()) / 2, 30);
      st7735.printf("%d", remaining);

      st7735.setTextSize(2);
      st7735.setCursor(43, 58);
      st7735.printf("seconds");

      st7735.setRotation(2);
    }
};

class DisplaySignal : public DisplayStyle {
  public:
    virtual int getStyleNum() const { return DISPLAY_SIGNAL; }

    void displayFull() override {
      st7735.fillScreen(ST7735_BLACK);
      this->displayHeader();
      this->displayLast();
      this->displayLoRaWAN();
      this->displayBattery();
    }

    void displayLoRaWAN() override {
      st7735.setTextSize(3);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);
      st7735.drawFastHLine(0, 75, 80, ST7735_WHITE);

      if(!wasDownlink || frameDownSize == 0) {
        st7735.setCursor(31, 20);
        st7735.printf("-");
        st7735.setCursor(31, 47);
        st7735.printf("-");
        st7735.setCursor(31, 81);
        st7735.printf("-");
        st7735.setCursor(31, 108);
        st7735.printf("-");

      } else {

        char buffer[10];
        int16_t x;
        uint8_t rssiLen, snrLen;

        int rssiUp = -frameDown[3];
        memset(buffer, 0, 10);
        sprintf(buffer, "%d", rssiUp);
        rssiLen = strlen(buffer);
        x = rssiLen == 3 ? 13 : (rssiLen == 4 ? 4 : -9);
        st7735.setCursor(x, 20);
        st7735.printf("%d", rssiUp);

        uint8_t rawSnrUp = frameDown[2];
        float snrUp = 0;
        if (rawSnrUp & 0x80) {
          snrUp = -(rawSnrUp & 0x7F) / 5;
        } else {
          snrUp = rawSnrUp / 5;
        }
        memset(buffer, 0, 10);
        sprintf(buffer, "%.1f", snrUp);
        snrLen = strlen(buffer);
        x = snrLen == 3 ? 13 : (snrLen == 4 ? 4 : -9);
        st7735.setCursor(x, 47);
        st7735.printf("%.1f", snrUp);


        memset(buffer, 0, 10);
        sprintf(buffer, "%.0f", rssi);
        rssiLen = strlen(buffer);
        x = rssiLen == 3 ? 13 : (rssiLen == 4 ? 4 : -9);
        st7735.setCursor(x, 81);
        st7735.printf("%.0f", rssi);

        memset(buffer, 0, 10);
        sprintf(buffer, "%.1f", snr);
        snrLen = strlen(buffer);
        x = snrLen == 3 ? 13 : (snrLen == 4 ? 4 : -9);
        st7735.setCursor(x, 108);
        st7735.printf("%.1f", snr);
      }
    }

    void displayLast() override {
      st7735.setTextSize(2);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);

      st7735.setCursor(6, 138);
      st7735.printf("%5ds", nextUplink - time(NULL) + 1);
    }

    void displayWake() override {
      st7735.setRotation(3);
      st7735.fillRect(12, 0, 142, 80, ST7735_BLACK);
      st7735.setTextColor(ST7735_WHITE, ST7735_BLACK);

      st7735.setTextSize(2);
      st7735.setCursor(37, 8);
      st7735.printf("Sleeping");

      st7735.setTextSize(3);
      uint32_t remaining = nextUplink - time(NULL) + 1;
      st7735.setCursor(14 + (142 - 18*String(remaining).length()) / 2, 30);
      st7735.printf("%d", remaining);

      st7735.setTextSize(2);
      st7735.setCursor(43, 58);
      st7735.printf("seconds");

      st7735.setRotation(2);
    }
};

class DisplayMenu : public DisplayStyle {
  private:
    typedef void (*Callback)(int);
    struct MenuItem {
      const char* text;
      uint16_t bgColor;
      bool close;
      Callback callback;
      int val;
    };
    MenuItem menuItems[MAX_NUM_ROWS];
    int width;
    int height;
    int numRows;
    int selectedRow;

  public:
    virtual int getStyleNum() const { return DISPLAY_MENU; }

    DisplayMenu(int w, int h) : 
      width(w), height(h), numRows(0), selectedRow(0) {
    }

    void displayFull() override {
      selectedRow = 0;
      for (int i = 0; i < numRows; ++i) {
        uint16_t textColor = (i == selectedRow) ? ST7735_BLACK : ST7735_WHITE;
        uint16_t bgColor = (i == selectedRow) ? this->menuItems[i].bgColor : ST7735_BLACK;
        drawRow(i, textColor, bgColor);
      }
    }

    void drawRow(int row, uint16_t textColor, uint16_t bgColor) {
      st7735.setTextSize(0);
      st7735.fillRect(0, row * (height / numRows), width, height / numRows, bgColor);
      int offset = (height / numRows - Y_ADVANCE) / 2;
      st7735.setCursor(2, row * (height / numRows) + offset);
      st7735.setTextColor(textColor);
      st7735.print(menuItems[row].text);
    }

    void next() {
      uint16_t prevTextColor = ST7735_WHITE;
      uint16_t prevBgColor = ST7735_BLACK;

      // Clear previous selected row
      drawRow(selectedRow, prevTextColor, prevBgColor);

      // Update selected row
      selectedRow = (selectedRow + 1) % numRows;

      uint16_t newTextColor = ST7735_BLACK;
      uint16_t newBgColor = this->menuItems[selectedRow].bgColor;

      // Draw newly selected row
      drawRow(selectedRow, newTextColor, newBgColor);
    }

    void select() {
      if (menuItems[selectedRow].callback) {
        menuItems[selectedRow].callback(menuItems[selectedRow].val);
      }
    }

    void setCallback(int slot, const char* text, uint16_t bg, bool close, Callback cb, int val = -1) {
      if (slot >= 0 && slot < MAX_NUM_ROWS) {
        menuItems[slot] = {text, bg, close, cb, val};
        numRows = max(numRows, slot + 1);
      }
    }

    bool ifClose() {
      return(menuItems[selectedRow].close);
    }

    void displayHeader(int symbol = 0) override {}
    void displayBattery() override {}
    void displayLoRaWAN() override {}
    void displayLast() override {}
    void displayGNSS(bool force = false) override {}
    void displayWiFi() override {}
    void displayWake() override {}
};

DisplayCompact displayCompact = DisplayCompact();
DisplayLoRaWAN displayLoRaWAN = DisplayLoRaWAN();
DisplayGNSS    displayGNSS    = DisplayGNSS();
Display2_4G    display2_4G    = Display2_4G();
DisplaySignal  displaySignal  = DisplaySignal();

DisplayStyle* displayStyle = &displayGNSS;
DisplayStyle* displayStyles[NUM_STYLES] = { &displayGNSS, &displayLoRaWAN, &displaySignal, &displayCompact, &display2_4G };

void setDisplayStyle(DisplayStyle* style, bool update = false) {
  displayStyle = style;
  if(update) {
    displayStyle->displayFull();
  }
}

void setDisplayStyle(int num) {
  styleNum = num;
}

enum DisplayMenus {
  MENU_MAIN,
  MENU_STYLE,
  MENU_LORA,
  MENU_DR,
  MENU_DBM,
  MENU_RELAY,
  MENU_OPER,
  MENU_INT,
  MENU_TMT,
  MENU_CONN,
  MENU_ABT,
  NUM_MENUS
};

DisplayMenu* displayMenus[NUM_MENUS];
DisplayMenu* menu;

// 0xB5F6  = gray
// 0x97D2  = green
// 0xFB0F  = red

void selectMenu(int val) {
  menu = displayMenus[val];

  switch(val) {
    case MENU_LORA: {
      displayMenus[MENU_LORA]->setCallback(1, cfg.uplink.adr ? "ADR (ON)" : "ADR (OFF)", 0x97D2, true, [](int v){ cfg.uplink.adr = !cfg.uplink.adr; });
      displayMenus[MENU_LORA]->setCallback(2, "Tx DR       >", 0x97D2, false, selectMenu, MENU_DR);
      displayMenus[MENU_LORA]->setCallback(3, "Tx dBm      >", 0x97D2, false, selectMenu, MENU_DBM);
      displayMenus[MENU_LORA]->setCallback(4, cfg.uplink.confirmed ? "Confirm (ON)" : "Confirm (OFF)", 0x97D2, true, [](int v){ cfg.uplink.confirmed = !cfg.uplink.confirmed; });
      break;
    }
    case MENU_RELAY: {
      displayMenus[MENU_RELAY]->setCallback(1, cfg.relay.enabled ? "Relay (ON)" : "Relay (OFF)", 0x97D2, true, [](int v){ cfg.relay.enabled = !cfg.relay.enabled; });
      break;
    }
    case MENU_OPER: {
      displayMenus[MENU_OPER]->setCallback(3, cfg.operation.sleep ? "Sleep (ON)" : "Sleep (OFF)", 0x97D2, true, [](int v){ cfg.operation.sleep = !cfg.operation.sleep; });
      displayMenus[MENU_OPER]->setCallback(4, cfg.operation.mobile ? "Mobile (ON)" : "Mobile (OFF)", 0x97D2, true, [](int v){ cfg.operation.mobile = !cfg.operation.mobile; });
      break;
    }
    case MENU_CONN: {
      displayMenus[MENU_CONN]->setCallback(1, usbOn ? "USB (ON)" : "USB (OFF)", 0x97D2, true, [](int v){ usbOn = !usbOn; });
      // displayMenus[MENU_CONN]->setCallback(2, wifiMode ? "WiFi (ON)" : "WiFi (OFF)", 0x97D2, true, wifiEnable);
      // displayMenus[MENU_CONN]->setCallback(3, ble.state ? "BLE (ON)" : "BLE (OFF)", 0x97D2, true, bleEnable);
      break;
    }
    case MENU_ABT: {
      sprintf(devAddrText,"Add: %08X", node.getDevAddr());
      displayMenus[MENU_ABT]->setCallback(1, devAddrText,        0xB5F6, false, NULL);
      displayMenus[MENU_ABT]->setCallback(2, cfg.wl2g4.name.c_str(), 0xB5F6, false, NULL);
      break;
    }
    default:
      break;
  }

  setDisplayStyle(menu, true);
}

void loadMenus() {
  displayMenus[MENU_MAIN] = new DisplayMenu(80, 160);
  displayMenus[MENU_MAIN]->setCallback(0, "Uplink now   ", 0x97D2, true,  [](int v){scheduleUplink(0, tNow);});
  displayMenus[MENU_MAIN]->setCallback(1, "Style       >", 0x97D2, false, selectMenu, MENU_STYLE);
  displayMenus[MENU_MAIN]->setCallback(2, "LoRaWAN     >", 0x97D2, false, selectMenu, MENU_LORA);
  displayMenus[MENU_MAIN]->setCallback(3, "Relay       >", 0x97D2, false, selectMenu, MENU_RELAY);
  displayMenus[MENU_MAIN]->setCallback(4, "Operation   >", 0x97D2, false, selectMenu, MENU_OPER);
  displayMenus[MENU_MAIN]->setCallback(5, "Connections >", 0x97D2, false, selectMenu, MENU_CONN);
  displayMenus[MENU_MAIN]->setCallback(6, "About       >", 0x97D2, false, selectMenu, MENU_ABT);
  displayMenus[MENU_MAIN]->setCallback(7, "- Exit menu -", 0xFB0F, true,  NULL);

  displayMenus[MENU_STYLE] = new DisplayMenu(80, 160);
  displayMenus[MENU_STYLE]->setCallback(0, "Set style",     0xB5F6, false, NULL);
  displayMenus[MENU_STYLE]->setCallback(1, "Compact",       0x97D2, true,  setDisplayStyle, DISPLAY_COMPACT);
  displayMenus[MENU_STYLE]->setCallback(2, "LoRaWAN",       0x97D2, true,  setDisplayStyle, DISPLAY_LORAWAN);
  displayMenus[MENU_STYLE]->setCallback(3, "Signal",        0x97D2, true,  setDisplayStyle, DISPLAY_SIGNAL);
  displayMenus[MENU_STYLE]->setCallback(4, "GNSS",          0x97D2, true,  setDisplayStyle, DISPLAY_GNSS);
  displayMenus[MENU_STYLE]->setCallback(5, "2.4G",          0x97D2, true,  setDisplayStyle, DISPLAY_2_4G);
  displayMenus[MENU_STYLE]->setCallback(6, "- Exit menu -", 0xFB0F, true,  NULL);

  displayMenus[MENU_LORA] = new DisplayMenu(80, 160);
  displayMenus[MENU_LORA]->setCallback(0, "Set LoRaWAN",   0xB5F6, false, NULL);
  displayMenus[MENU_LORA]->setCallback(1, cfg.uplink.adr ? "ADR (ON)" : "ADR (OFF)", 0x97D2, true, [](int v){ cfg.uplink.adr = !cfg.uplink.adr; });
  displayMenus[MENU_LORA]->setCallback(2, "Tx DR       >", 0x97D2, false, selectMenu, MENU_DR);
  displayMenus[MENU_LORA]->setCallback(3, "Tx dBm      >", 0x97D2, false, selectMenu, MENU_DBM);
  displayMenus[MENU_LORA]->setCallback(4, cfg.uplink.confirmed ? "Confirm (ON)" : "Confirm (OFF)", 0x97D2, true, [](int v){ cfg.uplink.confirmed = !cfg.uplink.confirmed; });
  displayMenus[MENU_LORA]->setCallback(5, "Rejoin",        0x97D2, true,  [](int v) { node.clearSession(); scheduleUplink(0, tNow); });
  displayMenus[MENU_LORA]->setCallback(6, "- Exit menu -", 0xFB0F, true,  NULL);

  displayMenus[MENU_DR] = new DisplayMenu(80, 160);
  displayMenus[MENU_DR]->setCallback(0, "Datarate",      0xB5F6, false, NULL);
  displayMenus[MENU_DR]->setCallback(1, "5: SF 7 BW125", 0x97D2, true,  [](int v) { String cmd = "+DR=5"; execCommand(cmd); });
  displayMenus[MENU_DR]->setCallback(2, "4: SF 8 BW125", 0x97D2, true,  [](int v) { String cmd = "+DR=4"; execCommand(cmd); });
  displayMenus[MENU_DR]->setCallback(3, "3: SF 9 BW125", 0x97D2, true,  [](int v) { String cmd = "+DR=3"; execCommand(cmd); });
  displayMenus[MENU_DR]->setCallback(4, "2: SF10 BW125", 0x97D2, true,  [](int v) { String cmd = "+DR=2"; execCommand(cmd); });
  displayMenus[MENU_DR]->setCallback(5, "1: SF11 BW125", 0x97D2, true,  [](int v) { String cmd = "+DR=1"; execCommand(cmd); });
  displayMenus[MENU_DR]->setCallback(6, "0: SF12 BW125", 0x97D2, true,  [](int v) { String cmd = "+DR=0"; execCommand(cmd); });
  displayMenus[MENU_DR]->setCallback(7, "DR even",       0x97D2, true,  [](int v) { String cmd = "+DR=even"; execCommand(cmd); });
  displayMenus[MENU_DR]->setCallback(8, "DR odd",        0x97D2, true,  [](int v) { String cmd = "+DR=odd";  execCommand(cmd); });
  displayMenus[MENU_DR]->setCallback(9, "- Exit menu -", 0xFB0F, true,  NULL);

  displayMenus[MENU_DBM] = new DisplayMenu(80, 160);
  displayMenus[MENU_DBM]->setCallback(0, "Tx power",      0xB5F6, false, NULL);
  displayMenus[MENU_DBM]->setCallback(1, "16 dBm",        0x97D2, true,  [](int v) { String cmd = "+dBm=16"; execCommand(cmd); });
  displayMenus[MENU_DBM]->setCallback(2, "12 dBm",        0x97D2, true,  [](int v) { String cmd = "+dBm=12"; execCommand(cmd); });
  displayMenus[MENU_DBM]->setCallback(3, " 8 dBm",        0x97D2, true,  [](int v) { String cmd = "+dBm=8"; execCommand(cmd); });
  displayMenus[MENU_DBM]->setCallback(4, " 4 dBm",        0x97D2, true,  [](int v) { String cmd = "+dBm=4"; execCommand(cmd); });
  displayMenus[MENU_DBM]->setCallback(5, " 0 dBm",        0x97D2, true,  [](int v) { String cmd = "+dBm=0"; execCommand(cmd); });
  displayMenus[MENU_DBM]->setCallback(6, "- Exit menu -", 0xFB0F, true,  NULL);
  
  displayMenus[MENU_RELAY] = new DisplayMenu(80, 160);
  displayMenus[MENU_RELAY]->setCallback(0, "Relay",         0xB5F6, false, NULL);
  displayMenus[MENU_RELAY]->setCallback(1, cfg.relay.enabled ? "Relay (ON)" : "Relay (OFF)", 0x97D2, true, [](int v){ cfg.relay.enabled = !cfg.relay.enabled; });
  displayMenus[MENU_RELAY]->setCallback(2, "- Exit menu -", 0xFB0F, true,  NULL);

  displayMenus[MENU_OPER] = new DisplayMenu(80, 160);
  displayMenus[MENU_OPER]->setCallback(0, "Set operation", 0xB5F6, false, NULL);
  displayMenus[MENU_OPER]->setCallback(1, "Interval    >", 0x97D2, false, selectMenu, MENU_INT);
  displayMenus[MENU_OPER]->setCallback(2, "Timeout     >", 0x97D2, false, selectMenu, MENU_TMT);
  displayMenus[MENU_OPER]->setCallback(3, cfg.operation.sleep ? "Sleep (ON)" : "Sleep (OFF)", 0x97D2, true, [](int v){ cfg.operation.sleep = !cfg.operation.sleep; });
  displayMenus[MENU_OPER]->setCallback(4, cfg.operation.mobile ? "Mobile (ON)" : "Mobile (OFF)", 0x97D2, true, [](int v){ cfg.operation.mobile = !cfg.operation.mobile; });
  displayMenus[MENU_OPER]->setCallback(5, "- Exit menu -", 0xFB0F, true,  NULL);

  displayMenus[MENU_INT] = new DisplayMenu(80, 160);
  displayMenus[MENU_INT]->setCallback(0, "Interval",       0xB5F6, false, NULL);
  displayMenus[MENU_INT]->setCallback(1, "DC: FUP",        0x97D2, true,  [](int v) { String cmd = "+Interval=dc,fup";   execCommand(cmd); });
  displayMenus[MENU_INT]->setCallback(2, "DC: 0.1%",       0x97D2, true,  [](int v) { String cmd = "+Interval=dc,0.1%";  execCommand(cmd); });
  displayMenus[MENU_INT]->setCallback(3, "DC:   1%",       0x97D2, true,  [](int v) { String cmd = "+Interval=dc,1%";    execCommand(cmd); });
  displayMenus[MENU_INT]->setCallback(4, "Fixed:   15s",   0x97D2, true,  [](int v) { String cmd = "+Interval=fixed,15"; execCommand(cmd); });
  displayMenus[MENU_INT]->setCallback(5, "Fixed:   30s",   0x97D2, true,  [](int v) { String cmd = "+Interval=fixed,30"; execCommand(cmd); });
  displayMenus[MENU_INT]->setCallback(6, "Fixed:   60s",   0x97D2, true,  [](int v) { String cmd = "+Interval=fixed,60"; execCommand(cmd); });
  displayMenus[MENU_INT]->setCallback(7, "Fixed:  300s",   0x97D2, true,  [](int v) { String cmd = "+Interval=fixed,300"; execCommand(cmd); });
  displayMenus[MENU_INT]->setCallback(8, "Fixed:  600s",   0x97D2, true,  [](int v) { String cmd = "+Interval=fixed,600"; execCommand(cmd); });
  displayMenus[MENU_INT]->setCallback(9, "Fixed: 3600s",   0x97D2, true,  [](int v) { String cmd = "+Interval=fixed,3600"; execCommand(cmd); });
  displayMenus[MENU_INT]->setCallback(10, "- Exit menu -", 0xFB0F, true,  NULL);

  displayMenus[MENU_TMT] = new DisplayMenu(80, 160);
  displayMenus[MENU_TMT]->setCallback(0, "GNSS timeout",  0xB5F6, false, NULL);
  displayMenus[MENU_TMT]->setCallback(1, "0s",            0x97D2, true,  [](int v) { String cmd = "+timeout=0"; execCommand(cmd); });
  displayMenus[MENU_TMT]->setCallback(2, "30s",           0x97D2, true,  [](int v) { String cmd = "+timeout=30"; execCommand(cmd); });
  displayMenus[MENU_TMT]->setCallback(3, "60s",           0x97D2, true,  [](int v) { String cmd = "+timeout=60"; execCommand(cmd); });
  displayMenus[MENU_TMT]->setCallback(4, "120s",          0x97D2, true,  [](int v) { String cmd = "+timeout=120"; execCommand(cmd); });
  displayMenus[MENU_TMT]->setCallback(5, "300s",          0x97D2, true,  [](int v) { String cmd = "+timeout=300"; execCommand(cmd); });
  displayMenus[MENU_TMT]->setCallback(6, "600s",          0x97D2, true,  [](int v) { String cmd = "+timeout=600"; execCommand(cmd); });
  displayMenus[MENU_TMT]->setCallback(7, "3600s",         0x97D2, true,  [](int v) { String cmd = "+timeout=3600"; execCommand(cmd); });
  displayMenus[MENU_TMT]->setCallback(8, "- Exit menu -", 0xFB0F, true,  NULL);
  
  displayMenus[MENU_CONN] = new DisplayMenu(80, 160);
  displayMenus[MENU_CONN]->setCallback(0, "Connections",   0xB5F6, false, NULL);
  displayMenus[MENU_CONN]->setCallback(1, usbOn ? "USB (ON)" : "USB (OFF)", 0x97D2, true, [](int v){ usbOn = !usbOn; });
  // displayMenus[MENU_CONN]->setCallback(2, wifiMode ? "WiFi (ON)" : "WiFi (OFF)", 0x97D2, true, wifiEnable);
  // displayMenus[MENU_CONN]->setCallback(3, ble.state ? "BLE (ON)" : "BLE (OFF)", 0x97D2, true, bleEnable);
  displayMenus[MENU_CONN]->setCallback(4, "- Exit menu -", 0xFB0F, true,  NULL);
  
  displayMenus[MENU_ABT] = new DisplayMenu(80, 160);
  displayMenus[MENU_ABT]->setCallback(0, "    About    ",   0xB5F6, false, NULL);
  sprintf(devAddrText,"Add: %08X", node.getDevAddr());
  displayMenus[MENU_ABT]->setCallback(1, devAddrText,        0xB5F6, false, NULL);
  displayMenus[MENU_ABT]->setCallback(2, cfg.wl2g4.name.c_str(), 0xB5F6, false, NULL);
  displayMenus[MENU_ABT]->setCallback(3, "FW " MJLO_VERSION, 0xB5F6, false, NULL);
  displayMenus[MENU_ABT]->setCallback(4, "    LRF-1    ",    0xB5F6, false, NULL);
  displayMenus[MENU_ABT]->setCallback(5, "   Kroonos   ",    0xB5F6, false, NULL);
  displayMenus[MENU_ABT]->setCallback(6, "- Exit menu -",    0xFB0F, true,  NULL);
}

#endif