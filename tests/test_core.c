/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int pause_calls, change_calls, action_calls, reject;
static spo_id register_setting(void);
static void pause_game(uint32_t on, void *user) {
    (void)on;
    (void)user;
    pause_calls++;
}
static spo_result changed(spo_id id, int32_t value, void *user) {
    (void)id;
    (void)value;
    (void)user;
    change_calls++;
    return reject ? SPO_REJECTED : SPO_OK;
}
static void action(spo_id id, void *user) {
    (void)id;
    (void)user;
    action_calls++;
}
static spo_config config(const char *path) {
    spo_config c = {0};
    c.struct_size = sizeof(c);
    c.abi_version = SPO_ABI_VERSION;
    c.flags = SPO_INIT_NO_RENDER | SPO_INIT_NO_SAMPLER_THREAD;
    c.port_name = "Test";
    c.port_version = "1";
    c.config_path = path;
    c.on_pause = pause_game;
    return c;
}
static void tap(spo_button b) {
    spo_handle_input(1, b, 1);
    spo_handle_input(1, b, 0);
}
static void reset(void) {
    spo_shutdown();
    spo_config c = config(NULL);
    assert(spo_init(&c) == SPO_OK);
}
static void input(void) {
    reset();
    tap(SPO_START);
    tap(SPO_SELECT);
    assert(!spo_menu_is_open());
    spo_handle_input(1, SPO_GUIDE, 1);
    spo_handle_input(1, SPO_START, 1);
    spo_handle_input(1, SPO_START, 0);
    spo_handle_input(1, SPO_GUIDE, 0);
    assert(!spo_menu_is_open());
    spo_handle_input(1, SPO_START, 1);
    tap(SPO_GUIDE);
    spo_handle_input(1, SPO_START, 0);
    assert(!spo_menu_is_open());
    spo_handle_input(1, SPO_GUIDE, 1);
    spo_handle_axis(1, SPO_LX, 20000);
    spo_handle_axis(1, SPO_LX, 0);
    spo_handle_input(1, SPO_GUIDE, 0);
    assert(!spo_menu_is_open());
    spo_handle_input(1, SPO_GUIDE, 0);
    assert(!spo_menu_is_open());
    pause_calls = 0;
    assert(!spo_handle_input(1, SPO_GUIDE, 1));
    assert(!spo_handle_input(1, SPO_GUIDE, 1));
    assert(spo_handle_input(1, SPO_GUIDE, 0));
    assert(spo_menu_is_open() && pause_calls == 1);
    tap(SPO_START);
    tap(SPO_SELECT);
    assert(spo_menu_is_open());
    assert(spo_handle_axis(1, SPO_LX, 20000));
    assert(spo_handle_input(1, SPO_EAST, 1));
    assert(!spo_menu_is_open() && spo_input_captured() && pause_calls == 2);
    assert(spo_handle_input(1, SPO_EAST, 0));
    assert(spo_handle_axis(1, SPO_LX, 0));
    assert(!spo_input_captured());
    spo_handle_input(1, SPO_GUIDE, 1);
    spo_input_reset(1);
    spo_handle_input(1, SPO_GUIDE, 0);
    assert(!spo_menu_is_open());
    spo_handle_input(1, SPO_GUIDE, 1);
    spo_handle_input(2, SPO_START, 1);
    spo_handle_input(1, SPO_GUIDE, 0);
    assert(spo_menu_is_open());
    spo_input_reset(SPO_ALL_CONTROLLERS);
    tap(SPO_GUIDE);
    assert(!spo_menu_is_open());
    spo_input_reset(9);
    spo_handle_input(9, SPO_GUIDE, 1);
    spo_handle_input(9, SPO_GUIDE, 0);
    assert(!spo_menu_is_open());
    spo_handle_axis(9, SPO_LX, 0);
    spo_handle_input(9, SPO_GUIDE, 1);
    spo_handle_input(9, SPO_GUIDE, 0);
    assert(spo_menu_is_open());
    spo_input_reset(SPO_ALL_CONTROLLERS);
    tap(SPO_GUIDE);
    assert(!spo_menu_is_open());
    spo_handle_input(1, SPO_PADDLE1, 1);
    spo_handle_input(1, SPO_PADDLE2, 1);
    spo_handle_input(1, SPO_PADDLE1, 0);
    tap(SPO_GUIDE);
    assert(!spo_menu_is_open());
    spo_handle_input(1, SPO_PADDLE2, 0);
}
static void snapshots_and_editing(void) {
    int32_t axes[SPO_AXIS_COUNT] = {0};
    spo_overlay_settings before = {0}, after = {0};
    reset();
    assert(spo_input_sync(1, SPO_BIT(SPO_GUIDE), axes) == SPO_OK);
    assert(!spo_handle_input(1, SPO_GUIDE, 0));
    assert(!spo_menu_is_open());
    tap(SPO_GUIDE);
    assert(spo_menu_is_open());
    tap(SPO_DOWN);
    tap(SPO_DOWN);
    tap(SPO_DOWN);
    before.struct_size = after.struct_size = sizeof(before);
    assert(spo_overlay_get(&before) == SPO_OK);
    tap(SPO_SOUTH);
    tap(SPO_UP);
    tap(SPO_RIGHT);
    assert(spo_overlay_get(&after) == SPO_OK && after.font_scale == before.font_scale + 1);
    tap(SPO_EAST);
    assert(spo_menu_is_open());
    tap(SPO_EAST);
    assert(!spo_menu_is_open());
    axes[SPO_LT] = -1;
    assert(spo_input_sync(1, 0, axes) == SPO_INVALID);
}
static void anchors(void) {
    int a;
    for (a = 0; a < 9; a++) {
        spo_rect r = spo_anchor(640, 480, 100, 40, a);
        assert(r.x == (a % 3 == 0 ? 8 : a % 3 == 1 ? 270 : 532));
        assert(r.y == (a / 3 == 0 ? 8 : a / 3 == 1 ? 220 : 432));
        r = spo_anchor(1280, 720, 252, 200, a);
        assert(r.x >= 8 && r.y >= 8 && r.x + r.w <= 1272 && r.y + r.h <= 712);
    }
}
static void presets(void) {
    uint32_t p;
    reset();
    for (p = 0; p < 5; p++) {
        assert(spo_preset_set(p) == SPO_OK);
        assert(!!(spo->overlay.items & SPO_BIT(SPO_FPS)) == (p >= 1));
        assert(!!(spo->overlay.items & SPO_BIT(SPO_FRAME_TIME)) == (p >= 2));
        assert(!!(spo->overlay.items & SPO_BIT(SPO_GRAPH)) == (p >= 3));
        assert(!!(spo->overlay.items & SPO_BIT(SPO_SELF_COST)) == (p == 4));
        assert(!(spo->overlay.items & SPO_BIT(SPO_LOW_1)));
    }
    assert(spo_preset_set(5) == SPO_INVALID);
    assert(spo_preset_set(0) == SPO_OK);
    assert(spo_frame(640, 480) == SPO_OK);
    assert(!spo->mesh.count && !spo->frame_reads);
    spo_overlay_settings o = spo->overlay;
    o.items = SPO_BIT(SPO_FPS);
    assert(spo_overlay_set(&o) == SPO_OK);
    assert(spo->overlay.custom);
    assert(spo_frame(640, 480) == SPO_OK && spo->mesh.count > 0);
    assert(spo_frame(1, 1) == SPO_CAPACITY);
}
static void parsers(void) {
    uint64_t a, b;
    double d;
    assert(spo_parse_cpu("cpu  10 2 8 70 5 3 1 1 100 200\n", &a, &b) && a == 25 && b == 100);
    assert(!spo_parse_cpu("cpu 1 2", &a, &b));
    assert(!spo_parse_cpu("cpu -1 2 3 4 5 6 7 8", &a, &b));
    assert(
        spo_parse_process(
            "42 (name (odd) )) R 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21", &a, &b) &&
        a == 23 && b == 21);
    assert(!spo_parse_process("42 (name) R 1 2", &a, &b));
    assert(spo_parse_mem("MemFree: 1 kB\nMemAvailable: 131072 kB\n", &d) && d == 128);
    assert(!spo_parse_mem("MemAvailable: 10 MB\n", &d));
    assert(!spo_parse_mem("MemFree: 1024 kB\n", &d));
    assert(spo_parse_number(" 42\n", &d) && d == 42);
    assert(!spo_parse_number("nan", &d));
    assert(!spo_parse_number("1 MHz", &d));
    assert(spo_parse_gpu_load("71@650000000Hz\n", &d) && d == 71);
    assert(spo_parse_gpu_load("42 %\n", &d) && d == 42);
    assert(!spo_parse_gpu_load("101@600Hz", &d));
    assert(!spo_parse_gpu_load("nan", &d));
    assert(!spo_parse_gpu_load("42@badHz", &d));
    assert(!spo_parse_gpu_load("42@1MHz", &d));
}
static void fixtures(void) {
    static const char *names[] = {"_proc_stat",
                                  "_proc_self_stat",
                                  "_proc_meminfo",
                                  "_sys_class_devfreq_fde60000.gpu_load",
                                  "_sys_class_devfreq_fde60000.gpu_cur_freq",
                                  "_sys_class_thermal_thermal_zone0_temp"};
    size_t i;
    char path[1024], buf[16385];
    uint64_t a, b;
    double value;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        FILE *f;
        size_t n;
        snprintf(path, sizeof(path), "%s/rk3566-spruce/%s", SPO_FIXTURE_DIR, names[i]);
        f = fopen(path, "rb");
        assert(f);
        n = fread(buf, 1, sizeof(buf) - 1, f);
        assert(n && n < sizeof(buf) - 1);
        buf[n] = 0;
        fclose(f);
        if (i == 0)
            assert(spo_parse_cpu(buf, &a, &b) && b >= a);
        else if (i == 1)
            assert(spo_parse_process(buf, &a, &b));
        else if (i == 2)
            assert(spo_parse_mem(buf, &value) && value > 0);
        else if (i == 3)
            assert(spo_parse_gpu_load(buf, &value) && value >= 0 && value <= 100);
        else
            assert(spo_parse_number(buf, &value) && value > 0);
    }
    {
        FILE *f;
        size_t n;
        snprintf(path, sizeof(path), "%s/h700-knulli/_proc_stat", SPO_FIXTURE_DIR);
        f = fopen(path, "rb");
        assert(f);
        n = fread(buf, 1, sizeof(buf) - 1, f);
        assert(n && n < sizeof(buf) - 1);
        buf[n] = 0;
        fclose(f);
        assert(spo_parse_cpu(buf, &a, &b) && b >= a);
        snprintf(path, sizeof(path), "%s/h700-knulli/_proc_self_stat", SPO_FIXTURE_DIR);
        f = fopen(path, "rb");
        assert(f);
        n = fread(buf, 1, sizeof(buf) - 1, f);
        assert(n && n < sizeof(buf) - 1);
        buf[n] = 0;
        fclose(f);
        assert(spo_parse_process(buf, &a, &b));
    }
}
static void maximal_layout(void) {
    uint32_t i;
    spo_id id;
    spo_counter_desc c = {sizeof(c), NULL, "COUNTER", "MiB", 3};
    spo_indicator_desc ind = {sizeof(ind), NULL, "INDICATOR"};
    reset();
    for (i = 0; i < 8; i++) {
        char key[8];
        snprintf(key, sizeof(key), "item%u", i);
        c.key = key;
        ind.key = key;
        assert(spo_register_counter(&c, &id) == SPO_OK);
        assert(spo_counter_set(id, 99999.25) == SPO_OK);
        assert(spo_register_indicator(&ind, &id) == SPO_OK);
        assert(spo_indicator_set(id, 1) == SPO_OK);
    }
    assert(spo_preset_set(4) == SPO_OK);
    for (i = SPO_SYSTEM_CPU; i <= SPO_MEM_AVAILABLE; i++)
        spo->metrics[i] = (spo_metric){100, spo_now(), 1};
    assert(spo_layout(640, 480) == SPO_OK);
    assert(spo->line_count >= 20 && spo->panel.y >= 8 && spo->panel.y + spo->panel.h <= 472);
    spo->overlay.font_scale = 4;
    assert(spo_layout(640, 480) == SPO_OK && spo->actual_scale <= 2);
    assert(spo_layout(1280, 720) == SPO_OK);
    assert(spo->panel.x >= 8 && spo->panel.x + spo->panel.w <= 1272);
    spo_set_open(1);
    spo->detail = 3;
    spo->cursor = 0;
    assert(spo_layout(640, 480) == SPO_OK);
    spo_set_open(0);
}
static void malformed_config(void) {
    char path[] = "/tmp/spo-invalid-XXXXXX";
    int fd = mkstemp(path);
    spo_config c = config(path);
    const char *text = "[spo]\nfont_scale=999\ngraph_samples=31\n[port.graphics]\nscale=999\n";
    spo_shutdown();
    assert(fd >= 0);
    assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    close(fd);
    assert(spo_init(&c) == SPO_OK);
    (void)register_setting();
    assert(spo->overlay.font_scale == 2 && spo->overlay.graph_samples == 120 &&
           spo_last_error() == SPO_INVALID);
    spo_shutdown();
    {
        FILE *f = fopen(path, "w");
        assert(f);
        fputs("[spo]\nformat=2\n", f);
        fclose(f);
    }
    assert(spo_init(&c) == SPO_ABI);
    assert(!spo);
    unlink(path);
}
static spo_id register_setting(void) {
    spo_id page, id;
    spo_page_desc p = {sizeof(p), "graphics", "Graphics"};
    spo_option_desc d = {0};
    assert(spo_register_page(&p, &page) == SPO_OK);
    d.struct_size = sizeof(d);
    d.page = page;
    d.type = SPO_INTEGER;
    d.key = "scale";
    d.label = "Scale";
    d.minimum = 50;
    d.maximum = 100;
    d.step = 5;
    d.default_value = 100;
    d.on_change = changed;
    assert(spo_register_option(&d, &id) == SPO_OK);
    return id;
}
static void persistence(void) {
    char path[] = "/tmp/spo-test-XXXXXX";
    int fd = mkstemp(path);
    spo_id id;
    int32_t value;
    spo_config c;
    const char *ini = "[spo]\npreset=3\ncustom=1\nanchor=bottom_right\n[spo.items]\nprocess_cpu="
                      "1\n[port.graphics]\nscale=75\n[future]\nretain=yes\n";
    assert(fd >= 0);
    assert(write(fd, ini, strlen(ini)) == (ssize_t)strlen(ini));
    close(fd);
    spo_shutdown();
    c = config(path);
    assert(spo_init(&c) == SPO_OK);
    change_calls = 0;
    id = register_setting();
    assert(change_calls == 1);
    assert(spo_option_get(id, &value) == SPO_OK && value == 75);
    assert(spo->overlay.anchor == 8 && spo->overlay.items & SPO_BIT(SPO_PROCESS_CPU));
    reject = 1;
    assert(spo_option_set(id, 80) == SPO_REJECTED);
    reject = 0;
    assert(spo_option_get(id, &value) == SPO_OK && value == 75);
    assert(spo_option_set(id, 81) == SPO_INVALID);
    assert(spo_option_set(id, 80) == SPO_OK);
    assert(spo_config_dirty());
    assert(spo_config_save() == SPO_OK && !spo_config_dirty());
    spo_shutdown();
    assert(spo_init(&c) == SPO_OK);
    id = register_setting();
    assert(spo_option_get(id, &value) == SPO_OK && value == 80);
    {
        FILE *f = fopen(path, "r");
        char buf[4096];
        size_t n;
        assert(f);
        n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = 0;
        fclose(f);
        assert(strstr(buf, "retain=yes"));
    }
    assert(spo_frame(640, 480) == SPO_OK);
    {
        spo_page_desc p = {sizeof(p), "late", "Late"};
        assert(spo_register_page(&p, &id) == SPO_REGISTRATION_CLOSED);
    }
    spo_shutdown();
    unlink(path);
    c = config("/missing-spo-test-dir/settings.ini");
    assert(spo_init(&c) == SPO_OK);
    spo_preset_set(4);
    assert(spo_config_save() == SPO_IO && spo_config_dirty());
}
static void actions(void) {
    spo_id page, id;
    spo_page_desc p = {sizeof(p), "test", "Test"};
    spo_option_desc d = {0};
    reset();
    assert(spo_register_page(&p, &page) == SPO_OK);
    d.struct_size = sizeof(d);
    d.page = page;
    d.key = "run";
    d.label = "Run";
    d.type = SPO_ACTION;
    d.on_action = action;
    d.choice_count = 1;
    assert(spo_register_option(&d, &id) == SPO_INVALID);
    d.choice_count = 0;
    assert(spo_register_option(&d, &id) == SPO_OK);
    assert(!action_calls);
    tap(SPO_GUIDE);
    spo->root = 1;
    spo_handle_input(1, SPO_SOUTH, 1);
    spo_handle_input(1, SPO_SOUTH, 1);
    assert(action_calls == 1);
    spo_handle_input(1, SPO_SOUTH, 0);
    spo_input_tick(UINT64_MAX);
    assert(action_calls == 1);
}
static void timing(void) {
    uint32_t i;
    reset();
    spo->window_ns = 500000000;
    spo->window_frames = 30;
    spo_numeric_refresh(1000000000);
    assert(fabs(spo->fps - 60) < 0.001);
    spo->window_ns = 500000000;
    spo->window_frames = 120;
    spo_numeric_refresh(2000000000);
    assert(fabs(spo->fps - 240) < 0.001);
    spo->overlay.items |= SPO_BIT(SPO_LOW_1);
    spo->history_count = 512;
    for (i = 0; i < 512; i++)
        spo->intervals[i] = 10000000;
    spo->intervals[2] = spo->intervals[5] = 30000000;
    spo->window_ns = 500000000;
    spo->window_frames = 50;
    spo_numeric_refresh(3000000000);
    assert(fabs(spo->low_1 - 60) < 0.001);
    spo_reset_timing();
    assert(!spo->history_count && !spo->fps && !spo->last_stamp);
    assert(spo_frame(0, 0) == SPO_OK);
    spo_context_lost();
    assert(spo_frame(640, 480) == SPO_CONTEXT_LOST);
}
static void graph_cache(void) {
    spo_mesh *saved = malloc(sizeof(*saved));
    spo_overlay_settings settings;
    assert(saved);
    reset();
    settings = spo->overlay;
    settings.items = SPO_BIT(SPO_FPS) | SPO_BIT(SPO_GRAPH);
    assert(spo_overlay_set(&settings) == SPO_OK);
    spo->history_count = spo->history_next = 30;
    for (int i = 0; i < 30; i++)
        spo->intervals[i] = 10000000;
    spo->graph_range_at = spo_now();
    assert(spo_layout(640, 480) == SPO_OK);
    *saved = spo->mesh;
    spo->intervals[0]++;
    assert(spo_layout(640, 480) == SPO_OK);
    assert(saved->count == spo->mesh.count);
    assert(!memcmp(saved->vertices, spo->mesh.vertices, saved->count * sizeof(spo_vertex)));
    spo->intervals[0] = 100000000;
    assert(spo_layout(640, 480) == SPO_OK);
    assert(spo->mesh.count == saved->count);
    assert(memcmp(saved->graph_words, spo->mesh.graph_words, sizeof(saved->graph_words)));
    free(saved);
}
int main(void) {
    input();
    snapshots_and_editing();
    anchors();
    presets();
    parsers();
    fixtures();
    persistence();
    actions();
    timing();
    graph_cache();
    maximal_layout();
    malformed_config();
    spo_shutdown();
    puts("core: input, anchors, presets, parsers, persistence, callbacks and timing passed");
    return 0;
}
