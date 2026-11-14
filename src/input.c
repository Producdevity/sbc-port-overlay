/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"
#include <string.h>

static spo_pad *pad_for(uint32_t id) {
    uint32_t i;
    spo_pad *empty = NULL, *neutral = NULL;
    if (id == SPO_ALL_CONTROLLERS)
        return NULL;
    for (i = 0; i < 4; i++) {
        if (spo->pads[i].used && spo->pads[i].id == id)
            return &spo->pads[i];
        if (!spo->pads[i].used && !empty)
            empty = &spo->pads[i];
        if (!spo->pads[i].held && !spo->pads[i].axes && !neutral)
            neutral = &spo->pads[i];
    }
    if (!empty && neutral) {
        empty = neutral;
        memset(empty, 0, sizeof(*empty));
    }
    if (empty) {
        empty->id = id;
        empty->used = 1;
        empty->ready = 1;
    }
    return empty;
}
static int all_neutral(void) {
    uint32_t i;
    for (i = 0; i < 4; i++)
        if (spo->pads[i].held || spo->pads[i].axes)
            return 0;
    return 1;
}
int32_t spo_menu_is_open(void) {
    return spo ? spo->open : 0;
}
int32_t spo_input_captured(void) {
    return spo ? (spo->open || spo->drain) : 0;
}
void spo_set_open(int open) {
    uint32_t i;
    if (spo->open == open)
        return;
    spo->open = open;
    spo->editing = 0;
    if (open) {
        spo->detail = 0;
        spo->cursor = 0;
    } else
        spo->drain = !all_neutral();
    for (i = 0; i < 4; i++) {
        spo->pads[i].armed = 0;
        spo->pads[i].repeat_at = 0;
    }
    if (spo->config.on_pause) {
        spo->in_callback = 1;
        spo->config.on_pause((uint32_t)open, spo->config.pause_user);
        spo->in_callback = 0;
    }
}
void spo_input_reset(uint32_t id) {
    uint32_t i;
    if (!spo || spo->in_callback)
        return;
    if (id != SPO_ALL_CONTROLLERS)
        (void)pad_for(id);
    for (i = 0; i < 4; i++)
        if (id == SPO_ALL_CONTROLLERS || (spo->pads[i].used && spo->pads[i].id == id)) {
            uint32_t previous = spo->pads[i].id, used = spo->pads[i].used;
            memset(&spo->pads[i], 0, sizeof(spo_pad));
            spo->pads[i].id = previous;
            spo->pads[i].used = used;
            if (id == SPO_ALL_CONTROLLERS)
                spo->pads[i].ready = 1;
        }
    if (!spo->open && all_neutral())
        spo->drain = 0;
}
spo_result spo_input_sync(uint32_t id, uint32_t buttons, const int32_t axes[SPO_AXIS_COUNT]) {
    spo_pad *p;
    uint32_t i;
    if (!spo || spo->in_callback)
        return SPO_NOT_INITIALIZED;
    if (id == SPO_ALL_CONTROLLERS || buttons >= SPO_BIT(SPO_BUTTON_COUNT))
        return spo_fail(SPO_INVALID);
    for (i = 0; axes && i < SPO_AXIS_COUNT; i++)
        if (axes[i] < (i >= SPO_LT ? 0 : -32768) || axes[i] > 32767)
            return spo_fail(SPO_INVALID);
    p = pad_for(id);
    if (!p)
        return spo_fail(SPO_CAPACITY);
    p->held = buttons;
    p->axes = 0;
    p->armed = 0;
    p->guide = !!(buttons & SPO_BIT(SPO_GUIDE));
    p->repeat_at = 0;
    for (i = 0; i < SPO_AXIS_COUNT; i++) {
        int32_t v = axes ? axes[i] : 0;
        p->axis[i] = v;
        if (v > 4915 || v < -4915)
            p->axes |= SPO_BIT(i);
    }
    p->ready = !p->held && !p->axes;
    if (!spo->open && all_neutral())
        spo->drain = 0;
    return SPO_OK;
}
static int item_index(int row) {
    static const int frame[] = {SPO_FPS, SPO_FRAME_TIME, SPO_GRAPH, SPO_LOW_1};
    static const int system[] = {SPO_SYSTEM_CPU, SPO_PROCESS_CPU, SPO_GPU_LOAD,
                                 SPO_CPU_CLOCK,  SPO_GPU_CLOCK,   SPO_TEMPERATURE,
                                 SPO_BATTERY,    SPO_RSS,         SPO_MEM_AVAILABLE};
    return spo->detail == 2 ? frame[row] : system[row];
}
static int item_available(int item) {
    if (item < SPO_SYSTEM_CPU || item == SPO_SELF_COST)
        return 1;
    return spo_metrics_available(item);
}
static int item_index_count(void) {
    return 3 + (spo->counter_count ? 1 : 0) + (spo->indicator_count ? 1 : 0);
}
static int item_index_target(int row) {
    if (row < 2)
        return row + 2;
    row -= 2;
    if (spo->counter_count) {
        if (row == 0)
            return 4;
        row--;
    }
    if (spo->indicator_count) {
        if (row == 0)
            return 5;
    }
    return 6;
}
int spo_menu_rows(void) {
    if (spo->detail == 1)
        return item_index_count();
    if (spo->detail == 2)
        return 4;
    if (spo->detail == 3)
        return 9;
    if (spo->detail == 4)
        return (int)spo->counter_count;
    if (spo->detail == 5)
        return (int)spo->indicator_count;
    if (!spo->root)
        return 7;
    if (spo->root <= (int)spo->page_count)
        return (int)spo->pages[spo->root - 1].count;
    return 0;
}
spo_option *spo_menu_option(int row) {
    uint32_t i;
    for (i = 0; i < spo->option_count; i++)
        if (spo->options[i].desc.page == (uint32_t)spo->root && row-- == 0)
            return &spo->options[i];
    return NULL;
}
static uint32_t clamp_step(uint32_t v, int dir, uint32_t step, uint32_t min, uint32_t max) {
    int64_t next = (int64_t)v + (int64_t)dir * step;
    return (uint32_t)(next < min ? min : next > max ? max : next);
}
static void change(int dir, int activate) {
    int row = spo->cursor;
    if (row < 0) {
        int count = (int)spo->page_count + 2;
        spo->root = (spo->root + dir + count) % count;
        spo->detail = 0;
        return;
    }
    if (spo->detail == 1) {
        if (activate) {
            int target = item_index_target(row);
            if (target == 6) {
                spo->overlay.items ^= SPO_BIT(SPO_SELF_COST);
                spo->overlay.custom = 1;
                spo->dirty = 1;
            } else {
                spo->detail = target;
                spo->cursor = 0;
            }
        }
    } else if (spo->detail == 2 || spo->detail == 3) {
        int item = item_index(row);
        if (item_available(item)) {
            spo->overlay.items ^= SPO_BIT(item);
            spo->overlay.custom = 1;
            spo->dirty = 1;
            spo_metrics_reset();
            spo_metrics_request();
        }
    } else if (spo->detail == 4 || spo->detail == 5) {
        uint32_t *mask = spo->detail == 4 ? &spo->counter_mask : &spo->indicator_mask;
        *mask ^= SPO_BIT(row);
        spo->overlay.custom = 1;
        spo->dirty = 1;
    } else if (!spo->root) {
        spo_overlay_settings *o = &spo->overlay;
        if (activate && row >= 3) {
            spo->editing = !spo->editing;
            return;
        }
        if (row == 0)
            spo_preset_set((o->preset + 5 + (uint32_t)dir) % 5);
        else if (row == 1) {
            if (activate) {
                spo->detail = 1;
                spo->cursor = 0;
            }
            return;
        } else if (row == 2)
            o->anchor = (o->anchor + 9 + (uint32_t)dir) % 9;
        else if (row == 3)
            o->font_scale = clamp_step(o->font_scale, dir, 1, 1, 4);
        else if (row == 4)
            o->background_opacity = clamp_step(o->background_opacity, dir, 5, 0, 100);
        else if (row == 5)
            o->text_opacity = clamp_step(o->text_opacity, dir, 5, 10, 100);
        else if (row == 6)
            o->graph_samples = clamp_step(o->graph_samples, dir, 30, 30, 120);
        spo->dirty = 1;
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
        } else {
            if (o->desc.type == SPO_TOGGLE)
                value = !o->value;
            else if (o->desc.type == SPO_CHOICE)
                value = ((int64_t)o->value + dir + o->desc.choice_count) % o->desc.choice_count;
            else {
                if (activate) {
                    spo->editing = !spo->editing;
                    return;
                }
                value = (int64_t)o->value + (int64_t)dir * o->desc.step;
                if (value < o->desc.minimum)
                    value = o->desc.minimum;
                if (value > o->desc.maximum)
                    value = o->desc.maximum;
            }
            spo_option_set((spo_id)(o - spo->options + 1), (int32_t)value);
        }
    }
}
void spo_menu_button(spo_button button) {
    int rows = spo_menu_rows();
    if (button == SPO_EAST) {
        if (spo->editing)
            spo->editing = 0;
        else if (spo->detail) {
            spo->detail = spo->detail == 1 ? 0 : 1;
            spo->cursor = spo->detail ? 0 : 1;
        } else
            spo_set_open(0);
    } else if (button == SPO_UP && !spo->editing) {
        if (spo->cursor > -1)
            spo->cursor--;
    } else if (button == SPO_DOWN && !spo->editing) {
        if (spo->cursor < rows - 1)
            spo->cursor++;
    } else if (button == SPO_LEFT)
        change(-1, 0);
    else if (button == SPO_RIGHT)
        change(1, 0);
    else if (button == SPO_SOUTH)
        change(1, 1);
}
int32_t spo_handle_input(uint32_t id, spo_button button, uint32_t pressed) {
    spo_pad *p;
    uint32_t bit;
    int captured;
    if (!spo || spo->in_callback)
        return 0;
    captured = spo_input_captured();
    if (button >= SPO_BUTTON_COUNT || pressed > 1)
        return captured;
    p = pad_for(id);
    if (!p)
        return captured;
    bit = SPO_BIT(button);
    if (!!(p->held & bit) == !!pressed) {
        if (!pressed && !p->held && !p->axes)
            p->ready = 1;
        return captured;
    }
    if (pressed) {
        if (button == SPO_GUIDE) {
            p->armed = p->ready && !p->held && !p->axes && !spo->drain;
            p->guide = 1;
        } else if (p->guide)
            p->armed = 0;
        p->held |= bit;
        if (spo->open && button != SPO_GUIDE) {
            spo_menu_button(button);
            if (button >= SPO_UP && button <= SPO_RIGHT) {
                p->repeat_button = button;
                p->repeat_at = spo_now() + UINT64_C(300000000);
            }
        }
    } else {
        p->held &= ~bit;
        if (button == SPO_GUIDE) {
            int click = p->guide && p->armed;
            p->guide = p->armed = 0;
            if (click) {
                spo_set_open(!spo->open);
                captured = 1;
            }
        }
        if (p->repeat_button == button)
            p->repeat_at = 0;
    }
    if (!p->held && !p->axes)
        p->ready = 1;
    if (!spo->open && all_neutral())
        spo->drain = 0;
    return captured || spo->open;
}
int32_t spo_handle_axis(uint32_t id, spo_axis axis, int32_t value) {
    spo_pad *p;
    int magnitude, was;
    if (!spo || spo->in_callback)
        return 0;
    if (axis >= SPO_AXIS_COUNT || value < (axis >= SPO_LT ? 0 : -32768) || value > 32767)
        return spo_input_captured();
    p = pad_for(id);
    if (!p)
        return spo_input_captured();
    was = spo_input_captured();
    magnitude = value < 0 ? -value : value;
    p->axis[axis] = value;
    if (magnitude >= 6554) {
        p->axes |= SPO_BIT(axis);
        if (p->guide)
            p->armed = 0;
    } else if (magnitude <= 4915)
        p->axes &= ~SPO_BIT(axis);
    if (!p->held && !p->axes)
        p->ready = 1;
    if (!spo->open && all_neutral())
        spo->drain = 0;
    return was;
}
void spo_input_tick(uint64_t now) {
    uint32_t i;
    if (!spo->open)
        return;
    for (i = 0; i < 4; i++) {
        spo_pad *p = &spo->pads[i];
        uint32_t opposite;
        if (!p->repeat_at || now < p->repeat_at)
            continue;
        opposite = p->repeat_button == SPO_UP     ? SPO_DOWN
                   : p->repeat_button == SPO_DOWN ? SPO_UP
                   : p->repeat_button == SPO_LEFT ? SPO_RIGHT
                                                  : SPO_LEFT;
        if (!(p->held & SPO_BIT(opposite))) {
            int repeat =
                p->repeat_button == SPO_UP || p->repeat_button == SPO_DOWN || spo->cursor < 0;
            if (!spo->detail && spo->root == 0 && spo->cursor >= 3)
                repeat = 1;
            if (!spo->detail && spo->root > 0) {
                spo_option *o = spo_menu_option(spo->cursor);
                if (o && o->desc.type == SPO_INTEGER)
                    repeat = 1;
            }
            if (repeat)
                spo_menu_button(p->repeat_button);
        }
        p->repeat_at = now + UINT64_C(100000000);
    }
}
