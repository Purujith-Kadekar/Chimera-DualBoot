#pragma once
// ============================================================================
//  input.h -- joystick (resistor ladder) + touch, merged into one event.
//
//  Safety: nothing is reported until the inputs have been seen RELEASED for a
//  short time after boot. Otherwise the OK press that woke the device from
//  deep sleep (or a finger still on the screen) would instantly launch an app.
// ============================================================================
#include <Arduino.h>
#include "display.h"

enum class Key : uint8_t { NONE, OK, RIGHT, UP, DOWN, LEFT };

struct InputEvent {
  Key  key = Key::NONE;   // joystick press (one-shot, on press)
  bool tap = false;       // touchscreen tap (one-shot, on touch-down)
  bool tripleTap = false; // touch PAD tapped 3 times quickly (one-shot) = power off
  int  x = 0, y = 0;      // tap position in screen pixels
};

class Input {
public:
  explicit Input(Display& d) : _disp(d) {}
  void begin();
  InputEvent poll();      // call every loop iteration

private:
  Display& _disp;

  // joystick
  Key      _cand = Key::NONE;
  int      _candCount = 0;
  Key      _state = Key::NONE;
  uint32_t _joyIdleSince = 0;
  bool     _joyArmed = false;

  // touch
  uint32_t _lastTouchPoll = 0;
  uint32_t _lastTapMs = 0;
  bool     _touching = false;
  int      _noTouchCount = 0;
  bool     _touchArmed = false;

  // touch pad (TTP223) triple-tap detection
  bool     _padPrev = false;
  bool     _padArmed = false;       // pad must be seen released once after boot
  uint8_t  _padTaps = 0;
  uint32_t _padFirstTap = 0;
  uint32_t _padLastEdge = 0;

  static Key classify(int raw);
  int readLadder();
};
