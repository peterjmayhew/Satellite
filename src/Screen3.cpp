#include <Arduino.h>
#include "display.h"
#include "config.h"
#include "gnss_extra.h"

void updateDisplay3() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  tft.println("Satellites in view:");

  // Show count + age of last update
  tft.setTextSize(1);
  tft.printf("count=%d  age=%lus\n",
             skySatCount,
             (unsigned long)((millis() - skyLastUpdateMs) / 1000));
  tft.setTextSize(2);

  if (skySatCount <= 0) {
    tft.println("No GSV data yet");
    tft.println("(wait for GSV)");
    return;
  }

  // Print satellites
  for (int i = 0; i < skySatCount; i++) {
    tft.printf("%c %2d El:%2d Az:%3d SNR:%2d\n",
               skySats[i].talker,
               (int)skySats[i].prn,
               (int)skySats[i].elev,
               (int)skySats[i].az,
               (int)skySats[i].snr);
  }
}
