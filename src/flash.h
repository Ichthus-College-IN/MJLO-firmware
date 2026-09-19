#ifndef _FLASH_H
#define _FLASH_H

#include <Arduino.h>
#include "config.h"
#include <LittleFS.h>
#include "esp_rom_crc.h"

void checkAvailableStorage(const char *dateBuf) {
    File root = LittleFS.open("/");
    File file = root.openNextFile();

    while (LittleFS.usedBytes() > 0.8 * LittleFS.totalBytes()) {
        if(!file)
            break;

        String path = file.path();
        file.close();

        Serial.printf("Removing file %s (%d)\r\n", path.c_str(), path.length());
        if(!LittleFS.remove(path)) {
            Serial.printf("Failed to remove file\r\n");
        }
        file = root.openNextFile();
    }
    
    char fileBuf[16];
    sprintf(fileBuf, "/%s.csv", dateBuf);
    Serial.printf("Opening file %s for appending\r\n", fileBuf);
    file = LittleFS.open(fileBuf, "a");
    if (!file.size()) {
        Serial.printf("Created new file\r\n");
        // print(), not println(): the measurement lines end in a bare \n, and a
        // CRLF header would leave the file with mixed line endings that trip up
        // CSV parsers (Papa Parse then reads the whole file as a single record)
        file.print("date,time,dev_eui,f_port,PayloadHex\n");
    }
    file.close();
}

// ============= Serial file transfer =============
// Machine-readable file access for a host (e.g. a WebSerial page).
// Every protocol line starts with '@' so it can be picked out between debug output.
//
//   +ls                        ->  @LS BEGIN
//                                  @F <size> <name>          (one per file)
//                                  @LS END <count> <usedBytes> <totalBytes>
//
//   +cat=<name>[,<off>[,<len>]] -> @CAT BEGIN <off> <len> <fileSize> <name>
//                                  <exactly len raw bytes>
//                                  (newline) @CAT END <crc32>   or   @CAT FAIL <bytesSent>
//
// The CRC32 is the IEEE/zlib variant over the raw bytes, as 8 uppercase hex digits.

// LittleFS is used as a flat root: reject anything that could leave it
static bool serialSafeName(const String &n) {
  return n.length() > 0 && n.length() < 64 &&
         n.indexOf('/') < 0 && n.indexOf('\\') < 0 && n.indexOf("..") < 0;
}

static bool parseUint32(const String &s, uint32_t &out) {
  if (s.length() == 0 || s.length() > 10) return false;
  uint64_t v = 0;
  for (size_t i = 0; i < s.length(); i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
  }
  if (v > UINT32_MAX) return false;
  out = (uint32_t)v;
  return true;
}

int serialListFiles() {
  File root = LittleFS.open("/");
  if (!root) return fileError;

  Serial.print("@LS BEGIN\r\n");
  uint32_t count = 0;
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      const char *raw  = f.name();
      const char *name = (raw[0] == '/') ? raw + 1 : raw;
      Serial.printf("@F %u %s\r\n", (unsigned)f.size(), name);
      count++;
    }
    f.close();
    f = root.openNextFile();
  }
  root.close();
  Serial.printf("@LS END %u %u %u\r\n", (unsigned)count,
                (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  return noError;
}

// value: "<name>[,<offset>[,<length>]]"; length is clamped to the end of the file
int serialDumpFile(const String &value) {
  String name = value;
  String offStr = "", lenStr = "";
  int c1 = value.indexOf(',');
  if (c1 >= 0) {
    name = value.substring(0, c1);
    int c2 = value.indexOf(',', c1 + 1);
    if (c2 >= 0) {
      offStr = value.substring(c1 + 1, c2);
      lenStr = value.substring(c2 + 1);
    } else {
      offStr = value.substring(c1 + 1);
    }
  }
  name.trim();
  if (!serialSafeName(name)) return valueError;

  uint32_t offset = 0, length = UINT32_MAX;
  if (c1 >= 0 && !parseUint32(offStr, offset)) return valueError;
  if (lenStr.length() > 0 && !parseUint32(lenStr, length)) return valueError;

  String path = "/" + name;
  if (!LittleFS.exists(path)) return fileError;
  File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory()) return fileError;

  uint32_t size = f.size();
  if (offset > size) offset = size;
  if (length > size - offset) length = size - offset;
  if (!f.seek(offset)) { f.close(); return fileError; }

  Serial.printf("@CAT BEGIN %u %u %u %s\r\n", (unsigned)offset, (unsigned)length,
                (unsigned)size, name.c_str());

  uint8_t buf[512];
  uint32_t crc = 0, sent = 0;
  bool ok = true;
  while (sent < length) {
    size_t want = min((size_t)(length - sent), sizeof(buf));
    size_t rd = f.read(buf, want);
    if (rd == 0) { ok = false; break; }
    size_t wr = Serial.write(buf, rd);
    crc = esp_rom_crc32_le(crc, buf, wr);
    sent += wr;
    if (wr != rd) { ok = false; break; }
    yield();
  }
  f.close();

  // pad a failed transfer so the host's byte count still lands on the trailer
  if (!ok) {
    memset(buf, 0, sizeof(buf));
    while (sent < length) {
      size_t chunk = min((size_t)(length - sent), sizeof(buf));
      size_t wr = Serial.write(buf, chunk);
      if (wr == 0) break;
      sent += wr;
    }
    Serial.printf("\r\n@CAT FAIL %u\r\n", (unsigned)sent);
    return fileError;
  }

  Serial.printf("\r\n@CAT END %08X\r\n", (unsigned)crc);
  return noError;
}

#endif
