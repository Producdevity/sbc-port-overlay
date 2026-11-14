/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

spo_context *spo;
_Static_assert(sizeof(spo_vertex) == 20, "vertex ABI");
_Static_assert(sizeof(spo_context) < 1024 * 1024, "CPU storage budget");
static uint64_t clock_ns(clockid_t clock) {
    struct timespec t;
    if (clock_gettime(clock, &t))
        return 0;
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
uint64_t spo_now(void) {
    return clock_ns(CLOCK_MONOTONIC);
}
uint64_t spo_cpu_now(void) {
    return clock_ns(CLOCK_THREAD_CPUTIME_ID);
}
int spo_text_copy(char *out, size_t capacity, const char *in) {
    size_t n;
    if (!in)
        in = "";
    n = strlen(in);
    if (n >= capacity)
        return 0;
    memcpy(out, in, n + 1);
    return 1;
}
int spo_key_valid(const char *key) {
    size_t i, n = key ? strlen(key) : 0;
    if (!n || n > 32)
        return 0;
    for (i = 0; i < n; i++)
        if (!((key[i] >= 'a' && key[i] <= 'z') || (key[i] >= '0' && key[i] <= '9') ||
              key[i] == '_' || key[i] == '-'))
            return 0;
    return 1;
}
spo_result spo_fail(spo_result result) {
    if (spo)
        spo->error = result;
    return result;
}
const char *spo_version(void) {
    return SPO_VERSION;
}
spo_result spo_last_error(void) {
    return spo ? spo->error : SPO_NOT_INITIALIZED;
}
static int usable(void) {
    return spo && !spo->in_callback;
}
static int registering(void) {
    return usable() && !spo->frozen;
}

uint32_t spo_preset_items(uint32_t p) {
    uint32_t mask = 0;
    if (p >= 1)
        mask |= SPO_BIT(SPO_FPS);
    if (p >= 2)
        mask |= SPO_BIT(SPO_FRAME_TIME);
    if (p == 3)
        mask |= SPO_BIT(SPO_GRAPH) | SPO_BIT(SPO_SYSTEM_CPU) | SPO_BIT(SPO_GPU_LOAD);
    if (p == 4)
        mask = SPO_BIT(SPO_ITEM_COUNT) - 1;
    return mask;
}
void spo_apply_preset(uint32_t p) {
    spo->overlay.preset = p;
    spo->overlay.custom = 0;
    spo->overlay.items = spo_preset_items(p);
    spo->counter_mask = p == 4 ? 255 : 0;
    spo->indicator_mask = p == 4 ? 255 : 0;
    spo_metrics_request();
}
/* The preset field names the preset the toggles match; custom is set when none matches. */
void spo_overlay_sync(void) {
    static const uint32_t sensors = SPO_BIT(SPO_SYSTEM_CPU) | SPO_BIT(SPO_PROCESS_CPU) |
                                    SPO_BIT(SPO_GPU_LOAD) | SPO_BIT(SPO_CPU_CLOCK) |
                                    SPO_BIT(SPO_GPU_CLOCK) | SPO_BIT(SPO_TEMPERATURE) |
                                    SPO_BIT(SPO_BATTERY) | SPO_BIT(SPO_RSS) |
                                    SPO_BIT(SPO_MEM_AVAILABLE) | SPO_BIT(SPO_CHARGING);
    uint32_t compared = ~(sensors & ~spo->available_items), p, i;
    uint32_t counters = SPO_BIT(spo->counter_count) - 1, indicators = SPO_BIT(spo->indicator_count) - 1;
    for (i = 0; i < 5; i++) {
        p = (spo->overlay.preset + i) % 5;
        if (((spo->overlay.items ^ spo_preset_items(p)) & compared) == 0 &&
            (spo->counter_mask & counters) == (p == 4 ? counters : 0) &&
            (spo->indicator_mask & indicators) == (p == 4 ? indicators : 0)) {
            spo->overlay.preset = p;
            spo->overlay.custom = 0;
            return;
        }
    }
    spo->overlay.custom = 1;
}
spo_result spo_init(const spo_config *c) {
    spo_result result;
    if (spo)
        return SPO_ALREADY_INITIALIZED;
    if (!c || c->struct_size != sizeof(*c) || c->abi_version != SPO_ABI_VERSION)
        return SPO_ABI;
    if (c->flags & ~(SPO_INIT_NO_RENDER | SPO_INIT_NO_SAMPLER_THREAD))
        return SPO_INVALID;
    if (!(c->flags & SPO_INIT_NO_RENDER) && !c->get_proc_address)
        return SPO_INVALID;
    spo = calloc(1, sizeof(*spo));
    if (!spo)
        return SPO_CAPACITY;
    spo->config = *c;
    spo_text_copy(spo->renderer_name, sizeof(spo->renderer_name), "Headless");
    spo->cursor = 0;
    spo->graph_max = 33.333333;
    spo->graph_serial = UINT64_MAX;
    spo->overlay =
        (spo_overlay_settings){sizeof(spo_overlay_settings), 0, 0, 0, 2, 65, 100, 120, 0};
    spo->stats.struct_size = sizeof(spo_stats);
    if (!spo_text_copy(spo->path, sizeof(spo->path), c->config_path) ||
        !spo_text_copy(spo->name, sizeof(spo->name), c->port_name) ||
        !spo_text_copy(spo->version, sizeof(spo->version), c->port_version)) {
        spo_shutdown();
        return SPO_CAPACITY;
    }
    result = spo_config_load();
    if (result == SPO_CAPACITY || result == SPO_ABI) {
        spo_shutdown();
        return result;
    }
    result = spo_metrics_init();
    if (result != SPO_OK) {
        spo_shutdown();
        return result;
    }
    spo_overlay_sync();
    if (!(c->flags & SPO_INIT_NO_RENDER)) {
        result = spo_backend_init();
        if (result != SPO_OK) {
            spo_shutdown();
            return result;
        }
    }
    return SPO_OK;
}
void spo_shutdown(void) {
    if (!spo || spo->in_callback)
        return;
    if (spo->open)
        spo_set_open(0);
    spo_metrics_close();
    spo_backend_shutdown();
    free(spo);
    spo = NULL;
}
void spo_context_lost(void) {
    if (usable())
        spo->lost = 1;
}
void spo_reset_timing(void) {
    if (!usable())
        return;
    spo->last_stamp = spo->window_ns = spo->refresh_stamp = 0;
    spo->history_count = spo->history_next = spo->window_frames = 0;
    spo->fps = spo->frame_ms = spo->low_1 = 0;
    spo->formatted_at = spo->mesh_formatted_at = spo->format_checked_at = 0;
    spo->history_serial = 0;
    spo->graph_serial = UINT64_MAX;
    spo->graph_max = 33.333333;
    spo->graph_range_at = spo->graph_high_at = 0;
    spo->target_locked = 0;
    spo->target_candidate = 0;
    spo->target_since = spo->low_floor = 0;
    memset(spo->tone_ready, 0, sizeof(spo->tone_ready));
    memset(spo->bar_tone, 0, sizeof(spo->bar_tone));
    spo_metrics_reset();
}
spo_result spo_register_page(const spo_page_desc *d, spo_id *id) {
    uint32_t i;
    spo_page *p;
    if (!spo)
        return SPO_NOT_INITIALIZED;
    if (!registering())
        return spo_fail(SPO_REGISTRATION_CLOSED);
    if (!d || d->struct_size != sizeof(*d) || !id || !spo_key_valid(d->key))
        return spo_fail(SPO_INVALID);
    if (spo->page_count == 8)
        return spo_fail(SPO_CAPACITY);
    for (i = 0; i < spo->page_count; i++)
        if (!strcmp(spo->pages[i].key, d->key))
            return spo_fail(SPO_INVALID);
    p = &spo->pages[spo->page_count];
    if (!spo_text_copy(p->key, sizeof(p->key), d->key) ||
        !spo_text_copy(p->label, sizeof(p->label), d->label))
        return spo_fail(SPO_CAPACITY);
    p->first = spo->option_count;
    *id = ++spo->page_count;
    return SPO_OK;
}
static int valid_option(const spo_option_desc *d, int32_t value) {
    if (d->type == SPO_TOGGLE)
        return value == 0 || value == 1;
    if (d->type == SPO_CHOICE)
        return value >= 0 && (uint32_t)value < d->choice_count;
    if (d->type == SPO_INTEGER)
        return d->step > 0 && value >= d->minimum && value <= d->maximum &&
               ((int64_t)value - d->minimum) % d->step == 0;
    return d->type == SPO_ACTION;
}
spo_result spo_register_option(const spo_option_desc *d, spo_id *id) {
    spo_option *o;
    spo_page *p;
    uint32_t i;
    int32_t saved;
    char section[65];
    if (!spo)
        return SPO_NOT_INITIALIZED;
    if (!registering())
        return spo_fail(SPO_REGISTRATION_CLOSED);
    if (!d || d->struct_size != sizeof(*d) || !id || !d->page || d->page > spo->page_count ||
        !spo_key_valid(d->key) || d->type > SPO_ACTION || d->choice_count > 16 ||
        (d->choice_count && !d->choices) ||
        (d->type == SPO_CHOICE && (!d->choices || !d->choice_count)) ||
        (d->type == SPO_INTEGER && (d->minimum > d->maximum || d->step <= 0 ||
                                    ((int64_t)d->maximum - d->minimum) % d->step)) ||
        !valid_option(d, d->default_value))
        return spo_fail(SPO_INVALID);
    p = &spo->pages[d->page - 1];
    if (p->count == 8 || spo->option_count == 64)
        return spo_fail(SPO_CAPACITY);
    for (i = 0; i < spo->option_count; i++)
        if (spo->options[i].desc.page == d->page && !strcmp(spo->options[i].key, d->key))
            return spo_fail(SPO_INVALID);
    o = &spo->options[spo->option_count];
    memset(o, 0, sizeof(*o));
    o->desc = *d;
    if (!spo_text_copy(o->key, sizeof(o->key), d->key) ||
        !spo_text_copy(o->label, sizeof(o->label), d->label) ||
        !spo_text_copy(o->help, sizeof(o->help), d->help))
        return spo_fail(SPO_CAPACITY);
    for (i = 0; i < d->choice_count; i++)
        if (!spo_text_copy(o->choices[i], 25, d->choices[i]))
            return spo_fail(SPO_CAPACITY);
    o->value = d->default_value;
    snprintf(section, sizeof(section), "port.%s", p->key);
    if (d->type != SPO_ACTION && spo_ini_has(section, d->key)) {
        if (spo_ini_get(section, d->key, &saved) && valid_option(d, saved))
            o->value = saved;
        else
            spo->error = SPO_INVALID;
    }
    if (d->type != SPO_ACTION && d->on_change) {
        spo_result result;
        spo->in_callback = 1;
        result = d->on_change(spo->option_count + 1, o->value, d->user);
        spo->in_callback = 0;
        if (result != SPO_OK)
            return spo_fail(SPO_REJECTED);
    }
    p->count++;
    *id = ++spo->option_count;
    return SPO_OK;
}
spo_result spo_option_get(spo_id id, int32_t *value) {
    if (!spo)
        return SPO_NOT_INITIALIZED;
    if (!id || id > spo->option_count || !value)
        return spo_fail(SPO_INVALID);
    *value = spo->options[id - 1].value;
    return SPO_OK;
}
spo_result spo_option_set(spo_id id, int32_t value) {
    spo_option *o;
    if (!usable())
        return spo_fail(SPO_NOT_INITIALIZED);
    if (!id || id > spo->option_count)
        return spo_fail(SPO_INVALID);
    o = &spo->options[id - 1];
    if (o->desc.type == SPO_ACTION || !valid_option(&o->desc, value))
        return spo_fail(SPO_INVALID);
    if (o->value == value)
        return SPO_OK;
    if (o->desc.on_change) {
        spo_result result;
        spo->in_callback = 1;
        result = o->desc.on_change(id, value, o->desc.user);
        spo->in_callback = 0;
        if (result != SPO_OK)
            return spo_fail(SPO_REJECTED);
    }
    o->value = value;
    spo->dirty = 1;
    return SPO_OK;
}
spo_result spo_register_counter(const spo_counter_desc *d, spo_id *id) {
    uint32_t i;
    int32_t saved;
    spo_counter *c;
    if (!spo)
        return SPO_NOT_INITIALIZED;
    if (!registering())
        return spo_fail(SPO_REGISTRATION_CLOSED);
    if (!d || d->struct_size != sizeof(*d) || !id || !spo_key_valid(d->key) || d->decimals > 3)
        return spo_fail(SPO_INVALID);
    if (spo->counter_count == 8)
        return spo_fail(SPO_CAPACITY);
    for (i = 0; i < spo->counter_count; i++)
        if (!strcmp(spo->counters[i].key, d->key))
            return spo_fail(SPO_INVALID);
    c = &spo->counters[spo->counter_count];
    if (!spo_text_copy(c->key, 33, d->key) || !spo_text_copy(c->label, 11, d->label) ||
        !spo_text_copy(c->unit, 5, d->unit))
        return spo_fail(SPO_CAPACITY);
    c->decimals = d->decimals;
    if (spo->overlay.custom && spo_ini_has("spo.counters", d->key)) {
        if (!spo_ini_get("spo.counters", d->key, &saved) || (saved != 0 && saved != 1)) {
            spo->error = SPO_INVALID;
        } else {
            if (saved)
                spo->counter_mask |= SPO_BIT(spo->counter_count);
            else
                spo->counter_mask &= ~SPO_BIT(spo->counter_count);
        }
    }
    *id = ++spo->counter_count;
    spo_overlay_sync();
    return SPO_OK;
}
spo_result spo_register_indicator(const spo_indicator_desc *d, spo_id *id) {
    uint32_t i;
    int32_t saved;
    spo_indicator *c;
    if (!spo)
        return SPO_NOT_INITIALIZED;
    if (!registering())
        return spo_fail(SPO_REGISTRATION_CLOSED);
    if (!d || d->struct_size != sizeof(*d) || !id || !spo_key_valid(d->key))
        return spo_fail(SPO_INVALID);
    if (spo->indicator_count == 8)
        return spo_fail(SPO_CAPACITY);
    for (i = 0; i < spo->indicator_count; i++)
        if (!strcmp(spo->indicators[i].key, d->key))
            return spo_fail(SPO_INVALID);
    c = &spo->indicators[spo->indicator_count];
    if (!spo_text_copy(c->key, 33, d->key) || !spo_text_copy(c->label, 17, d->label))
        return spo_fail(SPO_CAPACITY);
    if (spo->overlay.custom && spo_ini_has("spo.indicators", d->key)) {
        if (!spo_ini_get("spo.indicators", d->key, &saved) || (saved != 0 && saved != 1)) {
            spo->error = SPO_INVALID;
        } else {
            if (saved)
                spo->indicator_mask |= SPO_BIT(spo->indicator_count);
            else
                spo->indicator_mask &= ~SPO_BIT(spo->indicator_count);
        }
    }
    *id = ++spo->indicator_count;
    spo_overlay_sync();
    return SPO_OK;
}
spo_result spo_counter_set(spo_id id, double value) {
    if (!usable())
        return SPO_NOT_INITIALIZED;
    if (!id || id > spo->counter_count || isinf(value))
        return spo_fail(SPO_INVALID);
    spo->counters[id - 1].value = value;
    spo->counters[id - 1].valid = !isnan(value);
    return SPO_OK;
}
spo_result spo_indicator_set(spo_id id, uint32_t on) {
    if (!usable())
        return SPO_NOT_INITIALIZED;
    if (!id || id > spo->indicator_count || on > 1)
        return spo_fail(SPO_INVALID);
    spo->indicators[id - 1].value = on;
    spo->indicators[id - 1].valid = 1;
    return SPO_OK;
}
spo_result spo_overlay_get(spo_overlay_settings *out) {
    if (!spo)
        return SPO_NOT_INITIALIZED;
    if (!out || out->struct_size != sizeof(*out))
        return spo_fail(SPO_ABI);
    *out = spo->overlay;
    return SPO_OK;
}
spo_result spo_overlay_set(const spo_overlay_settings *in) {
    if (!usable())
        return SPO_NOT_INITIALIZED;
    if (!in || in->struct_size != sizeof(*in) || in->preset > 4 || in->custom > 1 ||
        in->anchor > 8 || in->font_scale < 1 || in->font_scale > 4 ||
        in->background_opacity > 100 || in->text_opacity < 10 || in->text_opacity > 100 ||
        in->graph_samples < 30 || in->graph_samples > 120 || in->graph_samples % 30 ||
        in->items >= SPO_BIT(SPO_ITEM_COUNT))
        return spo_fail(SPO_INVALID);
    if (in->items != spo->overlay.items) {
        spo_metrics_reset();
    }
    {
        uint32_t preset = spo->overlay.preset;
        spo->overlay = *in;
        spo->overlay.preset = preset;
    }
    spo_overlay_sync();
    spo->dirty = 1;
    spo_metrics_request();
    return SPO_OK;
}
spo_result spo_target_fps_set(uint32_t fps) {
    if (!usable())
        return SPO_NOT_INITIALIZED;
    if (fps && (fps < 10 || fps > 1000))
        return spo_fail(SPO_INVALID);
    spo->host_target = fps;
    spo->formatted_at = 0;
    return SPO_OK;
}
spo_result spo_preset_set(uint32_t preset) {
    if (!usable())
        return SPO_NOT_INITIALIZED;
    if (preset > 4)
        return spo_fail(SPO_INVALID);
    spo_apply_preset(preset);
    spo->dirty = 1;
    spo_metrics_reset();
    return SPO_OK;
}
void spo_numeric_refresh(uint64_t now) {
    if (spo->window_ns >= UINT64_C(500000000)) {
        spo->fps = (double)spo->window_frames * 1e9 / (double)spo->window_ns;
        spo->frame_ms = 1000.0 / spo->fps;
        spo->window_ns = 0;
        spo->window_frames = 0;
        spo->refresh_stamp = now;
        {
            uint32_t frames = spo->history_serial > spo->low_floor
                                  ? (uint32_t)(spo->history_serial - spo->low_floor)
                                  : 0;
            if (frames > spo->history_count)
                frames = spo->history_count;
            spo->low_1 = 0;
            if ((spo->overlay.items & SPO_BIT(SPO_LOW_1)) && frames >= 120) {
                uint64_t worst[6] = {0};
                uint32_t i, j, n = (frames + 99) / 100;
                double sum = 0;
                for (i = 0; i < frames; i++) {
                    uint64_t value = spo->intervals[(spo->history_next + SPO_HISTORY - 1 - i) % SPO_HISTORY];
                    for (j = 0; j < n; j++)
                        if (value > worst[j]) {
                            uint64_t old = worst[j];
                            worst[j] = value;
                            value = old;
                        }
                }
                for (j = 0; j < n; j++)
                    sum += (double)worst[j];
                spo->low_1 = sum > 0 ? 1e9 * (double)n / sum : 0;
            }
        }
    }
}
spo_result spo_get_stats(spo_stats *out) {
    if (!spo)
        return SPO_NOT_INITIALIZED;
    if (!out || out->struct_size != sizeof(*out))
        return spo_fail(SPO_ABI);
    *out = spo->stats;
    return SPO_OK;
}
void spo_history_push(uint64_t interval) {
    spo->history_serial++;
    spo->intervals[spo->history_next] = interval;
    spo->bar_tone[spo->history_next] = 0;
    spo->history_next = (spo->history_next + 1) % SPO_HISTORY;
    if (spo->history_count < SPO_HISTORY)
        spo->history_count++;
}
spo_result spo_frame(uint32_t width, uint32_t height) {
    uint64_t now, start, cpu, end;
    spo_result result = SPO_OK;
    uint32_t draws = 0;
    if (!usable())
        return SPO_NOT_INITIALIZED;
    if (spo->lost)
        return spo_fail(SPO_CONTEXT_LOST);
    spo->frozen = 1;
    if (!width || !height) {
        spo_reset_timing();
        return SPO_OK;
    }
    if (width > 32768 || height > 32768)
        return spo_fail(SPO_INVALID);
    start = spo_now();
    cpu = spo_cpu_now();
    now = start;
    spo->frame_reads = 0;
    if (spo->last_stamp && now > spo->last_stamp) {
        uint64_t dt = now - spo->last_stamp;
        spo_history_push(dt);
        spo->window_ns += dt;
        spo->window_frames++;
    }
    spo->last_stamp = now;
    spo_numeric_refresh(now);
    spo_input_tick(now);
    spo_metrics_request();
    spo_metrics_collect(now);
    if (spo->overlay.items || spo->counter_mask || spo->indicator_mask || spo->open) {
        result = spo_layout(width, height);
        if (result == SPO_OK && spo->mesh.count && !(spo->config.flags & SPO_INIT_NO_RENDER))
            result = spo_backend_draw(&spo->mesh, width, height, &draws);
    } else
        spo->mesh.count = 0;
    end = spo_cpu_now();
    {
        double c = (double)(end - cpu) / 1000.0, w = (double)(spo_now() - start) / 1000.0;
        spo->cpu_sum += c;
        spo->wall_sum += w;
        if (c > spo->cpu_max)
            spo->cpu_max = c;
        spo->stats_frames++;
        if (!spo->stats_window)
            spo->stats_window = now;
        if (now - spo->stats_window >= UINT64_C(500000000)) {
            spo->stats.cpu_us_mean = spo->cpu_sum / spo->stats_frames;
            spo->stats.wall_us_mean = spo->wall_sum / spo->stats_frames;
            spo->stats.cpu_us_max = spo->cpu_max;
            spo->stats.sample_frames = spo->stats_frames;
            spo->cost_frame_ns = (double)(now - spo->stats_window) / spo->stats_frames;
            spo->cost_render_percent = spo->cpu_sum * 100000.0 / (double)(now - spo->stats_window);
            spo_metrics_cost();
            spo->cpu_sum = spo->wall_sum = spo->cpu_max = 0;
            spo->stats_frames = 0;
            spo->stats_window = now;
        }
    }
    spo->stats.draw_calls = draws;
    spo->stats.vertices = spo->mesh.count;
    spo->stats.telemetry_reads = spo->frame_reads;
    return result == SPO_OK ? SPO_OK : spo_fail(result);
}
