#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  button_manager.h — 5D navigation joystick + TTP223 capacitive touch pad
//
//  TWO separate inputs:
//    1. 5D joystick (resistor ladder on GPIO 6) — OK, RIGHT, UP, DOWN, LEFT
//    2. TTP223 capacitive touch pad (GPIO 40) — power off ONLY
//
//  The capacitive touch pad has ONE job:
//    - Long hold 5s → power off (deep sleep), fires IMMEDIATELY at 5s
//    - It does NOT act as confirm/select (that's the OK button only)
//    - Short taps do NOTHING (no pattern entry, no confirm)
//
//  Resume from deep sleep: OK button on the joystick (GPIO6, ext0 wake).
//  GPIO40 cannot be a wake source (outside GPIO1-21 range on ESP32-S3).
//
//  The OK button on the joystick is the ONE and ONLY confirm/select button.
//  It does NOT handle power off — that's the touch pad.
//
//  Anti-misread: a reading must be STABLE for 2 consecutive polls before
//  it's accepted. This kills the "UP sometimes reads as LEFT" bug caused
//  by ADC noise / battery droop / component tolerance drift.
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>

enum class BtnEvent { IDLE, OK, TOUCH, RIGHT, UP, DOWN, LEFT };

class ButtonManager {
public:
  void begin();
  void poll(); // averages the ladder ADC and updates state — call periodically

  BtnEvent state() const { return _state; }
  bool pressed() const { return _pressed; } // true only on the rising edge of a new press

  // simulateTouchPress() REMOVED — the touch pad is ONLY for 5s hold-to-
  // power-off. It does NOT act as confirm/select and has no pattern entry.

  // ── Capacitive touch (TTP223) hold tracking ────────────────────────
  // The TTP223 capacitive pad has a dedicated GPIO (HIGH=touching,
  // LOW=not touching). These track:
  //   - 5-second hold-to-power-off — long press triggers deep sleep
  //     (fires IMMEDIATELY at 5s, does not wait for release)
  // Short taps do NOTHING — no pattern entry, no confirm/select.
  // The touch pad is ONLY for power off.
  bool touchActive() const { return _touchState; }
  unsigned long touchHoldDuration() const {
    if (!_touchState) return 0;
    return millis() - _touchStartTime;
  }
  unsigned long lastTouchDuration() const { return _lastTouchDuration; }
  bool touchReleased() const { return _touchReleased; }
  bool touchPressed() const { return _touchPressed; }

  // ── OK button hold tracking (joystick MID) ─────────────────────────
  // Tracks the OK button on the joystick for deferred confirm dispatch
  // and pattern entry (dots & dashes) on the lock screen.
  // OK is NOT used for power off — that's the touch pad.
  bool okActive() const { return _okState; }
  unsigned long okHoldDuration() const {
    if (!_okState) return 0;
    return millis() - _okStartTime;
  }
  unsigned long lastOkDuration() const { return _lastOkDuration; }
  bool okReleased() const { return _okReleased; }
  bool okPressed() const { return _okPressed; }

private:
  BtnEvent _state = BtnEvent::IDLE;
  BtnEvent _prev = BtnEvent::IDLE;
  bool _pressed = false;

  // ── Anti-misread: require 2 consecutive same readings ─────────────
  BtnEvent _candidate = BtnEvent::IDLE;
  int _candidateCount = 0;

  // ── Capacitive touch (TTP223) hold tracking ───────────────────────
  bool _touchState = false;          // is touch pad currently held?
  bool _prevTouchState = false;      // was touch pad held last poll?
  unsigned long _touchStartTime = 0; // when touch was first detected (ms)
  unsigned long _lastTouchDuration = 0; // duration of last completed touch
  bool _touchPressed = false;        // one-shot: touch just detected (rising edge)
  bool _touchReleased = false;       // one-shot: touch just released (falling edge)

  // ── OK button hold tracking (joystick MID) ─────────────────────────
  bool _okState = false;          // is OK currently held?
  bool _prevOkState = false;      // was OK held last poll?
  unsigned long _okStartTime = 0; // when OK was first pressed (ms)
  unsigned long _lastOkDuration = 0; // duration of last completed OK press
  bool _okPressed = false;        // one-shot: OK just pressed (rising edge)
  bool _okReleased = false;       // one-shot: OK just released (falling edge)

  static BtnEvent classify(int raw);
};
