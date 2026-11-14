/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const keys[SPO_ITEM_COUNT] = {
    "fps",         "frame_time", "graph",         "low_1",     "system_cpu",
    "process_cpu", "gpu_load",   "cpu_clock",     "gpu_clock", "temperature",
    "battery",     "rss",        "ram",       "self_cost", "charging"};
static const char *const anchors[] = {"top_left",    "top_center",    "top_right",
                                      "center_left", "center",        "center_right",
                                      "bottom_left", "bottom_center", "bottom_right"};
static char *trim(char *s) {
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\r')
        s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        *--end = 0;
    return s;
}
static spo_ini_entry *entry(const char *section, const char *key) {
    uint32_t i;
    for (i = 0; i < spo->ini_count; i++)
        if (!strcmp(spo->ini[i].section, section) && !strcmp(spo->ini[i].key, key))
            return &spo->ini[i];
    return NULL;
}
static int put_text(const char *section, const char *key, const char *value) {
    spo_ini_entry *e = entry(section, key);
    if (strlen(section) > 64 || strlen(key) > 64 || strlen(value) > 160)
        return 0;
    if (!e) {
        if (spo->ini_count == SPO_INI_ENTRIES)
            return 0;
        e = &spo->ini[spo->ini_count++];
    }
    spo_text_copy(e->section, sizeof(e->section), section);
    spo_text_copy(e->key, sizeof(e->key), key);
    spo_text_copy(e->value, sizeof(e->value), value);
    return 1;
}
int spo_ini_has(const char *section, const char *key) {
    return entry(section, key) != NULL;
}
int spo_ini_get(const char *section, const char *key, int32_t *out) {
    spo_ini_entry *e = entry(section, key);
    char *end;
    long value;
    if (!e || !e->value[0])
        return 0;
    errno = 0;
    value = strtol(e->value, &end, 10);
    if (errno || *end || value < INT32_MIN || value > INT32_MAX)
        return 0;
    *out = (int32_t)value;
    return 1;
}
int spo_ini_put(const char *section, const char *key, int32_t value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    return put_text(section, key, buf);
}
static uint32_t get_range(const char *key, uint32_t def, uint32_t min, uint32_t max) {
    int32_t v;
    if (!entry("spo", key))
        return def;
    if (!spo_ini_get("spo", key, &v) || (int64_t)v < min || (uint64_t)v > max) {
        spo->error = SPO_INVALID;
        return def;
    }
    return (uint32_t)v;
}
spo_result spo_config_load(void) {
    int fd;
    size_t used = 0;
    ssize_t n;
    char section[65] = "", *line, *next;
    uint32_t i;
    spo_ini_entry *a;
    if (!spo->path[0])
        return SPO_OK;
    fd = open(spo->path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT ? SPO_OK : spo_fail(SPO_IO);
    while (used < sizeof(spo->ini_buffer)) {
        n = read(fd, spo->ini_buffer + used, sizeof(spo->ini_buffer) - used);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            close(fd);
            return spo_fail(SPO_IO);
        }
        if (!n)
            break;
        used += (size_t)n;
    }
    close(fd);
    if (used >= sizeof(spo->ini_buffer))
        return spo_fail(SPO_CAPACITY);
    spo->ini_buffer[used] = 0;
    line = spo->ini_buffer;
    while (line && *line) {
        char *text, *eq;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        text = trim(line);
        if (*text == '[') {
            char *end = strchr(text + 1, ']');
            if (!end || *trim(end + 1) || end == text + 1) {
                spo->error = SPO_INVALID;
                section[0] = 0;
            } else {
                *end = 0;
                if (!spo_text_copy(section, sizeof(section), trim(text + 1)))
                    return spo_fail(SPO_CAPACITY);
            }
        } else if (*text && *text != ';' && *text != '#') {
            eq = strchr(text, '=');
            if (!eq || !section[0])
                spo->error = SPO_INVALID;
            else {
                *eq = 0;
                if (!*trim(text))
                    spo->error = SPO_INVALID;
                else if (!put_text(section, trim(text), trim(eq + 1)))
                    return spo_fail(SPO_CAPACITY);
            }
        }
        line = next;
    }
    {
        int32_t format;
        if (entry("spo", "format") && (!spo_ini_get("spo", "format", &format) || format != 1))
            return spo_fail(SPO_ABI);
    }
    spo_apply_preset(get_range("preset", 0, 0, 4));
    spo->overlay.custom = get_range("custom", 0, 0, 1);
    spo->overlay.font_scale = get_range("font_scale", 2, 1, 4);
    spo->overlay.background_opacity = get_range("background_opacity", 65, 0, 100);
    spo->overlay.text_opacity = get_range("text_opacity", 100, 10, 100);
    spo->overlay.graph_samples = get_range("graph_samples", 120, 30, 120);
    if (spo->overlay.graph_samples % 30) {
        spo->overlay.graph_samples = 120;
        spo->error = SPO_INVALID;
    }
    spo->fps_config_low = get_range("fps_low", 30, 1, 1000);
    spo->fps_config_high = get_range("fps_high", 60, 1, 1000);
    if (spo->fps_config_low >= spo->fps_config_high) {
        spo->fps_config_low = 30;
        spo->fps_config_high = 60;
        spo->error = SPO_INVALID;
    }
    spo->fps_low = spo->fps_config_low;
    spo->fps_high = spo->fps_config_high;
    a = entry("spo", "anchor");
    if (a) {
        for (i = 0; i < 9; i++)
            if (!strcmp(a->value, anchors[i])) {
                spo->overlay.anchor = i;
                break;
            }
        if (i == 9)
            spo->error = SPO_INVALID;
    }
    if (spo->overlay.custom)
        for (i = 0; i < SPO_ITEM_COUNT; i++) {
            int32_t value;
            if (i == SPO_PROCESS_CPU)
                continue;
            if (i == SPO_CHARGING && !spo_ini_has("spo.items", keys[i]))
                value = !!(spo->overlay.items & SPO_BIT(SPO_BATTERY));
            else if (!spo_ini_get("spo.items", keys[i], &value))
                continue;
            if (value == 0 || value == 1) {
                if (value)
                    spo->overlay.items |= SPO_BIT(i);
                else
                    spo->overlay.items &= ~SPO_BIT(i);
            }
        }
    return SPO_OK;
}
int32_t spo_config_dirty(void) {
    return spo ? spo->dirty : 0;
}
static int snapshot(void) {
    uint32_t i;
    char section[65];
    spo_overlay_settings *o = &spo->overlay;
    if (!spo_ini_put("spo", "format", 1) || !spo_ini_put("spo", "preset", (int32_t)o->preset) ||
        !spo_ini_put("spo", "custom", (int32_t)o->custom) ||
        !put_text("spo", "anchor", anchors[o->anchor]) ||
        !spo_ini_put("spo", "font_scale", (int32_t)o->font_scale) ||
        !spo_ini_put("spo", "background_opacity", (int32_t)o->background_opacity) ||
        !spo_ini_put("spo", "text_opacity", (int32_t)o->text_opacity) ||
        !spo_ini_put("spo", "graph_samples", (int32_t)o->graph_samples) ||
        !spo_ini_put("spo", "fps_low", (int32_t)spo->fps_config_low) ||
        !spo_ini_put("spo", "fps_high", (int32_t)spo->fps_config_high))
        return 0;
    for (i = 0; i < SPO_ITEM_COUNT; i++)
        if (i != SPO_PROCESS_CPU && !spo_ini_put("spo.items", keys[i], !!(o->items & SPO_BIT(i))))
            return 0;
    for (i = 0; i < spo->counter_count; i++)
        if (!spo_ini_put("spo.counters", spo->counters[i].key, !!(spo->counter_mask & SPO_BIT(i))))
            return 0;
    for (i = 0; i < spo->indicator_count; i++)
        if (!spo_ini_put("spo.indicators", spo->indicators[i].key,
                         !!(spo->indicator_mask & SPO_BIT(i))))
            return 0;
    for (i = 0; i < spo->option_count; i++)
        if (spo->options[i].desc.type != SPO_ACTION) {
            snprintf(section, sizeof(section), "port.%s",
                     spo->pages[spo->options[i].desc.page - 1].key);
            if (!spo_ini_put(section, spo->options[i].key, spo->options[i].value))
                return 0;
        }
    return 1;
}
spo_result spo_config_save(void) {
    char temp[4120], directory[4096];
    int fd = -1, dir = -1, renamed = 0;
    size_t used = 0, written = 0;
    uint32_t i;
    spo_result result = SPO_IO;
    if (!spo || spo->in_callback)
        return SPO_NOT_INITIALIZED;
    if (!spo->path[0]) {
        spo->dirty = 0;
        return SPO_OK;
    }
    if (!snapshot())
        return spo_fail(SPO_CAPACITY);
    for (i = 0; i < spo->ini_count; i++) {
        uint32_t j;
        int n;
        for (j = 0; j < i; j++)
            if (!strcmp(spo->ini[j].section, spo->ini[i].section))
                break;
        if (j < i)
            continue;
        n = snprintf(spo->ini_buffer + used, sizeof(spo->ini_buffer) - used, "[%s]\n",
                     spo->ini[i].section);
        if (n < 0 || (size_t)n >= sizeof(spo->ini_buffer) - used)
            return spo_fail(SPO_CAPACITY);
        used += (size_t)n;
        for (j = i; j < spo->ini_count; j++)
            if (!strcmp(spo->ini[j].section, spo->ini[i].section)) {
                n = snprintf(spo->ini_buffer + used, sizeof(spo->ini_buffer) - used, "%s=%s\n",
                             spo->ini[j].key, spo->ini[j].value);
                if (n < 0 || (size_t)n >= sizeof(spo->ini_buffer) - used)
                    return spo_fail(SPO_CAPACITY);
                used += (size_t)n;
            }
    }
    snprintf(temp, sizeof(temp), "%s.tmp.XXXXXX", spo->path);
    fd = mkstemp(temp);
    if (fd < 0)
        return spo_fail(SPO_IO);
    while (written < used) {
        ssize_t n = write(fd, spo->ini_buffer + written, used - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            goto done;
        written += (size_t)n;
    }
    if (fsync(fd))
        goto done;
    if (close(fd)) {
        fd = -1;
        goto done;
    }
    fd = -1;
    if (rename(temp, spo->path))
        goto done;
    renamed = 1;
    spo_text_copy(directory, sizeof(directory), spo->path);
    {
        char *slash = strrchr(directory, '/');
        if (slash) {
            if (slash == directory)
                slash[1] = 0;
            else
                *slash = 0;
        } else
            spo_text_copy(directory, sizeof(directory), ".");
    }
    dir = open(directory, O_RDONLY | O_CLOEXEC);
    if (dir < 0)
        goto done;
    if (fsync(dir) && errno != EINVAL && errno != ENOTSUP)
        goto done;
    spo->dirty = 0;
    spo->error = SPO_OK;
    result = SPO_OK;
done:
    if (fd >= 0)
        close(fd);
    if (dir >= 0)
        close(dir);
    if (!renamed)
        unlink(temp);
    return result == SPO_OK ? result : spo_fail(result);
}
