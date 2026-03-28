# PlanktOS Firmware Documentation

## 1. Overview

`planktos.ino` is a monolithic Arduino firmware for an AVR-based device built around:

- A 128x64 SSD1306 OLED display over I2C.
- A Bosch BMP085/BMP180 pressure/temperature sensor over I2C.
- A FRAM device at I2C address `0x50` for persistent settings and calculator storage.
- Four input pads/buttons:
  - `PAD_P`  = pin `2`
  - `PAD_N`  = pin `4`
  - `PAD_P2` = pin `3`
  - `PAD_B`  = pin `5`

The firmware implements a small OS-style interface with four application modules:

1. **AVR Calc**
2. **Settings**
3. **Sys Info**
4. **Stopwatch**

The system boots with a staged animation, shows a main menu, supports auto-dim and auto-sleep, and stores user preferences in FRAM.

---

## 2. External Libraries and Platform Dependencies

### 2.1 Included headers

```cpp
#include <Arduino.h>
#include <Adafruit_BMP085.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#include <Wire.h>
#include <math.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
```

### 2.2 Library roles

- **Arduino.h**: core Arduino API.
- **Adafruit_BMP085.h**: sensor access for temperature, pressure, and altitude.
- **SSD1306Ascii / SSD1306AsciiAvrI2c**: OLED text rendering over I2C.
- **Wire.h**: I2C bus communication, used for OLED, BMP085, and FRAM.
- **math.h**: floating-point math used by calculator functions.
- **avr/sleep.h** and **avr/interrupt.h**: AVR sleep mode and pin-change interrupt support.

### 2.3 Target architecture assumptions

The sketch contains AVR-specific code paths and MCU identification for:

- ATmega328P
- ATmega328
- ATmega32U4
- ATmega328PB
- ATmega2560
- ATmega1280
- ATmega168P
- ATmega168

If no match exists, `MCU_STR` becomes `"Unlisted AVR"`.

---

## 3. Hardware Interface

### 3.1 I2C devices

| Device | Address | Purpose |
|---|---:|---|
| OLED display | `0x3C` | UI output |
| FRAM | `0x50` | persistent settings + saved calculator value |
| BMP085 | default sensor address from library | environmental data |

### 3.2 Input pins

| Symbol | Pin | Role |
|---|---:|---|
| `PAD_P` | 2 | previous / up |
| `PAD_N` | 4 | next / down |
| `PAD_P2` | 3 | select / confirm |
| `PAD_B` | 5 | back / home / exit |

All four inputs are configured as `INPUT` during setup.

### 3.3 Display assumptions

The display is initialized as an Adafruit 128x64 SSD1306-compatible layout using the `Adafruit5x7` font. The UI assumes 8 text rows (0 through 7).

---

## 4. Compile-Time Constants and Modes

### 4.1 Device and storage constants

```cpp
#define I2C_ADDRESS 0x3C
#define FRAM_ADDR 0x50
```

### 4.2 Input pins

```cpp
#define PAD_P 2
#define PAD_N 4
#define PAD_P2 3
#define PAD_B 5
```

### 4.3 App identifiers

```cpp
#define NO_APP 255
#define APP_IDX_STOPWATCH 3
```

`NO_APP` means the user is at the menu rather than inside an app.

### 4.4 FRAM storage map

The sketch uses fixed addresses in FRAM for settings and calculator persistence:

| Address | Symbol | Meaning |
|---:|---|---|
| `0x00` | `NV_BRIGHT` | brightness index |
| `0x01` | `NV_DIM` | auto-dim delay index |
| `0x02` | `NV_SLEEP` | auto-sleep delay index |
| `0x03` | `NV_PREC` | calculator precision index |
| `0x04` | `NV_SWDIM` | stopwatch dim behavior index |
| `0x10` | `NV_CALC_VALID` | calculator save marker (`0xAA`) |
| `0x11` | `NV_CALC_RESULT` | saved calculator result (`int64_t`) |

### 4.5 Settings system constants

```cpp
#define SET_COUNT 6
#define LONG_PRESS_MS 600
```

The Settings app has six rows:

1. Brightness
2. Auto-dim
3. Auto-sleep
4. Precision
5. Stopwatch screen behavior
6. Factory reset

A long press on select is defined as `600 ms`.

### 4.6 Stopwatch dim behavior

```cpp
#define SWDIM_AUTO 0
#define SWDIM_WAKE 1
#define SWDIM_KEEP 2
```

Meaning:

- `AUTO`: normal global dim/sleep behavior applies.
- `WAKE`: while dimmed, any input first wakes the screen instead of being routed directly to the stopwatch action.
- `KEEP`: stopwatch activity prevents dim/sleep from counting down.

### 4.7 Calculator state machine

```cpp
#define CS_A 0
#define CS_OP 1
#define CS_B 2
#define CS_RESULT 3
```

These are the calculator states:

- `CS_A`: entering operand A
- `CS_OP`: choosing an operator
- `CS_B`: entering operand B
- `CS_RESULT`: result available

### 4.8 Calculator selector boundaries

```cpp
#define CSEL_NUM 15
#define CSEL_OP 17
#define CSEL_RES 3
```

These represent the number of selectable items depending on the calculator state.

### 4.9 Calculator operators

```cpp
#define COP_ADD 0
#define COP_SUB 1
#define COP_MUL 2
#define COP_DIV 3
#define COP_MOD 4
#define COP_POW 5
#define COP_SIN 6
#define COP_COS 7
#define COP_TAN 8
#define COP_ASN 9
#define COP_ACS 10
#define COP_ATN 11
#define COP_SQT 12
#define COP_LOG 13
#define COP_LN 14
#define COP_ABS 15
#define COP_AVG 16
```

Supported operations:

- Add
- Subtract
- Multiply
- Divide
- Modulo
- Power
- Sine
- Cosine
- Tangent
- Arcsine
- Arccosine
- Arctangent
- Square root
- Log base 10
- Natural log
- Absolute value
- Average

### 4.10 Sys Info layout

```cpp
#define SI_LINES 15
#define SI_ROWS 6
```

There are 15 system information entries, with 6 visible at a time.

### 4.11 Conditional math feature flags

```cpp
#ifndef PLANKTOS_CALC_ENABLE_POW
#define PLANKTOS_CALC_ENABLE_POW 1
#endif
#ifndef PLANKTOS_CALC_ENABLE_LOG
#define PLANKTOS_CALC_ENABLE_LOG 1
#endif
#ifndef PLANKTOS_CALC_ENABLE_INVTRIG
#define PLANKTOS_CALC_ENABLE_INVTRIG 1
#endif
#ifndef PLANKTOS_CALC_ENABLE_SQRT
#define PLANKTOS_CALC_ENABLE_SQRT 1
#endif
#ifndef PLANKTOS_CALC_ENABLE_TRIG
#define PLANKTOS_CALC_ENABLE_TRIG 1
#endif
```

Each feature can be compiled out externally by redefining the symbol before compilation.

---

## 5. Data Model and Global State

### 5.1 Function pointer types

The firmware uses a small app interface:

```cpp
typedef void (*AppLaunchFn)();
typedef void (*AppInputFn)(bool, bool, bool, bool, bool);
typedef void (*AppTickFn)();
```

Each application may implement:

- `launch()`
- `input(...)`
- `tick()`

### 5.2 App descriptor structure

```cpp
struct AppDef {
  const char* namePgm;
  AppLaunchFn launch;
  AppInputFn input;
  AppTickFn tick;
};
```

### 5.3 Core device objects

```cpp
SSD1306AsciiAvrI2c display;
Adafruit_BMP085 bmp;
```

### 5.4 Sensor readiness flag

```cpp
static bool bmpReady = false;
```

This is set after successful sensor initialization.

### 5.5 Timing variables

- `tRefresh`: menu sensor refresh timestamp
- `tActivity`: last user activity timestamp
- `tSelDown`: press timestamp for `PAD_P2`
- `tSysRefresh`: Sys Info refresh timer
- `animT`: boot animation timing

### 5.6 UI and navigation state

- `passes`: boot animation pass counter
- `animStep`: boot animation phase
- `appIndex`: highlighted app in menu
- `appOpen`: currently opened app, or `NO_APP`
- `settingSel`: selected Settings row
- `sysScroll`: first visible Sys Info row

### 5.7 Persistent setting indexes

- `brightIdx`
- `dimIdx`
- `sleepIdx`
- `precIdx`
- `swDimIdx`

### 5.8 System info data cache

- `sysFram`: FRAM connectivity status
- `siTemp10`: temperature in tenths of a degree
- `siPres`: pressure in pascals
- `siAlt`: altitude in meters
- `siVcc`: supply voltage in millivolts
- `siRam`: free RAM in bytes

### 5.9 Bitfield flag group

```cpp
struct {
  uint8_t latch : 1;
  uint8_t inMenu : 1;
  uint8_t useF : 1;
  uint8_t lastP : 1;
  uint8_t lastN : 1;
  uint8_t lastP2 : 1;
  uint8_t lastB : 1;
  uint8_t dimmed : 1;
} f = {};
```

Meaning:

- `latch`: boot animation finished
- `inMenu`: menu mode active after first input wake
- `useF`: temperature display in Fahrenheit when set
- `lastP`, `lastN`, `lastP2`, `lastB`: previous raw input states
- `dimmed`: display currently dimmed

### 5.10 Error handling state

- `errorOpen`: true when error screen is active
- `errorMsg`: current error message string in flash

### 5.11 Calculator state

- `calcState`: current calculator FSM state
- `calcOp`: selected operator
- `calcSel`: current selector index
- `digitBuf[20]`: input buffer for the current operand
- `digitLen`: current buffer length
- `digitIntLen`: digits before decimal point
- `digitNeg`: sign flag
- `digitDecMode`: whether decimal entry is active
- `selLongFired`: long-press debounce flag
- `numA`, `numB`, `calcResult`: scaled integer operands/results
- `calcOvf`: overflow flag
- `calcDivZ`: divide-by-zero flag
- `calcSavedOk`: result saved to FRAM flag

### 5.12 Stopwatch state

- `swRunning`: timer running flag
- `swAccum`: accumulated elapsed milliseconds
- `swLastMs`: last start timestamp
- `swLap`: stored lap time
- `swLapSet`: lap value available

---

## 6. Flash-Based Lookup Tables and Text Resources

The sketch stores many strings and numeric tables in program memory (`PROGMEM`) to conserve SRAM.

### 6.1 Brightness levels

```cpp
const uint8_t brightContrast[] PROGMEM = { 1, 64, 128, 191, 255 };
```

Logical mapping:

- `0%`
- `25%`
- `50%`
- `75%`
- `100%`

### 6.2 Auto-dim delays

```cpp
const uint8_t dimSecs[] PROGMEM = { 5, 10, 30, 60 };
```

Displayed as:

- `5s`
- `10s`
- `30s`
- `1m`

### 6.3 Auto-sleep delays

```cpp
const uint8_t sleepMins[] PROGMEM = { 1, 30, 60 };
```

Displayed as:

- `1m`
- `30m`
- `60m`

### 6.4 Calculator precision tables

```cpp
const uint8_t dpDigits[] PROGMEM = { 2, 4, 8, 12 };
const uint8_t maxIntDigs[] PROGMEM = { 17, 15, 11, 7 };
const int64_t scaleTable[] PROGMEM = {
  100LL, 10000LL, 100000000LL, 1000000000000LL
};
```

These define fixed-point precision levels:

- `2 DP`   -> scale `100`
- `4 DP`   -> scale `10,000`
- `8 DP`   -> scale `100,000,000`
- `12DP`   -> scale `1,000,000,000,000`

The integer digit budget decreases as precision increases.

### 6.5 Stopwatch dim mode strings

- `AUTO`
- `WAKE`
- `KEEP`

### 6.6 Menu and settings labels

Includes labels for:

- Brightness
- Auto-dim
- Auto-sleep
- Precision
- SW SCREEN
- FACT.RESET

### 6.7 Calculator labels and descriptions

- Operator label grid: `+`, `-`, `*`, `/`, `%`, `^`, `SN`, `CS`, `TN`, `AS`, `AC`, `AT`, `SQ`, `LG`, `LN`, `AB`, `AV`
- Full descriptions for each operator are stored in flash and displayed in the operator selection screen.

### 6.8 Boot animation frames

The boot animation is represented as a set of ASCII art lines and a title banner for `PlanktOS`.

### 6.9 System info labels

The Sys Info page reports:

1. Uptime
2. Free RAM
3. VCC
4. MCU
5. CPU clock
6. Temperature
7. Pressure
8. Altitude
9. Brightness
10. Auto-dim
11. Auto-sleep
12. Precision
13. FRAM
14. Build date
15. Build time

### 6.10 Main menu labels

- `AVR CALC`
- `SETTINGS`
- `SYS INFO`
- `STP WTCH`

---

## 7. App Table and Dispatch Model

### 7.1 Application table

```cpp
const AppDef appTable[] PROGMEM = {
  { an0, launchCalc, inputCalc, NULL },
  { an1, launchSettings, inputSettings, NULL },
  { an2, launchSysInfo, inputSysInfo, tickSysInfo },
  { an3, launchStopwatch, inputStopwatch, tickStopwatch },
};
```

### 7.2 App count

```cpp
#define APP_COUNT ((uint8_t)(sizeof(appTable) / sizeof(appTable[0])))
```

There are exactly four apps.

### 7.3 Dispatch helpers

- `appLaunch(idx)` calls the app launch function.
- `appInput(idx, ...)` calls the app input handler.
- `appTick(idx)` calls the app tick handler.
- `appName(idx)` reads the app name string from flash.

If an app has no input handler and back is pressed, the firmware exits the app and redraws the menu.

---

## 8. FRAM I/O and Persistence

### 8.1 FRAM address write helper

```cpp
static void framSetAddr(uint16_t addr)
```
Writes the 16-bit memory address into the I2C transaction.

### 8.2 Byte write/read

- `framWrite(addr, val)` writes one byte.
- `framRead(addr)` reads one byte.

### 8.3 Multi-byte operations

- `framWriteBytes(addr, data, len)`
- `framReadBytes(addr, data, len)`

### 8.4 64-bit integer persistence

- `framWriteInt64(addr, val)` serializes `int64_t` little-endian.
- `framReadInt64(addr)` reconstructs an `int64_t` from 8 bytes.

### 8.5 FRAM presence test

```cpp
bool checkFram()
```
Returns `true` when the FRAM acknowledges on the I2C bus.

### 8.6 Missing FRAM behavior

If FRAM is absent:

- The firmware clears the screen.
- Shows `FRAM NOT FOUND`.
- Repeats until the device reappears.
- Once reconnected, the display is cleared and the firmware resumes.

### 8.7 Settings persistence

`saveSettings()` stores the five indexed settings.

`loadSettings()` restores them and clamps invalid values to defaults:

- Brightness default: `2`
- Dim default: `0`
- Sleep default: `1`
- Precision default: `0`
- Stopwatch dim default: `0`

### 8.8 Factory reset

`factoryReset()` restores default setting indexes and writes them back to FRAM.

---

## 9. Display Brightness and Dimming

### 9.1 Brightness application

`applyBrightness()` sets OLED contrast from the selected brightness table entry.

### 9.2 Dim application

`applyDim(dim)`:

- updates `f.dimmed`
- sets contrast to `1` when dimmed
- restores selected brightness when undimmed

### 9.3 Activity reset

`resetActivity()` updates the last-activity timestamp and undims the screen if needed.

### 9.4 Auto-dim and auto-sleep policy

The main loop compares `millis()` against `tActivity`:

- After `dimSecs[dimIdx]`, the screen dims.
- After `sleepMins[sleepIdx]`, the device enters sleep.

Exception:

- When the Stopwatch app is open and `swDimIdx == SWDIM_KEEP`, activity is continuously refreshed so dim/sleep do not trigger.

---

## 10. Error Handling

### 10.1 Error screen format

`drawErrorScreen(msg)` renders:

- a top error banner
- the message on row 2
- `[B]ACK` prompt on row 7

### 10.2 Error state entry

`raiseError(msg)`:

- stores the error message
- marks the error screen open
- draws the error screen
- refreshes the activity timer

### 10.3 Error exit path

Any input while the error screen is active closes the error, returns to the main menu, and resets calculator state.

---

## 11. Main Menu

### 11.1 Layout

The main menu shows:

- title line: `==[Main Menu]=======`
- application name inside a framed area
- live sensor line (temperature + pressure)
- bottom button hints

### 11.2 Menu navigation

- `PAD_P`: previous app
- `PAD_N`: next app
- `PAD_P2`: open selected app
- `PAD_B`: back is not meaningful in the menu

### 11.3 Sensor line

The menu also displays environmental data from the BMP085 if available.

Displayed values:

- temperature in `°C` or `°F` depending on `f.useF`
- pressure in `hPa`

If the sensor is not ready, the sensor row is left blank.

### 11.4 Live refresh

When idle in the menu, the sensor line is refreshed about every 2 seconds.

---

## 12. Settings App

### 12.1 Purpose

The Settings app controls device behavior and persistent configuration.

### 12.2 Settings rows

1. **BRIGHTNESS** — display contrast
2. **AUTO-DIM** — inactivity timeout before dimming
3. **AUTO-SLEEP** — inactivity timeout before sleep
4. **PRECISION** — calculator fixed-point precision
5. **SW SCREEN** — stopwatch dim behavior
6. **FACT.RESET** — restore defaults

### 12.3 Interaction model

- `PAD_P`: move selection up
- `PAD_N`: move selection down
- `PAD_P2`: change or activate selected row
- `PAD_B`: exit to menu

### 12.4 Row behaviors

#### Brightness
Cycles through the five contrast levels and applies the new contrast immediately.

#### Auto-dim
Cycles through the four dim delays.

#### Auto-sleep
Cycles through the three sleep delays.

#### Precision
Cycles through four calculator precision modes and calls `calcReset()` so the calculator state is internally consistent with the new scale.

#### Stopwatch screen
Cycles through `AUTO`, `WAKE`, and `KEEP`.

#### Factory reset
Resets all configurable settings to defaults and persists them.

### 12.5 Immediate redraw strategy

After a setting changes, the entire settings screen is redrawn to avoid partial-state artifacts.

---

## 13. System Information App

### 13.1 Purpose

The Sys Info app provides live device diagnostics and build metadata.

### 13.2 Scrolling model

- `PAD_P`: scroll up
- `PAD_N`: scroll down
- `PAD_P2`: force refresh
- `PAD_B`: exit

Only 6 rows are visible at once.

### 13.3 Auto refresh

The content refreshes every 2 seconds while the app is open.

### 13.4 Data fields

#### Uptime
Formatted as `HH:MM:SS` or more than 2 digits for hours if needed.

#### Free RAM
Computed by measuring the distance between the current stack and heap.

If the computed value becomes negative, the display shows `OVF!`.

#### VCC
Measured via the AVR internal reference using ADC logic. Output is in volts with three decimal places.

#### MCU
The compile-time MCU string.

#### CPU CLCK
Printed as `F_CPU / 1,000,000` in MHz.

#### TEMP
BMP085 temperature, shown in `C` or `F` depending on `f.useF`.

#### PRESSURE
BMP085 pressure in `hPa`.

#### ALTITUDE
BMP085 altitude in meters.

#### BRGHTNSS
Current brightness percentage.

#### AUTO-DIM
Current auto-dim duration.

#### AUTO-SLP
Current auto-sleep duration.

#### PRECISION
Current calculator precision in decimal places.

#### FRAM
Shows `OK` or `FAIL` depending on FRAM presence.

#### BLD DATE / BLD TIME
Compile-time build date and time.

### 13.5 FRAM check in Sys Info

`launchSysInfo()` checks FRAM first. If FRAM is missing, it waits for reconnection before proceeding.

---

## 14. Calculator App

## 14.1 Purpose

The calculator is the most complex module in the firmware. It is a fixed-point, menu-driven calculator with:

- digit entry
- signed numbers
- decimal mode toggling via long select
- operator selection
- unary and binary operators
- overflow and divide-by-zero detection
- saved result recall and persistence in FRAM

### 14.2 Numeric representation

The calculator stores numbers as scaled `int64_t` values.

The scale depends on selected precision:

- `2 DP` -> scale `100`
- `4 DP` -> scale `10,000`
- `8 DP` -> scale `100,000,000`
- `12 DP` -> scale `1,000,000,000,000`

All arithmetic is performed with these scaled integers except for functions that require floating-point math (`pow`, trig, log, sqrt).

### 14.3 Buffer handling

The input buffer tracks:

- total digits
- integer digit count
- sign
- decimal input mode

`bufToScaled()` converts the buffer into the scaled integer representation.

### 14.4 Digit selection map

When entering a number, selector positions mean:

- `0`–`9`: numeric digits
- `10`: backspace
- `11`: sign toggle
- `12`: clear
- `13`: commit/confirm
- `14`: load saved value from FRAM

### 14.5 App state flow

#### `CS_A`
Enter operand A.

#### `CS_OP`
Select operator.

#### `CS_B`
Enter operand B if the operator is binary.

#### `CS_RESULT`
Display computed result and offer result actions.

### 14.6 Screen layout

The calculator UI contains:

- top status header
- operand A row
- operator description row
- operand B row or blank
- result row
- divider line
- selector hint line
- bottom button hint line

### 14.7 Header content

The header shows:

- `=[AVR CALC]=`
- current precision in DP
- input mode indicator:
  - `D` when decimal entry is active
  - `I` otherwise
- `S` when the current result has been saved to FRAM

### 14.8 Long press behavior

Holding `PAD_P2` for at least `LONG_PRESS_MS` toggles decimal entry while entering A or B.

When not entering a number and a valid result exists, long press saves the current result to FRAM.

### 14.9 Input state behavior

#### State `CS_A`
- digit keys enter A
- selector `14` attempts to load saved value into A
- `PAD_P` / `PAD_N` move selector
- `PAD_P2` commits number or acts on selected utility function
- `PAD_B` exits calculator

#### State `CS_OP`
- selector chooses one of 17 operators
- unary operators compute immediately and move to result
- binary operators advance to B entry

#### State `CS_B`
- digit keys enter B
- selector `14` loads saved value into B
- committing B triggers computation

#### State `CS_RESULT`
- selector `0`: reset calculator
- selector `1`: reuse result as next A
- selector `2`: save result to FRAM

### 14.10 Supported operator semantics

#### Binary arithmetic

- **Add**: `A + B`
- **Subtract**: `A - B`
- **Multiply**: `(A * B) / scale`
- **Divide**: `(A * scale) / B`
- **Modulo**: `A % B`
- **Power**: `pow(A, B)` in floating-point form, re-scaled
- **Average**: `(A + B) / 2`

#### Unary trig and inverse trig

The trig functions interpret input degrees for forward trig and output degrees for inverse trig.

- **Sine**: `sin(A°)`
- **Cosine**: `cos(A°)`
- **Tangent**: `tan(A°)`
- **Arcsine**: `asin(A)` -> degrees
- **Arccosine**: `acos(A)` -> degrees
- **Arctangent**: `atan(A)` -> degrees

#### Other unary operations

- **Square root**: `sqrt(A)`
- **Log base 10**: `log10(A)`
- **Natural log**: `ln(A)`
- **Absolute value**: `abs(A)`

### 14.11 Error conditions

The calculator explicitly handles:

- divide by zero
- invalid inverse trig domain
- invalid logarithm input (`<= 0`)
- invalid square root input (`< 0`)
- arithmetic overflow
- floating-point overflow / NaN / infinity conversions

On error, an error screen is shown and the app is effectively suspended until the user exits it.

### 14.12 Save / recall mechanism

A result can be saved into FRAM.

Save flow:

1. result computed
2. user long-presses select or chooses `SV` in result state
3. result is written to FRAM
4. valid marker `0xAA` is written to `NV_CALC_VALID`

Recall flow:

- selector `14` in A or B attempts to load the saved result from FRAM.

If no valid saved value exists, the display shows `** NO SAVED VALUE **`.

### 14.13 Result rendering

Numbers are printed with the configured precision. If the rendered string exceeds the available space, the display truncates and appends `~`.

Overflow and divide-by-zero are printed as explicit text labels.

---

## 15. Stopwatch App

### 15.1 Purpose

A simple stopwatch with lap support.

### 15.2 Time base

Elapsed time is represented internally in milliseconds using:

- `swAccum`: accumulated elapsed time
- `swLastMs`: start time for the current running segment
- `swElapsed()`: total current elapsed time

### 15.3 Display format

The stopwatch prints time as:

`HH:MM:SS.t`

where `t` is tenths of a second.

### 15.4 Screen layout

- title
- current elapsed time
- separator
- lap time or placeholder
- separator
- action hints
- flag/home hints

### 15.5 Controls

- `PAD_P`: start / pause toggle
- `PAD_N`: reset all stopwatch state
- `PAD_P2`: store lap time
- `PAD_B`: exit to menu

### 15.6 Lap behavior

When a lap is flagged, the lap time is stored in `swLap` and displayed on row 3.

### 15.7 Refresh behavior

`tickStopwatch()` updates the displayed running time approximately every 100 ms.

### 15.8 Startup behavior for stopwatch

If the stopwatch is not running and the accumulated time is zero, launching the app also clears the stored lap state.

---

## 16. Boot Sequence

### 16.1 Setup phase

In `setup()` the sketch:

1. configures input pins
2. starts I2C
3. initializes the display
4. sets the font
5. clears the display
6. sets default settings indexes
7. applies brightness
8. ensures FRAM is present before continuing
9. loads saved settings
10. applies brightness again using loaded settings
11. attempts BMP085 initialization up to 5 times with 200 ms delay between attempts
12. stores the initial activity timestamp

### 16.2 Boot animation

The boot animation is rendered line by line with a staged reveal/erase/reveal cycle.

Behavior:

- each line is printed
- briefly blanked
- reprinted
- after all 8 passes, the system sets `f.latch = 1` and exits boot mode

### 16.3 Post-boot

After the animation latches, the firmware transitions into normal operation.

---

## 17. Main Loop Behavior

### 17.1 Boot gate

Before latch completes, the loop only advances the boot animation.

### 17.2 Menu wake gate

Before menu mode is active, any button press wakes the system into menu mode and redraws the menu.

### 17.3 Input edge detection

The loop tracks previous button states and generates rising-edge events:

- `rP`
- `rN`
- `rP2`
- `rB`

It also detects long press on `PAD_P2`.

### 17.4 Activity reset

Any real input event resets inactivity timers and can exit dimmed state.

### 17.5 Error handling priority

If `errorOpen` is true, any input clears the error and returns to the menu.

### 17.6 Menu dispatch

When no app is open:

- `rP` moves to previous app
- `rN` moves to next app
- `rP2` opens the current app
- menu sensor line auto-refreshes every 2 seconds

### 17.7 App dispatch

When an app is open:

- the app’s input handler receives events
- the app’s tick function runs if present
- stopwatch has extra wake behavior when configured with `SWDIM_WAKE`

### 17.8 Auto-dim / auto-sleep timing

At the end of each loop:

- the current inactivity time is checked
- the display dims after the configured dim period
- sleep is entered after the configured sleep period

---

## 18. Sleep Mode

### 18.1 Entering sleep

`enterSleep()` performs:

1. OLED display off command (`0xAE`)
2. enable pin-change interrupt mask for `PCINT21`
3. enable pin-change interrupt group `PCIE2`
4. configure `SLEEP_MODE_PWR_DOWN`
5. enable sleep
6. enter sleep

### 18.2 Waking from sleep

After wake:

- sleep is disabled
- pin-change masking is cleared
- OLED display is turned back on (`0xAF`)
- dim state is cleared
- brightness is restored
- activity timer is reset
- error screen or current UI is redrawn depending on state

### 18.3 Interrupt handler

```cpp
ISR(PCINT2_vect) {}
```

The ISR body is intentionally empty. Its job is to wake the MCU from sleep when the configured pin-change event occurs.

---

## 19. Utility Functions

### 19.1 Text and number helpers

- `appendUint()` writes an unsigned integer into a character buffer.
- `append2d()` writes a two-digit number with leading zero padding.
- `pgmCopy()` copies a flash-resident string into RAM.
- `printFixed()` prints a fixed one-decimal number right-aligned.
- `blankLine()` clears a display row.

### 19.2 Numeric formatting helpers

- `scaledToStr()` converts scaled integers to text with decimal point.
- `printScaledR()` right-aligns a scaled number into a fixed width.
- `bufToScaled()` converts the digit-entry buffer into a scaled integer.

### 19.3 Calculator helpers

- `calcIsInput()` checks whether the FSM is in A or B entry.
- `calcMaxSel()` returns the valid selector count for the current state.
- `getDP()` returns current decimal places.
- `getMaxInt()` returns current integer digit limit.
- `getScale()` returns the current fixed-point scale.
- `isUnaryOp()` determines whether an operator uses only A.
- `resetDigitBuf()` clears the input buffer state.
- `saveCalcResult()` stores the current result in FRAM.
- `loadCalcResult()` fetches a previously saved result.
- `calcReset()` resets the calculator FSM and internal state.
- `calcCompute()` executes the selected operator.
- `handleDigitSel()` processes selector inputs during digit entry.
- `commitNumber()` converts the typed buffer into a stored numeric operand.

### 19.4 Stopwatch helpers

- `swElapsed()` returns total elapsed stopwatch time.
- `printTime()` formats and centers the stopwatch timer.

### 19.5 Power and board helpers

- `readVcc()` measures supply voltage using the AVR ADC and internal reference.
- `freeRam()` estimates free SRAM.

---

## 20. Numeric and Overflow Policy

### 20.1 Fixed-point design

The calculator is not integer-only in effect; it is fixed-point. All stored numbers are scaled integers. Arithmetic is designed to preserve the chosen decimal precision.

### 20.2 Overflow rules

The sketch guards against overflow in several ways:

- checked integer addition/subtraction bounds
- multiplication pre-checks
- floating-point infinity / NaN checks
- saturation-like rejection via error flags rather than silent wraparound

### 20.3 Truncation policy

When a formatted number exceeds the display width, the code truncates and appends `~`.

---

## 21. UI Conventions

### 21.1 Button legend

Across the interface, the bottom row commonly shows:

`[P]  [N]    [>]  [B]`

This represents the four physical controls.

### 21.2 Selection cursor style

The active selector is typically shown with brackets or a leading `>` depending on the screen.

### 21.3 Consistency

All four applications follow the same high-level pattern:

- top header
- content rows
- bottom button hints
- back exits to menu

---

## 22. Compile-Time and Runtime Notes

### 22.1 Temperature units

The code contains a runtime flag `f.useF`, but this sketch does not show a user-facing toggle for it. In practice, the displayed unit follows that flag.

### 22.2 Sensor failure behavior

If the BMP085 is not ready:

- menu sensor line is blank
- sys info temperature/pressure/altitude fall back to zero-like display values

### 22.3 FRAM dependency

FRAM is treated as mandatory for normal operation. The sketch blocks and waits for reconnection instead of silently degrading when FRAM is missing.

### 22.4 Calculator persistence dependency

Saved calculator recall depends on FRAM availability and a valid marker byte.

---

## 23. Summary of Functional Behavior

PlanktOS is a compact AVR UI shell with:

- a boot animation
- a persistent settings system
- live environmental readings on the menu
- a fixed-point calculator with scientific functions
- a system diagnostics screen
- a stopwatch with lap support
- display dimming and sleep management
- FRAM-backed persistence

The code is organized around a small application dispatch table and a single cooperative main loop.

---

## 24. Practical Behavior Reference

### 24.1 At power-up

- init hardware
- verify FRAM
- load configuration
- try BMP085 init
- animate boot
- wait for first input to enter menu mode

### 24.2 In the menu

- browse apps with `P` / `N`
- open with `>`
- see live temperature and pressure

### 24.3 In calculator

- enter A
- choose operator
- enter B when needed
- compute result
- save or reuse result

### 24.4 In settings

- modify contrast, dim/sleep timing, precision, stopwatch dim mode
- factory reset when required

### 24.5 In sys info

- inspect runtime and build diagnostics
- refresh live readings

### 24.6 In stopwatch

- start/pause
- reset
- record lap
- exit back home

---

## 25. Source-Level Artifacts Worth Preserving

This sketch relies heavily on flash-resident data to keep SRAM usage low. The following patterns are central to the implementation:

- `PROGMEM` strings for all UI text
- `pgm_read_word()` and `pgm_read_byte()` for flash reads
- function pointers stored in a PROGMEM app table
- fixed-point arithmetic instead of dynamic numeric types
- minimal RAM allocation and no heap use for application logic

---

## 26. Maintenance Notes

### 26.1 If adding a new app

A new app must provide:

- a flash-resident name string
- a launch function
- an input handler
- an optional tick function

It must be appended to `appTable`, and the menu layout may need a corresponding label and navigation update.

### 26.2 If changing precision

Any change to `dpDigits`, `maxIntDigs`, or `scaleTable` must remain internally consistent. The calculator, formatting helpers, and storage assumptions all depend on those tables matching.

### 26.3 If changing storage layout

FRAM addresses are hard-coded. Moving one field requires auditing all read/write callers.

### 26.4 If changing display layout

Screen formatting assumes a 20-character row width. Any new text must fit or intentionally use truncation logic.

---

## 27. Concise Identification

**Firmware name:** PlanktOS

**Primary language:** Arduino C++

**Target family:** AVR

**Core features:** menu shell, settings, system info, scientific calculator, stopwatch, FRAM persistence, OLED UI, BMP085 sensor integration, sleep support

