#pragma once
#include <Arduino.h>
#include "config.h"

bool sdInit(bool runSelfTest = false);
bool sdIsReady();

// NEW helper APIs
bool sdExists(const char* path);
bool sdWriteText(const char* path, const char* text, bool append = true);
bool sdReadLastNonEmptyLine(const char* path, String& outLine);
bool sdReadFileToString(const char* path, String& out, size_t maxBytes = 4096);

// Line reader: calls sink() once per line
typedef void (*SdLineSink)(const char* line, void* user);
bool sdForEachLine(const char* path,
                   SdLineSink sink,
                   void* user,
                   size_t maxLines = 200,
                   size_t maxLineLen = 200);

// Root directory listing (bus-locked). Returns the number of files written.
struct SdFileInfo {
  char name[64];
  uint32_t size;
  uint32_t mtime;   // time_t from getLastWrite(), 0 if unavailable
};
int sdListRoot(SdFileInfo* out, int maxFiles);

// GPS logger
void sdLogGPS(
  const String& nmeaLine,
  bool fix,
  int sats,
  float lat,
  float lon,
  float speedKmph,
  float heading,
  const String& dateUTC,
  const String& timeUTC
);
