#include <Arduino.h>
#include "driver/gpio.h"
#include "driver/uart.h"

#include <Wire.h>
#include <SPI.h>

#include "pins.h"

#include "TinyGPS++.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2591.h>
#include <Adafruit_VEML6070.h>
#include <Adafruit_BME680.h>
#include <SensirionI2CScd4x.h>
#include <SensirionI2CSen5x.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <GxEPD2_BW.h>

#include "config.h"
#include "flash.h"
#include "lorawan.h"
#include "gnss.h"
#include "display.h"
#include "accelerometer.h"
#include "soundsensor.h"
#include "measurement.h"
#include "fs_browser.h"
#include "serial.h"

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFTEPD_DC, TFTEPD_MOSI, TFTEPD_SCK, TFTEPD_RST);
Adafruit_TSL2591 tsl = Adafruit_TSL2591();
Adafruit_VEML6070 veml = Adafruit_VEML6070();
Adafruit_BME680 bme = Adafruit_BME680(&Wire);
SensirionI2cScd4x scd4x;
SensirionI2CSen5x sen5x;
SoundSensor mic;

// e-ink display: GDEY0213B74 122x250, SSD1680 (FPC-A002 20.04.08)
// SPIClass hspi(HSPI);
GxEPD2_BW<GxEPD2_213_GDEY0213B74, GxEPD2_213_GDEY0213B74::HEIGHT> 
			epdDisplay(GxEPD2_213_GDEY0213B74(EPD_CS, TFTEPD_DC, TFTEPD_RST, EPD_BUSY));

float temp, humi, pres, lumi, db_min, db_avg, db_max;
float pm1_0, pm2_5, pm4_0, pm10_, hum5x, temp5x, vocIndex, noxIndex;
uint16_t batt_mv, co2;
uint32_t uva;
bool key, doGNSS;
RTC_DATA_ATTR bool doAllSensors, doAllIndices, doFastInterval;
uint32_t tStart;

enum DeviceStates {
  IDLE,
	JOIN,
	START_GNSS,
  WAIT_SATELLITE,
	START_PM,
	START_MIC,
	START_CO2,
	MEAS_BAT,
	MEAS_TPH,
	MEAS_LUM,
	MEAS_UV,
	MEAS_CO2,
	MEAS_MIC,
	MEAS_PM,
	WAIT_GNSS,
  SENDRECEIVE,
	SHOW_MEAS,
	SLEEP,
  MENU,   // TODO
  NONE
};

DeviceStates deviceState = JOIN;

uint32_t tNow = 0;

// WiFi and webserver stuff
bool serverRunning = false;

uint16_t battMillivolts = 0;
uint32_t lastBattUpdate = 0;
bool powerIsLow = false;

RTC_DATA_ATTR gpio_num_t wakePin0 = (gpio_num_t)0;
RTC_DATA_ATTR int numStationaryUplinks = 0;

bool buttonPressed = false;
bool buttonActive = false;
bool buttonReleased = false;
bool buttonSwitched = false;
long startPress = 0;
long endPress = 0;
long lastPress = 0;
bool wasMotion = false;
bool printGNSS = false;

void onKeyPress();
void onKeyRelease();

IRAM_ATTR void onKeyPress() {
  buttonPressed = true;
  attachInterrupt(0, onKeyRelease, RISING);       // action button
}

IRAM_ATTR void onKeyRelease() {
  buttonReleased = true;
  attachInterrupt(0, onKeyPress, FALLING);        // action button
}

IRAM_ATTR void onMotion() {
  detachInterrupt(5);
  if(isMotion) {
    return;
  }
  isMotion = true;
  if(!cfg.operation.mobile) {
    return;
  }
  if(nextUplink < tNow) {
    // do nothing, already waiting for uplink
  } else if(prevUplink + uplinkOffset > tNow) {
    // if motion-activated but not yet time for uplink, reschedule to short interval
    scheduleUplink(uplinkOffset, prevUplink);
  } else if(uplinkOffset) {
    // if motion-activated and already time for uplink, reschedule to now
    scheduleUplink(0, tNow);
  }
}

int ext1WakePinStatus() {
    uint64_t mask = esp_sleep_get_ext1_wakeup_status();
    int pin = 0;
    for(; mask > 1; mask >>= 1) {
        pin++;
    }
    return(pin);
}

void writeUplinkToLog() {
  snprintf(newDateBuf, 11, "%04d-%02d-%02d", gps.date.year(), gps.date.month(), gps.date.day());
  if (newDateBuf != dateBuf)
    checkAvailableStorage(newDateBuf);
  memcpy(dateBuf, newDateBuf, 11);
  snprintf(timeBuf, 9, "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());

  char line[128];
  snprintf(&line[ 0],10, "%s,", timeBuf);
  uint32_t dev32 = (cfg.actvn.otaa.devEUI >> 32);
  snprintf(&line[ 9], 9, "%08X", dev32);
  snprintf(&line[17],10, "%08X,", (uint32_t)cfg.actvn.otaa.devEUI);
  snprintf(&line[26], 3, "%d,", fPort);
  for(int i = 0; i < appDataSize; i++) {
    snprintf(&line[28+i*2], 3, "%02X", appData[i]);
  }
  Serial.printf("[%s] %s\n", dateBuf, line);
  File file = LittleFS.open("/" + String(dateBuf) + ".csv", "a");
  file.println(line);
  file.close();
  RADIOLIB_DEBUG_PROTOCOL_HEXDUMP((uint8_t*)line, 28);
  RADIOLIB_DEBUG_PROTOCOL_HEXDUMP(appData, appDataSize);
}

/* Prepares the payload of the frame */
uint8_t prepareTxFrame() {
  uint8_t port = 1;

  appData[0] = max(0, min(255, int((batt_mv - 2500) / 10))); // battery
  uint16_t cTemp = (uint16_t)max(0, min(32767, int(abs(temp) * 100)));
  if (temp < 0)
    cTemp |= (uint16_t(1) << 15);
  memcpy(&appData[1], &cTemp, 2);           	// temperature

  appData[3] = max(0, min(255, int(humi * 2)));    		// humidity

  uint16_t pPres = max(0, min(65535, int(pres * 10)));
  memcpy(&appData[4], &pPres, 2);           	// pressure

  uint16_t lLumi = max(0, min(65535, int(lumi)));
  memcpy(&appData[6], &lLumi, 2);           	// luminosity

  appData[8] = max(0, min(255, int(uva)));       	// UV

  appData[9] = max(0, min(255, int((db_min - 32) * 4))); 	// dbMin
  appData[10] = max(0, min(255, int((db_avg - 32) * 4)));	// dbAvg
  appData[11] = max(0, min(255, int((db_max - 32) * 4)));	// dbMax

  uint16_t mCO2 = (uint16_t)max(0, min(65535, (int)co2));
  memcpy(&appData[12], &mCO2, 2);           	// co2

  appDataSize = 14;

  if(doAllSensors) {  // includes PM
    port |= BIT(1);

    uint16_t mPM25 = max(0, min(1023, int(pm2_5 * 10)));
    uint16_t mPM10 = max(0, min(1023, int(pm10_ * 10)));
    appData[appDataSize+0] = (uint8_t)mPM25;             // pm2.5 LSB
    appData[appDataSize+1] = (uint8_t)mPM10;             // pm10  LSB
    appData[appDataSize+2] = ((mPM25 >> 8) << 4) | (mPM10 >> 8);   // pm2.5 | pm10 MSB
		appDataSize += 3;
  }
  if(doAllIndices) {  // includes VOC and NOx
    port |= BIT(2);
    
    // insert VOC and NOx
  }
  if(doGNSS) {    // includes GNSS
    port |= BIT(3);
    
    uint32_t rawLat = abs(gps.location.lat()) * 10000000;
    if (gps.location.lat() < 0)
      rawLat |= (1ul << 31);
    uint32_t rawLng = abs(gps.location.lng()) * 10000000;
    if (gps.location.lng() < 0)
      rawLng |= (1ul << 31);
    uint8_t rawHdop = gps.hdop.hdop() * 5;
    uint8_t rawSats = min((int)gps.satellites.value(), 15);
    uint8_t hdopSats = (rawHdop << 4) | rawSats;
    memcpy(&appData[appDataSize+0], &rawLat, 4);
    memcpy(&appData[appDataSize+4], &rawLng, 4);
    memcpy(&appData[appDataSize+8], &hdopSats, 1);

    appDataSize += 9;
  }

	return port;
}

void sendUplink() {
  fPort = prepareTxFrame();           // parse payload
  prevUplink = tNow;
  int16_t window = node.sendReceive(appData, appDataSize, fPort, networkData, &networkDataSize, 
                                    cfg.uplink.confirmed, &eventUp, &eventDown);
  
  uint8_t *persist = node.getBufferSession();
  memcpy(LWsession, persist, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
#if RADIOLIB_LORAWAN_NODE_R
  persist = node.getBufferNodeR();
  memcpy(LWnoder, persist, RADIOLIB_LORAWAN_NODER_BUF_SIZE);
#endif
  
  if(window < 0) {
    PRINTF("[LoRaWAN] Error while sending uplink: code %d\r\n", window);
  }
  if(window > 0) {
    wasDownlink = true;
  } else {
    wasDownlink = false;
  }
  rssi = radio.getRSSI();
  snr = radio.getSNR();

  writeUplinkToLog();  // write info to log
}

void setupSerial() {
  Serial.setTimeout(50);
  Serial.begin(115200);
  // Serial.setDebugOutput(false);

  const uart_port_t uart_num = UART_NUM_0;
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_APB,
  };

  // Configure UART parameters
  ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
}

void closeSerial() {
  Serial.flush();
  Serial.end();
}


void VextOn(void) {
  pinMode(V_EXT, OUTPUT);
  digitalWrite(V_EXT, HIGH);  // active HIGH
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);  // active HIGH
}

void VextOff(void) {
  pinMode(V_EXT, OUTPUT);
  digitalWrite(V_EXT, LOW);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  pinMode(EPD_BUSY, INPUT);
}

void printDirectory(File dir, int numTabs) {
  while (true) {
 
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

void turnOff() {
  // handle Timer source if the device is enabled
  if(digitalRead(POWER) == LOW && !powerIsLow) {
    uint64_t timeToSleep = nextUplink - tNow - 30;
    esp_sleep_enable_timer_wakeup(timeToSleep * 1000000ULL);
  }

  // handle EXT0 source (power button or action button)
  bool wakeLevel0;
  if(digitalRead(POWER) == HIGH) {      // turned off?
    wakePin0 = (gpio_num_t)POWER;       // listen to power-button
    wakeLevel0 = LOW;

  } else {                          // turned on?
    wakePin0 = (gpio_num_t)KEY;       // listen to action-button
    wakeLevel0 = LOW;

  }
  esp_sleep_enable_ext0_wakeup(wakePin0, wakeLevel0);

  // handle EXT1 sources (power button and accelerometer)
  uint64_t wakePins1 = 0x00000000;  // no pin interrupts
  esp_sleep_ext1_wakeup_mode_t wakeLevel1;
  if(digitalRead(POWER) == LOW) {       // turned on?
    wakePins1 |= (1 << POWER);          // listen to power-off
    if(cfg.operation.mobile && !isMotion && !powerIsLow) {
      wakePins1 |= (1 << ACC_INT);        // listen to accelGyro
    }
    wakeLevel1 = ESP_EXT1_WAKEUP_ANY_HIGH;

  }
  esp_sleep_enable_ext1_wakeup(wakePins1, wakeLevel1);

  esp_deep_sleep_start();
}

void uplinkASAP(int val = -1) {
  (void)val;
  if(nextUplink > tNow) {
    scheduleUplink(0, tNow);
  } else {
    scheduleUplink(-cfg.operation.timeout, tNow);
  }
}

int execCommand(String &command) {
  if (deviceState == SENDRECEIVE)
    return busyError;
  
  int idx =    command.indexOf('\n');
  if (idx > 0) command.remove(idx);

      idx =    command.indexOf('\r');
  if (idx > 0) command.remove(idx);

  if (command.length() <= 1 || !command.startsWith("+"))
    return formatError;

  String key = "";
  String value = "";
  if (command.indexOf("=") > 0) {
    key = command.substring(1, command.indexOf("="));
    value = command.substring(command.indexOf("=") + 1, command.length());
  } else {
    key = command.substring(1, command.length());
  }
  key.toLowerCase();

  if (command.indexOf("=") > 0) {
    int state = doSetting(key, value);
    if (state == noError) {
      displayStyle->displayFull();
    }
    return state;
  }

  if (key == "at") {
    Serial.print(printFullConfig(true));
    Serial.printf("\r\n");
  } else
  if (key == "scan") {
    Serial.println("Scanning for networks...");
    setCpuFrequencyMhz(240);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    Serial.println("Scan done");
    if (n == 0) {
        Serial.println("no networks found");
    } else {
        Serial.print(n);
        Serial.println(" networks found");
        for (int i = 0; i < n; ++i) {
            Serial.print(i + 1);
            Serial.print(": ");
            Serial.print(WiFi.SSID(i));
            Serial.print(" (");
            Serial.print(WiFi.RSSI(i));
            Serial.print(")");
            Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN)?" ":"*");
            delay(10);
        }
    }
    Serial.println("");
    setCpuFrequencyMhz(40);
  } else
  if (key == "dir") {
    File dir = LittleFS.open("/");
    printDirectory(dir, 0);
    Serial.printf("FS total: %d, used: %d\r\n", LittleFS.totalBytes(), LittleFS.usedBytes());
  } else
  if (key == "fill") {
    int x = 0;
    while(LittleFS.usedBytes() < (LittleFS.totalBytes() * 0.8)) {
      char fileBuf[16];
      sprintf(fileBuf, "/00filler%d.csv", x);
      Serial.printf("Opening file %s (%d) for appending\r\n", fileBuf, strlen(fileBuf));
      File f = LittleFS.open(fileBuf, "a");
      for(int i = 0; i < 200; i++) {
        f.println("time,latitude,longitude,altitude,hdop,sats,freq,fcnt_up,fport,sf,txpower,fcnt_down,rssi,snr");
      }
      f.close();
      Serial.println(x);
      x++;
    }
  } else
  if (key == "load") {
    loadConfig();
  } else
  if (key == "check") {
    checkAvailableStorage("1999-99-99");
  } else
  if (key == "join") {
    node.clearSession();
    deviceState = JOIN;
  } else
  if (key == "devaddr") {
    Serial.printf("DevAddr: %08X\r\n", node.getDevAddr());
  } else
  if (key == "wipecfg") {
    for (int i = 0; i < NUM_SETTINGS; i++) {
      String key = Settings[i].lower;
      String val = "";
      doSetting(key, val);  // set all commands to empty String
    }
    deviceState = JOIN;
  } else
  if (key == "wipelw") {
    store.begin("radiolib");
    store.remove("nonces");
    store.end();
    node.clearSession();
    deviceState = JOIN;
  } else
  if (key == "uplink") {
    if (deviceState == WAIT_GNSS) {
      deviceState = SENDRECEIVE;
    }
    else if (node.isActivated()) {
      deviceState = START_PM;
    }
    else if(deviceState == JOIN) {
      uplinkASAP();
    }
  } else
  if (key == "sleep") {
    turnOff();
  } else
  if (key == "restart") {
    ESP.restart();
  } else
  if (key == "accel") {
    while(Serial.available() < 5) {
      while(!digitalRead(ACC_INT)) {
        delay(1);
      }
      Serial.println("Motion");
      while(digitalRead(ACC_INT)) {
        delay(1);
      }
    }
    (void)Serial.readString();
  } else 
  if (key == "gnss") {
    printGNSS = !printGNSS;
  } else 
  if (key == "id") {
    uint64_t chipId = ESP.getEfuseMac();
    Serial.printf("%04X",(uint16_t)(chipId>>32)); // print first two bytes
    Serial.printf("%08X\r\n",(uint32_t)chipId);   // print lower four bytes
  }
  else {
    return commandError;
  }

  return noError;
}

void wifiEnable(int val) {
  (void)val;

  wifiMode = WIFI_MODE_STA;
  if (connectWiFi()) {
    displayStyle->displayWiFi();
    start_file_browser();
    serverRunning = true;
  }
}

void wifiDisable(int val) {
  (void)val;
  if(serverRunning) {
    end_file_browser();
    serverRunning = false;
  }
  disconnectWiFi();
}

// check if the battery has enough juice
// if not, flash LED a few times and go into infinite deepsleep
// being connected to USB power is also OK
void goLowPower() {
  powerIsLow = true;
  pinMode(LED_B, OUTPUT);
  for(int i = 0; i < 4; i++) {
    digitalWrite(LED_B, HIGH);
    delay(200);
    digitalWrite(LED_B, LOW);
    delay(200);
  }
  turnOff();
}

static float zweighting[] = Z_WEIGHTING;		// weighting lists
static Measurement zMeasurement( zweighting);	// measurement buffers

volatile bool mic_stop = false;
volatile bool mic_stopped = false;

void mic_get_db(void * params) {

	mic.offset(-1.8);		// for SPH0645
	mic.begin(BCLK, LRCLK, DIN);
	long startMic = millis();
	bool reset = false;

	while (!mic_stop) {
		float* energy = mic.readSamples();
		zMeasurement.update(energy);
		if (millis() - startMic > 3000) {
			if (!reset) {
				zMeasurement.reset();
				reset = true;
			}
		}
	}
	zMeasurement.calculate();
	mic.disable();
	mic_stopped = true;
	vTaskDelete(NULL);
}

// Function to extract the decimal part and return it as a String without the integer part
String to_decimal(float value) {
	String decimalString = String(value, 1);
	int pos = decimalString.indexOf(".");
	return decimalString.substring(pos);
}

void display_value(float val, String unit, int16_t x, int16_t y) { 
	epdDisplay.setCursor(x, y);
	epdDisplay.print(int(val));
	epdDisplay.setCursor(x, y+8);
	epdDisplay.print(to_decimal(val));
	epdDisplay.setCursor(x, y+16);
	epdDisplay.print(unit);
}

void display_eink() {
  Serial.println("Drawing values");
	Serial.println(millis());

	epdDisplay.setRotation(0);
	epdDisplay.setTextColor(GxEPD_BLACK, GxEPD_WHITE);
	epdDisplay.setFont(0);

  epdDisplay.setPartialWindow(0, 0, 120, 250);
  epdDisplay.firstPage();
  do
  {
    // epdDisplay.fillScreen(GxEPD_WHITE);

    epdDisplay.setTextSize(1);
    epdDisplay.setCursor(81, 1 + 3);
    epdDisplay.printf("Temp");
    epdDisplay.setCursor(81, 1 + 13);
    epdDisplay.printf("*C");
    epdDisplay.setCursor(13, 29 + 3);
    epdDisplay.printf("Vocht");
    epdDisplay.setCursor(37, 29 + 13);
    epdDisplay.printf("%%");
    epdDisplay.setCursor(81, 57 + 3);
    epdDisplay.printf("Druk");
    epdDisplay.setCursor(81, 57 + 13);
    epdDisplay.printf("hPa");
    epdDisplay.setCursor(25, 85 + 3);
    epdDisplay.printf("CO2");
    epdDisplay.setCursor(25, 85 + 13);
    epdDisplay.printf("ppm");

    epdDisplay.setTextSize(3);
    epdDisplay.setCursor(1, 1);
    epdDisplay.printf("%4.1f", temp);
    epdDisplay.setCursor(49, 29);
    epdDisplay.printf("%4.1f", humi);
    epdDisplay.setCursor(1, 57);
    epdDisplay.printf("%4.0f", pres);
    epdDisplay.setCursor(49, 85);
    epdDisplay.printf("%4d", co2);

    epdDisplay.setCursor(1, 113);
    epdDisplay.printf("%4.1f", db_avg);

    if(doAllSensors) {
      epdDisplay.setCursor(49, 141);
      epdDisplay.printf("%4.1f", pm2_5);
    }

    int width = map((int)batt_mv, 2850, 4050, 0, 104);
    width = max(0, min(104, width));
    epdDisplay.drawRoundRect(7, 240, 106, 11, 2, GxEPD_BLACK);
    epdDisplay.fillRect(8, 241, width, 9, GxEPD_BLACK);
    epdDisplay.fillRect(8 + 104 - width, 241, 104 - width, 9, GxEPD_WHITE);
  }
  while (epdDisplay.nextPage());

	epdDisplay.hibernate();
	Serial.println(millis());
  Serial.flush();
  delay(50);
}

void setup() {
  pinMode(BAT_ADC, INPUT);
  pinMode(BAT_CTRL, INPUT_PULLUP);
  pinMode(POWER, INPUT_PULLDOWN);
  pinMode(LED_B, OUTPUT);
  pinMode(EPD_BUSY, INPUT);  // TFT_BL
  pinMode(EPD_CS, OUTPUT);
  pinMode(DIP1, INPUT);
  pinMode(DIP2, INPUT);
  pinMode(DIP3, INPUT);

  battMillivolts = analogReadMilliVolts(BAT_ADC) * 4.9f;
  powerState = (digitalRead(POWER) == LOW);
  usbState = true;//usb_serial_jtag_is_connected();

  tNow = time(NULL);

  // first, check if tracker is enabled or connected to USB
  if(!powerState && !usbState) {
    turnOff();
  }

  // next, check if battery has enough juice
  if(battMillivolts < 2750) {
    goLowPower();
  }

  wakeup_reason = esp_sleep_get_wakeup_cause();

  // third, handle motion interrupt - might be able to go right back to sleep
  if(wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    int pin = ext1WakePinStatus();
    switch(pin) {
      // motion interrupt
      case(ACC_INT):
        isMotion = true;
        // if we are past the motion-interval, go into immediate uplink 
        if(tNow > prevUplink + uplinkOffset) {
          scheduleUplink(30, tNow);
          break;
        }
        // if we are within 30 seconds of next scheduled uplink, go into normal flow
        if(tNow > nextUplink - 30) {
          break;
        }
        // if neither of those applies, go back into sleep with motion interrupt disabled
        turnOff();
        break;
      default:
        break;
    }
  }

  // if not woken from sleep, can immediately transmit
  if(wakeup_reason < ESP_SLEEP_WAKEUP_EXT0) {
    scheduleUplink(0, tNow);
  }
  if((int)wakePin0 == (int)POWER) {
    // if waking up after scheduled interval, reschedule for now
    if(nextUplink < tNow) {
      scheduleUplink(30, tNow);
    }
    // otherwise, keep original interval to adhere to dutycycle
  }

  if(usbState) {
    setupSerial();
  }
  loadConfig();

  // start radio
  spiSX.begin(SXSD_SCK, SXSD_MISO, SXSD_MOSI, SX_CS);              // SCK/CLK, MISO, MOSI, NSS/CS
  radio.begin();                          // initialize SX1262 with default settings

  VextOn();

  spiST.begin(TFTEPD_SCK, TFTEPD_MISO, TFTEPD_MOSI);            // SCK/CLK, MISO, MOSI, NSS/CS
  st7735.initR(INITR_MINI160x80_PLUGIN);  // initialize ST7735S chip, mini display
  st7735.setRotation(2);

  // (try to) restore LoRaWAN session (also puts radio to sleep)
  if(lwBegin()) {
    int16_t state = lwRestore();
    if(state == RADIOLIB_ERR_NONE) {
      // simply restore
      lwActivate();
    } else {
      // join in the main loop
    }
  } else {
    Serial.println("No credentials - going into input mode:");
  }

	Wire.begin(SDA0, SCL0);
  // hspi.begin(TFTEPD_SCK, TFTEPD_MISO, TFTEPD_MOSI, EPD_CS); // remap hspi for EPD (swap pins)

  epdDisplay.epd2.selectSPI(spiST, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  epdDisplay.init(115200, true, 2, false);

  // housekeeping stuff on fresh boot
  if(wakeup_reason < ESP_SLEEP_WAKEUP_EXT0) {
    // configure accelerometer to listen for small bumps
    accelSetup();
    accelAnyMotion();

    // read the DIP configuration - this requires Vext enabled
    VextOn();
    delay(100);
    doAllSensors = !digitalRead(DIP1);		  // include PM
    doAllIndices = !digitalRead(DIP2);		  // include VOC and NOx
    doFastInterval = !digitalRead(DIP3);	  // uplink interval

    // act as if there was motion to do full GNSS cycle etc.
    isMotion = true;
  }
  isMotion = false; // TODO

  if(isMotion) {
    doGNSS = true;
    gps = TinyGPSPlus();
    VextOn();
  } else {
    doGNSS = false;
  }

  pinMode(KEY, INPUT);
  pinMode(ACC_INT, INPUT);
  // attachInterrupt(KEY, onKeyPress, FALLING);        // action button
  // attachInterrupt(ACC_INT, onMotion, RISING);           // accelerometer

  PRINTF("Starting filesystem...\r\n");
  if (!LittleFS.begin())  { PRINTF("Failed to initialize filesystem"); while(1) { delay(10); }; }

  setDisplayStyle(displayStyles[0], true);

  loadMenus();

  // start the state machine
  if(!node.isActivated()) {
    deviceState = JOIN;
  } else if (doGNSS) {
    deviceState = START_GNSS;
  } else {
    deviceState = START_PM;
  }

}

void handleSerialNmea() {
  int sec = gps.time.second();

  while (Serial1.available()) {
    if(printGNSS) {
      Serial.print((char)Serial1.peek());
    }
    gps.encode(Serial1.read());
  }

  double hdop = gps.hdop.hdop();
  if ((gps.location.age() < 2000) && (hdop <= 3) && (hdop > 0)) {
    if(gps.time.second() != sec) {
      numConsecutiveFix = numConsecutiveFix >= 5 ? 5 : numConsecutiveFix + 1;
    }
  } else {
    numConsecutiveFix = 0;
  }
  gpsFixLevel =  numConsecutiveFix >= 5 ? GPS_GOOD_FIX : 
                (hdop > 0 && hdop < 99) ? GPS_BAD_FIX : 
                                          GPS_NO_FIX;

  digitalWrite(LED_B, HIGH);
  delay(50);
  digitalWrite(LED_B, LOW);
}

void loop() {
  delay(10);
  tNow = time(NULL);

	switch(deviceState) {
    case(IDLE): {
      // don't do anything, just stay awake
      break;
    }
    case(JOIN): {
      // wait for next scheduled join-request
      if (tNow > nextUplink) {

        // test credentials, go idle if incomplete
        if(!lwBegin()) {
          deviceState = IDLE;
          return;
        }
        // show uplink symbol
        displayStyle->displayUplink();
        
        Serial.println("Activating...");
        lwRestore(false);
#if RADIOLIB_LORAWAN_NODE_R          
        if(cfg.relay.enabled) {
          node.setRelayActivation(cfg.relay.mode, cfg.relay.backOff, cfg.relay.smartLevel);
        } else {
          node.setRelayActivation(0);
        }
#endif
        prevUplink = tNow;
        lwActivate();
        tNow = time(NULL);  // update tNow as lwActivate() is blocking

        // calculate when next uplink should be scheduled
        if(cfg.interval.fixed) {
          uplinkOffset = cfg.interval.period;
        } else {
          uplinkOffset = node.dutyCycleInterval((cfg.interval.dutycycle * 1000) / 24, node.getLastToA()) / 1000 - 5;
        }
        uplinkOffset = max(uplinkOffset, (uint32_t)30);
        scheduleUplink(uplinkOffset, prevUplink);

        // wrap back up
        if(!node.isActivated()) {
          // TODO decrease datarate until 0
          return;
        }

        if(doGNSS) {
          deviceState = START_GNSS;
        } else {
          deviceState = START_PM;
        }
      }
      break;
    }
		case(START_GNSS): {
      VextOn();

      // open GPS comms and configure for L1+L5, disabling GSA and GSV
      Serial1.setTimeout(50);
      Serial1.begin(115200, SERIAL_8N1, 33, 34);
      Serial1.println("$CFGSYS,h35155*68");
      Serial1.println("$CFGMSG,0,2,0*05");
      Serial1.println("$CFGMSG,0,3,0*04");

      deviceState = WAIT_SATELLITE;
      break;
    }
    case(WAIT_SATELLITE): {
      if(gps.satellites.value()) {
        Serial.printf("[%d-%02d-%02d %02d:%02d:%02d] ", gps.date.year(), gps.date.month(), gps.date.day(),
                        gps.time.hour(), gps.time.minute(), gps.time.second());
        Serial.printf("% 8.5f, % 7.5f | HDOP % 4.1f | % 2d sats\r\n", gps.location.lat(), gps.location.lng(),
                        gps.hdop.hdop(), gps.satellites.value());

        deviceState = START_PM;
      }

      break;
    }
    case(START_PM): {
      pinMode(V5_CTRL, OUTPUT);
      digitalWrite(V5_CTRL, HIGH);
      delay(75);   // theoretical startup time is 50ms
      sen5x.begin(Wire);
      (void)sen5x.deviceReset();
      (void)sen5x.startMeasurement();

      deviceState = START_MIC;
      break; 
    }
    case(START_MIC): {
      xTaskCreatePinnedToCore(
        mic_get_db, 	/* Task function. */
        "Task1",   		/* name of task. */
        40000,   		/* Stack size of task */
        NULL,    		/* parameter of the task */
        1,     		/* priority of the task */
        NULL,  		/* Task handle to keep track of created task */
        0);    		/* pin task to core 0 */

      tStart = time(NULL);

      deviceState = START_CO2;
      break;
    }
    case(START_CO2): {
      scd4x.begin(Wire, 0x62);
      (void)scd4x.wakeUp();
      
      // manually call the measureSingleShot() registers as the library does a blocking call
      uint8_t buffer_ptr[9] = { 0 };
      SensirionI2CTxFrame txFrame =
          SensirionI2CTxFrame::createWithUInt16Command(0x219d, buffer_ptr, 2);
      (void)SensirionI2CCommunication::sendFrame(0x62, txFrame, Wire);

      deviceState = MEAS_BAT;
      break;
    }
    case(MEAS_BAT): {
      batt_mv = analogReadMilliVolts(BAT_ADC) * 4.9f;
      Serial.printf("Battery: %d mV\r\n", batt_mv);

      deviceState = MEAS_TPH;
      break;
    }
    case(MEAS_TPH): {
      bme.begin();	// 119, &Wire
      bme.performReading();
      delay(200);
      bme.performReading();
      temp = bme.temperature;
      humi = bme.humidity;
      pres = bme.pressure / 100.0F;
      Serial.printf("Temp: %.2f, humi: %.2f, pres: %.2f\r\n", temp, humi, pres);
      
      deviceState = MEAS_LUM;
      break;
    }
    case(MEAS_LUM): {
      tsl.begin(&Wire, 41);
      tsl.enable();
      uint32_t full = tsl.getFullLuminosity();
      lumi = tsl.calculateLux(full & 0xFFFF, full >> 16);
      tsl.disable();
      Serial.printf("Lumi: %d\r\n", (int)lumi);
      
      deviceState = MEAS_UV;
      break;
    }
    case(MEAS_UV): {
      Serial.print("UV: ");
      veml.begin(VEML6070_1_T, &Wire);
      veml.sleep(false);
      delay(100);
      uva = veml.readUV();
      veml.sleep(true);
      Serial.println(uva);
      
      deviceState = MEAS_CO2;
      break;
    }
    case(MEAS_CO2): {
      bool co2_ready;
      (void)scd4x.getDataReadyStatus(co2_ready);

      if(co2_ready) {
        float scd_temp, scd_hum;
        (void)scd4x.readMeasurement(co2, scd_temp, scd_hum);
        // (void)scd4x.powerDown();
        Serial.printf("CO2: %d\r\n", co2);
        
        deviceState = MEAS_MIC;
      }

      break;
    }
    case(MEAS_MIC): {
      // wait for a total of 30 seconds of measurement
      if(tNow - tStart > 30) {
        mic_stop = true;
        Serial.println("Stopping microphone");
        while (!mic_stopped)
          delay(10);
        db_min = zMeasurement.min;
        db_avg = zMeasurement.avg;
        db_max = zMeasurement.max;
        Serial.printf("dB: [%.1f | %.1f | %.1f]\r\n", db_min, db_avg, db_max);

        deviceState = MEAS_PM;
      }
      
      break;
    }
    case(MEAS_PM): {
      // wait for a total of 30 seconds of measurement
      if(tNow - tStart > 30) {
        (void)sen5x.readMeasuredValues(pm1_0, pm2_5, pm4_0, pm10_, hum5x, temp5x, vocIndex, noxIndex);
        digitalWrite(V5_CTRL, LOW);
        Serial.printf("PM2.5: %.2f, PM10: %.2f\n", pm2_5, pm10_);
        
        if(doGNSS) {
          deviceState = WAIT_GNSS;
        } else {
          deviceState = SENDRECEIVE;
        }
      }

      break;
    }
    case(WAIT_GNSS): {
      Serial.println("Waiting for GNSS fix");
      
      // if got a fix for five consecutive seconds, send uplink
      if (tNow > nextUplink && gpsFixLevel == GPS_GOOD_FIX) {
        deviceState = SENDRECEIVE;
        Serial1.end();
        Serial.printf("[%d-%02d-%02d %02d:%02d:%02d] ", gps.date.year(), gps.date.month(), gps.date.day(),
                        gps.time.hour(), gps.time.minute(), gps.time.second());
        Serial.printf("% 8.5f, % 7.5f | % 3.2f | % 2d\r\n", gps.location.lat(), gps.location.lng(),
                        gps.hdop.hdop(), gps.satellites.value());
                        
        deviceState = SENDRECEIVE;
      }
      // TODO wrap back around to measuring while no GNSS fix
      
      break;
    }
    case(SENDRECEIVE): {
      displayStyle->displayUplink();

      radio.standby();

      uint32_t airtime = 0;

      if(numStationaryUplinks == 0) {
          node.setADR(false);
          node.setDatarate(2);
      } else {
          node.setADR(true);
      }

      sendUplink();
      airtime += node.getLastToA();

      radio.sleep();
      
      if(isMotion) {
        numStationaryUplinks = 0;
      } else {
        numStationaryUplinks++;
      }
      wasMotion = isMotion;
      isMotion = false;

      tNow = time(NULL);                      // update tNow as sendUplink() is blocking

      // calculate when next uplink should be scheduled
      if (cfg.interval.fixed) {
        uplinkOffset = cfg.interval.period;
      } else {
        uplinkOffset = node.dutyCycleInterval((cfg.interval.dutycycle * 1000) / 24, airtime) / 1000 - 5;
      }
      uplinkOffset = max(uplinkOffset, (uint32_t)30);

      // if there was motion before this uplink, schedule another one soon
      // wrap back up to the begin, and read the DIP switches for the next measurement
      if(wasMotion) {
        scheduleUplink(30, prevUplink);
        deviceState = WAIT_SATELLITE;

        Serial.println("Reading DIP registers");
        doAllSensors = !digitalRead(DIP1);		  // include PM
        doAllIndices = !digitalRead(DIP2);		  // include VOC and NOx
        doFastInterval = !digitalRead(DIP3);	  // uplink interval
      } else {
        scheduleUplink(cfg.operation.heartbeat, prevUplink);
        deviceState = SHOW_MEAS;
      }

      break;
    }
    case(SHOW_MEAS): {
      VextOff();

      display_eink();

      deviceState = SLEEP;
      break;
    }
    case(SLEEP): {
      turnOff();
      
      deviceState = JOIN;
      break;	
    }
    default: {
      deviceState = IDLE;
      break;
    }
  }

  if(usbState && Serial.available())
    handleSerialUSB();

  if(Serial1.available())
    handleSerialNmea();
	
}