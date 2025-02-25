/*
  This software, the ideas and concepts is Copyright (c) David Bird 2022
  All rights to this software are reserved.
  It is prohibited to redistribute or reproduce of any part or all of the software contents in any form other than the following:
  1. You may print or download to a local hard disk extracts for your personal and non-commercial use only.
  2. You may copy the content to individual third parties for their personal use, but only if you acknowledge the author David Bird as the source of the material.
  3. You may not, except with my express written permission, distribute or commercially exploit the content.
  4. You may not transmit it or store it in any other website or other form of electronic retrieval system for commercial purposes.
  5. You MUST include all of this copyright and permission notice ('as annotated') and this shall be included in all copies or substantial portions of the software
     and where the software use is visible to an end-user.
  6. *** DONT USE THE SOFTWARE IF YOU DONT LIKE THE LICNCE CONDITIONS ***
  THE SOFTWARE IS PROVIDED "AS IS" FOR PRIVATE USE ONLY, IT IS NOT FOR COMMERCIAL USE IN WHOLE OR PART OR CONCEPT.
  FOR PERSONAL USE IT IS SUPPLIED WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR
  A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OR
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  See more at http://dsbird.org.uk
*/

#include <Arduino.h>
#include <LittleFS.h>      // Built-in
#include <WiFi.h>        // Built-in
#include <ESPmDNS.h>       // Built-in
#include <AsyncTCP.h>      // https://github.com/me-no-dev/AsyncTCP
#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer
#include <ElegantOTA.h>        // https://github.com/ayushsharma82/ElegantOTA
#include "esp_system.h"    // Built-in
#include "esp_wifi_types.h"  // Built-in
#include "esp_bt.h"      // Built-in
#include "esp_eap_client.h"
#include "esp_wifi.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "config.h"

#include "fs_browser.h"

#define FS LittleFS

AsyncWebServer server(80);

//################  VERSION  ###########################################
String Version_MQ = MJLO_VERSION;
String Version_FB = "v1.1";
//################ VARIABLES ###########################################

typedef struct
{
  String filename;
  String ftype;
  String fsize;
} fileinfo;

String   webpage, MessageLine;
fileinfo Filenames[366]; // Enough for most purposes!
bool   StartupErrors = false;
int    start, downloadtime = 1, uploadtime = 1, downloadsize, uploadsize, downloadrate, uploadrate, numfiles;

wifi_mode_t wifiMode = WIFI_MODE_NULL;
IPAddress IP;

bool connectWiFi() {
  wifiMode = WIFI_MODE_STA;
  Serial.printf("Attemping WiFi connection\r\n");
  // WiFi.disconnect(true);
  bool mod = WiFi.mode(wifiMode);

  if (cfg.wl2g4.user == "") {
    // the line below is for connecting to an 'open' network
    wl_status_t begin = WiFi.begin(cfg.wl2g4.ssid.c_str(), cfg.wl2g4.pass.c_str());
    Serial.printf("WiFi begin: %d . %d\r\n", mod, begin);
  } else {
    // the lines below are for connecting to a WPA2 enterprise network 
    // (taken from the oficial wpa2_enterprise example from esp-idf)
    ESP_ERROR_CHECK( esp_eap_client_set_identity((uint8_t *)cfg.wl2g4.user.c_str(), strlen(cfg.wl2g4.user.c_str())) );
    ESP_ERROR_CHECK( esp_eap_client_set_identity((uint8_t *)cfg.wl2g4.user.c_str(), strlen(cfg.wl2g4.user.c_str())) );
    ESP_ERROR_CHECK( esp_eap_client_set_password((uint8_t *)cfg.wl2g4.pass.c_str(), strlen(cfg.wl2g4.pass.c_str())) );
    ESP_ERROR_CHECK( esp_wifi_sta_enterprise_enable() );
    WiFi.begin(cfg.wl2g4.ssid.c_str());
  }

  Serial.printf("Connecting to [%s] with password [%s]...\r\n", cfg.wl2g4.ssid.c_str(), cfg.wl2g4.pass.c_str());

  Serial.printf("Waiting for connection result..\r\n");
  uint8_t wifiStatus = WiFi.waitForConnectResult(20000);
  Serial.printf("Status: %d\r\n", wifiStatus);
  switch (wifiStatus) {
    case WL_NO_SSID_AVAIL:
      return false;
    case WL_CONNECTED:
      IP = WiFi.localIP();
      break;
    case WL_DISCONNECTED:
      WiFi.disconnect(true);
      wifiMode = WIFI_MODE_AP;
      WiFi.mode(wifiMode);
      WiFi.softAP(cfg.wl2g4.ssid.c_str(), cfg.wl2g4.pass.c_str());
      IP = WiFi.softAPIP();
      break;
  }

  Serial.printf("IP Address: %s\r\n", IP.toString().c_str());
  if (WiFi.scanComplete() == -2) 
    WiFi.scanNetworks(true);          // Complete an initial scan for WiFi networks, otherwise = 0 on first display!
  
  esp_err_t err = mdns_init();          // Initialise mDNS service
  if (!err) {
    mdns_hostname_set(cfg.wl2g4.name.c_str());      // Set hostname
  } else {
    printf("MDNS Init failed: %d\n", err);
  }
  return true;
}

void disconnectWiFi() {
  wifiMode = WIFI_MODE_NULL;
  bool dis = WiFi.disconnect(true);
  bool mod = WiFi.mode(wifiMode);
  Serial.printf("WiFi disconnect: %d . %d\r\n", dis, mod);
}


void start_file_browser() {

  // ##################### HOMEPAGE HANDLER ###########################
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("Home Page...");
    
    Home(); // Build webpage ready for display
    request->send(200, "text/html", webpage);
  });

  // ##################### DOWNLOAD HANDLER ##########################
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("Downloading file...");
    
    Select_File_For_Function("[DOWNLOAD]", "downloadhandler"); // Build webpage ready for display
    request->send(200, "text/html", webpage);
  });

  // ##################### UPLOAD HANDLERS ###########################
  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("Uploading file...");
    
    UploadFileSelect(); // Build webpage ready for display
    request->send(200, "text/html", webpage);
  });

  // Set handler for '/handleupload'
  server.on("/handleupload", HTTP_POST, [](AsyncWebServerRequest * request) {},
  [](AsyncWebServerRequest * request, const String & filename, size_t index, uint8_t *data,
     size_t len, bool final) {
    
    handleFileUpload(request, filename, index, data, len, final);
  });

  // Set handler for '/handleformat'
  server.on("/handleformat", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("Processing Format Request of File System...");
    
    if (request->getParam("format")->value() == "YES") {
      Serial.print("Starting to Format Filing System...");
      FS.end();
      bool formatted = FS.format();
      if (formatted) {
        Serial.println(" Successful Filing System Format...");
      }
      else     {
        Serial.println(" Formatting Failed...");
      }
    }
    request->redirect("/dir");
  });

  // ##################### STREAM HANDLER ############################
  server.on("/stream", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("Streaming file...");
          
    Select_File_For_Function("[STREAM]", "streamhandler"); // Build webpage ready for display
    request->send(200, "text/html", webpage);
  });

  // ##################### RENAME HANDLER ############################
  server.on("/rename", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("Renaming file...");
    
    File_Rename(); // Build webpage ready for display
    request->send(200, "text/html", webpage);
  });

  // ##################### DIR HANDLER ###############################
  server.on("/dir", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("File Directory...");
    
    Dir(request); // Build webpage ready for display
    request->send(200, "text/html", webpage);
  });

  // ##################### DELETE HANDLER ############################
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("Deleting file...");
          
    Select_File_For_Function("[DELETE]", "deletehandler"); // Build webpage ready for display
    request->send(200, "text/html", webpage);
  });

  // ##################### FORMAT HANDLER ############################
  server.on("/format", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("Request to Format File System...");
          
    Format(); // Build webpage ready for display
    request->send(200, "text/html", webpage);
  });

  // ##################### SYSTEM HANDLER ############################
  server.on("/system", HTTP_GET, [](AsyncWebServerRequest * request) {
    
    Display_System_Info(); // Build webpage ready for display
    request->send(200, "text/html", webpage);
  });

  // ##################### IMAGE HANDLER ############################
  server.on("/icon", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(FS, "/icon.gif", "image/gif");
  });

  ElegantOTA.setAutoReboot(true);
  ElegantOTA.begin(&server);  // Start ElegantOTA

  // ##################### NOT FOUND HANDLER #########################
  server.onNotFound(notFound);

  server.begin();  // Start the server
  Serial.println("System started successfully...");
  Directory();   // Update the file list
}

void end_file_browser() {
  server.end();
}
//#############################################################################################
String HTML_Header = "\
<!DOCTYPE html>\
<html lang='en'>\
<head>\
<meta charset='UTF-8'>\
<meta name='viewport' content='width=device-width, initial-scale=1.0'>\
<title>LoRangeFinder-1 webserver</title>\
<style>\
body{margin:0;padding:0;font-family:Arial,sans-serif;background-color:#f4f4f4;overflow-x:hidden;margin-bottom:120px}\
header{background-color:#333;color:#fff;padding:10px;text-align:center;width:100%}\
nav{background-color:#444;padding:10px;display:flex;justify-content:space-around;width:100%}\
nav a{text-decoration:none;color:#fff;padding:8px 16px;border-radius:5px;transition:background-color .3s ease}\
nav a:hover{background-color:#555}\
table{width:80%;margin:20px auto;border-collapse:collapse;background-color:#fff;box-shadow:0 0 10px rgba(0,0,0,.1)}\
th,td{padding:12px;text-align:left;border-bottom:1px solid #ddd}\
th{background-color:#333;color:#fff}\
tbody tr:nth-child(even){background-color:#f0f0f0}\
footer{background-color:#333;color:#fff;text-align:center;padding:10px;position:fixed;bottom:0;width:100%}\
h3{text-align:center}\
h4{color:#333;text-align:center}\
@media only screen and (max-width: 600px) {nav{flex-direction:column;text-align:center}nav a{margin-bottom:5px}}\
</style>\
</head>\
<body>\
<header><h1>LoRangeFinder-1 webserver</h1></header>\
<nav>\
<a href='/'>Home</a>\
<a href='/dir'>Directory</a>\
<a href='/download'>Download</a>\
<a href='/stream'>Stream</a>\
<a href='/delete'>Delete</a>\
<a href='/system'>Status</a>\
<a href='/update'>Update</a>\
</nav>";
// <a href='/upload'>Upload</a>\
// <a href='/rename'>Rename</a>\

String HTML_Footer = "\
<footer>\
<p>LoRangeFinder-1: Copyright © Kroonos (" + Version_MQ + ")</p>\
<p>File-browser: Copyright © D L Bird (" + Version_FB + ")</p>\
</footer>\
</body>\
</html>";
//#############################################################################################
void Dir(AsyncWebServerRequest * request) {
  String Fname1, Fname2;
  int index = 0;
  Directory(); // Get a list of the current files on the FS
  webpage  = HTML_Header;
  webpage += "<h3>File System Content</h3><br>";
  if (numfiles > 0) {
    webpage += "<table class='center'>";
    webpage += "<tr><th>Type</th><th>File Name</th><th>File Size</th><th class='sp'></th><th>Type</th><th>File Name</th><th>File Size</th></tr>";
    while (index < numfiles) {
      Fname1 = Filenames[index].filename;
      Fname2 = Filenames[index + 1].filename;
      webpage += "<tr>";
      webpage += "<td style = 'width:5%'>" + Filenames[index].ftype + "</td><td style = 'width:25%'>" + Fname1 + "</td><td style = 'width:10%'>" + Filenames[index].fsize + "</td>";
      webpage += "<td class='sp'></td>";
      if (index < numfiles - 1) {
        webpage += "<td style = 'width:5%'>" + Filenames[index + 1].ftype + "</td><td style = 'width:25%'>" + Fname2 + "</td><td style = 'width:10%'>" + Filenames[index + 1].fsize + "</td>";
      }
      webpage += "</tr>";
      index = index + 2;
    }
    webpage += "</table>";
    webpage += "<p style='background-color:yellow;'><b>" + MessageLine + "</b></p>";
    MessageLine = "";
  }
  else
  {
    webpage += "<h2>No Files Found</h2>";
  }
  webpage += HTML_Footer;
  request->send(200, "text/html", webpage);
}
//#############################################################################################
void Directory() {
  numfiles  = 0; // Reset number of FS files counter
  File root = FS.open("/");
  if (root) {
    root.rewindDirectory();
    File file = root.openNextFile();
    while (file) { // Now get all the filenames, file types and sizes
      String filename = file.name();
      Filenames[numfiles].filename = (String(file.name()).startsWith("/") ? String(file.name()).substring(1) : file.name());
      Filenames[numfiles].ftype  = (file.isDirectory() ? "Dir" : "File");
      Filenames[numfiles].fsize  = ConvBinUnits(file.size(), 1);
      numfiles++;
      file = root.openNextFile();
    }
    root.close();
  }
}
//#############################################################################################
void UploadFileSelect() {
  webpage  = HTML_Header;
  webpage += "<h3>Select a File to [UPLOAD] to this device</h3>";
  webpage += "<form method = 'POST' action = '/handleupload' enctype='multipart/form-data'>";
  webpage += "<input type='file' name='filename'><br><br>";
  webpage += "<input type='submit' value='Upload'>";
  webpage += "</form>";
  webpage += HTML_Footer;
}
//#############################################################################################
void Format() {
  webpage  = HTML_Header;
  webpage += "<h3>***  Format Filing System on this device ***</h3>";
  webpage += "<form action='/handleformat'>";
  webpage += "<input type='radio' id='YES' name='format' value = 'YES'><label for='YES'>YES</label><br><br>";
  webpage += "<input type='radio' id='NO'  name='format' value = 'NO' checked><label for='NO'>NO</label><br><br>";
  webpage += "<input type='submit' value='Format?'>";
  webpage += "</form>";
  webpage += HTML_Footer;
}
//#############################################################################################
void handleFileUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    String file = filename;
    if (!filename.startsWith("/")) file = "/" + filename;
    request->_tempFile = FS.open(file, "w");
    if (!request->_tempFile) Serial.println("Error creating file for upload...");
    start = millis();
  }
  if (request->_tempFile) {
    if (len) {
      request->_tempFile.write(data, len); // Chunked data
      Serial.println("Transferred : " + String(len) + " Bytes");
    }
    Directory();              // update file list
    if (final) {
      uploadsize = request->_tempFile.size();
      request->_tempFile.close();
      uploadtime = millis() - start;
      request->redirect("/dir");
    }
  }
}
//#############################################################################################
void Handle_File_Delete(String filename) { // Delete the file
  webpage = HTML_Header;
  if (!filename.startsWith("/")) filename = "/" + filename;
  File dataFile = FS.open(filename, "r"); // Now read FS to see if file exists
  if (dataFile) {  // It does so delete it
    dataFile.close();  // as per https://github.com/lorol/LITTLEFS/issues/22
    FS.remove(filename);
    Directory();    // update file list
    webpage += "<h3>File '" + filename.substring(1) + "' has been deleted</h3>";
    webpage += "<a href='/dir'>[Enter]</a><br><br>";
  }
  else
  {
    webpage += "<h3>File [ " + filename + " ] does not exist</h3>";
    webpage += "<a href='/dir'>[Enter]</a><br><br>";
  }
  webpage  += HTML_Footer;
}
//#############################################################################################
void File_Rename() { // Rename the file
  webpage = HTML_Header;
  webpage += "<h3>Select a File to [RENAME] on this device</h3>";
  webpage += "<FORM action='/renamehandler'>";
  webpage += "<table class='center'>";
  webpage += "<tr><th>File name</th><th>New Filename</th><th>Select</th></tr>";
  int index = 0;
  while (index < numfiles) {
    webpage += "<tr><td><input type='text' name='oldfile' style='color:blue;' value = '" + Filenames[index].filename + "' readonly></td>";
    webpage += "<td><input type='text' name='newfile'></td><td><input type='radio' name='choice'></tr>";
    index++;
  }
  webpage += "</table><br>";
  webpage += "<input type='submit' value='Enter'>";
  webpage += "</form>";
  webpage += HTML_Footer;
}
//#############################################################################################
void Handle_File_Rename(AsyncWebServerRequest *request, String filename, int Args) { // Rename the file
  String newfilename;
  //int Args = request->args();
  webpage = HTML_Header;
  for (int i = 0; i < Args; i++) {
    if (request->arg(i) != "" && request->arg(i + 1) == "on") {
      filename  = request->arg(i - 1);
      newfilename = request->arg(i);
    }
  }
  if (!filename.startsWith("/"))  filename = "/" + filename;
  if (!newfilename.startsWith("/")) newfilename = "/" + newfilename;
  File CurrentFile = FS.open(filename, "r");  // Now read FS to see if file exists
  if (CurrentFile && (filename != "/") && (newfilename != "/") && (filename != newfilename)) { // It does so rename it, ignore if no entry made, or Newfile name exists already
    CurrentFile.close();  // gotta close first
    if (FS.rename(filename, newfilename)) {
      filename  = filename.substring(1);
      newfilename = newfilename.substring(1);
      webpage += "<h3>File '" + filename + "' has been renamed to '" + newfilename + "'</h3>";
      webpage += "<a href='/dir'>[Enter]</a><br><br>";
      Directory();  // update file list
    }
  }
  else
  {
    if (filename == "/" && newfilename == "/") webpage += "<h3>File was not renamed</h3>";
    else webpage += "<h3>New filename exists, cannot rename</h3>";
    webpage += "<a href='/rename'>[Enter]</a><br><br>";
  }
  CurrentFile.close();
  webpage  += HTML_Footer;
}

//#############################################################################################
// Not found handler is also the handler for 'delete', 'download' and 'stream' functions
void notFound(AsyncWebServerRequest *request) { // Process selected file types
  String filename;
  if (request->url().startsWith("/downloadhandler") ||
      request->url().startsWith("/streamhandler")   ||
      request->url().startsWith("/deletehandler")   ||
      request->url().startsWith("/renamehandler"))
  {
    
    // Now get the filename and handle the request for 'delete' or 'download' or 'stream' functions
    if (!request->url().startsWith("/renamehandler")) filename = request->url().substring(request->url().indexOf("~/") + 1);
    start = millis();
    if (request->url().startsWith("/downloadhandler"))
    {
      Serial.println("Download handler started...");
      MessageLine = "";
      File file = FS.open(filename, "r");
      String contentType = getContentType("download");
      AsyncWebServerResponse *response = request->beginResponse(contentType, file.size(), [file](uint8_t *buffer, size_t maxLen, size_t total) mutable ->  size_t
                                                                { return file.read(buffer, maxLen); });
      response->addHeader("Server", "ESP Async Web Server");
      request->send(response);
      downloadtime = millis() - start;
      downloadsize = GetFileSize(filename);
      // request->redirect("/dir");
    }
    if (request->url().startsWith("/streamhandler"))
    {
      Serial.println("Stream handler started...");
      String ContentType = getContentType(filename);
      AsyncWebServerResponse *response = request->beginResponse(FS, filename, ContentType);
      request->send(response);
      downloadsize = GetFileSize(filename);
      downloadtime = millis() - start;
      // request->redirect("/dir");
    }
    if (request->url().startsWith("/deletehandler"))
    {
      Serial.println("Delete handler started...");
      Handle_File_Delete(filename); // Build webpage ready for display
      request->send(200, "text/html", webpage);
    }
    if (request->url().startsWith("/renamehandler"))
    {
      Handle_File_Rename(request, filename, request->args()); // Build webpage ready for display
      request->send(200, "text/html", webpage);
    }
  }
  else
  {
    Page_Not_Found();
    request->send(200, "text/html", webpage);
  }
}
//#############################################################################################
String getContentType(String filenametype) { // Tell the browser what file type is being sent
  if (filenametype == "download") {
    return "application/octet-stream";
  } else if (filenametype.endsWith(".txt"))  {
    return "text/plain";
  } else if (filenametype.endsWith(".htm"))  {
    return "text/html";
  } else if (filenametype.endsWith(".html")) {
    return "text/html";
  } else if (filenametype.endsWith(".css"))  {
    return "text/css";
  } else if (filenametype.endsWith(".js"))   {
    return "application/javascript";
  } else if (filenametype.endsWith(".png"))  {
    return "image/png";
  } else if (filenametype.endsWith(".gif"))  {
    return "image/gif";
  } else if (filenametype.endsWith(".jpg"))  {
    return "image/jpeg";
  } else if (filenametype.endsWith(".ico"))  {
    return "image/x-icon";
  } else if (filenametype.endsWith(".xml"))  {
    return "text/xml";
  } else if (filenametype.endsWith(".pdf"))  {
    return "application/x-pdf";
  } else if (filenametype.endsWith(".zip"))  {
    return "application/x-zip";
  } else if (filenametype.endsWith(".gz"))   {
    return "application/x-gzip";
  }
  return "text/plain";
}
//#############################################################################################
void Select_File_For_Function(String title, String function) {
  String Fname1, Fname2;
  int index = 0;
  webpage = HTML_Header;
  webpage += "<h3>Select a File to " + title + " from this device</h3>";
  webpage += "<table class='center'>";
  webpage += "<tr><th>File Name</th><th>File Size</th><th class='sp'></th><th>File Name</th><th>File Size</th></tr>";
  while (index < numfiles) {
    Fname1 = Filenames[index].filename;
    Fname2 = Filenames[index + 1].filename;
    if (Fname1.startsWith("/")) Fname1 = Fname1.substring(1);
    if (Fname2.startsWith("/")) Fname2 = Fname2.substring(1);
    webpage += "<tr>";
    webpage += "<td style='width:25%'><button><a href='" + function + "~/" + Fname1 + "'>" + Fname1 + "</a></button></td><td style = 'width:10%'>" + Filenames[index].fsize + "</td>";
    webpage += "<td class='sp'></td>";
    if (index < numfiles - 1) {
      webpage += "<td style='width:25%'><button><a href='" + function + "~/" + Fname2 + "'>" + Fname2 + "</a></button></td><td style = 'width:10%'>" + Filenames[index + 1].fsize + "</td>";
    }
    webpage += "</tr>";
    index = index + 2;
  }
  webpage += "</table>";
  webpage += HTML_Footer;
}
//#############################################################################################
int GetFileSize(String filename) {
  int filesize;
  File CheckFile = FS.open(filename, "r");
  filesize = CheckFile.size();
  CheckFile.close();
  return filesize;
}
//#############################################################################################
void Home() {
  webpage = HTML_Header;
  webpage += "<div style='text-align: center; margin: 20px;'>";
  webpage += "<svg version='1.2' baseProfile='tiny' xmlns='http://www.w3.org/2000/svg' viewBox='0 0 541.8 155.9' width='40%' preserveAspectRatio='xMidYMid' xml:space='preserve'><g fill='#231F20'><path d='m221.3 56.3-15.6 37.2 16.8 26.1H214l-15.8-25v25H191V56.3h7.2v36.9l15.5-36.9h7.6zM246.4 98.5l13.4 21.2h-8.5l-15.1-24.2v24.2H229V56.3c2.2-.5 4.6-.7 7.2-.7 23.2 0 26.5 33.2 10.2 42.9zm-10.2-4.2c17.4 0 17.4-31.8 0-31.8v31.8zM326.8 88c0 17.8-14.5 32.4-32.4 32.4-17.8 0-32.4-14.6-32.4-32.4 0-17.9 14.6-32.4 32.4-32.4 17.9 0 32.4 14.5 32.4 32.4zm-7.3 0c0-14.1-11.2-25.5-25.1-25.5S269.3 73.9 269.3 88c0 14 11.2 25.5 25.1 25.5S319.5 102 319.5 88zM397 88c0 17.8-14.5 32.4-32.4 32.4-17.8 0-32.4-14.6-32.4-32.4 0-17.9 14.6-32.4 32.4-32.4 17.9 0 32.4 14.5 32.4 32.4zm-7.3 0c0-14.1-11.2-25.5-25.1-25.5S339.5 73.9 339.5 88c0 14 11.2 25.5 25.1 25.5S389.7 102 389.7 88zM405.1 119.7V55.3h1.4l22.1 40.3V56.3h7.2v64.4h-1.4l-22.1-40.3v39.3h-7.2zM508.8 88c0 17.8-14.5 32.4-32.4 32.4-17.8 0-32.4-14.6-32.4-32.4 0-17.9 14.6-32.4 32.4-32.4 17.9 0 32.4 14.5 32.4 32.4zm-7.3 0c0-14.1-11.2-25.5-25.1-25.5s-25 11.4-25 25.5c0 14 11.2 25.5 25.1 25.5s25-11.5 25-25.5zM535.3 56.3v7.2c-1.5-.7-3.4-1.1-5.7-1.1-10 0-12 7.5-6.9 14.2l11.8 15.1c10.1 13 2.3 28.6-13.2 28.6-2.4 0-4.7-.3-6.7-.8v-7.1c1.8.7 4.1 1.1 6.7 1.1 10 0 13.4-9.7 7.8-16.9l-11.8-15.1c-9.6-12.4-3.3-25.9 12.3-25.9 2.1 0 4 .3 5.7.7z'/></g><path fill='none' stroke='#231F20' stroke-width='2.835' stroke-miterlimit='10' d='M-.2 85.8c9.2 1.3 36.4 6.3 58.9 29.5 15 15.4 21.3 32.1 24 41.3M165.6 85.8c-9.2 1.3-36.4 6.3-58.9 29.5-15 15.4-21.3 32.1-24 41.3M82.6.3C74.9 53.7 67 80.4 58.8 80.5c-3.3 0-9.7-9.9-14.3-26.5M82.6.3c7.7 53.4 15.7 80.2 23.8 80.2 4.7 0 9.4-8.8 14.3-26.5'/><path fill='none' stroke='#231F20' stroke-width='2.835' stroke-miterlimit='10' d='M1.2 103.6c4.1-.5 17.9-2.8 29.3-14.7 13.3-13.9 14-31.3 14-34.9M163.6 103.1c-4.1-.5-17.9-2.8-29.3-14.7C121 74.5 120.7 57.6 120.7 54'/></svg>";
  webpage += "</div>";
  webpage += "<p align='center'>Welcome to the webserver dashboard of your LoRangeFinder-1.</p>";
  webpage += "<p align='center'>Please use the menu to navigate to the different pages.</p>";
  webpage += HTML_Footer;
}
//#############################################################################################
void Page_Not_Found() {
  webpage = HTML_Header;
  webpage += "<div class='notfound'>";
  webpage += "<h1>Sorry</h1>";
  webpage += "<p>Error 404 - Page Not Found</p>";
  webpage += "</div><div class='left'>";
  webpage += "<p>The page you were looking for was not found, it may have been moved or is currently unavailable.</p>";
  webpage += "<p>Please check the address is spelt correctly and try again.</p>";
  webpage += "<p>Or click <b><a href='/'>[Here]</a></b> for the home page.</p></div>";
  webpage += HTML_Footer;
}
//#############################################################################################
void Display_System_Info() {
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  uint32_t size_flash_chip;
  esp_flash_get_size(NULL, &size_flash_chip);
  if (WiFi.scanComplete() == -2) WiFi.scanNetworks(true, false); // Scan parameters are (async, show_hidden) if async = true, don't wait for the result
  webpage = HTML_Header;
  webpage += "<h3>System Information</h3>";
  webpage += "<h4>Transfer Statistics</h4>";
  webpage += "<table class='center'>";
  webpage += "<tr><th>Last Upload</th><th>Last Download/Stream</th><th>Units</th></tr>";
  webpage += "<tr><td>" + ConvBinUnits(uploadsize, 1) + "</td><td>" + ConvBinUnits(downloadsize, 1) + "</td><td>File Size</td></tr> ";
  webpage += "<tr><td>" + ConvBinUnits((float)uploadsize / uploadtime * 1024.0, 1) + "/Sec</td>";
  webpage += "<td>" + ConvBinUnits((float)downloadsize / downloadtime * 1024.0, 1) + "/Sec</td><td>Transfer Rate</td></tr>";
  webpage += "</table>";
  webpage += "<h4>Filing System</h4>";
  webpage += "<table class='center'>";
  webpage += "<tr><th>Total Space</th><th>Used Space</th><th>Free Space</th><th>Number of Files</th></tr>";
  webpage += "<tr><td>" + ConvBinUnits(FS.totalBytes(), 1) + "</td>";
  webpage += "<td>" + ConvBinUnits(FS.usedBytes(), 1) + "</td>";
  webpage += "<td>" + ConvBinUnits(FS.totalBytes() - FS.usedBytes(), 1) + "</td>";
  webpage += "<td>" + (numfiles == 0 ? "Pending Dir or Empty" : String(numfiles)) + "</td></tr>";
  webpage += "</table>";
  webpage += "<h4>CPU Information</h4>";
  webpage += "<table class='center'>";
  webpage += "<tr><th>Parameter</th><th>Value</th></tr>";
  webpage += "<tr><td>Number of Cores</td><td>" + String(chip_info.cores) + "</td></tr>";
  webpage += "<tr><td>Chip revision</td><td>" + String(chip_info.revision) + "</td></tr>";
  webpage += "<tr><td>Internal or External Flash Memory</td><td>" + String(((chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded" : "External")) + "</td></tr>";
  webpage += "<tr><td>Flash Memory Size</td><td>" + String((size_flash_chip / (1024 * 1024))) + " MB</td></tr>";
  webpage += "<tr><td>Current Free RAM</td><td>" + ConvBinUnits(ESP.getFreeHeap(), 1) + "</td></tr>";
  webpage += "</table>";
  webpage += "<h4>Network Information</h4>";
  webpage += "<table class='center'>";
  webpage += "<tr><th>Parameter</th><th>Value</th></tr>";
  webpage += "<tr><td>LAN IP Address</td><td>"        + String(WiFi.localIP().toString()) + "</td></tr>";
  webpage += "<tr><td>Network Adapter MAC Address</td><td>" + String(WiFi.BSSIDstr()) + "</td></tr>";
  webpage += "<tr><td>WiFi SSID</td><td>"           + String(WiFi.SSID()) + "</td></tr>";
  webpage += "<tr><td>WiFi RSSI</td><td>"           + String(WiFi.RSSI()) + " dB</td></tr>";
  webpage += "<tr><td>WiFi Channel</td><td>"        + String(WiFi.channel()) + "</td></tr>";
  webpage += "<tr><td>WiFi Encryption Type</td><td>"    + String(EncryptionType(WiFi.encryptionType(0))) + "</td></tr>";
  webpage += "</table> ";
  webpage += HTML_Footer;
}
//#############################################################################################
String ConvBinUnits(int bytes, int resolution) {
  if    (bytes < 1024)         {
    return String(bytes) + " B";
  }
  else if (bytes < 1024 * 1024)      {
    return String((bytes / 1024.0), resolution) + " KB";
  }
  else if (bytes < (1024 * 1024 * 1024)) {
    return String((bytes / 1024.0 / 1024.0), resolution) + " MB";
  }
  else return "";
}
//#############################################################################################
String EncryptionType(wifi_auth_mode_t encryptionType) {
  switch (encryptionType) {
    case (WIFI_AUTH_OPEN):
      return "OPEN";
    case (WIFI_AUTH_WEP):
      return "WEP";
    case (WIFI_AUTH_WPA_PSK):
      return "WPA PSK";
    case (WIFI_AUTH_WPA2_PSK):
      return "WPA2 PSK";
    case (WIFI_AUTH_WPA_WPA2_PSK):
      return "WPA WPA2 PSK";
    case (WIFI_AUTH_WPA2_ENTERPRISE):
      return "WPA2 ENTERPRISE";
    case (WIFI_AUTH_MAX):
      return "WPA2 MAX";
    default:
      return "";
  }
}