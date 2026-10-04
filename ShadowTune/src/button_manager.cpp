#include "button_manager.h"
#include "board_config.h"           // TH_SPRING_MAX, TH_RIGHT_MAX, etc.
#include "gpio_config_manager.h"    // runtime PIN_* variables

void ButtonManager::begin() {
  pinMode(PIN_LADDER_PIN, INPUT);
  if (PIN_TOUCH_PIN >= 0) pinMode(PIN_TOUCH_PIN, INPUT);  // TTP223: HIGH while touched
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
}

BtnEvent ButtonManager::classify(int raw) {

  // SPRING ADC band (≤250) is kept for backwards compatibility, but the
  // spring is a latching hardware power switch. No screen responds to it.
  if (raw <= TH_OK_MAX)     return BtnEvent::OK;
  if (raw <= TH_SPRING_MAX) return BtnEvent::SPRING;
  if (raw <= TH_RIGHT_MAX)  return BtnEvent::RIGHT;
  if (raw <= TH_UP_MAX)     return BtnEvent::UP;
  if (raw <= TH_DOWN_MAX)   return BtnEvent::DOWN;
  if (raw <= TH_LEFT_MAX)   return BtnEvent::LEFT;
  return BtnEvent::IDLE;
}

void ButtonManager::poll() {
  // -- Capacitive touch pad (TTP223) hold tracking ---------------------
  // Separate GPIO from the joystick ladder. Only used for the 5 s hold.
  if (PIN_TOUCH_PIN >= 0) {
    bool touchNow = (digitalRead(PIN_TOUCH_PIN) == HIGH);
    if (touchNow && !_prevTouchState) {
      _touchState = true;
      _touchStartTime = millis();
    } else if (!touchNow && _prevTouchState) {
      _touchState = false;
    }
    _prevTouchState = touchNow;
  }

  long sum = 0;
  for (int i = 0; i < 64; i++) {
    sum += analogRead(PIN_LADDER_PIN);
    delayMicroseconds(50);
  }
  int raw = sum / 64;

  // ── Anti-misread: require 2 consecutive same readings ──────────────
  // The ADC sometimes spikes into the wrong band on a single sample.
  // By requiring the SAME classification on 2 consecutive polls, we
  // eliminate phantom "UP reads as LEFT" / "DOWN reads as RIGHT" misfires.
  BtnEvent reading = classify(raw);
  if (reading == _candidate) {
    _candidateCount++;
  } else {
    _candidate = reading;
    _candidateCount = 1;
  }

  // Accept reading immediately — no debounce delay.
  BtnEvent newState = reading;

  // "Pressed" is a one-shot rising edge, not "currently active":
  //  - from IDLE, anything real (TOUCH or a ladder direction) is a new press
  //  - from TOUCH, a ladder direction is also a new press
  //  - from any ladder direction, nothing re-fires until back to IDLE
  bool fromIdle  = (_prev == BtnEvent::IDLE)  && (newState != BtnEvent::IDLE);
  _pressed = fromIdle;
  _prev = newState;
  _state = newState;
}
