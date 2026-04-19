/* [TYPE: HEADER]
display.h | SSD1306 display driver — C API and C++ wrapper for PlanktOS

Coordinate system
─────────────────
  All drawing functions operate in absolute screen coordinates.
  (0, 0) = top-left corner.  x increases rightward.  y increases downward.
  Valid range: x ∈ [0, DISPLAY_W-1],  y ∈ [0, DISPLAY_H-1].
  No implicit centering is performed inside any core primitive.

  For callers that think in center-relative terms, the inline helpers
  cx() and cy() are provided at the bottom of this file.  They must NOT
  be called from within core rendering functions.
*/
#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

/* ── Screen geometry ────────────────────────────────────────────── */

#define DISPLAY_W 128
#define DISPLAY_H 64

/* ── C API ──────────────────────────────────────────────────────── */

#ifdef __cplusplus
extern "C" {
#endif

  void display_init(void);
  void clf(void); /* clear framebuffer only (no push to display)  */
  void cls(void); /* clear framebuffer and push to display        */
  void display_render(void);

  /* Core drawing primitives — all coordinates are absolute (0,0 = top-left) */
  void draw_pixel(uint8_t x, uint8_t y, bool inv);
  void draw_boot(uint8_t offsetX, uint8_t offsetY);
  void display_draw_bitmap(const uint8_t *bmp);
  void draw_line(uint8_t x0, uint8_t y0,
                 uint8_t x1, uint8_t y1, bool inv);
  void draw_hline(uint8_t x0, uint8_t x1, uint8_t y, bool inv);
  void draw_vline(uint8_t y0, uint8_t y1, uint8_t x, bool inv);
  void draw_rect(uint8_t x, uint8_t y,
                 uint8_t w, uint8_t h, bool inv, bool filled);
  void draw_circle(uint8_t x, uint8_t y, uint8_t r, bool inv, bool filled);
  void draw_triangle(uint8_t x1, uint8_t y1,
                     uint8_t x2, uint8_t y2,
                     uint8_t x3, uint8_t y3, bool inv);

#ifdef __cplusplus
}
#endif


/* ── C++ wrapper (Arduino / .ino only) ──────────────────────────── */

#ifdef __cplusplus

class SSD1306 {
public:
  void begin() {
    display_init();
  }
  void render() {
    display_render();
  }
  void clearScr() {
    cls();
  }
  void clearBuf() {
    clf();
  }

  /* All methods take absolute coordinates (0,0 = top-left). */

  void pixel(uint8_t x, uint8_t y, bool inv = false) {
    draw_pixel(x, y, inv);
  }
  /* draw the boot animation to the display */
  void bootanim(uint8_t offsetX, uint8_t offsetY) {
    draw_boot(offsetX, offsetY);
  }
  /* x, y = top-left corner of the rectangle */
  void rectangle(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                 bool inv = false, bool filled = false) {
    draw_rect(x, y, w, h, inv, filled);
  }
  /* x, y = centre of the circle */
  void circle(uint8_t x, uint8_t y, uint8_t r,
              bool inv = false, bool filled = false) {
    draw_circle(x, y, r, inv, filled);
  }
  void triangle(uint8_t x1, uint8_t y1,
                uint8_t x2, uint8_t y2,
                uint8_t x3, uint8_t y3, bool inv = false) {
    draw_triangle(x1, y1, x2, y2, x3, y3, inv);
  }
  /* Horizontal line from (x0, y) to (x1, y) */
  void hline(uint8_t x0, uint8_t x1, uint8_t y, bool inv = false) {
    draw_hline(x0, x1, y, inv);
  }
  /* Vertical line from (x, y0) to (x, y1) */
  void vline(uint8_t y0, uint8_t y1, uint8_t x, bool inv = false) {
    draw_vline(y0, y1, x, inv);
  }
  void line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,
            bool inv = false) {
    draw_line(x0, y0, x1, y1, inv);
  }
};

#endif /* __cplusplus */
#endif /* DISPLAY_DRIVER_H */
