/* [TYPE: IMPLEMENTATION]
font.cpp | POSFont — text rendering via FRAM-backed glyph data

Coordinate system
─────────────────
  Matches display.c exactly: (0,0) = top-left, x right, y down.
  Glyph positions are computed in absolute pixel coordinates and
  passed directly to draw_pixel() as absolute (x, y) — no center
  conversion is performed here.

  Characters are 5×7 px with 1-px gaps: CHAR_STEP=6, ROW_STEP=8.
  Row 1 starts at y=0, row 8 ends at y=63 — all 64 rows used.
  Column 1 starts at x=0; up to 21 characters per row fit in 128 px.
*/
#include "font.h"
#include "fram.h"
#include <string.h>

/* ── Font geometry ───────────────────────────────────────────────── */

#define CHAR_W    5   /* glyph pixel width                */
#define CHAR_H    7   /* glyph pixel height               */
#define CHAR_STEP 6   /* CHAR_W + 1 px inter-char gap     */
#define ROW_STEP  8   /* CHAR_H + 1 px inter-row gap      */

#define DISPLAY_W 128

/* ── Per-row write cursors ─────────────────────────────────────── */
/* cur[0] = column count for row 1, cur[1] = row 2, etc.          */

static uint8_t cur[FONT_MAX_ROWS];

/* ── Character → FRAM glyph index ──────────────────────────────── */
/*
 *  Glyph order in the System partition (matches font_pgm[] in fram.c):
 *    [0–9]  [A–Z]  [a–z]
 *    ! ? ' " # ^ = - _ | / \ : ; [ ] ( ) < >  . ,  (space)
 */
static int16_t char_index(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
  if (c >= 'a' && c <= 'z') return 36 + (c - 'a');
  switch (c) {
    case '!':  return 62;
    case '?':  return 63;
    case '\'': return 64;
    case '"':  return 65;
    case '#':  return 66;
    case '^':  return 67;
    case '=':  return 68;
    case '-':  return 69;
    case '_':  return 70;
    case '|':  return 71;
    case '/':  return 72;
    case '\\': return 73;
    case ':':  return 74;
    case ';':  return 75;
    case '[':  return 76;
    case ']':  return 77;
    case '(':  return 78;
    case ')':  return 79;
    case '<':  return 80;
    case '>':  return 81;
    case '.':  return 82;
    case ',':  return 83;
    case ' ':  return 84;
    default:   return -1;  /* unknown char — advance cursor, skip pixels */
  }
}

/* ── External symbol (C linkage) from display.c ─────────────────── *
 *  draw_pixel now accepts absolute (x, y) coordinates directly.      *
 * ─────────────────────────────────────────────────────────────────── */
extern "C" void draw_pixel(uint8_t x, uint8_t y, bool inv);

/* ── POSFont::reset ─────────────────────────────────────────────── */

void POSFont::reset(uint8_t row) {
  if (row == 0)
    memset(cur, 0, sizeof(cur));
  else if (row <= FONT_MAX_ROWS)
    cur[row - 1] = 0;
}

/* ── POSFont::write ──────────────────────────────────────────────── *
 *                                                                     *
 *  Reads glyph data from the FRAM System partition and plots each     *
 *  lit pixel via draw_pixel() in absolute screen coordinates.         *
 *                                                                     *
 *  Row 1 → y origin = 0                                              *
 *  Row N → y origin = (N-1) * ROW_STEP                               *
 *  Column position → x origin = cur[ri] * CHAR_STEP                  *
 *                                                                     *
 *  No center conversion is required or performed.                     *
 * ─────────────────────────────────────────────────────────────────── */
void POSFont::write(const char *msg, uint8_t row) {
  if (!msg || row < 1 || row > FONT_MAX_ROWS) return;

  const uint8_t  ri  = row - 1;
  const uint8_t  y0  = (uint8_t)((uint16_t)ri * ROW_STEP);  /* abs y origin */

  for (uint8_t ci = 0; msg[ci] != '\0'; ci++) {

    const uint8_t x0 = (uint8_t)((uint16_t)cur[ri] * CHAR_STEP); /* abs x origin */
    if ((uint16_t)x0 + CHAR_W > DISPLAY_W) break;  /* past right edge — stop */

    const int16_t fi = char_index(msg[ci]);
    cur[ri]++;
    if (fi < 0) continue;  /* unknown char — advance cursor, skip pixels */

    /* Read all 5 columns from FRAM in one sequential transaction */
    uint8_t glyph[CHAR_W];
    fram_read_buf(FRAM_FONT_BASE + (uint16_t)fi * FRAM_FONT_GLYPH_SZ,
                  glyph, CHAR_W);

    for (uint8_t col = 0; col < CHAR_W; col++) {
      uint8_t col_data = glyph[col];
      for (uint8_t bit = 0; bit < CHAR_H; bit++) {
        if (col_data & (1u << bit)) {
          /* Absolute pixel coordinates — no centering needed */
          draw_pixel((uint8_t)(x0 + col), (uint8_t)(y0 + bit), false);
        }
      }
    }
  }
}
