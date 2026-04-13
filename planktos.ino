/*
planktOS (from "plankton" meaning wanderer/drifting 'small little creatures')
A simple RTOS for small AVR MCUs (ATmega328PB)

Uses:
Adafruit_BMP085
SSD1306Ascii
*/

#include <Arduino.h>
#include <Adafruit_BMP085.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#include <Wire.h>
#include <math.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>

#ifndef M_PI
#define M_PI 3.14159265358979f
#endif

#define I2C_ADDRESS 0x3C
#define FRAM_ADDR 0x50
#define PAD_P 2
#define PAD_N 4
#define PAD_P2 3
#define PAD_B 5

#define NO_APP 255
#define APP_IDX_STOPWATCH 3

#define NV_BRIGHT 0x00
#define NV_DIM 0x01
#define NV_SLEEP 0x02
#define NV_PREC 0x03
#define NV_SWDIM 0x04
#define NV_CALC_VALID 0x10
#define NV_CALC_RESULT 0x11

#define SET_COUNT 6
#define LONG_PRESS_MS 600

#define SWDIM_AUTO 0
#define SWDIM_WAKE 1
#define SWDIM_KEEP 2

#define CS_A 0
#define CS_OP 1
#define CS_B 2
#define CS_RESULT 3

#define CSEL_NUM 15
#define CSEL_OP 17
#define CSEL_RES 3

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

#define SI_LINES 15
#define SI_ROWS 6

#if defined(__AVR_ATmega328P__)
#define MCU_STR "ATmega328P"
#elif defined(__AVR_ATmega328__)
#define MCU_STR "ATmega328"
#elif defined(__AVR_ATmega32U4__)
#define MCU_STR "ATmega32U4"
#elif defined(__AVR_ATmega328PB__)
#define MCU_STR "ATmega328PB"
#elif defined(__AVR_ATmega2560__)
#define MCU_STR "ATmega2560"
#elif defined(__AVR_ATmega1280__)
#define MCU_STR "ATmega1280"
#elif defined(__AVR_ATmega168P__)
#define MCU_STR "ATmega168P"
#elif defined(__AVR_ATmega168__)
#define MCU_STR "ATmega168"
#else
#define MCU_STR "Unlisted AVR"
#endif

#define REFRESH() (tRefresh = (uint16_t)millis())
#define PGM_STR(p) ((__FlashStringHelper*)(p))

typedef void (*AppLaunchFn)();
typedef void (*AppInputFn)(bool, bool, bool, bool, bool);
typedef void (*AppTickFn)();

struct AppDef {
  const char* namePgm;
  AppLaunchFn launch;
  AppInputFn input;
  AppTickFn tick;
};

SSD1306AsciiAvrI2c display;
Adafruit_BMP085 bmp;
static bool bmpReady = false;

uint16_t tRefresh = 0;
uint32_t tActivity = 0;
uint32_t tSelDown = 0;
uint32_t tSysRefresh = 0;
uint32_t animT = 0;

uint8_t passes = 0;
uint8_t animStep = 0;
uint8_t appIndex = 0;
uint8_t appOpen = NO_APP;
uint8_t settingSel = 0;
uint8_t sysScroll = 0;

uint8_t brightIdx = 2;
uint8_t dimIdx = 0;
uint8_t sleepIdx = 1;
uint8_t precIdx = 0;
uint8_t swDimIdx = 0;

bool sysFram = false;
int16_t siTemp10 = 0;
int32_t siPres = 0;
int16_t siAlt = 0;
uint16_t siVcc = 0;
int16_t siRam = 0;

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

static bool errorOpen = false;
static const __FlashStringHelper* errorMsg = NULL;

uint8_t calcState = CS_A;
uint8_t calcOp = 0;
uint8_t calcSel = 0;
char digitBuf[20];
uint8_t digitLen = 0;
uint8_t digitIntLen = 0;
bool digitNeg = false;
bool digitDecMode = false;
bool selLongFired = false;
int64_t numA = 0;
int64_t numB = 0;
int64_t calcResult = 0;
bool calcOvf = false;
bool calcDivZ = false;
bool calcSavedOk = false;

static bool swRunning = false;
static uint64_t swAccum = 0;
static uint32_t swLastMs = 0;
static uint64_t swLap = 0;
static bool swLapSet = false;

const uint8_t brightContrast[] PROGMEM = { 1, 64, 128, 191, 255 };
const uint8_t dimSecs[] PROGMEM = { 5, 10, 30, 60 };
const uint8_t sleepMins[] PROGMEM = { 1, 30, 60 };
const uint8_t dpDigits[] PROGMEM = { 2, 4, 8, 12 };
const uint8_t maxIntDigs[] PROGMEM = { 17, 15, 11, 7 };
const int64_t scaleTable[] PROGMEM = {
  100LL, 10000LL, 100000000LL, 1000000000000LL
};

const char bv0[] PROGMEM = "  0%";
const char bv1[] PROGMEM = " 25%";
const char bv2[] PROGMEM = " 50%";
const char bv3[] PROGMEM = " 75%";
const char bv4[] PROGMEM = "100%";
const char* const brightStrs[] PROGMEM = { bv0, bv1, bv2, bv3, bv4 };

const char dv0[] PROGMEM = "  5s";
const char dv1[] PROGMEM = " 10s";
const char dv2[] PROGMEM = " 30s";
const char dv3[] PROGMEM = "  1m";
const char* const dimStrs[] PROGMEM = { dv0, dv1, dv2, dv3 };

const char sv0[] PROGMEM = "  1m";
const char sv1[] PROGMEM = " 30m";
const char sv2[] PROGMEM = " 60m";
const char* const sleepStrs[] PROGMEM = { sv0, sv1, sv2 };

const char pv0[] PROGMEM = "2 DP";
const char pv1[] PROGMEM = "4 DP";
const char pv2[] PROGMEM = "8 DP";
const char pv3[] PROGMEM = "12DP";
const char* const precStrs[] PROGMEM = { pv0, pv1, pv2, pv3 };

const char sdv0[] PROGMEM = "AUTO";
const char sdv1[] PROGMEM = "WAKE";
const char sdv2[] PROGMEM = "KEEP";
const char* const swDimStrs[] PROGMEM = { sdv0, sdv1, sdv2 };

const char rvReset[] PROGMEM = "  >>";

const char lBright[] PROGMEM = "BRIGHTNESS";
const char lDim[] PROGMEM = "AUTO-DIM  ";
const char lSleep[] PROGMEM = "AUTO-SLEEP";
const char lPrec[] PROGMEM = "PRECISION ";
const char lSwDim[] PROGMEM = "SW SCREEN ";
const char lReset[] PROGMEM = "FACT.RESET";

const char calcOpLabels[] PROGMEM =
  " +"
  " -"
  " *"
  " /"
  " %"
  " ^"
  "SN"
  "CS"
  "TN"
  "AS"
  "AC"
  "AT"
  "SQ"
  "LG"
  "LN"
  "AB"
  "AV";

const char copd0[] PROGMEM = "Add          A + B  ";
const char copd1[] PROGMEM = "Subtract     A - B  ";
const char copd2[] PROGMEM = "Multiply     A * B  ";
const char copd3[] PROGMEM = "Divide       A / B  ";
const char copd4[] PROGMEM = "Modulo       A % B  ";
const char copd5[] PROGMEM = "Power        A ^ B  ";
const char copd6[] PROGMEM = "Sine    (deg input) ";
const char copd7[] PROGMEM = "Cosine  (deg input) ";
const char copd8[] PROGMEM = "Tangent (deg input) ";
const char copd9[] PROGMEM = "Arcsin  (deg output)";
const char copd10[] PROGMEM = "Arccos  (deg output)";
const char copd11[] PROGMEM = "Arctan  (deg output)";
const char copd12[] PROGMEM = "Square root of A    ";
const char copd13[] PROGMEM = "Log base 10 of A    ";
const char copd14[] PROGMEM = "Natural log of A    ";
const char copd15[] PROGMEM = "Absolute value of A ";
const char copd16[] PROGMEM = "Average  (A + B) / 2";
const char* const calcOpDescs[] PROGMEM = {
  copd0, copd1, copd2, copd3, copd4, copd5, copd6, copd7,
  copd8, copd9, copd10, copd11, copd12, copd13, copd14, copd15, copd16
};

const char bl0[] PROGMEM = " .------------------.";
const char bl1[] PROGMEM = " |                  |";
const char bl2[] PROGMEM = " |                  |";
const char bl3[] PROGMEM = " |  [  PlanktOS  ]  |";
const char bl4[] PROGMEM = " |                  |";
const char bl5[] PROGMEM = " |                  |";
const char bl6[] PROGMEM = " '------------------'";
const char bl7[] PROGMEM = "  [i] Touch any key  ";
const char* const bootLines[] PROGMEM = { bl0, bl1, bl2, bl3, bl4, bl5, bl6, bl7 };

const char sil0[] PROGMEM = "UPTIME   ";
const char sil1[] PROGMEM = "FREE RAM ";
const char sil2[] PROGMEM = "VCC      ";
const char sil3[] PROGMEM = "MCU      ";
const char sil4[] PROGMEM = "CPU CLCK ";
const char sil5[] PROGMEM = "TEMP     ";
const char sil6[] PROGMEM = "PRESSURE ";
const char sil7[] PROGMEM = "ALTITUDE ";
const char sil8[] PROGMEM = "BRGHTNSS ";
const char sil9[] PROGMEM = "AUTO-DIM ";
const char sil10[] PROGMEM = "AUTO-SLP ";
const char sil11[] PROGMEM = "PRECISION";
const char sil12[] PROGMEM = "FRAM     ";
const char sil13[] PROGMEM = "BLD DATE ";
const char sil14[] PROGMEM = "BLD TIME ";
const char* const siLabels[] PROGMEM = {
  sil0, sil1, sil2, sil3, sil4, sil5, sil6, sil7,
  sil8, sil9, sil10, sil11, sil12, sil13, sil14
};

const char siMcu[] PROGMEM = MCU_STR;
const char siBldDate[] PROGMEM = __DATE__;
const char siBldTime[] PROGMEM = __TIME__;

const char an0[] PROGMEM = " <- : AVR CALC :  ->";
const char an1[] PROGMEM = " <- : SETTINGS :  ->";
const char an2[] PROGMEM = " <- : SYS INFO :  ->";
const char an3[] PROGMEM = " <- : STP WTCH :  ->";

void launchCalc();
void inputCalc(bool, bool, bool, bool, bool);
void launchSettings();
void inputSettings(bool, bool, bool, bool, bool);
void launchSysInfo();
void inputSysInfo(bool, bool, bool, bool, bool);
void tickSysInfo();
void launchStopwatch();
void inputStopwatch(bool, bool, bool, bool, bool);
void tickStopwatch();
void drawMenu();
void calcReset();
static void drawCalcHeader();

const AppDef appTable[] PROGMEM = {
  { an0, launchCalc, inputCalc, NULL },
  { an1, launchSettings, inputSettings, NULL },
  { an2, launchSysInfo, inputSysInfo, tickSysInfo },
  { an3, launchStopwatch, inputStopwatch, tickStopwatch },
};

#define APP_COUNT ((uint8_t)(sizeof(appTable) / sizeof(appTable[0])))

inline void appLaunch(uint8_t idx) {
  AppLaunchFn fn = (AppLaunchFn)pgm_read_word(&appTable[idx].launch);
  if (fn) fn();
}

inline void appInput(uint8_t idx, bool rP, bool rN, bool rSel, bool rBck, bool longSel) {
  AppInputFn fn = (AppInputFn)pgm_read_word(&appTable[idx].input);
  if (fn) fn(rP, rN, rSel, rBck, longSel);
  else if (rBck) {
    appOpen = NO_APP;
    drawMenu();
    REFRESH();
  }
}

inline void appTick(uint8_t idx) {
  AppTickFn fn = (AppTickFn)pgm_read_word(&appTable[idx].tick);
  if (fn) fn();
}

inline const char* appName(uint8_t idx) {
  return (const char*)pgm_read_word(&appTable[idx].namePgm);
}

static inline void framSetAddr(uint16_t addr) {
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
}

void framWrite(uint16_t addr, uint8_t val) {
  framRequireConnected();
  Wire.beginTransmission(FRAM_ADDR);
  framSetAddr(addr);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t framRead(uint16_t addr) {
  framRequireConnected();
  Wire.beginTransmission(FRAM_ADDR);
  framSetAddr(addr);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)FRAM_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

static void framWriteBytes(uint16_t addr, const uint8_t* data, uint8_t len) {
  framRequireConnected();
  Wire.beginTransmission(FRAM_ADDR);
  framSetAddr(addr);
  for (uint8_t i = 0; i < len; i++) Wire.write(data[i]);
  Wire.endTransmission();
}

static void framReadBytes(uint16_t addr, uint8_t* data, uint8_t len) {
  framRequireConnected();
  Wire.beginTransmission(FRAM_ADDR);
  framSetAddr(addr);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)FRAM_ADDR, len);
  for (uint8_t i = 0; i < len; i++)
    data[i] = Wire.available() ? Wire.read() : 0;
}

void framWriteInt64(uint16_t addr, int64_t val) {
  uint8_t buf[8];
  uint64_t uv = (uint64_t)val;
  for (uint8_t i = 0; i < 8; i++, uv >>= 8) buf[i] = (uint8_t)(uv & 0xFF);
  framWriteBytes(addr, buf, 8);
}

int64_t framReadInt64(uint16_t addr) {
  uint8_t buf[8] = {};
  framReadBytes(addr, buf, 8);
  int64_t val = 0;
  for (uint8_t i = 0; i < 8; i++) val |= ((int64_t)buf[i]) << (i * 8);
  return val;
}

bool checkFram() {
  Wire.beginTransmission(FRAM_ADDR);
  return Wire.endTransmission() == 0;
}

static void framWaitForReconnect() {
  errorOpen = false;
  errorMsg = NULL;
  display.clear();
  display.setCursor(0, 0);
  display.print(F("FRAM NOT FOUND"));
  display.setCursor(0, 2);
  display.print(F("--------------"));
  while (!checkFram()) {
    REFRESH();
    delay(1000);
    display.setCursor(0, 0);
    display.print(F("FRAM NOT FOUND"));
    display.setCursor(0, 2);
    display.print(F("Waiting for reconnection..."));
  }
  display.clear();
}

static void framRequireConnected() {
  if (!checkFram()) framWaitForReconnect();
}

void saveSettings() {
  framWrite(NV_BRIGHT, brightIdx);
  framWrite(NV_DIM, dimIdx);
  framWrite(NV_SLEEP, sleepIdx);
  framWrite(NV_PREC, precIdx);
  framWrite(NV_SWDIM, swDimIdx);
}

void loadSettings() {
  uint8_t b = framRead(NV_BRIGHT), d = framRead(NV_DIM);
  uint8_t s = framRead(NV_SLEEP), p = framRead(NV_PREC);
  uint8_t w = framRead(NV_SWDIM);
  brightIdx = (b < 5) ? b : 2;
  dimIdx = (d < 4) ? d : 0;
  sleepIdx = (s < 3) ? s : 1;
  precIdx = (p < 4) ? p : 0;
  swDimIdx = (w < 3) ? w : 0;
}

void factoryReset() {
  brightIdx = 2;
  dimIdx = 0;
  sleepIdx = 1;
  precIdx = 0;
  swDimIdx = 0;
  saveSettings();
}

void applyBrightness() {
  display.setContrast(pgm_read_byte(&brightContrast[brightIdx]));
}

void applyDim(bool dim) {
  f.dimmed = dim;
  display.setContrast(dim ? 1 : pgm_read_byte(&brightContrast[brightIdx]));
}

void resetActivity() {
  tActivity = millis();
  if (f.dimmed) applyDim(false);
}

static void blankLine(uint8_t row) {
  display.setCursor(0, row);
  display.print(F("                    "));
}

static void exitApp() {
  appOpen = NO_APP;
  drawMenu();
  REFRESH();
}

static void drawErrorScreen(const __FlashStringHelper* msg) {
  display.clear();
  display.setCursor(0, 0);
  display.print(F("---[ ERROR ]--------"));
  display.setCursor(0, 2);
  display.print(msg);
  display.setCursor(0, 7);
  display.print(F("              [B]ACK"));
}

static void raiseError(const __FlashStringHelper* msg) {
  errorMsg = msg;
  errorOpen = true;
  drawErrorScreen(msg);
  REFRESH();
}

static uint8_t appendUint(char* b, uint8_t p, uint32_t v) {
  if (!v) {
    b[p++] = '0';
    return p;
  }
  char tmp[10];
  int8_t ti = 0;
  while (v) {
    tmp[ti++] = (char)('0' + v % 10);
    v /= 10;
  }
  for (int8_t j = ti - 1; j >= 0; j--) b[p++] = tmp[j];
  return p;
}

static inline uint8_t append2d(char* b, uint8_t p, uint8_t v) {
  b[p++] = '0' + v / 10;
  b[p++] = '0' + v % 10;
  return p;
}

static uint8_t pgmCopy(char* buf, uint8_t pos, const char* src) {
  uint8_t c;
  while ((c = pgm_read_byte(src++))) buf[pos++] = (char)c;
  return pos;
}

static void printFixed(int16_t val, uint8_t width) {
  bool neg = val < 0;
  if (neg) val = -val;
  uint16_t w = val / 10;
  uint8_t d = val % 10;
  uint8_t len = (w >= 100 ? 3 : w >= 10 ? 2 : 1) + 2 + neg;
  for (uint8_t s = width - len; s > 0; s--) display.print(' ');
  if (neg) display.print('-');
  display.print(w);
  display.print('.');
  display.print(d);
}

static void drawSensorLine() {
  display.setCursor(0, 1);
  if (!bmpReady) {
    // no sensor or is not ready
    return;
  }
  int16_t temp10 = (int16_t)(bmp.readTemperature() * 10.0f);
  if (f.useF) temp10 = (int32_t)temp10 * 9 / 5 + 320;
  int32_t presRaw = bmp.readPressure();
  int16_t presH = presRaw / 100;
  uint8_t presD = (uint8_t)((presRaw % 100) / 10);
  printFixed(temp10, 5);
  display.print(f.useF ? F(" 'F  ") : F(" 'C  "));
  if (presH < 1000) display.print(' ');
  if (presH < 100) display.print(' ');
  display.print(presH);
  display.print('.');
  display.print(presD);
  display.println(F(" hPa"));
}

static void drawMenuAppLine() {
  display.setCursor(0, 5);
  display.print(PGM_STR(appName(appIndex)));
}

void drawMenu() {
  display.clear();
  display.setCursor(0, 0);
  display.print(F("==[Main Menu]======="));
  display.setCursor(0, 2);
  display.print(F("                    "));
  display.setCursor(0, 3);
  display.print(F("____APPLICATIONS____"));
  display.setCursor(0, 4);
  display.print(F("    .----------.    "));
  drawMenuAppLine();
  display.setCursor(0, 6);
  display.print(F("    `----------'    "));
  display.setCursor(0, 7);
  display.print(("[P]  [N]    [>]  [B]"));
  drawSensorLine();
}

static void drawSettingRow(uint8_t row, const char* lbl, const char* val, bool sel) {
  display.setCursor(0, row);
  display.print(sel ? '>' : ' ');
  display.print(' ');
  display.print(PGM_STR(lbl));
  display.print(F("  ["));
  display.print(PGM_STR(val));
  display.print(']');
}

static void drawSettings() {
  display.clear();
  display.setCursor(0, 0);
  display.print(F("==[SETTINGS]========"));
  drawSettingRow(1, lBright, (const char*)pgm_read_word(&brightStrs[brightIdx]), settingSel == 0);
  drawSettingRow(2, lDim, (const char*)pgm_read_word(&dimStrs[dimIdx]), settingSel == 1);
  drawSettingRow(3, lSleep, (const char*)pgm_read_word(&sleepStrs[sleepIdx]), settingSel == 2);
  drawSettingRow(4, lPrec, (const char*)pgm_read_word(&precStrs[precIdx]), settingSel == 3);
  drawSettingRow(5, lSwDim, (const char*)pgm_read_word(&swDimStrs[swDimIdx]), settingSel == 4);
  drawSettingRow(6, lReset, rvReset, settingSel == 5);
  display.setCursor(0, 7);
  display.print(F("[P]  [N]    [>]  [B]"));
}

static void drawSettingsRowByIndex(uint8_t idx, bool sel) {
  uint8_t row = idx + 1;
  switch (idx) {
    case 0: drawSettingRow(row, lBright, (const char*)pgm_read_word(&brightStrs[brightIdx]), sel); break;
    case 1: drawSettingRow(row, lDim, (const char*)pgm_read_word(&dimStrs[dimIdx]), sel); break;
    case 2: drawSettingRow(row, lSleep, (const char*)pgm_read_word(&sleepStrs[sleepIdx]), sel); break;
    case 3: drawSettingRow(row, lPrec, (const char*)pgm_read_word(&precStrs[precIdx]), sel); break;
    case 4: drawSettingRow(row, lSwDim, (const char*)pgm_read_word(&swDimStrs[swDimIdx]), sel); break;
    default: drawSettingRow(row, lReset, rvReset, sel); break;
  }
}

void launchSettings() {
  drawSettings();
}

void inputSettings(bool rP, bool rN, bool rSel, bool rBck, bool) {
  if (rP && settingSel > 0) {
    uint8_t prev = settingSel;
    settingSel--;
    drawSettingsRowByIndex(prev, false);
    drawSettingsRowByIndex(settingSel, true);
  }
  if (rN && settingSel < SET_COUNT - 1) {
    uint8_t prev = settingSel;
    settingSel++;
    drawSettingsRowByIndex(prev, false);
    drawSettingsRowByIndex(settingSel, true);
  }
  if (rSel) {
    switch (settingSel) {
      case 0:
        brightIdx = (brightIdx + 1) % 5;
        applyBrightness();
        break;
      case 1: dimIdx = (dimIdx + 1) % 4; break;
      case 2: sleepIdx = (sleepIdx + 1) % 3; break;
      case 3:
        precIdx = (precIdx + 1) % 4;
        calcReset();
        break;
      case 4: swDimIdx = (swDimIdx + 1) % 3; break;
      case 5: factoryReset(); break;
    }
    saveSettings();
    drawSettings();
  }
  if (rBck) exitApp();
}

static uint16_t readVcc() {
#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#else
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#endif
  delay(2);
  ADCSRA |= _BV(ADSC);
  loop_until_bit_is_clear(ADCSRA, ADSC);
  uint16_t adc = ADC;
  return adc ? (uint16_t)(1125300UL / adc) : 0;
}

static int16_t freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int16_t)((int)&v - (__brkval ? (int)__brkval : (int)&__heap_start));
}

static void refreshSysReadings() {
  if (!bmpReady) bmpReady = bmp.begin();
  siTemp10 = bmpReady ? (int16_t)(bmp.readTemperature() * 10.0f) : 0;
  siPres = bmpReady ? bmp.readPressure() : 0;
  siAlt = bmpReady ? (int16_t)bmp.readAltitude() : 0;
  siVcc = readVcc();
  siRam = freeRam();
}

static uint8_t buildSysVal(uint8_t idx, char* buf) {
  uint8_t p = 0;
  switch (idx) {
    case 0: {
      uint32_t ms = millis();
      uint16_t h = (uint16_t)(ms / 3600000UL);
      uint8_t m = (uint8_t)((ms / 60000UL) % 60);
      uint8_t s = (uint8_t)((ms / 1000UL) % 60);
      p = (h < 100) ? append2d(buf, p, (uint8_t)h) : appendUint(buf, p, h);
      buf[p++] = ':';
      p = append2d(buf, p, m);
      buf[p++] = ':';
      p = append2d(buf, p, s);
      break;
    }
    case 1:
      if (siRam < 0) {
        buf[p++] = 'O';
        buf[p++] = 'V';
        buf[p++] = 'F';
        buf[p++] = '!';
      } else {
        p = appendUint(buf, p, (uint32_t)siRam);
        buf[p++] = ' ';
        buf[p++] = 'B';
      }
      break;
    case 2: {
      uint16_t v = siVcc, fr = v % 1000;
      p = appendUint(buf, p, v / 1000);
      buf[p++] = '.';
      buf[p++] = '0' + fr / 100;
      buf[p++] = '0' + (fr % 100) / 10;
      buf[p++] = '0' + fr % 10;
      buf[p++] = ' ';
      buf[p++] = 'V';
      break;
    }
    case 3: p = pgmCopy(buf, p, siMcu); break;
    case 4:
      p = appendUint(buf, p, (uint32_t)(F_CPU / 1000000UL));
      buf[p++] = ' ';
      buf[p++] = 'M';
      buf[p++] = 'H';
      buf[p++] = 'z';
      break;
    case 5: {
      int16_t t = siTemp10;
      if (f.useF) t = (int32_t)t * 9 / 5 + 320;
      bool neg = t < 0;
      if (neg) t = -t;
      if (neg) buf[p++] = '-';
      p = appendUint(buf, p, (uint32_t)(t / 10));
      buf[p++] = '.';
      buf[p++] = '0' + (t % 10);
      buf[p++] = ' ';
      buf[p++] = '\'';
      buf[p++] = f.useF ? 'F' : 'C';
      break;
    }
    case 6: {
      int16_t ph = (int16_t)(siPres / 100);
      uint8_t pd = (uint8_t)((siPres % 100) / 10);
      p = appendUint(buf, p, (uint32_t)ph);
      buf[p++] = '.';
      buf[p++] = '0' + pd;
      buf[p++] = ' ';
      buf[p++] = 'h';
      buf[p++] = 'P';
      buf[p++] = 'a';
      break;
    }
    case 7: {
      int16_t alt = siAlt;
      if (alt < 0) {
        buf[p++] = '-';
        alt = -alt;
      }
      p = appendUint(buf, p, (uint32_t)alt);
      buf[p++] = ' ';
      buf[p++] = 'm';
      break;
    }
    case 8: {
      const uint8_t bv[5] = { 0, 25, 50, 75, 100 };
      p = appendUint(buf, p, bv[brightIdx]);
      buf[p++] = '%';
      break;
    }
    case 9:
      p = appendUint(buf, p, pgm_read_byte(&dimSecs[dimIdx]));
      buf[p++] = 's';
      break;
    case 10:
      p = appendUint(buf, p, pgm_read_byte(&sleepMins[sleepIdx]));
      buf[p++] = 'm';
      break;
    case 11:
      p = appendUint(buf, p, pgm_read_byte(&dpDigits[precIdx]));
      buf[p++] = ' ';
      buf[p++] = 'D';
      buf[p++] = 'P';
      break;
    case 12:
      if (sysFram) {
        buf[p++] = 'O';
        buf[p++] = 'K';
      } else {
        buf[p++] = 'F';
        buf[p++] = 'A';
        buf[p++] = 'I';
        buf[p++] = 'L';
      }
      break;
    case 13: p = pgmCopy(buf, p, siBldDate); break;
    case 14: p = pgmCopy(buf, p, siBldTime); break;
  }
  buf[p] = 0;
  return p;
}

static void printSysRow(uint8_t row, uint8_t idx) {
  char val[12];
  uint8_t vlen = buildSysVal(idx, val);
  display.setCursor(0, row);
  display.print(PGM_STR(pgm_read_word(&siLabels[idx])));
  for (uint8_t j = vlen; j < 11; j++) display.print(' ');
  display.print(val);
}

static void drawSysInfoContent() {
  for (uint8_t i = 0; i < SI_ROWS; i++) {
    uint8_t idx = sysScroll + i;
    if (idx >= SI_LINES) blankLine(i + 1);
    else printSysRow(i + 1, idx);
  }
}

static void drawSysInfo() {
  display.clear();
  uint8_t from = sysScroll + 1;
  uint8_t to = sysScroll + SI_ROWS;
  if (to > SI_LINES) to = SI_LINES;
  display.setCursor(0, 0);
  display.print(F("[SYS INFO]  "));
  if (from < 10) display.print('0');
  display.print(from);
  display.print('-');
  if (to < 10) display.print('0');
  display.print(to);
  display.print('/');
  if (SI_LINES < 10) display.print('0');
  display.print(SI_LINES);
  drawSysInfoContent();
  display.setCursor(0, 7);
  display.print(F("[P]  [N]    [>]  [B]"));
}

void launchSysInfo() {
  sysScroll = 0;
  sysFram = checkFram();
  if (!sysFram) framWaitForReconnect();
  sysFram = true;
  refreshSysReadings();
  drawSysInfo();
  tSysRefresh = millis();
}

void inputSysInfo(bool rP, bool rN, bool rSel, bool rBck, bool) {
  if (rP && sysScroll > 0) {
    sysScroll--;
    drawSysInfo();
  }
  if (rN && sysScroll < SI_LINES - SI_ROWS) {
    sysScroll++;
    drawSysInfo();
  }
  if (rSel) {
    refreshSysReadings();
    drawSysInfoContent();
    tSysRefresh = millis();
  }
  if (rBck) exitApp();
}

void tickSysInfo() {
  if (millis() - tSysRefresh >= 2000UL) {
    refreshSysReadings();
    drawSysInfoContent();
    tSysRefresh = millis();
  }
}

static inline bool calcIsInput() {
  return calcState == CS_A || calcState == CS_B;
}

static inline uint8_t calcMaxSel() {
  if (calcIsInput()) return CSEL_NUM;
  if (calcState == CS_OP) return CSEL_OP;
  return CSEL_RES;
}

static inline uint8_t getDP() {
  return pgm_read_byte(&dpDigits[precIdx]);
}

static inline uint8_t getMaxInt() {
  return pgm_read_byte(&maxIntDigs[precIdx]);
}

static int64_t getScale() {
  int64_t s;
  memcpy_P(&s, &scaleTable[precIdx], sizeof(s));
  return s;
}

static inline bool isUnaryOp(uint8_t op) {
  return (op >= COP_SIN && op <= COP_ABS);
}

static inline float scaledToFloat(int64_t v) {
  return (float)v / (float)getScale();
}

static inline int64_t floatToScaled(float v) {
  return (int64_t)(v * (float)getScale());
}

static inline float scaledToFloatWithScale(int64_t v, int64_t scale) {
  return (float)v / (float)scale;
}

static inline int64_t floatToScaledWithScale(float v, int64_t scale) {
  return (int64_t)(v * (float)scale);
}

#define DEG_TO_RAD_F (M_PI / 180.0f)
#define RAD_TO_DEG_F (180.0f / M_PI)
#define SCALED_OVF_LIMIT 9.2e18f

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

static inline void resetDigitBuf() {
  digitLen = 0;
  digitIntLen = 0;
  digitNeg = false;
  digitDecMode = false;
}

static void saveCalcResult() {
  framWriteInt64(NV_CALC_RESULT, calcResult);
  framWrite(NV_CALC_VALID, 0xAA);
  calcSavedOk = true;
}

static bool loadCalcResult(int64_t* out) {
  if (framRead(NV_CALC_VALID) != 0xAA) return false;
  *out = framReadInt64(NV_CALC_RESULT);
  return true;
}

void calcReset() {
  calcState = CS_A;
  calcOp = 0;
  calcSel = 0;
  resetDigitBuf();
  selLongFired = false;
  numA = 0;
  numB = 0;
  calcResult = 0;
  calcOvf = false;
  calcDivZ = false;
  calcSavedOk = false;
}

static uint8_t scaledToStr(int64_t v, uint8_t dp, int64_t scale, char* buf) {
  bool neg = v < 0;
  uint64_t uv = neg ? (v == INT64_MIN ? (uint64_t)9223372036854775808ULL : (uint64_t)(-v))
                    : (uint64_t)v;
  uint64_t uscale = (uint64_t)scale;
  uint64_t intPart = uv / uscale;
  uint64_t fracPart = uv % uscale;
  uint8_t pos = 0;
  if (neg) buf[pos++] = '-';
  if (!intPart) {
    buf[pos++] = '0';
  } else {
    char tmp[20];
    uint8_t ti = 0;
    for (uint64_t ip = intPart; ip; ip /= 10) tmp[ti++] = '0' + (uint8_t)(ip % 10);
    for (uint8_t i = 0; i < ti; i++) buf[pos++] = tmp[ti - 1 - i];
  }
  buf[pos++] = '.';
  char frac[13];
  uint64_t fp = fracPart;
  for (uint8_t i = dp; i > 0; i--) {
    frac[i - 1] = '0' + (uint8_t)(fp % 10);
    fp /= 10;
  }
  for (uint8_t i = 0; i < dp; i++) buf[pos++] = frac[i];
  buf[pos] = 0;
  return pos;
}

static void printScaledR(uint8_t row, int64_t val, uint8_t width) {
  uint8_t dp = getDP();
  int64_t scale = getScale();
  char buf[22];
  uint8_t len = scaledToStr(val, dp, scale, buf);
  display.setCursor(0, row);
  if (len <= width) {
    for (uint8_t i = 0; i < width - len; i++) display.print(' ');
    display.print(buf);
  } else {
    for (uint8_t i = 0; i < width - 1; i++) display.print(buf[i]);
    display.print('~');
  }
}

static int64_t bufToScaled() {
  int64_t scale = getScale(), intPart = 0;
  for (uint8_t i = 0; i < digitIntLen; i++) intPart = intPart * 10 + (digitBuf[i] - '0');
  uint8_t fracLen = digitLen - digitIntLen;
  int64_t fracPart = 0;
  for (uint8_t i = digitIntLen; i < digitLen; i++) fracPart = fracPart * 10 + (digitBuf[i] - '0');
  for (uint8_t i = fracLen; i < getDP(); i++) fracPart *= 10;
  int64_t result = intPart * scale + fracPart;
  return digitNeg ? -result : result;
}

static void getOpLabel(uint8_t op, char out[3]) {
  const char* base = calcOpLabels + (uint16_t)op * 2;
  out[0] = (char)pgm_read_byte(base);
  out[1] = (char)pgm_read_byte(base + 1);
  out[2] = 0;
}

static void drawOpGridRow(uint8_t row, uint8_t firstOp) {
  display.setCursor(0, row);
  for (uint8_t i = 0; i < 5; i++) {
    uint8_t op = firstOp + i;
    if (op >= CSEL_OP) {
      display.print(F("    "));
    } else {
      char lb[3];
      getOpLabel(op, lb);
      bool sel = (calcSel == op);
      display.print(sel ? '[' : ' ');
      display.print(lb[0]);
      display.print(lb[1]);
      display.print(sel ? ']' : ' ');
    }
  }
}

static void drawOpDescRow() {
  display.setCursor(0, 2);
  display.print(PGM_STR((const char*)pgm_read_word(&calcOpDescs[calcSel])));
}

static void drawCalcOpSelect() {
  display.clear();
  drawCalcHeader();
  printScaledR(1, numA, 20);
  drawOpDescRow();
  drawOpGridRow(3, 0);
  drawOpGridRow(4, 5);
  drawOpGridRow(5, 10);
  drawOpGridRow(6, 15);
  display.setCursor(0, 7);
  display.print(F("[P]  [N]    [>]  [B]"));
}

static void updateCalcOpGrid() {
  drawOpDescRow();
  drawOpGridRow(3, 0);
  drawOpGridRow(4, 5);
  drawOpGridRow(5, 10);
  drawOpGridRow(6, 15);
}

static void drawCalcHeader() {
  uint8_t dp = getDP();
  display.setCursor(0, 0);
  display.print(F("=[AVR CALC]="));
  if (dp < 10) display.print(' ');
  display.print(dp);
  display.print(F("DP["));
  display.print((calcIsInput() && digitDecMode) ? 'D' : 'I');
  display.print(']');
  display.print(calcSavedOk ? 'S' : ' ');
}

static void printNumRowBuf(uint8_t row) {
  uint8_t dp = getDP(), maxI = getMaxInt();
  uint8_t fracLen = digitLen - digitIntLen;
  bool showDot = digitDecMode || fracLen > 0;
  bool cursor = digitDecMode ? (fracLen < dp) : (digitIntLen < maxI);
  uint8_t visLen = (digitNeg ? 1 : 0)
                   + (digitIntLen ? digitIntLen : 1)
                   + (showDot ? 1 + fracLen : 0)
                   + (cursor ? 1 : 0);
  display.setCursor(0, row);
  for (uint8_t i = 0; i < 20 - visLen; i++) display.print(' ');
  if (digitNeg) display.print('-');
  if (!digitIntLen) display.print('0');
  else
    for (uint8_t i = 0; i < digitIntLen; i++) display.print(digitBuf[i]);
  if (showDot) {
    display.print('.');
    for (uint8_t i = digitIntLen; i < digitLen; i++) display.print(digitBuf[i]);
  }
  if (cursor) display.print('_');
}

static void getSelLabel(uint8_t idx, char out[3]) {
  out[2] = 0;
  switch (calcState) {
    case CS_A:
    case CS_B:
      switch (idx) {
        case 10:
          out[0] = 'B';
          out[1] = 'S';
          break;
        case 11:
          out[0] = 'N';
          out[1] = 'G';
          break;
        case 12:
          out[0] = 'C';
          out[1] = 'R';
          break;
        case 13:
          out[0] = 'O';
          out[1] = 'K';
          break;
        case 14:
          out[0] = 'L';
          out[1] = 'D';
          break;
        default:
          out[0] = ' ';
          out[1] = '0' + idx;
          break;
      }
      break;
    case CS_OP:
      getOpLabel(idx, out);
      break;
    case CS_RESULT:
      out[0] = 'C';
      switch (idx) {
        case 0: out[1] = 'R'; break;
        case 1: out[1] = 'H'; break;
        default:
          out[0] = 'S';
          out[1] = 'V';
          break;
      }
      break;
  }
}

static void drawCalcSelector() {
  uint8_t maxS = calcMaxSel();
  uint8_t prev = (calcSel == 0) ? maxS - 1 : calcSel - 1;
  uint8_t nxt = (calcSel + 1) % maxS;
  char lp[3], lc[3], ln[3];
  getSelLabel(prev, lp);
  getSelLabel(calcSel, lc);
  getSelLabel(nxt, ln);
  display.setCursor(0, 6);
  display.print(F("    "));
  display.print(lp);
  display.print(F("  ["));
  display.print(lc);
  display.print(F("]  "));
  display.print(ln);
  display.print(F("    "));
}

static void printOpRow() {
  display.setCursor(0, 2);
  if (calcState >= CS_OP) {
    display.print(PGM_STR((const char*)pgm_read_word(&calcOpDescs[calcOp])));
  } else {
    display.print(F("                    "));
  }
}

static void printResultRow() {
  if (calcState < CS_RESULT) {
    blankLine(4);
    return;
  }
  display.setCursor(0, 4);
  if (calcOvf) {
    display.print(F("=    [OVERFLOW!]    "));
    return;
  }
  if (calcDivZ) {
    display.print(F("=    [DIV/ZERO!]    "));
    return;
  }
  uint8_t dp = getDP();
  int64_t scale = getScale();
  char buf[22];
  uint8_t len = scaledToStr(calcResult, dp, scale, buf);
  display.print('=');
  if (len <= 19) {
    for (uint8_t i = 0; i < 19 - len; i++) display.print(' ');
    display.print(buf);
  } else {
    for (uint8_t i = 0; i < 18; i++) display.print(buf[i]);
    display.print('~');
  }
}

static void drawCalcFull() {
  if (calcState == CS_OP) {
    drawCalcOpSelect();
    return;
  }
  display.clear();
  drawCalcHeader();
  if (calcState == CS_A) printNumRowBuf(1);
  else printScaledR(1, numA, 20);
  printOpRow();
  if (calcState == CS_B) printNumRowBuf(3);
  else if (calcState == CS_RESULT) {
    if (isUnaryOp(calcOp)) blankLine(3);
    else printScaledR(3, numB, 20);
  } else blankLine(3);
  printResultRow();
  display.setCursor(0, 5);
  display.print(F("--------------------"));
  drawCalcSelector();
  display.setCursor(0, 7);
  display.print(F("[P]  [N]    [>]  [B]"));
}

static bool i64MulOvf(int64_t a, int64_t b) {
  if (!a || !b) return false;
  if (a == -1) return b == INT64_MIN;
  if (b == -1) return a == INT64_MIN;
  int64_t lim = ((a ^ b) < 0) ? INT64_MIN : INT64_MAX;
  return ((a ^ b) < 0) ? (a < lim / b) : (a > lim / b);
}

static void calcCompute() {
  calcOvf = false;
  calcDivZ = false;
  int64_t scale = getScale();
  switch (calcOp) {
    case COP_ADD:
      if ((numB > 0 && numA > INT64_MAX - numB) || (numB < 0 && numA < INT64_MIN - numB))
        calcOvf = true;
      else calcResult = numA + numB;
      break;
    case COP_SUB:
      if ((numB < 0 && numA > INT64_MAX + numB) || (numB > 0 && numA < INT64_MIN + numB))
        calcOvf = true;
      else calcResult = numA - numB;
      break;
    case COP_MUL:
      if (!numA || !numB) {
        calcResult = 0;
        break;
      }
      if (i64MulOvf(numA, numB)) {
        calcOvf = true;
        break;
      }
      calcResult = (numA * numB) / scale;
      break;
    case COP_DIV: {
      if (!numB) {
        calcDivZ = true;
        break;
      }
      if (i64MulOvf(numA, scale)) {
        calcOvf = true;
        break;
      }
      int64_t num = numA * scale;
      if (num == INT64_MIN && numB == -1) {
        calcOvf = true;
        break;
      }
      calcResult = num / numB;
      break;
    }
    case COP_MOD:
      if (!numB) {
        calcDivZ = true;
        break;
      }
      calcResult = numA % numB;
      break;
    case COP_POW:
#if PLANKTOS_CALC_ENABLE_POW
      {
        float result = powf(scaledToFloatWithScale(numA, scale), scaledToFloatWithScale(numB, scale));
        if (isinf(result) || isnan(result)) {
          calcOvf = true;
        } else {
          float sr = result * (float)scale;
          if (sr >= SCALED_OVF_LIMIT || sr <= -SCALED_OVF_LIMIT || isinf(sr) || isnan(sr)) {
            calcOvf = true;
          } else calcResult = (int64_t)sr;
        }
      }
#else
      { calcOvf = true; }
#endif
      break;
    case COP_SIN:
#if PLANKTOS_CALC_ENABLE_TRIG
      calcResult = floatToScaledWithScale(sinf(scaledToFloatWithScale(numA, scale) * DEG_TO_RAD_F), scale);
#else
      calcOvf = true;
#endif
      break;
    case COP_COS:
#if PLANKTOS_CALC_ENABLE_TRIG
      calcResult = floatToScaledWithScale(cosf(scaledToFloatWithScale(numA, scale) * DEG_TO_RAD_F), scale);
#else
      calcOvf = true;
#endif
      break;
    case COP_TAN:
#if PLANKTOS_CALC_ENABLE_TRIG
      {
        float t = tanf(scaledToFloatWithScale(numA, scale) * DEG_TO_RAD_F);
        if (isinf(t) || isnan(t)) {
          calcDivZ = true;
          break;
        }
        float st = t * (float)scale;
        if (st >= SCALED_OVF_LIMIT || st <= -SCALED_OVF_LIMIT || isinf(st) || isnan(st)) {
          calcOvf = true;
          break;
        }
        calcResult = (int64_t)st;
        break;
      }
#else
      {
        calcOvf = true;
        break;
      }
#endif
    case COP_ASN:
#if PLANKTOS_CALC_ENABLE_INVTRIG
      {
        float v = scaledToFloatWithScale(numA, scale);
        if (v < -1.0f || v > 1.0f) {
          calcOvf = true;
          break;
        }
        calcResult = floatToScaledWithScale(asinf(v) * RAD_TO_DEG_F, scale);
        break;
      }
#else
      {
        calcOvf = true;
        break;
      }
#endif
    case COP_ACS:
#if PLANKTOS_CALC_ENABLE_INVTRIG
      {
        float v = scaledToFloatWithScale(numA, scale);
        if (v < -1.0f || v > 1.0f) {
          calcOvf = true;
          break;
        }
        calcResult = floatToScaledWithScale(acosf(v) * RAD_TO_DEG_F, scale);
        break;
      }
#else
      {
        calcOvf = true;
        break;
      }
#endif
    case COP_ATN:
#if PLANKTOS_CALC_ENABLE_INVTRIG
      calcResult = floatToScaledWithScale(atanf(scaledToFloatWithScale(numA, scale)) * RAD_TO_DEG_F, scale);
#else
      calcOvf = true;
#endif
      break;
    case COP_SQT:
#if PLANKTOS_CALC_ENABLE_SQRT
      {
        float v = scaledToFloatWithScale(numA, scale);
        if (v < 0.0f) {
          calcOvf = true;
          break;
        }
        calcResult = floatToScaledWithScale(sqrtf(v), scale);
        break;
      }
#else
      {
        calcOvf = true;
        break;
      }
#endif
    case COP_LOG:
#if PLANKTOS_CALC_ENABLE_LOG
      {
        float v = scaledToFloatWithScale(numA, scale);
        if (v <= 0.0f) {
          calcOvf = true;
          break;
        }
        calcResult = floatToScaledWithScale(log10f(v), scale);
        break;
      }
#else
      {
        calcOvf = true;
        break;
      }
#endif
    case COP_LN:
#if PLANKTOS_CALC_ENABLE_LOG
      {
        float v = scaledToFloatWithScale(numA, scale);
        if (v <= 0.0f) {
          calcOvf = true;
          break;
        }
        calcResult = floatToScaledWithScale(logf(v), scale);
        break;
      }
#else
      {
        calcOvf = true;
        break;
      }
#endif
    case COP_ABS:
      if (numA == INT64_MIN) {
        calcOvf = true;
        break;
      }
      calcResult = (numA < 0) ? -numA : numA;
      break;
    case COP_AVG:
      if ((numA > 0 && numB > INT64_MAX - numA) || (numA < 0 && numB < INT64_MIN - numA)) {
        calcOvf = true;
        break;
      }
      calcResult = (numA + numB) / 2;
      break;
  }
}

static bool handleDigitSel() {
  uint8_t dp = getDP(), maxI = getMaxInt();
  uint8_t fracLen = digitLen - digitIntLen;
  switch (calcSel) {
    case 10:
      if (digitLen > 0) {
        digitLen--;
        if (digitLen < digitIntLen) digitIntLen = digitLen;
      }
      return false;
    case 11: digitNeg = !digitNeg; return false;
    case 12: resetDigitBuf(); return false;
    case 13: return true;
    default:
      if (!digitDecMode) {
        if (digitIntLen < maxI) {
          digitBuf[digitLen++] = '0' + calcSel;
          digitIntLen++;
        }
      } else {
        if (fracLen < dp) digitBuf[digitLen++] = '0' + calcSel;
      }
      return false;
  }
}

static void commitNumber(int64_t* target, uint8_t nextState) {
  if (!digitLen) {
    digitBuf[0] = '0';
    digitLen = 1;
    digitIntLen = 1;
  }
  *target = bufToScaled();
  resetDigitBuf();
  calcSel = 0;
  calcState = nextState;
}

static void calcNotify_P(const __FlashStringHelper* msg) {
  display.setCursor(0, 5);
  display.print(msg);
}

void launchCalc() {
  drawCalcFull();
}

void inputCalc(bool rP, bool rN, bool rSel, bool rBck, bool longSel) {
  if (rBck) {
    exitApp();
    return;
  }
  if (longSel) {
    if (calcIsInput()) {
      digitDecMode = !digitDecMode;
      if (!digitDecMode) digitLen = digitIntLen;
      drawCalcHeader();
      printNumRowBuf(calcState == CS_A ? 1 : 3);
    } else if (calcState == CS_RESULT && !calcOvf && !calcDivZ) {
      saveCalcResult();
      calcNotify_P(F("** SAVED TO FRAM ** "));
      drawCalcHeader();
    }
    return;
  }
  uint8_t maxS = calcMaxSel();
  if (rP) {
    calcSel = (calcSel == 0) ? maxS - 1 : calcSel - 1;
    if (calcState == CS_OP) updateCalcOpGrid();
    else drawCalcSelector();
    return;
  }
  if (rN) {
    calcSel = (calcSel + 1) % maxS;
    if (calcState == CS_OP) updateCalcOpGrid();
    else drawCalcSelector();
    return;
  }
  if (!rSel) return;
  switch (calcState) {
    case CS_A:
      if (calcSel == 14) {
        int64_t loaded;
        if (loadCalcResult(&loaded)) {
          numA = loaded;
          resetDigitBuf();
          calcSel = 0;
          calcState = CS_OP;
          drawCalcFull();
        } else {
          calcNotify_P(F("** NO SAVED VALUE **"));
        }
        return;
      }
      if (handleDigitSel()) {
        commitNumber(&numA, CS_OP);
        drawCalcFull();
      } else {
        printNumRowBuf(1);
        drawCalcSelector();
      }
      break;
    case CS_OP:
      calcOp = calcSel;
      calcSel = 0;
      if (isUnaryOp(calcOp)) {
        numB = 0;
        calcCompute();
        if (calcDivZ) {
          raiseError(F("DIV/ZERO!"));
          return;
        }
        if (calcOvf) {
          raiseError(F("OVERFLOW!"));
          return;
        }
        calcState = CS_RESULT;
        drawCalcFull();
      } else {
        calcState = CS_B;
        drawCalcFull();
      }
      break;
    case CS_B:
      if (calcSel == 14) {
        int64_t loaded;
        if (loadCalcResult(&loaded)) {
          numB = loaded;
          resetDigitBuf();
          calcSel = 0;
          calcCompute();
          calcState = CS_RESULT;
          drawCalcFull();
        } else {
          calcNotify_P(F("** NO SAVED VALUE **"));
        }
        return;
      }
      if (handleDigitSel()) {
        commitNumber(&numB, CS_RESULT);
        calcCompute();
        if (calcDivZ) {
          raiseError(F("DIV/ZERO!"));
          return;
        }
        if (calcOvf) {
          raiseError(F("OVERFLOW!!"));
          return;
        }
        drawCalcFull();
      } else {
        printNumRowBuf(3);
        drawCalcSelector();
      }
      break;
    case CS_RESULT:
      switch (calcSel) {
        case 0:
          calcReset();
          drawCalcFull();
          break;
        case 1:
          if (!calcOvf && !calcDivZ) {
            numA = calcResult;
            resetDigitBuf();
            calcSel = 0;
            calcState = CS_OP;
            drawCalcFull();
          }
          break;
        case 2:
          if (!calcOvf && !calcDivZ) {
            saveCalcResult();
            calcNotify_P(F("** SAVED TO FRAM ** "));
            drawCalcHeader();
          }//u
          break;
      }
      break;
  }
}

static uint64_t swElapsed() {
  return swAccum + (swRunning ? (uint64_t)(uint32_t)(millis() - swLastMs) : 0);
}

static void printTime(uint8_t row, uint64_t ms) {
  uint8_t t = (uint8_t)((ms / 100ULL) % 10);
  uint8_t s = (uint8_t)((ms / 1000ULL) % 60);
  uint8_t m = (uint8_t)((ms / 60000ULL) % 60);
  uint32_t h = (uint32_t)(ms / 3600000ULL);
  char buf[22];
  uint8_t p = 0;
  if (h < 10) {
    buf[p++] = '0';
    buf[p++] = (char)('0' + h);
  } else p = appendUint(buf, p, h);
  buf[p++] = ':';
  buf[p++] = (char)('0' + m / 10);
  buf[p++] = (char)('0' + m % 10);
  buf[p++] = ':';
  buf[p++] = (char)('0' + s / 10);
  buf[p++] = (char)('0' + s % 10);
  buf[p++] = '.';
  buf[p++] = (char)('0' + t);
  buf[p] = 0;
  uint8_t pad = (uint8_t)((20 - p) / 2);
  display.setCursor(0, row);
  for (uint8_t i = 0; i < pad; i++) display.print(' ');
  display.print(buf);
  for (uint8_t i = pad + p; i < 20; i++) display.print(' ');
}

static void drawStopwatch() {
  display.clear();
  display.setCursor(0, 0);
  display.print(F("==[STOPWATCH]======="));
  printTime(1, swElapsed());
  display.setCursor(0, 2);
  display.print(F("--------------------"));
  if (swLapSet) printTime(3, swLap);
  else {
    display.setCursor(0, 3);
    display.print(F("     --:--:--.--    "));
  }
  display.setCursor(0, 4);
  display.print(F("--------------------"));
  display.setCursor(0, 5);
  display.print(swRunning ? F("[PAUSE]      [RESET]") : F("[START]      [RESET]"));
  display.setCursor(0, 6);
  display.print(F("[FLAG]        [HOME]"));
}

void launchStopwatch() {
  if (!swRunning && swAccum == 0) {
    swLap = 0;
    swLapSet = false;
  }
  drawStopwatch();
}

void inputStopwatch(bool rP, bool rN, bool rSel, bool rBck, bool) {
  if (rP) {
    if (swRunning) {
      swAccum += (uint64_t)(uint32_t)(millis() - swLastMs);
      swRunning = false;
    } else {
      swLastMs = millis();
      swRunning = true;
    }
    display.setCursor(0, 5);
    display.print(swRunning ? F("[PAUSE]      [RESET]") : F("[START]      [RESET]"));
    if (!swRunning) printTime(1, swElapsed());
    return;
  }
  if (rN) {
    swAccum = 0;
    swLap = 0;
    swLapSet = false;
    swRunning = false;
    drawStopwatch();
    return;
  }
  if (rSel) {
    swLap = swElapsed();
    swLapSet = true;
    printTime(3, swLap);
    return;
  }
  if (rBck) {
    exitApp();
    return;
  }
}

void tickStopwatch() {
  static uint32_t tLast = 0;
  if (swRunning) {
    uint32_t now = millis();
    if ((uint32_t)(now - tLast) >= 100) {
      tLast = now;
      printTime(1, swElapsed());
    }
  }
}

ISR(PCINT2_vect) {}

void enterSleep() {
  display.ssd1306WriteCmd(0xAE);
  PCMSK2 |= (1 << PCINT21);
  PCICR |= (1 << PCIE2);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sei();
  sleep_cpu();
  sleep_disable();
  PCMSK2 &= ~(1 << PCINT21);
  PCICR &= ~(1 << PCIE2);
  display.ssd1306WriteCmd(0xAF);
  f.dimmed = 0;
  applyBrightness();
  tActivity = millis();
  if (errorOpen) {
    drawErrorScreen(errorMsg ? errorMsg : F("ERROR"));
    REFRESH();
  } else if (appOpen == NO_APP) {
    drawMenu();
    REFRESH();
  } else appLaunch(appOpen);
}

static void tickBoot() {
  uint32_t now = millis();
  if (passes == 0) {
    display.clear();
    passes = 1;
    animStep = 0;
    animT = now;
  }
  uint8_t row = passes - 1;
  switch (animStep) {
    case 0:
      display.setCursor(0, row);
      display.print(PGM_STR(pgm_read_word(&bootLines[row])));
      animStep = 1;
      animT = now;
      break;
    case 1:
      if (now - animT >= 25) {
        blankLine(row);
        animStep = 2;
        animT = now;
      }
      break;
    case 2:
      if (now - animT >= 80) {
        display.setCursor(0, row);
        display.print(PGM_STR(pgm_read_word(&bootLines[row])));
        animStep = 3;
        animT = now;
      }
      break;
    case 3:
      if (now - animT >= 60) {
        if (passes >= 8) f.latch = 1;
        else {
          passes++;
          animStep = 0;
        }
      }
      break;
  }
}

void setup() {
  pinMode(PAD_P, INPUT);
  pinMode(PAD_N, INPUT);
  pinMode(PAD_P2, INPUT);
  pinMode(PAD_B, INPUT);
  Wire.begin();
  display.begin(&Adafruit128x64, I2C_ADDRESS);
  display.setFont(Adafruit5x7);
  display.clear();
  brightIdx = 2;
  dimIdx = 0;
  sleepIdx = 1;
  precIdx = 0;
  swDimIdx = 0;
  applyBrightness();
  framRequireConnected();
  loadSettings();
  applyBrightness();
  for (uint8_t attempt = 0; attempt < 5 && !bmpReady; attempt++) {
    bmpReady = bmp.begin();
    if (!bmpReady) delay(200);
  }
  tActivity = millis();
}

void loop() {
  if (!f.latch) {
    tickBoot();
    return;
  }
  if (!f.inMenu) {
    if (digitalRead(PAD_P) || digitalRead(PAD_N) || digitalRead(PAD_P2) || digitalRead(PAD_B)) {
      f.inMenu = 1;
      resetActivity();
      REFRESH();
      drawMenu();
    }
    return;
  }
  bool curP = digitalRead(PAD_P), curN = digitalRead(PAD_N);
  bool curP2 = digitalRead(PAD_P2), curB = digitalRead(PAD_B);
  bool rP2 = false, longP2 = false;
  if (curP2 && !f.lastP2) {
    tSelDown = millis();
    selLongFired = false;
  }
  if (curP2 && !selLongFired && (millis() - tSelDown >= LONG_PRESS_MS)) {
    longP2 = true;
    selLongFired = true;
  }
  if (!curP2 && f.lastP2 && !selLongFired) rP2 = true;
  bool rP = curP && !f.lastP;
  bool rN = curN && !f.lastN;
  bool rB = curB && !f.lastB;
  if (rP || rN || rP2 || rB || longP2) resetActivity();
  bool skipAppDispatch = false;
  if (errorOpen) {
    skipAppDispatch = true;
    if (rP || rN || rP2 || rB || longP2) {
      errorOpen = false;
      appOpen = NO_APP;
      calcReset();
      drawMenu();
      REFRESH();
    }
  }
  if (!skipAppDispatch) {
    if (appOpen == NO_APP) {
      if (rP) {
        appIndex = (appIndex == 0) ? APP_COUNT - 1 : appIndex - 1;
        drawMenuAppLine();
        REFRESH();
      }
      if (rN) {
        appIndex = (appIndex + 1) % APP_COUNT;
        drawMenuAppLine();
        REFRESH();
      }
      if (rP2) {
        appOpen = appIndex;
        appLaunch(appOpen);
      }
      if (appOpen == NO_APP && (uint16_t)millis() - tRefresh >= 2000) {
        REFRESH();
        drawSensorLine();
      }
    } else {
      bool swWakeConsumed = false;
      if (appOpen == APP_IDX_STOPWATCH && swDimIdx == SWDIM_WAKE && f.dimmed) {
        if (rP || rN || rP2 || rB || longP2) {
          applyDim(false);
          tActivity = millis();
          swWakeConsumed = true;
        }
      }
      if (!swWakeConsumed) appInput(appOpen, rP, rN, rP2, rB, longP2);
      if (appOpen != NO_APP) appTick(appOpen);
    }
  }
  f.lastP = curP;
  f.lastN = curN;
  f.lastP2 = curP2;
  f.lastB = curB;
  uint32_t now32 = millis();
  uint32_t dimMs = (uint32_t)pgm_read_byte(&dimSecs[dimIdx]) * 1000UL;
  uint32_t slpMs = (uint32_t)pgm_read_byte(&sleepMins[sleepIdx]) * 60000UL;
  if (appOpen == APP_IDX_STOPWATCH && swDimIdx == SWDIM_KEEP) {
    tActivity = now32;
  } else {
    if (!f.dimmed && now32 - tActivity >= dimMs) applyDim(true);
    if (now32 - tActivity >= slpMs) enterSleep();
  }
}
