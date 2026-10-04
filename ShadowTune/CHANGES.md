# ShadowTune changes (5 files)

Copy these over the same paths in your ShadowTune project (src/ and include/), then rebuild.

| File | What changed |
|------|--------------|
| include/button_manager.h | Added touchActive() / touchHoldDuration() for the TTP223 pad |
| src/button_manager.cpp   | Reads the touch pad pin (PIN_TOUCH_PIN, GPIO40) and tracks how long it is held |
| include/ui_screens.h     | checkSpringPowerOff() renamed to checkTouchPowerOff() |
| src/ui_screens.cpp       | 5 s touch hold -> power-off (any screen); wake on OK button (GPIO6 LOW, like SecureVault); leaves a "slept" flag for the resume screen |
| src/main.cpp             | New animated boot splash; resume screen now shown after a power-off even when started through the launcher |

Why the old power-off never worked: the function existed but nothing ever called it
(it also listened for a "spring" that the ladder can never report), and it woke on
GPIO6 HIGH, which is the idle level of the joystick ladder.

Boot splash colours: SPLASH_ACCENT_LO / SPLASH_ACCENT_HI at the top of drawBootSplash()
in src/main.cpp. Currently red-orange -> amber.
