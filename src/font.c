/* SPDX-License-Identifier: MIT */
#include "font_data.h"
#include "internal.h"
#include <math.h>
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

/* Controller glyphs are drawn at run time into the atlas cells that the font leaves free. */
enum { SUPER = 4 };
static spo_glyph glyphs[4][SPO_GLYPH_COUNT];
static int glyphs_ready[4];
static const char *const keycap_text[] = {NULL, NULL, NULL, NULL, "L1", "R1", "MENU"};
static void caps_rows(uint32_t index, int *top, int *bottom) {
    const char *probe = "MENULR1";
    const int bytes = faces[index].stride * faces[index].info.ch;
    int lo = faces[index].info.ch, hi = 0;
    for (; *probe; probe++) {
        int line, byte;
        for (line = 0; line < faces[index].info.ch; line++)
            for (byte = 0; byte < faces[index].stride; byte++)
                if (faces[index].data[(*probe - 32) * bytes + line * faces[index].stride + byte]) {
                    if (line < lo)
                        lo = line;
                    if (line > hi)
                        hi = line;
                }
    }
    *top = lo;
    *bottom = hi;
}
static int text_width(uint32_t index, const char *label) {
    return (int)strlen(label) * faces[index].info.cw;
}
static void glyph_layout(uint32_t index) {
    const spo_font_info *f = &faces[index].info;
    int top, bottom, pad = f->ch / 8 + 1, yoff, height, i;
    int rows = (95 + f->cols - 1) / f->cols, used = 95 - (rows - 1) * f->cols;
    /* Free space: the unused cells of the last glyph row, then the rows under the grid. */
    int cursor_x[2] = {used * f->cw, 0}, cursor_y[2] = {(rows - 1) * f->ch, f->grid_h};
    int limit_y[2] = {rows * f->ch, SPO_ATLAS_SOLID_Y - 1};
    static const int order[SPO_GLYPH_COUNT] = {SPO_GLYPH_MENU,    SPO_GLYPH_L1,     SPO_GLYPH_R1,
                                               SPO_GLYPH_DPAD_UD, SPO_GLYPH_DPAD_LR, SPO_GLYPH_SOUTH,
                                               SPO_GLYPH_EAST};
    caps_rows(index, &top, &bottom);
    yoff = top - pad > 0 ? top - pad : 0;
    height = bottom + 1 + pad - yoff;
    if (height > 28)
        height = 28;
    for (i = 0; i < SPO_GLYPH_COUNT; i++) {
        spo_glyph *g = &glyphs[index][order[i]];
        int w = keycap_text[order[i]] ? text_width(index, keycap_text[order[i]]) + f->cw : height;
        int r;
        g->w = w;
        g->h = height;
        g->yoff = yoff;
        r = cursor_x[0] + w <= SPO_ATLAS && cursor_y[0] + height <= limit_y[0] ? 0 : 1;
        if (r && cursor_x[1] + w > SPO_ATLAS) {
            cursor_x[1] = 0;
            cursor_y[1] += height;
        }
        g->x = cursor_x[r];
        g->y = cursor_y[r];
        cursor_x[r] += w;
    }
    glyphs_ready[index] = 1;
}
const spo_glyph *spo_glyph_get(uint32_t size, int id) {
    uint32_t index = size < 1 ? 0 : size > 4 ? 3 : size - 1;
    if (!glyphs_ready[index])
        glyph_layout(index);
    return &glyphs[index][id < 0 || id >= SPO_GLYPH_COUNT ? 0 : id];
}
static int font_bit(uint32_t index, char code, int x, int y) {
    const spo_font_info *f = &faces[index].info;
    const int bytes = faces[index].stride * f->ch, cell = x / f->cw, bit = x % f->cw;
    if (code < 32 || code > 126 || y < 0 || y >= f->ch || cell < 0 || x < 0)
        return 0;
    return !!(faces[index].data[(code - 32) * bytes + y * faces[index].stride + bit / 8] &
              (0x80u >> (bit % 8)));
}
static int inside_cross(double x, double y, double size) {
    double c = size / 2, a = size * 0.22, dx = fabs(x - c), dy = fabs(y - c);
    return (dx <= a && dy <= c) || (dy <= a && dx <= c);
}
static int inside_disc(double x, double y, double cx, double cy, double r) {
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}
static int inside_keycap(double x, double y, double w, double h) {
    double r = h * 0.28, dx = fabs(x - w / 2) - (w / 2 - r), dy = fabs(y - h / 2) - (h / 2 - r);
    if (dx <= 0 || dy <= 0)
        return fabs(x - w / 2) <= w / 2 && fabs(y - h / 2) <= h / 2;
    return dx * dx + dy * dy <= r * r;
}
static int inside_triangle(double x, double y, double ax, double ay, double bx, double by,
                           double cx, double cy) {
    double d1 = (x - bx) * (ay - by) - (ax - bx) * (y - by),
           d2 = (x - cx) * (by - cy) - (bx - cx) * (y - cy),
           d3 = (x - ax) * (cy - ay) - (cx - ax) * (y - ay);
    int negative = d1 < 0 || d2 < 0 || d3 < 0, positive = d1 > 0 || d2 > 0 || d3 > 0;
    return !(negative && positive);
}
/* Arrows inside the D-pad arms: 0 up, 1 down, 2 left, 3 right. */
static int inside_arrow(double x, double y, double size, double stroke, int arm) {
    double c = size / 2, a = size * 0.22, tip = c - stroke - 0.5, base = a - 0.2,
           half = a - stroke - 0.2;
    if (half < 0.8)
        half = 0.8;
    if (arm == 0)
        return inside_triangle(x, y, c, c - tip, c - half, c - base, c + half, c - base);
    if (arm == 1)
        return inside_triangle(x, y, c, c + tip, c - half, c + base, c + half, c + base);
    if (arm == 2)
        return inside_triangle(x, y, c - tip, c, c - base, c - half, c - base, c + half);
    return inside_triangle(x, y, c + tip, c, c + base, c - half, c + base, c + half);
}
/* Shape membership of one sample point: 1 for the outline, 2 for a filled part. */
static int shape(int id, double x, double y, double w, double h, double stroke) {
    static const double offsets[8][2] = {{1, 0}, {-1, 0}, {0, 1},  {0, -1},
                                         {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    int k, inside = 0, boundary = 0;
    double d = h * 0.32, r = h * 0.19, c = h / 2;
    if (id == SPO_GLYPH_DPAD_UD || id == SPO_GLYPH_DPAD_LR) {
        int a = id == SPO_GLYPH_DPAD_UD ? 0 : 2;
        if (inside_arrow(x, y, h, stroke, a) || inside_arrow(x, y, h, stroke, a + 1))
            return 2;
        inside = inside_cross(x, y, h);
        for (k = 0; inside && k < 8; k++)
            boundary |= !inside_cross(x + offsets[k][0] * stroke, y + offsets[k][1] * stroke, h);
        return inside && boundary;
    }
    if (id <= SPO_GLYPH_EAST) {
        double cx[4] = {c, c + d, c, c - d}, cy[4] = {c - d, c, c + d, c};
        int active = id == SPO_GLYPH_SOUTH ? 2 : 1;
        for (k = 0; k < 4; k++) {
            if (!inside_disc(x, y, cx[k], cy[k], r))
                continue;
            if (k == active)
                return 2;
            if (!inside_disc(x, y, cx[k], cy[k], r - stroke))
                return 1;
        }
        return 0;
    }
    inside = inside_keycap(x, y, w, h);
    for (k = 0; inside && k < 8; k++)
        boundary |= !inside_keycap(x + offsets[k][0] * stroke, y + offsets[k][1] * stroke, w, h);
    return inside && boundary;
}
void spo_glyph_raster(uint32_t size, int id, uint8_t *rgba) {
    const uint32_t index = size < 1 ? 0 : size > 4 ? 3 : size - 1;
    const spo_glyph *g = spo_glyph_get(size, id);
    const char *label = keycap_text[id];
    double stroke = g->h / 10 > 1 ? g->h / 10 : 1;
    int x, y, sx, sy;
    for (y = 0; y < g->h; y++)
        for (x = 0; x < g->w; x++) {
            int covered = 0, mask = 0;
            uint8_t *out = rgba + ((size_t)y * (size_t)g->w + (size_t)x) * 4;
            for (sy = 0; sy < SUPER; sy++)
                for (sx = 0; sx < SUPER; sx++)
                    covered += !!shape(id, x + (sx + 0.5) / SUPER, y + (sy + 0.5) / SUPER, g->w,
                                       g->h, stroke);
            if (label) {
                int tx = x - faces[index].info.cw / 2, ty = y + g->yoff;
                mask = tx >= 0 && tx / faces[index].info.cw < (int)strlen(label) &&
                       font_bit(index, label[tx / faces[index].info.cw], tx, ty);
            }
            memset(out, 255, 3);
            out[3] = (uint8_t)(mask ? 255 : covered * 255 / (SUPER * SUPER));
        }
}
