# Hardware

Target: **EdgeHax S3 Pro**, ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM).

The pin numbers below come from the firmware sources (`include/board_config.h` in each project)
and `SecureVault/docs/GPIO_MAP.md`. The Launcher uses the same display, touch and input pins.

## Pin map

| GPIO | Signal | Part | Launcher | SecureVault | ShadowTune |
|---|---|---|---|---|---|
| 1 | I2C SDA | RTC, MPU, INA219, EEPROM | not used | yes | yes |
| 2 | I2C SCL | same bus | not used | yes | yes |
| 4 | TFT CS | ILI9341 | yes | yes | yes |
| 5 | TFT RST | ILI9341 | yes | yes | yes |
| 6 | Button ladder (analog) | 5-way joystick | yes | yes | yes |
| 7 | TFT DC | ILI9341 | yes | yes | yes |
| 9 | I2S MCLK | CS4344 DAC | not used | not used | yes |
| 10 | SD CS | microSD | not used | yes | yes |
| 11 | SD MOSI | microSD | not used | yes | yes |
| 12 | SD SCK | microSD | not used | yes | yes |
| 13 | SD MISO | microSD | not used | yes | yes |
| 14 | Touch CS | XPT2046 | yes | yes | yes |
| 15 | MOSI (shared display + touch) | | yes | yes | yes |
| 16 | CLK (shared display + touch) | | yes | yes | yes |
| 19 / 20 | USB D- / D+ | native USB-C | log output | yes | yes |
| 21 | I2S LRCLK | CS4344 | not used | not used | yes |
| 22 | Buzzer | passive buzzer | not used | not used | yes |
| 38 | Buzzer in SecureVault / I2S SDIN in ShadowTune | | not used | buzzer | I2S data |
| 39 | TFT MISO | ILI9341 | yes | yes | yes |
| 40 | Touch pad (TTP223), HIGH when touched | | yes | yes | yes |
| 41 | Touch DOUT | XPT2046 | yes | yes | yes |
| 42 | I2S BCLK | CS4344 | not used | not used | yes |

### The buzzer pin differs between the apps

SecureVault drives the passive buzzer on **GPIO 38**. ShadowTune uses GPIO 38 as the I2S data line
to the CS4344 DAC and moves the buzzer to **GPIO 22**. This is intentional in the sources. If you
see no buzzer sound in one app, check which pin your hardware actually has the buzzer wired to.

## Display and touch

- ILI9341, 320 x 240, SPI at 40 MHz, **landscape (rotation 1)**.
- XPT2046 touch controller. Its data-out pin is on GPIO 41, not on the display's MISO, so it is
  read by bit-banging on the shared MOSI/CLK lines (the SPI peripheral is released for each read).

## Joystick ladder (GPIO 6)

One ADC pin, five switches on a resistor ladder. 12-bit reading:

| Button | Wiring | Reading up to |
|---|---|---|
| OK (centre) | direct to GND | 157 (SecureVault, Launcher), 350 (ShadowTune) |
| RIGHT | 1 kOhm | 489 |
| UP | 2.2 kOhm | 935 |
| DOWN | 4.7 kOhm | 1565 |
| LEFT | 10 kOhm | 3009 |
| Idle | | above 3009 |

The OK threshold differs between the apps (157 vs 350). The Launcher uses 157. A press is only
accepted after the reading is stable for two polls.

## I2C devices (one bus, GPIO 1 / 2)

| Device | Address |
|---|---|
| RTC (DS3231) | 0x68 |
| MPU | 0x69 |
| INA219 (battery monitor) | 0x40 |
| EEPROM | 0x57 |

The Launcher never touches this bus, so RTC and EEPROM data are unaffected by the Launcher and
by flashing.

## Wake-up

Deep-sleep wake is the joystick **OK** button on GPIO 6 (`ext0`, wake when LOW). GPIO 6 must be
an RTC-capable pin (ESP32-S3 GPIO 0-21), which it is.

## Reserved / notes

- GPIO 3 is unused (a former microphone pin dropped because of a strapping-pin / MCLK conflict).
- ShadowTune can override pins from the `gpio_cfg` partition. The Launcher does **not** read it and
  always uses the pins above, which match both apps' defaults.
