/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "font_data.h"
#include "internal.h"
#include <string.h>

static const struct {
    spo_font_info info;
    const unsigned char *data;
    int stride;
} faces[4] = {
    {{6, 12, 42, 36}, &spo_font_6x12[0][0], 1},
    {{8, 16, 31, 64}, &spo_font_8x16[0][0], 1},
    {{12, 24, 21, 120}, &spo_font_12x24[0][0], 2},
    {{16, 32, 15, 224}, &spo_font_16x32[0][0], 2},
};
const spo_font_info *spo_font_get(uint32_t size) {
    return &faces[size < 1 ? 0 : size > 4 ? 3 : size - 1].info;
}
/* Atlas rows [y0, y0 + rows) of the glyph grid for one size, as RGBA. */
void spo_font_band(uint32_t size, uint32_t y0, uint32_t rows, uint8_t *rgba) {
    const uint32_t index = size < 1 ? 0 : size > 4 ? 3 : size - 1;
    const spo_font_info *f = &faces[index].info;
    const int bytes = faces[index].stride * f->ch;
    uint32_t y, x;
    memset(rgba, 0, (size_t)rows * SPO_ATLAS * 4);
    for (y = 0; y < rows; y++) {
        uint32_t row = (y0 + y) / (uint32_t)f->ch, line = (y0 + y) % (uint32_t)f->ch;
        for (x = 0; x < (uint32_t)(f->cols * f->cw); x++) {
            uint32_t glyph = row * (uint32_t)f->cols + x / (uint32_t)f->cw,
                     bit = x % (uint32_t)f->cw;
            uint8_t *out = rgba + ((size_t)y * SPO_ATLAS + x) * 4;
            if (glyph >= 95)
                break;
            if (faces[index].data[glyph * (uint32_t)bytes + line * (uint32_t)faces[index].stride +
                                  bit / 8] &
                (0x80u >> (bit % 8)))
                memset(out, 255, 4);
        }
    }
}
