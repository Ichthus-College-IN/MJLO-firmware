#ifndef _LORAWAN_H
#define _LORAWAN_H

#include <RadioLib.h>
#include <SPI.h>

#include "config.h"

#if !defined(RADIOLIB_LORAWAN_NODE_R)
#define RADIOLIB_LORAWAN_NODE_R (0)
#endif

// LoRaWAN class
SPIClass spiSX(FSPI);
SX1262 radio = new Module(8, 14, 12, 13, spiSX, RADIOLIB_DEFAULT_SPI_SETTINGS);   // NSS/CS, DIO0, RST, DIO1
#if RADIOLIB_LORAWAN_NODE_R
LoRaWANNodeR node(&radio, band);
#else
LoRaWANNode node(&radio, band);
#endif
RTC_DATA_ATTR uint8_t LWsession[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
#if RADIOLIB_LORAWAN_NODE_R
RTC_DATA_ATTR uint8_t LWnoder[RADIOLIB_LORAWAN_NODER_BUF_SIZE];
#endif

// RadioLib pushes the persistence buffer whenever the nonces change (e.g. before every JoinRequest)
void lwStorePersistence(uint8_t* buffer, size_t len) {
  store.begin("radiolib");
  store.putBytes("nonces", buffer, len);
  store.end();
}

// RadioLib pulls the persistence buffer from loadBuffers(); clear it first, so that an
// empty or undersized entry is rejected by RadioLib instead of being parsed as garbage
void lwRestorePersistence(uint8_t* buffer, size_t len) {
  memset(buffer, 0, len);
  store.begin("radiolib");
  store.getBytes("nonces", buffer, len);
  store.end();
}

void lwStoreSession(uint8_t* buffer, size_t len) {
  memcpy(LWsession, buffer, len);
}

void lwRestoreSession(uint8_t* buffer, size_t len) {
  memcpy(buffer, LWsession, len);
}

// LoRaWAN uplink/downlink parameters
uint8_t fPort = 1;

const size_t maxFrameSize = 51;
size_t frameUpSize = 0;
uint8_t frameUp[maxFrameSize] = { 0 };

size_t frameDownSize = 0;
uint8_t frameDown[maxFrameSize] = { 0 };

LoRaWANEvent_t eventUp;
LoRaWANEvent_t eventDown;

void scheduleUplink(int offset, uint32_t ref) {
  nextUplink = ref + offset;
}

bool lwBegin() {
  // from here on, RadioLib stores both buffers by itself whenever they change
  node.setCallbackStorePersistence(lwStorePersistence);
  node.setCallbackStoreSession(lwStoreSession);

  if(cfg.actvn.method == OTAA) {
    if(isValidGroupOTAA()) {
      if(cfg.actvn.version == v104) {
        node.beginOTAA(cfg.actvn.otaa.joinEUI, cfg.actvn.otaa.devEUI, NULL, cfg.actvn.otaa.appKey);
      } else {
        node.beginOTAA(cfg.actvn.otaa.joinEUI, cfg.actvn.otaa.devEUI, cfg.actvn.otaa.nwkKey, cfg.actvn.otaa.appKey);
      }
    } else {
      PRINTF("[LoRaWAN] Activation failed: OTAA keys not complete");
      return(false);
    }
  } else {
    if(isValidGroupABP()) {
      if(cfg.actvn.version == v104) {
        node.beginABP(cfg.actvn.abp.devAddr, NULL, NULL, cfg.actvn.abp.nwkSEncKey, cfg.actvn.abp.appSKey);
      } else {
        node.beginABP(cfg.actvn.abp.devAddr, cfg.actvn.abp.fNwkSIntKey, cfg.actvn.abp.sNwkSIntKey, 
                      cfg.actvn.abp.nwkSEncKey, cfg.actvn.abp.appSKey);
      }
    } else {
      PRINTF("[LoRaWAN] Activation failed: ABP keys not complete");
      return(false);
    }
  }

  return(true);
}

// must be called after lwBegin(), as RadioLib checks the buffers against the current credentials
int16_t lwRestore(bool restoreSession = true) {
  radio.standby();

  // loadBuffers() only restores the buffers that have a callback registered
  node.setCallbackRestorePersistence(lwRestorePersistence);
  if(restoreSession) {
    node.setCallbackRestoreSession(lwRestoreSession);
  } else {
    node.setCallbackRestoreSession(nullptr);
  }

  int16_t state = node.loadBuffers();

  radio.sleep();

  return(state);
}

void lwActivate(uint8_t dr = RADIOLIB_LORAWAN_DATA_RATE_UNUSED) {
  int16_t state = RADIOLIB_ERR_NETWORK_NOT_JOINED;
  PRINTF("[LoRaWAN] Attempting network join ... ");

  radio.standby();

  if(dr != RADIOLIB_LORAWAN_DATA_RATE_UNUSED) {
    node.setDatarate(dr);
  }
  
  if(cfg.actvn.method == OTAA) {
    state = node.activateOTAA();
  } else {
    state = node.activateABP();
  }

  // dutycycle is handled by application
  node.setDutyCycle(false);

  radio.sleep();

  if(state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
    PRINTF("session restored!\r\n");
    return;
  } 


  if(state == RADIOLIB_LORAWAN_NEW_SESSION) {
    PRINTF("successfully started new session!\r\n");
    return;
  }

  if(state == RADIOLIB_ERR_NO_JOIN_ACCEPT) {
    PRINTF("failed, no JoinAccept received!\r\n");
  } else {
    PRINTF("failed, code %d!\r\n", state);
  }

}

#endif