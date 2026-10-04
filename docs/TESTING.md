# Hardware test checklist

The Launcher's boot-selection logic relies on bootloader rollback behaviour and on the real board.
Run this checklist after your first flash and after any change to the Launcher, partition table or
an app's sdkconfig. Each step lists the expected result.

Open the serial monitor (`pio device monitor -p COM5 -b 115200`) to see `[Launcher] ...` log lines.

## A. First flash

| # | Step | Expected |
|---|---|---|
| A1 | `python build_all.py --flash COM5`, then press reset | Launcher menu appears with two cards |
| A2 | Look at both cards | Both show READY with a build date (NOT INSTALLED means that slot was not flashed) |
| A3 | Check serial log | `[Launcher] slot 0 SecureVault ... valid=1` and `slot 1 ShadowTune ... valid=1` |

## B. Menu control

| # | Step | Expected |
|---|---|---|
| B1 | Joystick LEFT/RIGHT/UP/DOWN | Selection switches between the cards |
| B2 | Tap each card | That app launches |
| B3 | Joystick OK | Selected app launches |
| B4 | Hold OK while resetting the board | Menu appears and does not auto-launch until OK is released and pressed again |

## C. The core behaviour: menu on every boot

| # | Step | Expected |
|---|---|---|
| C1 | Launch ShadowTune, then press the reset button | **Launcher menu appears** (not ShadowTune) |
| C2 | Launch SecureVault, then press reset | Launcher menu appears |
| C3 | Launch an app, unplug and replug USB | Launcher menu appears |
| C4 | Launch an app, let it run, press reset twice quickly | Launcher menu appears both times |

If C1 fails (the app starts again instead of the menu), see Troubleshooting: this means the app
is confirming itself or rollback is not enabled in the bootloader.

## D. Power off and on

| # | Step | Expected |
|---|---|---|
| D1 | In the menu, triple-tap the touch pad quickly | Screen goes dark, board sleeps |
| D2 | Click joystick OK | Menu appears directly, no boot screen |
| D3 | In the menu, single tap, two taps, one long hold on the pad | Nothing happens |
| D4 | In ShadowTune, hold the touch pad 5 s | Countdown, then sleep |
| D5 | Click OK, then pick ShadowTune | Launcher menu appears; ShadowTune shows its RESUMING screen, then the normal UI |
| D6 | In SecureVault, hold the touch pad 5 s | Power-off, sleep |
| D7 | Click OK, pick SecureVault | Boot animation, PIN screen |

## E. Boot screens

| # | Step | Expected |
|---|---|---|
| E1 | Launch ShadowTune from cold | Equalizer animation, ~2.2 s, then the playlist |
| E2 | Launch SecureVault from cold | Vault ring and padlock animation, ~2.2 s, then the lock screen |

## F. Data and storage

| # | Step | Expected |
|---|---|---|
| F1 | ShadowTune: play from SD, change volume, power off and back on | Works; settings kept |
| F2 | SecureVault: unlock with PIN, open the vault | Works |
| F3 | Compare RTC time before and after flashing | RTC unaffected |
| F4 | After first boot of each app | LittleFS formats once; no repeated format on later boots |

## G. Failure handling

| # | Step | Expected |
|---|---|---|
| G1 | Flash only the Launcher on a blank board (`--only launcher`) | Cards show NOT INSTALLED; pressing OK shows a message and stays in the menu |
| G2 | Erase `ota_1` (`esptool erase_region 0x560000 0x200000`), reset | ShadowTune card shows NOT INSTALLED, SecureVault still launches |

## Recording results

Keep notes of your firmware commit, board revision, and which steps passed or failed. Include them
when opening an issue.
