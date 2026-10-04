#include "input.h"

#define TOUCH_POLL_MS      25
#define TAP_DEBOUNCE_MS    150
#define ARM_IDLE_MS        300    // inputs must be released this long after boot
#define PAD_DEBOUNCE_MS    40     // ignore pad edges closer together than this
#define TRIPLE_TAP_MS      1500   // all 3 taps must land within this window

void Input::begin() {
  pinMode(LADDER_PIN, INPUT);
  pinMode(TOUCH_PAD_PIN, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  _joyIdleSince = millis();
}

int Input::readLadder() {
  long sum = 0;
  for (int i = 0; i < 32; i++) {
    sum += analogRead(LADDER_PIN);
    delayMicroseconds(50);
  }
  return (int)(sum / 32);
}

Key Input::classify(int raw) {
  // Same bands as the apps' ButtonManager::classify().
  if (raw <= TH_OK_MAX)    return Key::OK;
  if (raw <= TH_RIGHT_MAX) return Key::RIGHT;
  if (raw <= TH_UP_MAX)    return Key::UP;
  if (raw <= TH_DOWN_MAX)  return Key::DOWN;
  if (raw <= TH_LEFT_MAX)  return Key::LEFT;
  return Key::NONE;
}

InputEvent Input::poll() {
  InputEvent ev;
  const uint32_t now = millis();

  // ---------------- joystick ----------------
  Key reading = classify(readLadder());
  if (reading == _cand) { if (_candCount < 1000) _candCount++; }
  else                  { _cand = reading; _candCount = 1; }

  // Accept a reading once it is stable for 2 polls (anti-misread), idle is
  // accepted immediately -- identical to the apps' logic.
  Key newState = (reading == Key::NONE || _candCount >= 2) ? reading : _state;

  if (newState == Key::NONE) {
    if (_state != Key::NONE) _joyIdleSince = now;   // just released
    if (!_joyArmed && (now - _joyIdleSince) >= ARM_IDLE_MS) _joyArmed = true;
  } else {
    if (!_joyArmed) _joyIdleSince = now;            // still held from before
  }

  if (_joyArmed && _state == Key::NONE && newState != Key::NONE) {
    ev.key = newState;                              // rising edge from idle
  }
  _state = newState;

  // ---------------- touch ----------------
  if (now - _lastTouchPoll >= TOUCH_POLL_MS) {
    _lastTouchPoll = now;
    int x = 0, y = 0;
    bool down = _disp.getTouchPoint(x, y);
    if (down) {
      _noTouchCount = 0;
      if (!_touching) {
        _touching = true;
        if (_touchArmed && (now - _lastTapMs) >= TAP_DEBOUNCE_MS) {
          ev.tap = true; ev.x = x; ev.y = y;
          _lastTapMs = now;
        }
      }
    } else {
      // need 2 consecutive "no touch" polls before treating it as released
      if (++_noTouchCount >= 2) {
        _touching = false;
        _touchArmed = true;      // seen released at least once -> safe
      }
    }
  }

  // ---------------- touch pad: triple-tap = power off ----------------
  bool pad = (digitalRead(TOUCH_PAD_PIN) == HIGH);
  if (!pad) _padArmed = true;                       // seen released -> safe
  if (pad && !_padPrev && _padArmed && (now - _padLastEdge) >= PAD_DEBOUNCE_MS) {
    _padLastEdge = now;
    if (_padTaps == 0 || (now - _padFirstTap) > TRIPLE_TAP_MS) {
      _padTaps = 1;                                 // start a new sequence
      _padFirstTap = now;
    } else if (++_padTaps >= 3) {
      ev.tripleTap = true;
      _padTaps = 0;
    }
  }
  _padPrev = pad;
  return ev;
}
