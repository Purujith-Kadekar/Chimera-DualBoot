# Controls and power

Inputs on the board: a **5-way joystick** (GPIO 6 ladder), the **touch screen** (XPT2046), and a
**capacitive touch pad** (TTP223, GPIO 40). The three apps use them differently.

## Launcher

| Input | Action |
|---|---|
| Joystick LEFT / RIGHT / UP / DOWN | Move selection between the two cards |
| Joystick OK | Launch the selected app |
| Tap a card on the screen | Select and launch that app |
| Touch pad, **3 quick taps** (within 1.5 s) | Power off (screen blanks, deep sleep) |
| Joystick OK while powered off | Power on, menu appears directly |

Details:

- No boot screen and no resume screen in the Launcher: it goes straight to the menu.
- A single tap or a long hold on the touch pad does nothing in the Launcher.
- Inputs held at boot (including the OK press that woke the board) are ignored until released.
- The card for the last-launched app is pre-selected.

## ShadowTune

| Input | Action |
|---|---|
| Joystick | Navigate and control playback (see the ShadowTune UI) |
| Touch pad, hold **5 s** | Power off from any screen: 3-2-1 countdown, then deep sleep |
| Joystick OK while powered off | Wake. The Launcher menu appears, pick ShadowTune. |

On the next start ShadowTune shows its RESUMING screen instead of the boot animation.

## SecureVault

| Input | Action |
|---|---|
| Joystick | PIN entry and navigation |
| Touch pad, hold **5 s** | Power off from any screen (on the lock screen directly; on other screens it also tears down network modes and clears the PIN first) |
| Joystick OK while powered off | Wake. The Launcher menu appears, pick SecureVault, then enter the PIN. |

The touch pad is **only** for power-off in SecureVault. It is not a select/confirm button.

## Getting back to the menu

There is no "exit to menu" button inside the apps. To return to the Launcher, power off
(5 s touch-pad hold) and wake with OK, or reset the board. Any reset returns to the menu.

## Quick reference

| Goal | Do this |
|---|---|
| Open an app | Menu: OK or tap |
| Power off from the menu | Triple-tap the touch pad |
| Power off from an app | Hold the touch pad 5 s |
| Power on | Click joystick OK |
| Back to the menu from an app | Power off and on, or press reset |
