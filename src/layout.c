/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const uint8_t palette[SPO_TONE_COUNT][3] = {
    {240, 242, 244}, /* value */
    {152, 162, 172}, /* label */
    {124, 132, 140}, /* dim */
    {98, 214, 126},  /* good */
    {248, 200, 64},  /* warning */
    {255, 90, 82},   /* bad */
    {126, 197, 168}, /* accent */
};
static const uint8_t panel_color[3] = {15, 19, 23}, highlight[3] = {40, 88, 108},
                     black[3] = {0, 0, 0}, rule[3] = {70, 80, 90};
static const spo_font_info *face;

const char *spo_item_name(uint32_t i) {
    static const char *names[] = {"FPS",
                                  "Frame time",
                                  "Frame-time graph",
                                  "1% low (512 samples)",
                                  "System CPU",
                                  "Process CPU",
                                  "GPU load",
                                  "CPU0 clock",
                                  "GPU clock",
                                  "SoC temperature",
                                  "Battery",
                                  "Process RSS",
                                  "Available memory",
                                  "Overlay cost",
                                  "Charging"};
    return i < SPO_ITEM_COUNT ? names[i] : "?";
}
static int quad(int x, int y, int w, int h, float u, float v, float uw, float vh,
                const uint8_t rgb[3], uint8_t alpha) {
    spo_vertex a, b, c, d;
    spo_vertex *out;
    if (w <= 0 || h <= 0)
        return 1;
    if (spo->mesh.count + 6 > SPO_QUADS * 6)
        return 0;
    a = (spo_vertex){(float)x, (float)y, u, v, {rgb[0], rgb[1], rgb[2], alpha}};
    b = a;
    b.y += (float)h;
    b.v += vh;
    c = b;
    c.x += (float)w;
    c.u += uw;
    d = a;
    d.x += (float)w;
    d.u += uw;
    out = &spo->mesh.vertices[spo->mesh.count];
    out[0] = a;
    out[1] = b;
    out[2] = c;
    out[3] = a;
    out[4] = c;
    out[5] = d;
    spo->mesh.count += 6;
    return 1;
}
static int rectangle(int x, int y, int w, int h, const uint8_t rgb[3], uint8_t alpha) {
    return quad(x, y, w, h, (float)SPO_ATLAS_SOLID_X / SPO_ATLAS,
                (float)SPO_ATLAS_SOLID_Y / SPO_ATLAS, 0, 0, rgb, alpha);
}
static int text(int x, int y, const char *value, const uint8_t rgb[3], uint8_t alpha) {
    const unsigned char *p = (const unsigned char *)value;
    while (*p) {
        unsigned char code = *p++;
        if (code >= 128) {
            while ((*p & 0xc0) == 0x80)
                p++;
            code = '?';
        }
        if (code < 32 || code > 126)
            code = '?';
        if (code != ' ') {
            unsigned i = code - 32;
            if (!quad(x, y, face->cw, face->ch,
                      (float)((int)(i % (unsigned)face->cols) * face->cw) / SPO_ATLAS,
                      (float)((int)(i / (unsigned)face->cols) * face->ch) / SPO_ATLAS,
                      (float)face->cw / SPO_ATLAS, (float)face->ch / SPO_ATLAS, rgb, alpha))
                return 0;
        }
        x += face->cw;
    }
    return 1;
}
static int text_right(int right, int y, const char *value, const uint8_t rgb[3], uint8_t alpha) {
    return text(right - (int)strlen(value) * face->cw, y, value, rgb, alpha);
}
static int glyph(int x, int y, int id, const uint8_t rgb[3], uint8_t alpha) {
    const spo_glyph *g = spo_glyph_get(spo->mesh.font, id);
    return quad(x, y + g->yoff, g->w, g->h, (float)g->x / SPO_ATLAS, (float)g->y / SPO_ATLAS,
                (float)g->w / SPO_ATLAS, (float)g->h / SPO_ATLAS, rgb, alpha);
}
static int glyph_width(int id) {
    return spo_glyph_get(spo->mesh.font, id)->w;
}
static void use_font(uint32_t size) {
    face = spo_font_get(size);
    spo->mesh.font = size < 1 ? 1 : size > 4 ? 4 : size;
}
spo_rect spo_anchor(uint32_t width, uint32_t height, int w, int h, int anchor) {
    int x = 8, y = 8;
    if (anchor % 3 == 1)
        x = ((int)width - w) / 2;
    else if (anchor % 3 == 2)
        x = (int)width - 8 - w;
    if (anchor / 3 == 1)
        y = ((int)height - h) / 2;
    else if (anchor / 3 == 2)
        y = (int)height - 8 - h;
    return (spo_rect){x, y, w, h};
}

/* Rates within 2% of the target count as on target, so a 59.94 Hz game is green at 60. */
static int tone_rate(double rate) {
    double ratio = spo->target_fps > 0 ? rate / spo->target_fps : 1;
    return ratio >= 0.98 ? SPO_TONE_GOOD : ratio >= 0.9 ? SPO_TONE_WARN : SPO_TONE_BAD;
}
static int tone_load(double percent) {
    return percent >= 95 ? SPO_TONE_BAD : percent >= 85 ? SPO_TONE_WARN : SPO_TONE_VALUE;
}
static int tone_temperature(double celsius) {
    return celsius >= 80 ? SPO_TONE_BAD : celsius >= 70 ? SPO_TONE_WARN : SPO_TONE_VALUE;
}
static int tone_battery(double percent) {
    return percent <= 10 ? SPO_TONE_BAD : percent <= 20 ? SPO_TONE_WARN : SPO_TONE_VALUE;
}
static void update_target(void) {
    uint64_t sorted[120];
    uint32_t i, count = spo->history_count < 120 ? spo->history_count : 120;
    double rate = spo->fps;
    static const double targets[] = {30, 60, 120};
    if (spo->host_target) {
        spo->target_fps = (double)spo->host_target;
        return;
    }
    if (count >= 8) {
        for (i = 0; i < count; i++) {
            uint64_t value =
                spo->intervals[(spo->history_next + SPO_HISTORY - count + i) % SPO_HISTORY];
            uint32_t j = i;
            while (j && sorted[j - 1] > value) {
                sorted[j] = sorted[j - 1];
                j--;
            }
            sorted[j] = value;
        }
        if (sorted[count / 2])
            rate = 1e9 / (double)sorted[count / 2];
    }
    spo->target_fps = 120;
    if (rate <= 0)
        spo->target_fps = 60;
    else
        for (i = 0; i < 3; i++)
            if (rate <= targets[i] * 1.02) {
                spo->target_fps = targets[i];
                break;
            }
}

static spo_row *new_row(const char *label) {
    static spo_row sink;
    spo_row *r = spo->row_count < 40 ? &spo->rows[spo->row_count++] : &sink;
    memset(r, 0, sizeof(*r));
    snprintf(r->label, sizeof(r->label), "%s", label);
    return r;
}
static void put(spo_row *r, int cell, int tone, int reserve, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(r->cell[cell], sizeof(r->cell[cell]), format, args);
    va_end(args);
    r->tone[cell] = (uint8_t)tone;
    r->reserve[cell] = (uint8_t)reserve;
}
static int visible(int id) {
    return (spo->overlay.items & SPO_BIT(id)) && spo->metrics[id].valid;
}
/* A value that is on shows a placeholder until the sampler delivers it. */
static int wanted(int id) {
    return (spo->overlay.items & SPO_BIT(id)) &&
           (spo->metrics[id].valid || (spo->available_items & SPO_BIT(id)));
}
static void formatted(uint64_t now) {
    spo_row previous[40];
    uint32_t i, previous_count = spo->row_count, previous_header = spo->header_rows;
    spo_row *r;
    double v;
    int fps = !!(spo->overlay.items & SPO_BIT(SPO_FPS)),
        time = !!(spo->overlay.items & SPO_BIT(SPO_FRAME_TIME)),
        low = !!(spo->overlay.items & SPO_BIT(SPO_LOW_1));
    memcpy(previous, spo->rows, sizeof(previous));
    spo->row_count = 0;
    update_target();
    if (fps || time || low) {
        r = new_row("FPS");
        if (fps && spo->fps > 0)
            put(r, 0, tone_rate(spo->fps), 5, "%.1f", spo->fps);
        else if (fps)
            put(r, 0, SPO_TONE_VALUE, 5, "--");
        if (time && spo->frame_ms > 0)
            put(r, 1, spo->fps > 0 ? tone_rate(spo->fps) : SPO_TONE_VALUE, 8, "%.1f ms",
                spo->frame_ms);
        else if (time)
            put(r, 1, SPO_TONE_VALUE, 8, "-- ms");
        if (low && spo->low_1 > 0)
            put(r, 2, tone_rate(spo->low_1), 8, "%.1f low", spo->low_1);
        else if (low)
            put(r, 2, SPO_TONE_VALUE, 8, "-- low");
    }
    spo->header_rows = spo->row_count;
    if (wanted(SPO_SYSTEM_CPU) || wanted(SPO_CPU_CLOCK) || wanted(SPO_TEMPERATURE)) {
        r = new_row("CPU");
        if (visible(SPO_SYSTEM_CPU)) {
            v = spo->metrics[SPO_SYSTEM_CPU].value;
            put(r, 0, tone_load(v), 4, "%.0f%%", v);
        } else if (wanted(SPO_SYSTEM_CPU))
            put(r, 0, SPO_TONE_VALUE, 4, "--");
        if (visible(SPO_CPU_CLOCK))
            put(r, 1, SPO_TONE_VALUE, 8, "%.0f MHz", spo->metrics[SPO_CPU_CLOCK].value);
        else if (wanted(SPO_CPU_CLOCK))
            put(r, 1, SPO_TONE_VALUE, 8, "--");
        if (visible(SPO_TEMPERATURE)) {
            v = spo->metrics[SPO_TEMPERATURE].value;
            put(r, 2, tone_temperature(v), 4, "%.0f C", v);
        } else if (wanted(SPO_TEMPERATURE))
            put(r, 2, SPO_TONE_VALUE, 4, "--");
    }
    if (wanted(SPO_GPU_LOAD) || wanted(SPO_GPU_CLOCK)) {
        r = new_row("GPU");
        if (visible(SPO_GPU_LOAD)) {
            v = spo->metrics[SPO_GPU_LOAD].value;
            put(r, 0, tone_load(v), 4, "%.0f%%", v);
        } else if (wanted(SPO_GPU_LOAD))
            put(r, 0, SPO_TONE_VALUE, 4, "--");
        if (visible(SPO_GPU_CLOCK))
            put(r, 1, SPO_TONE_VALUE, 8, "%.0f MHz", spo->metrics[SPO_GPU_CLOCK].value);
        else if (wanted(SPO_GPU_CLOCK))
            put(r, 1, SPO_TONE_VALUE, 8, "--");
    }
    if (wanted(SPO_PROCESS_CPU) || wanted(SPO_RSS)) {
        r = new_row("GAME");
        if (visible(SPO_PROCESS_CPU))
            put(r, 0, SPO_TONE_VALUE, 4, "%.0f%%", spo->metrics[SPO_PROCESS_CPU].value);
        else if (wanted(SPO_PROCESS_CPU))
            put(r, 0, SPO_TONE_VALUE, 4, "--");
        if (visible(SPO_RSS))
            put(r, 1, SPO_TONE_VALUE, 8, "%.0f MB", spo->metrics[SPO_RSS].value);
        else if (wanted(SPO_RSS))
            put(r, 1, SPO_TONE_VALUE, 8, "--");
    }
    if (wanted(SPO_MEM_AVAILABLE)) {
        r = new_row("RAM");
        r->span = 2;
        if (visible(SPO_MEM_AVAILABLE))
            put(r, 0, SPO_TONE_VALUE, 12, "%.0f MB free", spo->metrics[SPO_MEM_AVAILABLE].value);
        else
            put(r, 0, SPO_TONE_VALUE, 12, "--");
    }
    if (wanted(SPO_BATTERY) || wanted(SPO_CHARGING)) {
        r = new_row("BAT");
        if (visible(SPO_BATTERY)) {
            v = spo->metrics[SPO_BATTERY].value;
            put(r, 0, tone_battery(v), 4, "%.0f%%", v);
        } else if (wanted(SPO_BATTERY))
            put(r, 0, SPO_TONE_VALUE, 4, "--");
        if (visible(SPO_CHARGING))
            put(r, 1, spo->battery_charging ? SPO_TONE_GOOD : SPO_TONE_DIM, 3, "CHG");
        else if (wanted(SPO_CHARGING))
            put(r, 1, SPO_TONE_VALUE, 3, "--");
    }
    for (i = 0; i < spo->counter_count; i++)
        if ((spo->counter_mask & SPO_BIT(i)) && spo->counters[i].valid) {
            spo_counter *c = &spo->counters[i];
            char number[64];
            snprintf(number, sizeof(number), "%.*f", (int)c->decimals, c->value);
            if (strlen(number) > 14)
                snprintf(number, sizeof(number), "%.2g", c->value);
            r = new_row(c->label);
            r->span = 3;
            if (strlen(number) + (c->unit[0] ? strlen(c->unit) + 1 : 0) > 23)
                put(r, 0, SPO_TONE_VALUE, 0, "overflow");
            else
                put(r, 0, SPO_TONE_VALUE, 0, "%s%s%s", number, c->unit[0] ? " " : "", c->unit);
        }
    for (i = 0; i < spo->indicator_count; i++)
        if ((spo->indicator_mask & SPO_BIT(i)) && spo->indicators[i].valid) {
            int on = !!spo->indicators[i].value;
            r = new_row(spo->indicators[i].label);
            r->span = 3;
            put(r, 0, on ? SPO_TONE_VALUE : SPO_TONE_DIM, 0, on ? "ON" : "OFF");
        }
    if (spo->overlay.items & SPO_BIT(SPO_SELF_COST)) {
        r = new_row("OVERLAY");
        r->span = 2;
        if (spo->sampling_stats.sample_frames)
            put(r, 0, SPO_TONE_DIM, 7, "%.0f us", spo->sampling_stats.total_cpu_us_per_frame);
        else if (spo->stats.sample_frames)
            put(r, 0, SPO_TONE_DIM, 7, "%.0f us", spo->stats.cpu_us_mean);
        else
            put(r, 0, SPO_TONE_DIM, 7, "-- us");
    }
    if (!spo->formatted_at || previous_count != spo->row_count ||
        previous_header != spo->header_rows ||
        memcmp(previous, spo->rows, spo->row_count * sizeof(spo->rows[0])) ||
        memcmp(&spo->formatted_settings, &spo->overlay, sizeof(spo->overlay)))
        spo->formatted_at = now;
    spo->format_checked_at = now;
    spo->formatted_settings = spo->overlay;
    spo->formatted_counters = spo->counter_mask;
    spo->formatted_indicators = spo->indicator_mask;
}

typedef struct {
    int size, cw, ch, px, py, gap, gw, gh, label_chars, start[3], width[3], edge[4], w, h;
} plan;
static void column_starts(plan *p) {
    int k, chars = p->label_chars;
    for (k = 0; k < 3; k++) {
        p->start[k] = 0;
        if (p->width[k]) {
            p->start[k] = chars + 1;
            chars += 1 + p->width[k];
        }
    }
}
/* Right edge, in characters, of the last used column among the first n. */
static int span_edge(const plan *p, int n) {
    int k;
    for (k = n - 1; k >= 0; k--)
        if (p->width[k])
            return p->start[k] + p->width[k];
    return -1;
}
static void measure(plan *p, int size, int graph) {
    const spo_font_info *f = spo_font_get((uint32_t)size);
    uint32_t i;
    int k, chars;
    memset(p, 0, sizeof(*p));
    p->size = size;
    p->cw = f->cw;
    p->ch = f->ch;
    p->px = f->cw / 2;
    p->py = f->ch / 4;
    p->gap = f->ch / 8 > 1 ? f->ch / 8 : 1;
    p->gw = 120 * size;
    p->gh = f->ch * 7 / 4;
    for (i = 0; i < spo->row_count; i++) {
        int n = (int)strlen(spo->rows[i].label);
        if (n > p->label_chars)
            p->label_chars = n;
        for (k = spo->rows[i].span ? 1 : 0; k < 3; k++) {
            int need = (int)strlen(spo->rows[i].cell[k]);
            if (spo->rows[i].reserve[k] > need)
                need = spo->rows[i].reserve[k];
            if (need > p->width[k])
                p->width[k] = need;
        }
    }
    column_starts(p);
    for (i = 0; i < spo->row_count; i++) {
        const spo_row *r = &spo->rows[i];
        int need = (int)strlen(r->cell[0]), edge;
        if (!r->span)
            continue;
        if (r->reserve[0] > need)
            need = r->reserve[0];
        edge = span_edge(p, r->span);
        if (edge < 0) {
            p->width[r->span - 1] = need;
        } else if (need > edge - (p->label_chars + 1)) {
            for (k = r->span - 1; k > 0 && !p->width[k]; k--)
                ;
            p->width[k] += need - (edge - (p->label_chars + 1));
        }
        column_starts(p);
    }
    for (k = 1; k <= 3; k++)
        p->edge[k] = span_edge(p, k);
    chars = p->label_chars;
    for (k = 0; k < 3; k++)
        if (p->width[k])
            chars = p->start[k] + p->width[k];
    p->w = chars * p->cw;
    if (graph && p->gw > p->w)
        p->w = p->gw;
    p->w += 2 * p->px;
    p->h = 2 * p->py + (int)spo->row_count * p->ch;
    if (graph)
        p->h += p->gh + (spo->header_rows ? p->gap : 0) +
                (spo->row_count > spo->header_rows ? p->gap : 0);
}
static int draw_row(const plan *p, const spo_row *r, int x, int y, uint8_t alpha) {
    int k;
    if (!text(x, y, r->label, palette[SPO_TONE_LABEL], alpha))
        return 0;
    if (r->span)
        return !r->cell[0][0] ||
               text_right(x + p->edge[r->span] * p->cw, y, r->cell[0], palette[r->tone[0]], alpha);
    for (k = 0; k < 3; k++)
        if (r->cell[k][0] && !text_right(x + (p->start[k] + p->width[k]) * p->cw, y, r->cell[k],
                                         palette[r->tone[k]], alpha))
            return 0;
    return 1;
}
static const uint8_t *bar_color(uint64_t ns) {
    return palette[ns ? tone_rate(1e9 / (double)ns) : SPO_TONE_GOOD];
}
typedef struct {
    int size, cw, ch, padx, pady, bar, rowh, cols, w, h;
} menu_plan;
static int menu_fit(menu_plan *m, int size, uint32_t width, uint32_t height) {
    const spo_font_info *f = spo_font_get((uint32_t)size);
    m->size = size;
    m->cw = f->cw;
    m->ch = f->ch;
    m->padx = f->cw * 2;
    m->pady = f->ch / 2;
    m->bar = f->ch + 6;
    m->rowh = f->ch + 4;
    m->cols = ((int)width - 16 - 2 * m->padx) / f->cw;
    if (m->cols > 72)
        m->cols = 72;
    m->w = m->cols * f->cw + 2 * m->padx;
    m->h = m->pady + m->bar + 10 + SPO_MENU_ROWS * m->rowh + 6 + 2 * f->ch + 6 + f->ch + m->pady;
    return m->cols >= 40 && m->h <= (int)height - 16;
}
static int wrapped(const menu_plan *m, int x, int y, const char *value, const uint8_t rgb[3],
                   int max_lines) {
    size_t offset = 0, length = strlen(value);
    int line = 0;
    do {
        char chunk[80];
        size_t n = length - offset > (size_t)m->cols ? (size_t)m->cols : length - offset;
        if (n > sizeof(chunk) - 1)
            n = sizeof(chunk) - 1;
        memcpy(chunk, value + offset, n);
        chunk[n] = 0;
        if (line < max_lines && !text(x, y + line * m->ch, chunk, rgb, 255))
            return 0;
        offset += n;
        line++;
    } while (offset < length);
    return 1;
}
/* Tabs that fit between the shoulder keys; the active tab stays visible. */
static int tab_bar(const menu_plan *m, int x, int y) {
    int count = spo_menu_tabs(), first = spo->tab, last = spo->tab, used, i, left, right;
    int gap = m->cw * 2,
        room = m->w - 2 * m->padx - glyph_width(SPO_GLYPH_L1) - glyph_width(SPO_GLYPH_R1) - 4 * m->cw;
    used = (int)strlen(spo_menu_tab_label(spo->tab)) * m->cw;
    for (;;) {
        int grew = 0;
        if (first > 0) {
            int need = gap + (int)strlen(spo_menu_tab_label(first - 1)) * m->cw;
            if (used + need <= room) {
                first--;
                used += need;
                grew = 1;
            }
        }
        if (last < count - 1) {
            int need = gap + (int)strlen(spo_menu_tab_label(last + 1)) * m->cw;
            if (used + need <= room) {
                last++;
                used += need;
                grew = 1;
            }
        }
        if (!grew)
            break;
    }
    left = x + m->padx;
    right = x + m->w - m->padx;
    if (!glyph(left, y + 3, SPO_GLYPH_L1, palette[SPO_TONE_LABEL], 255) ||
        !glyph(right - glyph_width(SPO_GLYPH_R1), y + 3, SPO_GLYPH_R1, palette[SPO_TONE_LABEL], 255))
        return 0;
    left = x + (m->w - used) / 2;
    for (i = first; i <= last; i++) {
        const char *label = spo_menu_tab_label(i);
        int width = (int)strlen(label) * m->cw;
        if (i == spo->tab &&
            !rectangle(left - m->cw, y - 1, width + 2 * m->cw, m->bar - 2, highlight, 255))
            return 0;
        if (!text(left, y + 3, label, palette[i == spo->tab ? SPO_TONE_VALUE : SPO_TONE_LABEL], 255))
            return 0;
        left += width + gap;
    }
    return 1;
}
static int menu_rows(const menu_plan *m, int x, int y) {
    int rows = spo_menu_rows(), i;
    for (i = spo->scroll; i < rows && i < spo->scroll + SPO_MENU_ROWS; i++) {
        spo_menu_row r;
        int ry = y + (i - spo->scroll) * m->rowh;
        spo_menu_describe(i, &r);
        if (r.kind == SPO_ROW_HEADING) {
            if (!text(x + m->padx, ry + 2, r.label, palette[SPO_TONE_DIM], 255))
                return 0;
            continue;
        }
        if (i == spo->cursor && !rectangle(x + 4, ry, m->w - 8, m->rowh, highlight, 255))
            return 0;
        if (!text(x + m->padx + (spo->tab == 1 ? m->cw * 2 : 0), ry + 2, r.label,
                  palette[r.enabled ? SPO_TONE_VALUE : SPO_TONE_DIM], 255) ||
            !text_right(x + m->w - m->padx, ry + 2, r.value, palette[r.tone], 255))
            return 0;
    }
    if (rows > SPO_MENU_ROWS) {
        int track = SPO_MENU_ROWS * m->rowh, thumb = track * SPO_MENU_ROWS / rows,
            at = (track - thumb) * spo->scroll / (rows - SPO_MENU_ROWS);
        if (thumb < 6)
            thumb = 6;
        if (!rectangle(x + m->w - 7, y, 3, track, rule, 255) ||
            !rectangle(x + m->w - 7, y + at, 3, thumb, palette[SPO_TONE_LABEL], 255))
            return 0;
    }
    return 1;
}
static const char *menu_error(void) {
    return spo->error == SPO_OK         ? NULL
           : spo->error == SPO_REJECTED ? "Setting rejected"
           : spo->error == SPO_IO       ? "Could not save settings"
                                        : "Settings error";
}
typedef struct {
    int glyph;
    const char *word;
} hint;
static int hints_width(const menu_plan *m, const hint *h, int count, int words) {
    int i, width = 0;
    for (i = 0; i < count; i++) {
        width += glyph_width(h[i].glyph);
        if (words)
            width += m->cw + (int)strlen(h[i].word) * m->cw;
        if (i)
            width += words ? 2 * m->cw : m->cw;
    }
    return width;
}
/* Words follow the glyphs when they fit; otherwise the glyphs stand alone. */
static int menu_footer(const menu_plan *m, int x, int y) {
    int kind = spo_menu_row_kind(spo->cursor), count = 0, i, at, words;
    hint hints[4];
    hints[count++] = (hint){SPO_GLYPH_DPAD_UD, "Move"};
    if (kind == SPO_ROW_CHOICE || kind == SPO_ROW_SLIDER)
        hints[count++] = (hint){SPO_GLYPH_DPAD_LR, "Change"};
    else if (kind == SPO_ROW_TOGGLE || kind == SPO_ROW_ACTION)
        hints[count++] = (hint){SPO_GLYPH_SOUTH, kind == SPO_ROW_ACTION ? "Run" : "Toggle"};
    hints[count++] = (hint){SPO_GLYPH_EAST, "Back"};
    hints[count++] = (hint){SPO_GLYPH_MENU, "Close"};
    words = hints_width(m, hints, count, 1) <= m->cols * m->cw;
    at = x + m->padx;
    for (i = 0; i < count; i++) {
        if (!glyph(at, y, hints[i].glyph, palette[SPO_TONE_LABEL], 255))
            return 0;
        at += glyph_width(hints[i].glyph);
        if (words) {
            at += m->cw;
            if (!text(at, y, hints[i].word, palette[SPO_TONE_DIM], 255))
                return 0;
            at += (int)strlen(hints[i].word) * m->cw;
        }
        at += words ? 2 * m->cw : m->cw;
    }
    return 1;
}
static spo_result menu(uint32_t width, uint32_t height) {
    int size = height >= 720 ? 3 : 2, x, y;
    menu_plan m;
    const char *error;
    while (!menu_fit(&m, size, width, height) && size > 1)
        size--;
    if (m.cols < 24 || m.h > (int)height - 16)
        return SPO_CAPACITY;
    use_font((uint32_t)size);
    x = ((int)width - m.w) / 2;
    y = ((int)height - m.h) / 2;
    spo->panel = (spo_rect){x, y, m.w, m.h};
    if (!rectangle(x, y, m.w, m.h, panel_color, 245))
        return SPO_CAPACITY;
    if (!tab_bar(&m, x, y + m.pady) ||
        !rectangle(x + m.padx, y + m.pady + m.bar + 3, m.w - 2 * m.padx, 1, rule, 255))
        return SPO_CAPACITY;
    y += m.pady + m.bar + 10;
    if (spo->tab == spo_menu_tabs() - 1) {
        const char *about[] = {"sbc-port-overlay " SPO_VERSION,
                               spo->name,
                               spo->version,
                               spo->renderer_name,
                               "MIT",
                               "Spleen / BSD-2-Clause",
                               NULL};
        int row = 0, i;
        for (i = 0; about[i]; i++) {
            int lines = ((int)strlen(about[i]) + m.cols - 1) / m.cols;
            if (lines < 1)
                lines = 1;
            if ((row + lines) * m.ch > SPO_MENU_ROWS * m.rowh)
                break;
            if (!wrapped(&m, x + m.padx, y + row * m.ch, about[i],
                         palette[i < 4 ? SPO_TONE_VALUE : SPO_TONE_DIM], 8))
                return SPO_CAPACITY;
            row += lines;
        }
    } else if (!menu_rows(&m, x, y))
        return SPO_CAPACITY;
    y += SPO_MENU_ROWS * m.rowh + 6;
    error = menu_error();
    if (!wrapped(&m, x + m.padx, y, spo_menu_help(spo->cursor), palette[SPO_TONE_LABEL],
                 error ? 1 : 2) ||
        (error && !text(x + m.padx, y + m.ch, error, palette[SPO_TONE_BAD], 255)))
        return SPO_CAPACITY;
    y += 2 * m.ch + 6;
    return menu_footer(&m, x, y) ? SPO_OK : SPO_CAPACITY;
}
spo_result spo_layout(uint32_t width, uint32_t height) {
    uint64_t now = spo_now();
    uint32_t i, valid = 0;
    uint32_t old_count = spo->mesh.count;
    int reuse_base = 0, size = (int)spo->overlay.font_scale,
        graph = !!(spo->overlay.items & SPO_BIT(SPO_GRAPH));
    uint8_t alpha = (uint8_t)(spo->overlay.text_opacity * 255 / 100);
    spo_rect g;
    plan p;
    spo->mesh.count = 0;
    spo->mesh.graph_scale = 0;
    if (spo->open) {
        spo->mesh_formatted_at = 0;
        spo->mesh.revision++;
        return menu(width, height);
    }
    for (i = 0; i < SPO_ITEM_COUNT; i++)
        if (spo->metrics[i].valid)
            valid |= SPO_BIT(i);
    if (!spo->formatted_at || now - spo->format_checked_at >= UINT64_C(500000000) ||
        memcmp(&spo->formatted_settings, &spo->overlay, sizeof(spo->overlay)) ||
        spo->counter_mask != spo->formatted_counters ||
        spo->indicator_mask != spo->formatted_indicators || valid != spo->formatted_valid) {
        formatted(now);
        spo->formatted_valid = valid;
    }
    if (!spo->row_count && !graph)
        return SPO_OK;
    if (spo->mesh_formatted_at == spo->formatted_at && spo->base_count &&
        spo->mesh_width == width && spo->mesh_height == height) {
        reuse_base = 1;
        size = (int)spo->actual_scale;
        use_font((uint32_t)size);
        if (!graph) {
            spo->mesh.count = spo->base_count;
            return SPO_OK;
        }
        g = spo->graph_rect;
        goto draw_graph;
    }
    spo->mesh.revision++;
    for (;; size--) {
        measure(&p, size, graph);
        if (p.w <= (int)width - 16 && p.h <= (int)height - 16)
            break;
        if (size <= 1)
            return SPO_CAPACITY;
    }
    spo->actual_scale = (uint32_t)size;
    use_font((uint32_t)size);
    spo->panel = spo_anchor(width, height, p.w, p.h, (int)spo->overlay.anchor);
    if (!rectangle(spo->panel.x, spo->panel.y, spo->panel.w, spo->panel.h, panel_color,
                   (uint8_t)(spo->overlay.background_opacity * 255 / 100)))
        return SPO_CAPACITY;
    {
        int x = spo->panel.x + p.px, y = spo->panel.y + p.py;
        g = (spo_rect){x, 0, p.gw, p.gh};
        for (i = 0; i <= spo->row_count; i++) {
            if (i == spo->header_rows && graph) {
                y += spo->header_rows ? p.gap : 0;
                g.y = y;
                if (!rectangle(g.x, g.y, g.w, g.h, black,
                               (uint8_t)(spo->overlay.background_opacity * 128 / 100)))
                    return SPO_CAPACITY;
                y += p.gh + (spo->row_count > spo->header_rows ? p.gap : 0);
            }
            if (i < spo->row_count) {
                if (!draw_row(&p, &spo->rows[i], x, y, alpha))
                    return SPO_CAPACITY;
                y += p.ch;
            }
        }
    }
    spo->base_count = spo->mesh.count;
    spo->mesh_formatted_at = spo->formatted_at;
    spo->mesh_width = width;
    spo->mesh_height = height;
    spo->graph_rect = g;
draw_graph:
    if (graph) {
        uint32_t count = spo->history_count < spo->overlay.graph_samples
                             ? spo->history_count
                             : spo->overlay.graph_samples;
        spo->graph_max = 2000.0 / spo->target_fps;
        int rebuild = spo->graph_pixel_range != spo->graph_max || spo->graph_pixel_scale != g.h ||
                      spo->graph_pixel_target != spo->target_fps ||
                      spo->graph_serial > spo->history_serial ||
                      spo->history_serial - spo->graph_serial > 120;
        uint32_t updates = (uint32_t)(spo->history_serial - spo->graph_serial);
        if (rebuild) {
            spo->graph_pixel_range = spo->graph_max;
            spo->graph_pixel_scale = g.h;
            spo->graph_pixel_target = spo->target_fps;
            spo->graph_pixel_factor = (double)g.h / (spo->graph_max * 1e6);
            memset(spo->mesh.graph_data, 0, sizeof(spo->mesh.graph_data));
            spo->mesh.graph_head = 0;
            spo->mesh.graph_epoch++;
            updates = spo->history_count < 120 ? spo->history_count : 120;
        }
        for (i = 0; i < updates; i++) {
            uint32_t at = (spo->history_next + SPO_HISTORY - updates + i) % SPO_HISTORY;
            uint64_t ns = spo->intervals[at];
            double height = (double)ns * spo->graph_pixel_factor;
            int pixels = height >= g.h ? g.h : height < 1 ? 1 : (int)ceil(height);
            const uint8_t *color = bar_color(ns);
            uint8_t *pixel = spo->mesh.graph_data[spo->mesh.graph_head];
            pixel[0] = (uint8_t)pixels;
            pixel[1] = color[0];
            pixel[2] = color[1];
            pixel[3] = color[2];
            spo->mesh.graph_head = (spo->mesh.graph_head + 1) % 120;
            spo->mesh.graph_version++;
        }
        spo->graph_serial = spo->history_serial;
        spo->mesh.graph_scale = (uint32_t)g.h;
        spo->mesh.graph_count = count;
        if (reuse_base && old_count && spo->graph_label_range == spo->graph_max &&
            spo->graph_label_target == spo->target_fps) {
            spo->mesh.count = old_count;
            return SPO_OK;
        }
        if (reuse_base) {
            spo->mesh.revision++;
            spo->mesh.count = spo->base_count;
        }
        if (!quad(g.x, g.y, g.w, g.h, -1, 0, -120, 1, palette[SPO_TONE_VALUE], alpha))
            return SPO_CAPACITY;
        {
            int thickness = size >= 3 ? 2 : 1,
                line = g.y + g.h - (int)(1000.0 / spo->target_fps / spo->graph_max * g.h + 0.5);
            if (line > g.y + g.h - thickness)
                line = g.y + g.h - thickness;
            if (line < g.y)
                line = g.y;
            if (!rectangle(g.x, line, g.w, thickness, palette[SPO_TONE_VALUE],
                           (uint8_t)(alpha * 90 / 255)))
                return SPO_CAPACITY;
            spo->graph_label_range = spo->graph_max;
            spo->graph_label_target = spo->target_fps;
        }
    }
    return SPO_OK;
}
