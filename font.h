/* [TYPE: HEADER]
font.h | POSFont — text rendering via FRAM-backed glyph data
*/
#pragma once
#include <stdint.h>

#define FONT_MAX_ROWS    8   /* 64px / 8px per row  */
#define FONT_MAX_COLS   21   /* 128px / 6px per char */

class POSFont {
public:
    /* Write msg to the given row (1-indexed).
     * Always appends after the last written character on that row. */
    static void write(const char *msg, uint8_t row);

    /* Reset the write cursor.
     * row = 0  → reset all rows
     * row > 0  → reset only that row                              */
    static void reset(uint8_t row = 0);
};
