#include <Arduino.h>
#include <BLEDevice.h>

#include "config.h"
#include "ble.h"

// BLE configurator
BLEConfigurator ble = BLEConfigurator();

class ServerCallback: public BLEServerCallbacks {
  BLEConfigurator &configurator;

  public:
    ServerCallback(BLEConfigurator &configurator) : configurator(configurator) {}

    void onConnect(BLEServer* pServer) {
      configurator.state = BLE_NEWCONN;
      return;
    };

    void onDisconnect(BLEServer* pServer) {
      configurator.state = BLE_NEWDISCONN;
      return;
    }
};

class BLECallbackWrite: public BLECharacteristicCallbacks {
  BLEConfigurator &configurator;

  public:
    BLECallbackWrite(BLEConfigurator &configurator) : configurator(configurator) {}

    void onWrite(BLECharacteristic *pCharacteristic) {
      configurator.command = pCharacteristic->getValue().c_str();
      configurator.state = BLE_UPDATED;
    }
};

class BLECallbackRead: public BLECharacteristicCallbacks {
  BLEConfigurator &configurator;

  public:
    BLECallbackRead(BLEConfigurator &configurator) : configurator(configurator) {}

    void onRead(BLECharacteristic *pCharacteristic) {
      if(configurator.state == BLE_CONNECTED) {
        configurator.state = BLE_TRIGGERED;
      }
    }
};

BLEConfigurator::BLEConfigurator() { 
  this->state = BLE_INACTIVE;
}

void BLEConfigurator::start(String bleName) {
  BLEDevice::init(bleName.c_str());                      // create the BLE Device
  
  this->pServer = BLEDevice::createServer();                // create the BLE Server
  this->pServer->setCallbacks(new ServerCallback(*this));
  
  // Create a BLE Service for TX/RX
  this->uartService  = this->pServer->createService(SERVICE_UUID_UART);    // create the BLE Service
  this->txEndpoint   = this->uartService->createCharacteristic(CHARSTC_UUID_TX, BLECharacteristic::PROPERTY_WRITE);
  this->txEndpoint   ->setCallbacks(new BLECallbackWrite(*this));
  this->rxEndpoint   = this->uartService->createCharacteristic(CHARSTC_UUID_RX, BLECharacteristic::PROPERTY_READ);    
  this->rxEndpoint   ->setValue("Enter a command in the TX field");

  this->readService  = this->pServer->createService(SERVICE_UUID_MAPQUEST);    // create the BLE Service
  this->ctrlEndpoint = this->readService->createCharacteristic(CHARSTC_UUID_CTRL, BLECharacteristic::PROPERTY_READ);
  this->ctrlEndpoint ->setCallbacks(new BLECallbackRead(*this));
  for (int i = 0; i < GROUP_COUNT; i++) {
    this->cfgEndpoints[i] = this->readService->createCharacteristic(characteristicUUIDs[i], BLECharacteristic::PROPERTY_READ);
  }

  this->uartService->start();           // start the service
  delay(10);
  this->readService->start();
  delay(10);
  this->pServer->startAdvertising();    // start advertising

  this->update(noError);
  
  Serial.printf("Now advertising BLE\r\n");
}

void BLEConfigurator::stop() {
  this->pServer->getAdvertising()->stop();
  this->uartService->stop();   // this makes the callback stop as well
  this->readService->stop();
  BLEDevice::deinit(false);
  delete this->pServer;
  delete this->uartService;
  delete this->readService;
  delete this->txEndpoint;
  delete this->rxEndpoint;
  for (int i = 0; i < GROUP_COUNT; i++) {
    delete this->cfgEndpoints[i];
  }
  Serial.printf("BLE was deinitialized\r\n");
}

void BLEConfigurator::update(int errorCode) {
  String errorString = parseError(errorCode);
  this->rxEndpoint->setValue(errorString.c_str());

  for (int i = 0; i < GROUP_COUNT; i++) {
    String info = printConfig(i);
    this->cfgEndpoints[i]->setValue(info.c_str());
  }
}