# SecureVault boot animation (1 file)

Replace src/display_manager.cpp in your SecureVault project with the one in this zip, then rebuild.

Only DisplayManager::showBootSplash() changed (plus #include <math.h>). main.cpp is untouched
because it already calls disp.showBootSplash() on a normal boot.

Animation: a vault-door ring sweeps around with bolt ticks, a padlock appears open and its
shackle drops shut, the ring flashes on the "click", SECUREVAULT fades in, and a 12-segment
bolt bar fills. Colours: SPLASH_ACCENT / SPLASH_ACCENT_DIM at the top of showBootSplash()
(currently emerald green). boot_splash_preview.png shows frames of it.
