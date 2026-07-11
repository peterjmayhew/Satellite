#include <Arduino.h>
#include "display.h"

// Live state (defined in main.cpp)
extern bool gpsFix;
extern int fixType;
extern int satellitesSCRN1;

// Link diagnostics (defined in main.cpp)
extern uint32_t diagBytesRx, diagNmeaCount, diagUbxSync, diagUbxFrames, diagUbxBadCk, diagCfgSends, diagLastByteMs;
extern uint32_t diagNavPvt, diagNavDop, diagNavStatus, diagNavCov, diagNavSat, diagMonRf;

// GPS link diagnostics screen. Shows byte/frame counters from the parser and a
// plain-English verdict so the cause of "no fix" can be found on-device.
void updateDisplayDiag() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.setCursor(6, 6);
  tft.print("GPS Link Diag");

  tft.setTextSize(1);
  int y = 34;
  const int step = 13;

  uint32_t age = (diagLastByteMs == 0) ? 999UL : (millis() - diagLastByteMs) / 1000UL;
  bool dataFlowing = (diagBytesRx > 0) && (age <= 3);

#if USE_UBX
  const char* mode = "UBX";
#else
  const char* mode = "NMEA";
#endif

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(6, y); tft.printf("Mode: %s   Baud: 38400", mode); y += step;

  tft.setTextColor(diagBytesRx > 0 ? ILI9341_GREEN : ILI9341_RED, ILI9341_BLACK);
  tft.setCursor(6, y); tft.printf("Bytes RX: %lu  age: %lus",
    (unsigned long)diagBytesRx, (unsigned long)age); y += step;

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(6, y); tft.printf("NMEA '$': %lu", (unsigned long)diagNmeaCount); y += step;

  tft.setTextColor(diagUbxFrames > 0 ? ILI9341_GREEN : ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(6, y); tft.printf("UBX sync:%lu frames:%lu badCk:%lu",
    (unsigned long)diagUbxSync, (unsigned long)diagUbxFrames, (unsigned long)diagUbxBadCk); y += step;

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(6, y); tft.printf("Cfg sends: %lu", (unsigned long)diagCfgSends); y += step;

  tft.setTextColor(gpsFix ? ILI9341_GREEN : ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(6, y); tft.printf("Fix: %s  type:%d  sats:%d",
    gpsFix ? "YES" : "NO", fixType, satellitesSCRN1); y += step;

  tft.setTextColor(ILI9341_DARKGREY, ILI9341_BLACK);
  tft.setCursor(6, y); tft.printf("PVT:%lu DOP:%lu STA:%lu COV:%lu SAT:%lu RF:%lu",
    (unsigned long)diagNavPvt, (unsigned long)diagNavDop, (unsigned long)diagNavStatus,
    (unsigned long)diagNavCov, (unsigned long)diagNavSat, (unsigned long)diagMonRf); y += step;

  y += 4;
  tft.drawFastHLine(6, y, 308, ILI9341_DARKGREY);
  y += 8;

  const char* diag;
  uint16_t dcol;
#if USE_UBX
  if (!dataFlowing) {
    diag = "No GPS data. Check module TX -> ESP GPIO16, power & baud.";
    dcol = ILI9341_RED;
  } else if (diagUbxFrames > 0) {
    if (gpsFix) { diag = "UBX link OK, fix acquired."; dcol = ILI9341_GREEN; }
    else { diag = "UBX link OK. Waiting for fix - needs a clear sky view."; dcol = ILI9341_YELLOW; }
  } else if (diagNmeaCount > 0 && diagUbxSync == 0) {
    diag = "Getting NMEA but NO UBX! Config is not reaching the module -> check ESP GPIO17 (TX) -> module RX wire.";
    dcol = ILI9341_RED;
  } else if (diagUbxSync > 0) {
    diag = "UBX sync but no valid frames. Baud mismatch or line noise?";
    dcol = ILI9341_YELLOW;
  } else {
    diag = "Data flowing but unrecognized. Check baud / wiring.";
    dcol = ILI9341_YELLOW;
  }
#else
  if (!dataFlowing) {
    diag = "No GPS data. Check module TX -> ESP GPIO16, power & baud.";
    dcol = ILI9341_RED;
  } else if (gpsFix) {
    diag = "NMEA link OK, fix acquired.";
    dcol = ILI9341_GREEN;
  } else {
    diag = "NMEA data flowing. Waiting for fix - needs a clear sky view.";
    dcol = ILI9341_YELLOW;
  }
#endif

  tft.setTextColor(dcol, ILI9341_BLACK);
  tft.setCursor(6, y);
  tft.print(diag);   // TFT_eSPI wraps long text at the right edge
}
