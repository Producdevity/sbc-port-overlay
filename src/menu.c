/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdio.h>
#include <string.h>

static const char *const preset_names[] = {"Off", "FPS", "Basic", "Graph", "Full"};
static const char *const anchor_names[] = {"Top left",    "Top center",    "Top right",
                                           "Center left", "Center",        "Center right",
                                           "Bottom left", "Bottom center", "Bottom right"};
static const char *const size_names[] = {"6x12", "8x16", "12x24", "16x32"};

typedef struct {
    const char *group, *label;
    int item;
} show_item;
static const show_item show_items[] = {
    {"Frames", "FPS", SPO_FPS},
    {NULL, "Frame time", SPO_FRAME_TIME},
    {NULL, "1% low", SPO_LOW_1},
    {NULL, "Frame graph", SPO_GRAPH},
    {"CPU", "Load", SPO_SYSTEM_CPU},
    {NULL, "Clock", SPO_CPU_CLOCK},
    {NULL, "Temperature", SPO_TEMPERATURE},
    {"GPU", "Load", SPO_GPU_LOAD},
    {NULL, "Clock", SPO_GPU_CLOCK},
    {"Memory", "RAM", SPO_RAM_USED},
    {NULL, "Game memory", SPO_RSS},
    {"System", "Battery", SPO_BATTERY},
    {NULL, "Charging", SPO_CHARGING},
    {"Overlay", "Overlay cost", SPO_SELF_COST},
};
#define SHOW_ITEMS (sizeof(show_items) / sizeof(show_items[0]))

enum { ENTRY_HEADING, ENTRY_ITEM, ENTRY_COUNTER, ENTRY_INDICATOR };
typedef struct {
    int kind, index;
    const char *text;
} entry;

/* The Show tab is one flat list: a heading per group, then its toggles. */
static int show_entries(int want, entry *out) {
    int n = 0;
    size_t k;
    for (k = 0; k < SHOW_ITEMS; k++) {
        if (k == SHOW_ITEMS - 1 && (spo->counter_count || spo->indicator_count)) {
            uint32_t i;
            if (out && n == want)
                *out = (entry){ENTRY_HEADING, 0, spo->name[0] ? spo->name : "Port"};
            n++;
            for (i = 0; i < spo->counter_count; i++, n++)
                if (out && n == want)
                    *out = (entry){ENTRY_COUNTER, (int)i, spo->counters[i].label};
            for (i = 0; i < spo->indicator_count; i++, n++)
                if (out && n == want)
                    *out = (entry){ENTRY_INDICATOR, (int)i, spo->indicators[i].label};
        }
        if (show_items[k].group) {
            if (out && n == want)
                *out = (entry){ENTRY_HEADING, 0, show_items[k].group};
            n++;
        }
        if (out && n == want)
            *out = (entry){ENTRY_ITEM, (int)k, show_items[k].label};
        n++;
    }
    return n;
}
static int item_available(int item) {
    if (item < SPO_SYSTEM_CPU || item == SPO_SELF_COST)
        return 1;
    return spo_metrics_available(item);
}
spo_option *spo_menu_option(int row) {
    uint32_t i;
    for (i = 0; i < spo->option_count; i++)
        if (spo->options[i].desc.page == (uint32_t)spo->tab - 1 && row-- == 0)
            return &spo->options[i];
    return NULL;
}
int spo_menu_tabs(void) {
    return (int)spo->page_count + 3;
}
const char *spo_menu_tab_label(int tab) {
    if (tab == 0)
        return "Overlay";
    if (tab == 1)
        return "Show";
    if (tab - 2 < (int)spo->page_count)
        return spo->pages[tab - 2].label;
    return "About";
}
static int screen_row(void) {
    return spo->sink && spo->sink->available();
}
/* The Overlay tab ends with the Screen row, when there is one, and the Face buttons row. */
static int face_row(void) {
    return screen_row() ? 7 : 6;
}
int spo_menu_rows(void) {
    if (spo->tab == 0)
        return face_row() + 1;
    if (spo->tab == 1)
        return show_entries(-1, NULL);
    if (spo->tab - 2 < (int)spo->page_count)
        return (int)spo->pages[spo->tab - 2].count;
    return 0;
}
static void toggle_row(spo_menu_row *r, int on) {
    r->kind = SPO_ROW_TOGGLE;
    snprintf(r->value, sizeof(r->value), "%s", on ? "ON" : "OFF");
    r->tone = on ? SPO_TONE_GOOD : SPO_TONE_DIM;
}
void spo_menu_describe(int row, spo_menu_row *r) {
    memset(r, 0, sizeof(*r));
    r->enabled = 1;
    r->tone = SPO_TONE_ACCENT;
    if (spo->tab == 0) {
        static const char *const names[] = {
            "Preset",       "Position",     "Size",   "Background opacity",
            "Text opacity", "Graph length", "Screen", "Face buttons"};
        const spo_overlay_settings *o = &spo->overlay;
        int face = row == face_row(), screen = !face && row == 6;
        if (row < 0 || row > face_row())
            return;
        snprintf(r->label, sizeof(r->label), "%s", names[face ? 7 : row]);
        r->kind = row < 2 || screen || face ? SPO_ROW_CHOICE : SPO_ROW_SLIDER;
        if (face)
            snprintf(r->value, sizeof(r->value), "%s", spo->face_swapped ? "Swapped" : "As set");
        else if (screen)
            snprintf(r->value, sizeof(r->value), "%s", spo->screen_mode ? "Game" : "Second");
        else if (row == 0)
            snprintf(r->value, sizeof(r->value), "%s", o->custom ? "Custom" : preset_names[o->preset]);
        else if (row == 1)
            snprintf(r->value, sizeof(r->value), "%s", anchor_names[o->anchor]);
        else if (row == 2) {
            if (spo->actual_scale && spo->actual_scale < o->font_scale)
                snprintf(r->value, sizeof(r->value), "%s, fits %s", size_names[o->font_scale - 1],
                         size_names[spo->actual_scale - 1]);
            else
                snprintf(r->value, sizeof(r->value), "%s", size_names[o->font_scale - 1]);
        } else if (row == 3)
            snprintf(r->value, sizeof(r->value), "%u%%", o->background_opacity);
        else if (row == 4)
            snprintf(r->value, sizeof(r->value), "%u%%", o->text_opacity);
        else
            snprintf(r->value, sizeof(r->value), "%u frames", o->graph_samples);
    } else if (spo->tab == 1) {
        entry e = {ENTRY_HEADING, 0, ""};
        if (row < 0 || row >= show_entries(-1, NULL))
            return;
        show_entries(row, &e);
        snprintf(r->label, sizeof(r->label), "%s", e.text);
        if (e.kind == ENTRY_HEADING) {
            r->kind = SPO_ROW_HEADING;
            r->tone = SPO_TONE_DIM;
        } else if (e.kind == ENTRY_ITEM) {
            int item = show_items[e.index].item;
            if (!item_available(item)) {
                r->kind = SPO_ROW_TOGGLE;
                r->enabled = 0;
                r->tone = SPO_TONE_DIM;
                snprintf(r->value, sizeof(r->value), "Not available");
            } else
                toggle_row(r, !!(spo->overlay.items & SPO_BIT(item)));
        } else if (e.kind == ENTRY_COUNTER)
            toggle_row(r, !!(spo->counter_mask & SPO_BIT(e.index)));
        else
            toggle_row(r, !!(spo->indicator_mask & SPO_BIT(e.index)));
    } else if (spo->tab - 2 < (int)spo->page_count) {
        spo_option *o = spo_menu_option(row);
        if (!o)
            return;
        snprintf(r->label, sizeof(r->label), "%s", o->label);
        if (o->desc.type == SPO_TOGGLE)
            toggle_row(r, !!o->value);
        else if (o->desc.type == SPO_CHOICE) {
            r->kind = SPO_ROW_CHOICE;
            snprintf(r->value, sizeof(r->value), "%s", o->choices[o->value]);
        } else if (o->desc.type == SPO_ACTION) {
            r->kind = SPO_ROW_ACTION;
            r->tone = SPO_TONE_LABEL;
            snprintf(r->value, sizeof(r->value), "Run");
        } else {
            r->kind = SPO_ROW_SLIDER;
            snprintf(r->value, sizeof(r->value), "%d", o->value);
        }
    }
}
const char *spo_menu_help(int row) {
    spo_option *o;
    if (spo->tab == 0)
        return row == face_row()
                   ? "Swapped exchanges A and B, X and Y in the game. The menu keeps its buttons."
                   : "";
    if (spo->tab < 2 || spo->tab - 2 >= (int)spo->page_count)
        return "";
    o = spo_menu_option(row);
    return o ? o->help : "";
}
static int selectable(int row) {
    spo_menu_row r;
    spo_menu_describe(row, &r);
    return r.kind != SPO_ROW_HEADING && r.kind != SPO_ROW_NONE && r.enabled;
}
void spo_menu_scroll_fit(void) {
    int rows = spo_menu_rows(), first = spo->cursor;
    if (spo->cursor < 0)
        return;
    if (first > 0) {
        spo_menu_row above;
        spo_menu_describe(first - 1, &above);
        if (above.kind == SPO_ROW_HEADING)
            first--;
    }
    if (first < spo->scroll)
        spo->scroll = first;
    if (spo->cursor >= spo->scroll + SPO_MENU_ROWS)
        spo->scroll = spo->cursor - SPO_MENU_ROWS + 1;
    if (spo->scroll > rows - SPO_MENU_ROWS)
        spo->scroll = rows - SPO_MENU_ROWS;
    if (spo->scroll < 0)
        spo->scroll = 0;
}
void spo_menu_home(void) {
    int i, rows = spo_menu_rows();
    spo->cursor = -1;
    spo->scroll = 0;
    for (i = 0; i < rows; i++)
        if (selectable(i)) {
            spo->cursor = i;
            break;
        }
    spo_menu_scroll_fit();
}
static void move(int dir) {
    int i, rows = spo_menu_rows();
    for (i = spo->cursor + dir; i >= 0 && i < rows; i += dir)
        if (selectable(i)) {
            spo->cursor = i;
            break;
        }
    spo_menu_scroll_fit();
}
static void switch_tab(int dir) {
    int count = spo_menu_tabs();
    spo->tab = (spo->tab + dir + count) % count;
    spo_menu_home();
}
static uint32_t clamp_step(uint32_t v, int dir, uint32_t step, uint32_t min, uint32_t max) {
    int64_t next = (int64_t)v + (int64_t)dir * step;
    return (uint32_t)(next < min ? min : next > max ? max : next);
}
int spo_menu_row_kind(int row) {
    spo_menu_row r;
    spo_menu_describe(row, &r);
    return r.kind;
}
static void toggle_show(int row) {
    entry e = {ENTRY_HEADING, 0, ""};
    show_entries(row, &e);
    if (e.kind == ENTRY_ITEM) {
        int item = show_items[e.index].item;
        if (!item_available(item))
            return;
        spo->overlay.items ^= SPO_BIT(item);
        spo_metrics_reset();
        spo_metrics_request();
    } else if (e.kind == ENTRY_COUNTER)
        spo->counter_mask ^= SPO_BIT(e.index);
    else if (e.kind == ENTRY_INDICATOR)
        spo->indicator_mask ^= SPO_BIT(e.index);
    else
        return;
    spo_overlay_sync();
    spo->dirty = 1;
}
static void change(int dir, int activate) {
    int row = spo->cursor;
    spo_menu_row r;
    if (row < 0)
        return;
    spo_menu_describe(row, &r);
    if (!r.enabled || r.kind == SPO_ROW_HEADING || r.kind == SPO_ROW_NONE)
        return;
    if (r.kind == SPO_ROW_SLIDER && activate)
        return;
    if (spo->tab == 0) {
        spo_overlay_settings *o = &spo->overlay;
        if (row == 0)
            spo_preset_set((o->preset + 5 + (uint32_t)dir) % 5);
        else if (row == 1)
            o->anchor = (o->anchor + 9 + (uint32_t)dir) % 9;
        else if (row == 2)
            o->font_scale = clamp_step(o->font_scale, dir, 1, 1, 4);
        else if (row == 3)
            o->background_opacity = clamp_step(o->background_opacity, dir, 5, 0, 100);
        else if (row == 4)
            o->text_opacity = clamp_step(o->text_opacity, dir, 5, 10, 100);
        else if (row == face_row())
            spo->face_swapped = !spo->face_swapped;
        else if (row == 6)
            spo->screen_mode = !spo->screen_mode;
        else
            o->graph_samples = clamp_step(o->graph_samples, dir, 30, 30, 120);
        spo->dirty = 1;
    } else if (spo->tab == 1) {
        toggle_show(row);
    } else {
        spo_option *o = spo_menu_option(row);
        int64_t value;
        if (!o)
            return;
        if (o->desc.type == SPO_ACTION) {
            if (activate && o->desc.on_action) {
                spo->in_callback = 1;
                o->desc.on_action((spo_id)(o - spo->options + 1), o->desc.user);
                spo->in_callback = 0;
            }
            return;
        }
        if (o->desc.type == SPO_TOGGLE)
            value = !o->value;
        else if (o->desc.type == SPO_CHOICE)
            value = ((int64_t)o->value + dir + o->desc.choice_count) % o->desc.choice_count;
        else {
            value = (int64_t)o->value + (int64_t)dir * o->desc.step;
            if (value < o->desc.minimum)
                value = o->desc.minimum;
            if (value > o->desc.maximum)
                value = o->desc.maximum;
        }
        spo_option_set((spo_id)(o - spo->options + 1), (int32_t)value);
    }
}
void spo_menu_button(spo_button button) {
    if (button == SPO_EAST)
        spo_set_open(0);
    else if (button == SPO_L1)
        switch_tab(-1);
    else if (button == SPO_R1)
        switch_tab(1);
    else if (button == SPO_UP)
        move(-1);
    else if (button == SPO_DOWN)
        move(1);
    else if (button == SPO_LEFT)
        change(-1, 0);
    else if (button == SPO_RIGHT)
        change(1, 0);
    else if (button == SPO_SOUTH)
        change(1, 1);
}
static int hit(const spo_rect *r, int x, int y) {
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}
static void tap(int x, int y) {
    const spo_hit *h = &spo->hit;
    int i, rows = spo_menu_rows(), row;
    spo_menu_row r;
    if (!hit(&h->panel, x, y)) {
        spo_set_open(0);
        return;
    }
    for (i = 0; i < h->tabs; i++)
        if (hit(&h->tab[i], x, y)) {
            spo->tab = h->tab_index[i];
            spo_menu_home();
            return;
        }
    if (y < h->rows_y || y >= h->rows_y + SPO_MENU_ROWS * h->rowh)
        return;
    row = spo->scroll + (y - h->rows_y) / h->rowh;
    if (row >= rows)
        return;
    spo_menu_describe(row, &r);
    if (!r.enabled || r.kind == SPO_ROW_HEADING || r.kind == SPO_ROW_NONE)
        return;
    if (r.kind == SPO_ROW_TOGGLE || r.kind == SPO_ROW_ACTION) {
        spo->cursor = row;
        spo_menu_button(SPO_SOUTH);
    } else if (row == spo->cursor)
        spo_menu_button(x < h->rows_x + h->rows_w / 2 ? SPO_LEFT : SPO_RIGHT);
    else
        spo->cursor = row;
}
/* phase: 0 down, 1 move, 2 up; x and y are pixels of the surface the menu was laid out for. A tap
   acts on the row, tab or panel edge under it; a vertical drag over the rows scrolls them. */
int32_t spo_ui_pointer(uint32_t phase, int32_t x, int32_t y) {
    const spo_hit *h;
    if (!spo || !spo->open || !spo->hit.valid)
        return 0;
    h = &spo->hit;
    if (phase == 0) {
        spo->touch_down = 1;
        spo->touch_drag = 0;
        spo->touch_x = x;
        spo->touch_y = y;
        spo->touch_scroll = spo->scroll;
    } else if (phase == 1 && spo->touch_down && h->rowh > 0) {
        int dy = y - spo->touch_y, rows = spo_menu_rows(), top = rows - SPO_MENU_ROWS;
        if (dy > h->rowh / 2 || dy < -h->rowh / 2)
            spo->touch_drag = 1;
        if (spo->touch_drag && top > 0) {
            int at = spo->touch_scroll - dy / h->rowh;
            spo->scroll = at < 0 ? 0 : at > top ? top : at;
        }
    } else if (phase == 2 && spo->touch_down) {
        spo->touch_down = 0;
        if (!spo->touch_drag)
            tap(x, y);
    }
    return 1;
}
int spo_menu_repeats(spo_button button) {
    if (button == SPO_UP || button == SPO_DOWN)
        return 1;
    return spo_menu_row_kind(spo->cursor) == SPO_ROW_SLIDER;
}
