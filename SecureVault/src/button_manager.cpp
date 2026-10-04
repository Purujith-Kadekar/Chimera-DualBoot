#include "button_manager.h"
#include "board_config.h"

void ButtonManager::begin() {
  pinMode(LADDER_PIN, INPUT);
  pinMode(TOUCH_PIN, INPUT);  // TTP223 capacitive touch pad — HIGH when touched
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
}

BtnEvent ButtonManager::classify(int raw) {
  // 5D joystick ADC classification.
  // ADC bands (calibrated with 5D joystick + 10K pull-up):
  //   OK    ≤ 157  (direct wire to GND)
  //   RIGHT ≤ 489  (1KΩ)
  //   UP    ≤ 935  (2.2KΩ)
  //   DOWN  ≤ 1565 (4.7KΩ)
  //   LEFT  ≤ 3009 (10KΩ)
  //   IDLE  > 3009 (no button pressed, pulled to 3V3)
  if (raw <= TH_OK_MAX)    return BtnEvent::OK;
  if (raw <= TH_RIGHT_MAX) return BtnEvent::RIGHT;
  if (raw <= TH_UP_MAX)    return BtnEvent::UP;
  if (raw <= TH_DOWN_MAX)  return BtnEvent::DOWN;
  if (raw <= TH_LEFT_MAX)  return BtnEvent::LEFT;
  return BtnEvent::IDLE;
}

void ButtonManager::poll() {
  // ── 1. Capacitive touch (TTP223) tracking ──────────────────────────
  // The TTP223 has a dedicated GPIO (HIGH=touching, LOW=not touching).
  // This is a SEPARATE input from the joystick — it handles ONLY the
  // 5-second hold-to-power-off. Short taps do NOTHING.
  _touchPressed = false;
  _touchReleased = false;

  bool touchNow = (digitalRead(TOUCH_PIN) == HIGH);

  if (touchNow && !_prevTouchState) {
    // Touch just detected (rising edge)
    _touchState = true;
    _touchStartTime = millis();
    _touchPressed = true;
  } else if (!touchNow && _prevTouchState) {
    // Touch just released (falling edge)
    _lastTouchDuration = millis() - _touchStartTime;
    _touchState = false;
    _touchReleased = true;
  }
  _prevTouchState = touchNow;

  // ── 2. Joystick ladder ADC read + classification ───────────────────
  long sum = 0;
  for (int i = 0; i < 64; i++) {
    sum += analogRead(LADDER_PIN);
    delayMicroseconds(50);
  }
  int raw = sum / 64;

  // ── OK button hold tracking (joystick MID) ─────────────────────────
  // Used for deferred OK dispatch (short tap = confirm, long hold = discard)
  // and pattern entry (dots & dashes) on the lock screen.
  // OK is NOT used for power off — that's the touch pad.
  _okPressed = false;
  _okReleased = false;

  bool okNow = (raw <= TH_OK_MAX);

  if (okNow && !_prevOkState) {
    _okState = true;
    _okStartTime = millis();
    _okPressed = true;
  } else if (!okNow && _prevOkState) {
    _lastOkDuration = millis() - _okStartTime;
    _okState = false;
    _okReleased = true;
  }
  _prevOkState = okNow;

  // ── Anti-misread: require 2 consecutive same readings ──────────────
  BtnEvent reading = classify(raw);
  if (reading == _candidate) {
    _candidateCount++;
  } else {
    _candidate = reading;
    _candidateCount = 1;
  }

  BtnEvent newState;
  if (reading == BtnEvent::IDLE || _candidateCount >= 2) {
    newState = reading;
  } else {
    newState = _state;
  }

  // "Pressed" is a one-shot rising edge from IDLE.
  bool fromIdle = (_prev == BtnEvent::IDLE) && (newState != BtnEvent::IDLE);
  _pressed = fromIdle;
  _prev = newState;
  _state = newState;
}
