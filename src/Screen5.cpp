#include <Arduino.h>
#include <TFT_eSPI.h>
#include "gnss_extra.h"
#include "config.h"   // if you keep globals here

extern TFT_eSPI tft;

static uint16_t snrColor(int snr) {
  if (snr < 0) return TFT_DARKGREY;
  if (snr < 20) return TFT_RED;
  if (snr < 35) return TFT_YELLOW;
  return TFT_GREEN;
}

static char talkerToChar(char t) {
  switch (t) {
    case 'P': return 'G'; // GPS
    case 'L': return 'R'; // GLONASS (R for Russia)
    case 'A': return 'E'; // Galileo (Europe)
    case 'B': return 'B'; // BeiDou
    default:  return '?';
  }
}

void updateDisplay5_SkyPlot() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 10);
  tft.print("Sky Plot (SNR)");

  // Draw circle "sky"
  const int cx = 160;     // center of plot (tweak for your rotation)
  const int cy = 135;
  const int R  = 90;

  tft.drawCircle(cx, cy, R, TFT_DARKGREY);
  tft.drawCircle(cx, cy, R * 2 / 3, TFT_DARKGREY);
  tft.drawCircle(cx, cy, R / 3, TFT_DARKGREY);
  tft.drawFastHLine(cx - R, cy, 2 * R, TFT_DARKGREY);
  tft.drawFastVLine(cx, cy - R, 2 * R, TFT_DARKGREY);

  // Cardinal labels
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(cx - 3, cy - R - 10); tft.print("N");
  tft.setCursor(cx - 3, cy + R + 2);  tft.print("S");
  tft.setCursor(cx - R - 10, cy - 3); tft.print("W");
  tft.setCursor(cx + R + 2,  cy - 3); tft.print("E");

  // If no data, say so
  if (skySatCount == 0) {
    tft.setTextSize(2);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(10, 210);
    tft.print("No sat data yet");
    return;
  }

  // Plot satellites
  for (int i = 0; i < skySatCount; i++) {
    const SatInfo &s = skySats[i];
    if (s.elev < 0 || s.elev > 90) continue;
    if (s.az < 0 || s.az >= 360) continue;

    // Convert az/elev to x/y
    // Radius from center: horizon = R, zenith = 0
    float frac = (90.0f - s.elev) / 90.0f;
    float rr = frac * R;

    float rad = (float)s.az * 3.1415926f / 180.0f;
    int x = cx + (int)(rr * sinf(rad));
    int y = cy - (int)(rr * cosf(rad));

    uint16_t col = snrColor(s.snr);
    tft.fillCircle(x, y, 4, col);

    // Tiny label: constellation + PRN last 2 digits
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(x + 6, y - 3);
    tft.print(talkerToChar(s.talker));
    tft.print((int)(s.prn % 100));
  }

  // Footer info
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(10, 225);
  tft.print("Sats: ");
  tft.print(skySatCount);
  tft.print("  age: ");
  tft.print((millis() - skyLastUpdateMs) / 1000);
  tft.print("s");
}
