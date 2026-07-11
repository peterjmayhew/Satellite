#include "gnss_extra.h"

SatInfo skySats[64];
int skySatCount = 0;
uint32_t skyLastUpdateMs = 0;

void skyReset() {
  skySatCount = 0;
}

void skyAddSat(uint16_t prn, int elev, int az, int snr, char talker, bool used) {
  if (skySatCount >= 64) return;

  skySats[skySatCount].prn = prn;
  skySats[skySatCount].elev = elev;
  skySats[skySatCount].az = az;
  skySats[skySatCount].snr = snr;
  skySats[skySatCount].talker = talker;
  skySats[skySatCount].used = used;

  skySatCount++;
  skyLastUpdateMs = millis();
}
