/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const uint8_t white[3] = {240, 242, 244}, accent[3] = {126, 197, 168},
                     dark[3] = {15, 19, 23}, selected[3] = {45, 76, 88};
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
                                  "Self cost"};
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
    return quad(x, y, w, h, 127.5f / 128.0f, 63.5f / 64.0f, 0, 0, rgb, alpha);
}
static int text(int x, int y, int scale, const char *value, uint8_t alpha) {
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
            if (!quad(x, y, 5 * scale, 8 * scale, (float)((i % 16) * 6) / 128.0f,
                      (float)((i / 16) * 9) / 64.0f, 5.0f / 128.0f, 8.0f / 64.0f, white, alpha))
                return 0;
        }
        x += 5 * scale;
    }
    return 1;
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
static void line(const char *format, ...) {
    va_list args;
    char buf[64];
    if (spo->line_count >= 40)
        return;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    spo_text_copy(spo->lines[spo->line_count++], 64, buf);
}
static int visible(int id) {
    return (spo->overlay.items & SPO_BIT(id)) && spo->metrics[id].valid;
}
static void part(char *row, const char *format, ...) {
    va_list args;
    char buf[64];
    size_t n;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    n = strlen(row);
    if (n && n + 1 + strlen(buf) > 24) {
        line("%s", row);
        row[0] = 0;
        n = 0;
    }
    if (n) {
        row[n++] = ' ';
        row[n] = 0;
    }
    if (strlen(buf) > 24)
        spo_text_copy(buf, sizeof(buf), "VALUE OVERFLOW");
    snprintf(row + n, 64 - n, "%s", buf);
}
static void formatted(uint64_t now) {
    char row[64] = "", buf[64];
    uint32_t i;
    spo->line_count = 0;
    if (spo->overlay.items & SPO_BIT(SPO_FPS)) {
        if (spo->fps > 0)
            part(row, "%.1f FPS", spo->fps);
        else
            part(row, "-- FPS");
    }
    if (spo->overlay.items & SPO_BIT(SPO_FRAME_TIME)) {
        if (spo->frame_ms > 0)
            part(row, "%.1fms", spo->frame_ms);
        else
            part(row, "--ms");
    }
    if (row[0])
        line("%s", row);
    spo->header_lines = spo->line_count;
    row[0] = 0;
    if (visible(SPO_SYSTEM_CPU))
        part(row, "CPU%.0f%%", spo->metrics[SPO_SYSTEM_CPU].value);
    if (visible(SPO_PROCESS_CPU))
        part(row, "PRC%.0f%%", spo->metrics[SPO_PROCESS_CPU].value);
    if (visible(SPO_GPU_LOAD))
        part(row, "GPU%.0f%%", spo->metrics[SPO_GPU_LOAD].value);
    if (row[0])
        line("%s", row);
    row[0] = 0;
    if (visible(SPO_CPU_CLOCK))
        part(row, "CPU0 %.0fMHz", spo->metrics[SPO_CPU_CLOCK].value);
    if (visible(SPO_GPU_CLOCK))
        part(row, "GPU%.0fMHz", spo->metrics[SPO_GPU_CLOCK].value);
    if (row[0])
        line("%s", row);
    row[0] = 0;
    if (visible(SPO_TEMPERATURE))
        part(row, "SOC %.0fC", spo->metrics[SPO_TEMPERATURE].value);
    if (visible(SPO_BATTERY))
        part(row, "BAT %.0f%%%s", spo->metrics[SPO_BATTERY].value,
             spo->battery_charging ? " CHG" : "");
    if (row[0])
        line("%s", row);
    row[0] = 0;
    if (visible(SPO_RSS))
        part(row, "RSS %.0fM", spo->metrics[SPO_RSS].value);
    if (visible(SPO_MEM_AVAILABLE))
        part(row, "AVAIL %.0fM", spo->metrics[SPO_MEM_AVAILABLE].value);
    if (row[0])
        line("%s", row);
    if (spo->overlay.items & SPO_BIT(SPO_LOW_1)) {
        if (spo->low_1 > 0)
            line("1%% LOW %.1f FPS", spo->low_1);
        else
            line("1%% LOW --");
    }
    for (i = 0; i < spo->counter_count; i++)
        if ((spo->counter_mask & SPO_BIT(i)) && spo->counters[i].valid) {
            spo_counter *c = &spo->counters[i];
            snprintf(buf, sizeof(buf), "%s %.*f%s", c->label, (int)c->decimals, c->value, c->unit);
            if (strlen(buf) > 24)
                snprintf(buf, sizeof(buf), "%s %.2g%s", c->label, c->value, c->unit);
            line("%s", strlen(buf) > 24 ? "COUNTER VALUE OVERFLOW" : buf);
        }
    for (i = 0; i < spo->indicator_count; i++)
        if ((spo->indicator_mask & SPO_BIT(i)) && spo->indicators[i].valid)
            line("%s %s", spo->indicators[i].label, spo->indicators[i].value ? "ON" : "OFF");
    if (spo->overlay.items & SPO_BIT(SPO_SELF_COST)) {
        if (spo->sampling_stats.sample_frames)
            line("SELF %.0fus S%.0f %ud", spo->sampling_stats.total_cpu_us_per_frame,
                 spo->sampling_stats.cpu_us_per_frame, spo->stats.draw_calls);
        else if (spo->stats.sample_frames)
            line("SELF R%.0fus S-- %ud", spo->stats.cpu_us_mean, spo->stats.draw_calls);
        else
            line("SELF --us %u draw%s", spo->stats.draw_calls,
                 spo->stats.draw_calls == 1 ? "" : "s");
    }
    spo->formatted_at = now;
    spo->formatted_settings = spo->overlay;
    spo->formatted_counters = spo->counter_mask;
    spo->formatted_indicators = spo->indicator_mask;
}
static int available(int item) {
    return spo_metrics_available(item);
}
static void menu_row(int row, char *label, char *value) {
    static const char *presets[] = {"0: Off", "1: FPS", "2: Timing", "3: Graph", "4: Full"};
    static const char *anchors[] = {"Top left",    "Top center",    "Top right",
                                    "Center left", "Center",        "Center right",
                                    "Bottom left", "Bottom center", "Bottom right"};
    static const int frames[] = {SPO_FPS, SPO_FRAME_TIME, SPO_GRAPH, SPO_LOW_1};
    static const int system[] = {SPO_SYSTEM_CPU, SPO_PROCESS_CPU, SPO_GPU_LOAD,
                                 SPO_CPU_CLOCK,  SPO_GPU_CLOCK,   SPO_TEMPERATURE,
                                 SPO_BATTERY,    SPO_RSS,         SPO_MEM_AVAILABLE};
    label[0] = value[0] = 0;
    if (spo->detail == 1) {
        const char *names[5] = {"Frames", "System", NULL, NULL, "Self cost"};
        int n = 2;
        if (spo->counter_count)
            names[n++] = "Port counters";
        if (spo->indicator_count)
            names[n++] = "Port indicators";
        names[n++] = "Self cost";
        snprintf(label, 64, "%s", names[row]);
        snprintf(value, 64, "%s",
                 row == n - 1 ? (spo->overlay.items & SPO_BIT(SPO_SELF_COST) ? "[ON]" : "[OFF]")
                              : ">");
    } else if (spo->detail == 2 || spo->detail == 3) {
        int item = spo->detail == 2 ? frames[row] : system[row];
        snprintf(label, 64, "%s", spo_item_name((uint32_t)item));
        snprintf(value, 64, "%s",
                 spo->detail == 3 && !available(item) ? "Unavailable"
                 : spo->overlay.items & SPO_BIT(item) ? "[ON]"
                                                      : "[OFF]");
    } else if (spo->detail == 4) {
        snprintf(label, 64, "%s", spo->counters[row].label);
        snprintf(value, 64, "%s", spo->counter_mask & SPO_BIT(row) ? "[ON]" : "[OFF]");
    } else if (spo->detail == 5) {
        snprintf(label, 64, "%s", spo->indicators[row].label);
        snprintf(value, 64, "%s", spo->indicator_mask & SPO_BIT(row) ? "[ON]" : "[OFF]");
    } else if (!spo->root) {
        static const char *names[] = {
            "Preset",       "Items",       "Position", "Size", "Background opacity",
            "Text opacity", "Graph length"};
        snprintf(label, 64, "%s", names[row]);
        if (row == 0)
            snprintf(value, 64, "%s%s", spo->overlay.custom ? "Custom from " : "",
                     presets[spo->overlay.preset]);
        else if (row == 1)
            snprintf(value, 64, ">");
        else if (row == 2)
            snprintf(value, 64, "%s", anchors[spo->overlay.anchor]);
        else if (row == 3) {
            if (spo->actual_scale && spo->actual_scale < spo->overlay.font_scale)
                snprintf(value, 64, "%ux / %ux fits", spo->overlay.font_scale, spo->actual_scale);
            else
                snprintf(value, 64, "%ux", spo->overlay.font_scale);
        } else if (row == 4)
            snprintf(value, 64, "%u%%", spo->overlay.background_opacity);
        else if (row == 5)
            snprintf(value, 64, "%u%%", spo->overlay.text_opacity);
        else if (row == 6)
            snprintf(value, 64, "%u frames", spo->overlay.graph_samples);
        if (spo->editing && row >= 3) {
            size_t used = strlen(value);
            snprintf(value + used, 64 - used, " (edit)");
        }
    } else {
        spo_option *o = spo_menu_option(row);
        if (!o)
            return;
        snprintf(label, 64, "%s", o->label);
        if (o->desc.type == SPO_TOGGLE)
            snprintf(value, 64, "%s", o->value ? "[ON]" : "[OFF]");
        else if (o->desc.type == SPO_CHOICE)
            snprintf(value, 64, "%s", o->choices[o->value]);
        else if (o->desc.type == SPO_ACTION)
            snprintf(value, 64, "[Run]");
        else
            snprintf(value, 64, "%d%s", o->value, spo->editing ? " (edit)" : "");
    }
}
static spo_result menu(uint32_t width, uint32_t height) {
    int scale = height >= 720 ? 3 : 2, w, h, x, y, i, rows = spo_menu_rows();
    char title[64], label[64], value[64];
    while (scale > 1 && (60 * 5 * scale > (int)width - 24 || 216 * scale > (int)height - 24))
        scale--;
    w = 300 * scale;
    h = 216 * scale;
    if (w > (int)width - 16 || h > (int)height - 16)
        return SPO_CAPACITY;
    x = ((int)width - w) / 2;
    y = ((int)height - h) / 2;
    spo->panel = (spo_rect){x, y, w, h};
    if (!rectangle(x, y, w, h, dark, 245))
        return SPO_CAPACITY;
    if (spo->detail) {
        static const char *titles[] = {"",
                                       "Overlay / Items",
                                       "Overlay / Frames",
                                       "Overlay / System",
                                       "Overlay / Port counters",
                                       "Overlay / Port indicators"};
        snprintf(title, sizeof(title), "%s", titles[spo->detail]);
    } else
        snprintf(title, sizeof(title), "< %s >",
                 !spo->root                          ? "Overlay"
                 : spo->root <= (int)spo->page_count ? spo->pages[spo->root - 1].label
                                                     : "About");
    if (spo->cursor == -1)
        rectangle(x + 3 * scale, y + 3 * scale, w - 6 * scale, 14 * scale, selected, 255);
    if (!text(x + 6 * scale, y + 6 * scale, scale, title, 255))
        return SPO_CAPACITY;
    if (spo->root == (int)spo->page_count + 1 && !spo->detail) {
        const char *about[] = {"sbc-port-overlay " SPO_VERSION,
                               spo->name,
                               spo->version,
                               spo->renderer_name,
                               "GPL-3.0-or-later",
                               "Spleen / BSD-2-Clause",
                               NULL};
        int row = 0;
        for (i = 0; about[i]; i++) {
            size_t offset = 0, length = strlen(about[i]);
            do {
                char chunk[57];
                size_t n = length - offset > 56 ? 56 : length - offset;
                memcpy(chunk, about[i] + offset, n);
                chunk[n] = 0;
                if (!text(x + 6 * scale, y + (28 + row * 14) * scale, scale, chunk, 255))
                    return SPO_CAPACITY;
                offset += n;
                row++;
            } while (offset < length);
        }
    } else
        for (i = 0; i < rows; i++) {
            int ry = y + (26 + i * 14) * scale;
            menu_row(i, label, value);
            if (i == spo->cursor &&
                !rectangle(x + 3 * scale, ry - 3 * scale, w - 6 * scale, 13 * scale, selected, 255))
                return SPO_CAPACITY;
            if (!text(x + 6 * scale, ry, scale, label, 255) ||
                !text(x + w - 6 * scale - (int)strlen(value) * 5 * scale, ry, scale, value, 255))
                return SPO_CAPACITY;
        }
    if (!spo->detail && spo->root > 0 && spo->root <= (int)spo->page_count && spo->cursor >= 0) {
        spo_option *o = spo_menu_option(spo->cursor);
        if (o) {
            char help[57];
            size_t length = strlen(o->help), first = length > 56 ? 56 : length;
            memcpy(help, o->help, first);
            help[first] = 0;
            if (!text(x + 6 * scale, y + 166 * scale, scale, help, 255))
                return SPO_CAPACITY;
            if (length > first &&
                !text(x + 6 * scale, y + 176 * scale, scale, o->help + first, 255))
                return SPO_CAPACITY;
        }
    }
    if (!text(x + 6 * scale, y + h - 30 * scale, scale,
              spo->error == SPO_OK         ? (spo->dirty ? "Config: Unsaved" : "Config: Saved")
              : spo->error == SPO_REJECTED ? "Setting was rejected"
              : spo->error == SPO_IO       ? "Could not save settings"
                                           : "Settings error",
              255) ||
        !text(x + 6 * scale, y + h - 15 * scale, scale, "D-pad: change  South: select  East: back",
              255))
        return SPO_CAPACITY;
    return SPO_OK;
}
spo_result spo_layout(uint32_t width, uint32_t height) {
    uint64_t now = spo_now();
    uint32_t i, valid = 0;
    uint32_t old_count = spo->mesh.count;
    int reuse_base = 0;
    int scale = (int)spo->overlay.font_scale, base_w = 0, base_h,
        graph = !!(spo->overlay.items & SPO_BIT(SPO_GRAPH)), x, y, graph_y = 0;
    uint8_t alpha = (uint8_t)(spo->overlay.text_opacity * 255 / 100);
    spo->mesh.count = 0;
    spo->mesh.graph_scale = 0;
    if (spo->open) {
        spo->mesh_formatted_at = 0;
        return menu(width, height);
    }
    for (i = 0; i < SPO_ITEM_COUNT; i++)
        if (spo->metrics[i].valid)
            valid |= SPO_BIT(i);
    if (!spo->formatted_at || now - spo->formatted_at >= UINT64_C(500000000) ||
        memcmp(&spo->formatted_settings, &spo->overlay, sizeof(spo->overlay)) ||
        spo->counter_mask != spo->formatted_counters ||
        spo->indicator_mask != spo->formatted_indicators || valid != spo->formatted_valid) {
        formatted(now);
        spo->formatted_valid = valid;
    }
    if (!spo->line_count && !graph)
        return SPO_OK;
    if (spo->mesh_formatted_at == spo->formatted_at && spo->base_count &&
        spo->mesh_width == width && spo->mesh_height == height) {
        reuse_base = 1;
        if (!graph) {
            spo->mesh.count = spo->base_count;
            return SPO_OK;
        }
        scale = (int)spo->actual_scale;
        x = spo->panel.x + 3 * scale;
        graph_y = spo->cached_graph_y;
        goto draw_graph;
    }
    for (i = 0; i < spo->line_count; i++) {
        int n = (int)strlen(spo->lines[i]) * 5;
        if (n > base_w)
            base_w = n;
    }
    if (graph && base_w < 120)
        base_w = 120;
    base_w += 6;
    base_h = (int)spo->line_count * 9 + 6 + (graph ? 22 : 0);
    while (scale > 1 && (base_w * scale > (int)width - 16 || base_h * scale > (int)height - 16))
        scale--;
    if (base_w * scale > (int)width - 16 || base_h * scale > (int)height - 16)
        return SPO_CAPACITY;
    spo->actual_scale = (uint32_t)scale;
    spo->panel =
        spo_anchor(width, height, base_w * scale, base_h * scale, (int)spo->overlay.anchor);
    x = spo->panel.x + 3 * scale;
    y = spo->panel.y + 3 * scale;
    if (!rectangle(spo->panel.x, spo->panel.y, spo->panel.w, spo->panel.h, dark,
                   (uint8_t)(spo->overlay.background_opacity * 255 / 100)))
        return SPO_CAPACITY;
    for (i = 0; i <= spo->line_count; i++) {
        if (i == spo->header_lines && graph) {
            graph_y = y;
            y += 22 * scale;
        }
        if (i < spo->line_count) {
            if (!text(x, y, scale, spo->lines[i], alpha))
                return SPO_CAPACITY;
            y += 9 * scale;
        }
    }
    spo->base_count = spo->mesh.count;
    spo->mesh_formatted_at = spo->formatted_at;
    spo->mesh_width = width;
    spo->mesh_height = height;
    spo->cached_graph_y = graph_y;
draw_graph:
    if (graph) {
        uint32_t count = spo->history_count < spo->overlay.graph_samples
                             ? spo->history_count
                             : spo->overlay.graph_samples;
        if (now - spo->graph_range_at >= UINT64_C(500000000)) {
            double peak = 33.333333;
            for (i = 0; i < count; i++) {
                double ms =
                    (double)spo
                        ->intervals[(spo->history_next + SPO_HISTORY - count + i) % SPO_HISTORY] /
                    1e6;
                if (ms > peak)
                    peak = ms;
            }
            if (peak >= spo->graph_max) {
                spo->graph_max = peak;
                spo->graph_high_at = now;
            } else if (now - spo->graph_high_at > UINT64_C(5000000000))
                spo->graph_max = peak;
            spo->graph_range_at = now;
        }
        if (spo->graph_pixel_range != spo->graph_max || spo->graph_pixel_scale != scale) {
            memset(spo->graph_ns, 0, sizeof(spo->graph_ns));
            spo->graph_pixel_range = spo->graph_max;
            spo->graph_pixel_scale = scale;
            spo->graph_pixel_factor = 20.0 * scale / (spo->graph_max * 1e6);
        }
        memset(spo->mesh.graph_words, 0, sizeof(spo->mesh.graph_words));
        spo->mesh.graph_scale = (uint32_t)scale;
        for (i = 0; i < count; i++) {
            uint32_t at = (spo->history_next + SPO_HISTORY - count + i) % SPO_HISTORY;
            uint64_t ns = spo->intervals[at];
            if (ns != spo->graph_ns[at]) {
                int pixels = (int)ceil((double)ns * spo->graph_pixel_factor);
                if (pixels < 1)
                    pixels = 1;
                if (pixels > 20 * scale)
                    pixels = 20 * scale;
                spo->graph_pixels[at] =
                    (uint8_t)(pixels | ((double)ns > spo->graph_max * 1e6 ? 128 : 0));
                spo->graph_ns[at] = ns;
            }
            uint32_t column = 120 - count + i;
            spo->mesh.graph_words[column / 4] |= (uint32_t)spo->graph_pixels[at]
                                                 << ((column % 4) * 8);
        }
        if (reuse_base && old_count && spo->graph_label_range == spo->graph_max) {
            spo->mesh.count = old_count;
            return SPO_OK;
        }
        if (reuse_base) {
            spo->mesh.count = spo->base_count;
        }
        if (!quad(x, graph_y, 120 * scale, 20 * scale, -1, 0, -120, 20, accent, alpha))
            return SPO_CAPACITY;
        {
            if (!spo->graph_label[0] || spo->graph_label_range != spo->graph_max) {
                snprintf(spo->graph_label, sizeof(spo->graph_label), "%.0fms", spo->graph_max);
                spo->graph_label_range = spo->graph_max;
            }
            if (!text(x, graph_y, scale, spo->graph_label, alpha))
                return SPO_CAPACITY;
        }
    }
    return SPO_OK;
}
