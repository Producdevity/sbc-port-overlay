/* SPDX-License-Identifier: MIT */
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEXT 16384
typedef struct {
    char kind, name[80], label[32], help[96], type[16], choices[512];
    int32_t def, min, max, step;
} section;
static spo_id page_ids[8];
static char page_keys[8][33];
static uint32_t page_count;
static int armed;
static int errors;

static spo_result changed(spo_id id, int32_t value, void *user) {
    (void)id, (void)value, (void)user;
    if (armed)
        spo_notice("Applies at the next start");
    return SPO_OK;
}
static char *trim(char *s) {
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\r')
        s++;
    for (end = s + strlen(s); end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r');)
        *--end = 0;
    return s;
}
static void copy(char *out, size_t size, const char *in) {
    snprintf(out, size, "%s", in);
}
static int number(const char *s, int32_t *out) {
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || *end || v < INT32_MIN || v > INT32_MAX)
        return 0;
    *out = (int32_t)v;
    return 1;
}
static void fail(unsigned line, const char *what) {
    fprintf(stderr, "spo settings: line %u: %s\n", line, what);
    errors++;
}
static void flush(section *s, unsigned line) {
    spo_option_desc d = {sizeof d, 0, SPO_TOGGLE, NULL, NULL, NULL, 0, 0, 1, 1, NULL, 0,
                         changed, NULL, NULL};
    const char *choices[16];
    char *p, *dot, *rest, key[33];
    uint32_t i;
    spo_id id;
    if (!s->kind)
        return;
    if (s->kind == 'p') {
        spo_page_desc pd = {sizeof pd, s->name, s->label[0] ? s->label : s->name};
        if (page_count == 8 || spo_register_page(&pd, &id) != SPO_OK)
            return fail(line, "page not registered");
        copy(page_keys[page_count], sizeof page_keys[0], s->name);
        page_ids[page_count++] = id;
        return;
    }
    dot = strchr(s->name, '.');
    if (!dot)
        return fail(line, "option name must be page.key");
    *dot = 0;
    for (i = 0; i < page_count && strcmp(page_keys[i], s->name); i++)
        ;
    if (i == page_count)
        return fail(line, "option on an undeclared page");
    d.page = page_ids[i];
    copy(key, sizeof key, dot + 1);
    d.key = key, d.label = s->label[0] ? s->label : key, d.help = s->help;
    d.default_value = s->def;
    if (!strcmp(s->type, "toggle"))
        d.type = SPO_TOGGLE;
    else if (!strcmp(s->type, "integer")) {
        d.type = SPO_INTEGER;
        d.minimum = s->min, d.maximum = s->max, d.step = s->step ? s->step : 1;
    } else if (!strcmp(s->type, "choice")) {
        d.type = SPO_CHOICE;
        for (p = s->choices; p && *p && d.choice_count < 16; p = rest) {
            rest = strchr(p, ',');
            if (rest)
                *rest++ = 0;
            choices[d.choice_count++] = trim(p);
        }
        d.choices = choices;
    } else
        return fail(line, "type must be toggle, choice or integer");
    if (spo_register_option(&d, &id) != SPO_OK)
        fail(line, "option rejected");
}
spo_result spo_settings_parse(char *text) {
    section s;
    char *line, *next;
    unsigned n = 0, start = 0;
    memset(&s, 0, sizeof s);
    armed = 0, errors = 0, page_count = 0;
    for (line = text; line && *line; line = next) {
        char *eq, *v;
        n++;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        line = trim(line);
        if (!*line || *line == '#' || *line == ';')
            continue;
        if (*line == '[') {
            char *end = strchr(line, ']'), *name;
            flush(&s, start);
            memset(&s, 0, sizeof s);
            start = n;
            if (!end || !(name = strchr(line, ' ')) || name > end) {
                fail(n, "section must be [page key] or [option page.key]");
                continue;
            }
            *end = 0;
            s.kind = !strncmp(line + 1, "page ", 5) ? 'p' : !strncmp(line + 1, "option ", 7) ? 'o' : 0;
            if (!s.kind)
                fail(n, "unknown section");
            else
                copy(s.name, sizeof s.name, trim(name));
            s.step = 1, s.max = 1;
            continue;
        }
        eq = strchr(line, '=');
        if (!eq || !s.kind) {
            fail(n, "unusable line");
            continue;
        }
        *eq = 0;
        v = trim(eq + 1), line = trim(line);
        if (!strcmp(line, "label"))
            copy(s.label, sizeof s.label, v);
        else if (!strcmp(line, "help"))
            copy(s.help, sizeof s.help, v);
        else if (!strcmp(line, "type"))
            copy(s.type, sizeof s.type, v);
        else if (!strcmp(line, "choices"))
            copy(s.choices, sizeof s.choices, v);
        else if (!strcmp(line, "default") || !strcmp(line, "min") || !strcmp(line, "max") ||
                 !strcmp(line, "step")) {
            int32_t *field = line[0] == 'd' ? &s.def : !strcmp(line, "min") ? &s.min
                             : !strcmp(line, "max") ? &s.max : &s.step;
            if (!number(v, field))
                fail(n, "not a number");
        } else
            fail(n, "unknown key");
    }
    flush(&s, start);
    armed = 1;
    return errors ? SPO_INVALID : SPO_OK;
}
spo_result spo_settings_load(const char *path) {
    static char text[MAX_TEXT + 1];
    FILE *f = path ? fopen(path, "rb") : NULL;
    size_t n;
    if (!f)
        return SPO_IO;
    n = fread(text, 1, MAX_TEXT, f);
    fclose(f);
    text[n] = 0;
    return spo_settings_parse(text);
}
