#include <Arduino.h>
#include "config.h"

// --- Local debounce state ---
static bool lastButtonState = HIGH;
static bool buttonState = HIGH;
static unsigned long lastDebounceTime = 0;

// Returns true exactly once per physical press (falling edge, debounced).
// NOTE: this only *detects* the press. Advancing the screen (counter) is the
// caller's job via changeScreenBy() so a press moves exactly one screen.
bool checkButton() {
  const bool reading = digitalRead(BUTTON_PIN);
  bool pressedEvent = false;

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > (unsigned long)DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        pressedEvent = true;
        Serial.println("Button pressed -> next screen");
      }
    }
  }

  lastButtonState = reading;
  return pressedEvent;
}
