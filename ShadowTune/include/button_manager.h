#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  button_manager.h — 5-switch resistor ladder + TTP223 capacitive pad
//
//  The ladder provides RIGHT/UP/DOWN/LEFT (and SPRING, which is now a no-op
//  since the spring is a latching hardware power switch). The TTP223 pad
//  provides OK + direction button events from the 5D joystick.
//
//  Anti-misread: a reading must be STABLE for 2 consecutive polls before
//  it's accepted. This kills the "UP sometimes reads as LEFT" bug caused
//  by ADC noise / battery droop / component tolerance drift.
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>

enum class BtnEvent { IDLE, OK, SPRING, RIGHT, UP, DOWN, LEFT };

class ButtonManager {
public:
  void begin();
  void poll(); // averages the ladder ADC and updates state — call periodically

  BtnEvent state() const { return _state; }
  bool pressed() const { return _pressed; } // true only on the rising edge of a new press

  // For deferred TOUCH dispatch: simulates a TOUCH press so that
  // handleXxxButtons() sees _pressed=true and _state=TOUCH.
  // Used when TOUCH is dispatched on release (short tap) instead of
  // on press (rising edge), to distinguish short tap from long hold.

  // ── Pattern-entry support ──────────────────────────────────────────

  // -- Capacitive touch pad (TTP223) hold tracking -----------------------
  // HIGH = finger on the pad. Used ONLY for the 5-second hold-to-power-off.
  bool touchActive() const { return _touchState; }
  unsigned long touchHoldDuration() const {
    return _touchState ? (millis() - _touchStartTime) : 0;
  }

private:
  BtnEvent _state = BtnEvent::IDLE;
  BtnEvent _prev = BtnEvent::IDLE;
  bool _pressed = false;

  // ── Anti-misread: require 2 consecutive same readings ─────────────
  // The ADC sometimes spikes into the wrong band on a single sample
  // (noise, battery droop, touch-crosstalk). By requiring the SAME
  // classification on 2 consecutive polls, we eliminate phantom "UP
  // reads as LEFT" / "DOWN reads as RIGHT" misfires that were locking
  // the user out.
  BtnEvent _candidate = BtnEvent::IDLE;
  int _candidateCount = 0;


  // -- Capacitive touch pad hold tracking ---------------------------------
  bool _touchState = false;
  bool _prevTouchState = false;
  unsigned long _touchStartTime = 0;

  static BtnEvent classify(int raw);
};
