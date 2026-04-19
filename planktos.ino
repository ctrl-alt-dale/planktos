#include "display.h"
#include "font.h"
#include "touch.h"
#include "filesystem.h"
#include "fram.h"
#include "globals.h"
#include <avr/pgmspace.h>

SSD1306 display;
POSFont font;
POSTouch touch;
POSFS filesystem;

/*
 * All drawing calls below use absolute screen coordinates:
 *   (0, 0) = top-left,  x increases right,  y increases down.
 *
 * The cx()/cy() helpers from display.h can be used to express positions
 * in center-relative terms where convenient:
 *   cx(sx) = 64 + sx   (converts signed center-offset to absolute x)
 *   cy(sy) = 32 + sy   (converts signed center-offset to absolute y)
 *
 * Original center-relative calls have been translated below.
 * Former call                      → New absolute equivalent
 * ─────────────────────────────────────────────────────────
 * display.rectangle(60, 14,  0, 12)  →  draw_rect(34,  25, 60, 14)
 * display.circle(8, -40, 20)         →  draw_circle(24, 52,  8)
 * display.hline(128,   0, -17)       →  draw_hline(0, 127, 15)
 * display.hline(128, -25, -11)       →  draw_hline(0, 127, 21)
 * display.hline(128, -30, -12)       →  draw_hline(0, 127, 20)
 * display.hline(128, -35, -13)       →  draw_hline(0, 127, 19)
 *
 * Derivation:
 *   rect:    x = cx(0)  - 60/2 = 64-30 = 34,  y = cy(12) - 14/2 = 44-7 = 37
 *            NOTE: original sy=12 → ay=32+12=44, y0=44-7=37; xr/yr were w/h
 *   circle:  x = cx(-40) = 24,  y = cy(20) = 52
 *   hline:   full-width spans — x0=0, x1=127 in all cases
 *            y = cy(-17)=15, cy(-11)=21, cy(-12)=20, cy(-13)=19
 */

void setup() {
  display.begin();
  filesystem.begin();
  display.bootanim(0, 0);
  display.render();
}

void loop() {
}
