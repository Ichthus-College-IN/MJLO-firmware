#ifndef _FSBROWSER_H
#define _FSBROWSER_H

#include <Arduino.h>
#include <LittleFS.h>          // Built-in
#include <WiFi.h>              // Built-in
#include <ESPmDNS.h>           // Built-in
#include <AsyncTCP.h>          // https://github.com/me-no-dev/AsyncTCP
#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer
#include <ElegantOTA.h>   	   // https://github.com/ayushsharma82/ElegantOTA
#include "esp_system.h"        // Built-in
#include "esp_wifi_types.h"    // Built-in
#include "esp_bt.h"            // Built-in
#include "esp_eap_client.h"
#include "esp_wifi.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "config.h"

extern wifi_mode_t wifiMode;
extern IPAddress IP;

bool connectWiFi();
void disconnectWiFi();

void start_file_browser();
void end_file_browser();
extern String HTML_Header;
extern String HTML_Footer;
void Dir(AsyncWebServerRequest * request);
void Directory();
void UploadFileSelect();
void Format();
void handleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
void Handle_File_Delete(String filename);
void File_Rename();
void Handle_File_Rename();
void notFound(AsyncWebServerRequest *request);
String getContentType(String filenametype);
void Select_File_For_Function(String title, String function);
int GetFileSize(String filename);
void Home();
void Page_Not_Found();
void Display_System_Info();
String ConvBinUnits(int bytes, int resolution);
String EncryptionType(wifi_auth_mode_t encryptionType);

#endif