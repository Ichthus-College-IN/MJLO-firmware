#ifndef _LORAWAN_H
#define _LORAWAN_H

#include <RadioLib.h>
#include <SPI.h>

#include "config.h"

#if !defined(RADIOLIB_LORAWAN_NODE_R)
#define RADIOLIB_LORAWAN_NODE_R (0)
#endif

// LoRaWAN class
SPIClass spiSX(SPI);
SX1262 radio = new Module(8, 14, 12, 13, spiSX, RADIOLIB_DEFAULT_SPI_SETTINGS);   // NSS/CS, DIO0, RST, DIO1
#if RADIOLIB_LORAWAN_NODE_R
LoRaWANNodeR node(&radio, band);
#else
LoRaWANNode node(&radio, band);
#endif
uint8_t LWnonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
RTC_DATA_ATTR uint8_t LWsession[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
#if RADIOLIB_LORAWAN_NODE_R
RTC_DATA_ATTR uint8_t LWnoder[RADIOLIB_LORAWAN_NODER_BUF_SIZE];
#endif

// LoRaWAN uplink/downlink parameters
uint8_t fPort = 1;

const size_t maxAppDataSize = 51;
size_t appDataSize = 0;
uint8_t appData[maxAppDataSize] = { 0 };

size_t networkDataSize = 0;
uint8_t networkData[maxAppDataSize] = { 0 };

LoRaWANEvent_t eventUp;
LoRaWANEvent_t eventDown;

void scheduleUplink(int offset, uint32_t ref) {
  nextUplink = ref + offset;
}

bool lwBegin() {
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

  // dutycycle is handled by application
  node.setDutyCycle(false);

  return(true);
}

int16_t lwRestore(bool restoreSession = true) {
  int16_t state = RADIOLIB_ERR_UNKNOWN;

  store.begin("radiolib");
  if (store.isKey("nonces")) {
    radio.standby();

    store.getBytes("nonces", LWnonces, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    state = node.setBufferNonces(LWnonces);
    if(restoreSession) {
      state = node.setBufferSession(LWsession);
#if RADIOLIB_LORAWAN_NODE_R
      state = node.setBufferNodeR(LWnoder);
#endif
    }

    radio.sleep();
  }
  store.end();

  return(state);
}

void lwActivate(uint8_t dr) {
  int16_t state = RADIOLIB_ERR_NETWORK_NOT_JOINED;
  PRINTF("[LoRaWAN] Attempting network join ... ");

  radio.standby();
  
  if(cfg.actvn.method == OTAA) {
    state = node.activateOTAA(dr);
  } else {
    state = node.activateABP(dr);
  }

  radio.sleep();

  if(state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
    PRINTF("session restored!\r\n");
    return;
  } 

  store.begin("radiolib");
  uint8_t* persist = node.getBufferNonces();
  store.putBytes("nonces", persist, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
  store.end();

  persist = node.getBufferSession();
  memcpy(LWsession, persist, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
#if RADIOLIB_LORAWAN_NODE_R
  persist = node.getBufferNodeR();
  memcpy(LWnoder, persist, RADIOLIB_LORAWAN_NODER_BUF_SIZE);
#endif

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