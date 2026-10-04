# Security Policy (Chimera-DualBoot)

SecureVault stores credentials, so security reports are taken seriously.

## Reporting a vulnerability

Please **do not open a public issue** for a security problem.

Use GitHub's private reporting: **Security tab > Report a vulnerability**
(or contact: `[YOUR CONTACT EMAIL]`).

Include the firmware version or commit, what you did, what you expected, and what happened.
Please give a reasonable amount of time for a fix before disclosing publicly.

## Scope

- SecureVault: PIN handling, vault encryption on the SD card, the Dashboard Mode session
  (ECDH + AES-256-GCM over USB serial), BLE keyboard, hotspot mode
- Launcher: boot selection and partition layout
- ShadowTune: SD card access and the hotspot upload interface

## Things to know

- This is hobby firmware for a development board. It has not been independently audited unless
  a release note says otherwise.
- The device does not use flash encryption or secure boot by default. Someone with physical access
  to the board can read the flash. Protect the physical device accordingly.
- Both apps share one NVS and one LittleFS partition by design (see README).
