/* SPDX-License-Identifier: MIT */
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
        if (!spo->pads[i].held && !neutral)
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
        if (spo->pads[i].held)
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
    if (open) {
        spo->tab = 0;
        spo_menu_home();
    } else {
        spo->drain = !all_neutral();
        if (spo->dirty && spo->path[0])
            (void)spo_config_save();
    }
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
    p->armed = 0;
    p->guide = !!(buttons & SPO_BIT(SPO_GUIDE));
    p->repeat_at = 0;
    for (i = 0; i < SPO_AXIS_COUNT; i++) {
        p->axis[i] = axes ? axes[i] : 0;
        p->guide_axis[i] = p->axis[i];
    }
    p->ready = !p->held;
    if (!spo->open && all_neutral())
        spo->drain = 0;
    return SPO_OK;
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
        if (!pressed && !p->held)
            p->ready = 1;
        return captured;
    }
    if (pressed) {
        if (button == SPO_GUIDE) {
            p->armed = p->ready && !p->held && !spo->drain;
            p->guide = 1;
            memcpy(p->guide_axis, p->axis, sizeof(p->axis));
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
    if (!p->held)
        p->ready = 1;
    if (!spo->open && all_neutral())
        spo->drain = 0;
    return captured || spo->open;
}
int32_t spo_handle_axis(uint32_t id, spo_axis axis, int32_t value) {
    spo_pad *p;
    int was;
    if (!spo || spo->in_callback)
        return 0;
    if (axis >= SPO_AXIS_COUNT || value < (axis >= SPO_LT ? 0 : -32768) || value > 32767)
        return spo_input_captured();
    p = pad_for(id);
    if (!p)
        return spo_input_captured();
    was = spo_input_captured();
    p->axis[axis] = value;
    /* Sticks may rest off centre, so only movement while Guide is held cancels the tap. */
    if (p->guide && (value - p->guide_axis[axis] >= 6554 || p->guide_axis[axis] - value >= 6554))
        p->armed = 0;
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
        if (!(p->held & SPO_BIT(opposite)) && spo_menu_repeats(p->repeat_button))
            spo_menu_button(p->repeat_button);
        p->repeat_at = now + UINT64_C(100000000);
    }
}
