/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "font_data.h"
#include "internal.h"
#include <string.h>
void spo_font_atlas(uint8_t *rgba) {
    int glyph, x, y;
    memset(rgba, 0, 128 * 64 * 4);
    for (glyph = 0; glyph < 95; glyph++) {
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 5; x++) {
                size_t at = (size_t)(((glyph / 16) * 9 + y) * 128 + (glyph % 16) * 6 + x) * 4;
                rgba[at] = rgba[at + 1] = rgba[at + 2] = 255;
                rgba[at + 3] = (spo_font[glyph][y] & (0x80u >> x)) ? 255 : 0;
            }
        }
    }
    memset(rgba + (128 * 64 - 1) * 4, 255, 4);
}
