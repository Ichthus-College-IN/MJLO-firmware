#ifndef _BLE_H
#define _BLE_H

#include <Arduino.h>
#include <BLEDevice.h>

#include "config.h"

enum BLEStates { BLE_INACTIVE, BLE_ACTIVE, BLE_NEWCONN, BLE_CONNECTED, BLE_UPDATED, BLE_TRIGGERED, BLE_NEWDISCONN };

// BLE service UUID values - NordicRF UART default
#define SERVICE_UUID_UART     "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARSTC_UUID_RX       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARSTC_UUID_TX       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define SERVICE_UUID_MAPQUEST "CE771570-0000-2103-3902-53746576656E"
#define CHARSTC_UUID_CTRL     "CE771570-0001-2103-3902-53746576656E"

const char characteristicUUIDs[NUM_GROUPS][37] = {
    "CE771570-0002-2103-3902-53746576656E",
    "CE771570-0003-2103-3902-53746576656E",
    "CE771570-0004-2103-3902-53746576656E",
    "CE771570-0005-2103-3902-53746576656E",
    "CE771570-0006-2103-3902-53746576656E"
};

class BLEConfigurator {
  public:
    BLEStates state;
    String command;

    BLEConfigurator();

    void start(String bleName);

    void stop();

    void update(int errorCode);

  private:
    BLEServer *pServer = NULL;
    BLEService *uartService = NULL;
    BLEService *readService = NULL;

    BLECharacteristic *txEndpoint = nullptr;
    BLECharacteristic *rxEndpoint = nullptr;
    BLECharacteristic *cfgEndpoints[NUM_GROUPS] = { nullptr };
    BLECharacteristic *ctrlEndpoint = nullptr;
};

// BLE configurator
extern BLEConfigurator ble;


#endif