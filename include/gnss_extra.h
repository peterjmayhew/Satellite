#pragma once
#include <Arduino.h>

// A compact satellite record for a sky plot
struct SatInfo {
  uint16_t prn;     // satellite id
  int16_t elev;     // degrees 0..90
  int16_t az;       // degrees 0..359
  int16_t snr;      // dBHz, -1 if unknown
  char talker;      // 'P'=GPS, 'L'=GLONASS, 'A'=Galileo, 'B'=BeiDou, '?'=unknown
  bool used;        // true if this satellite is used in the position solution
};

extern SatInfo skySats[64];
extern int skySatCount;
extern uint32_t skyLastUpdateMs;

// Add a satellite to the sky list. `used` marks whether it is used in the fix.
void skyAddSat(uint16_t prn, int elev, int az, int snr, char talker, bool used = false);

// Optional: reset list (you can do this on entering Screen5)
void skyReset();
