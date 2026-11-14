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
    assert(spo->row_count >= 20 && spo->panel.y >= 8 && spo->panel.y + spo->panel.h <= 472);
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
    spo->graph_serial = UINT64_MAX;
    assert(spo_layout(640, 480) == SPO_OK);
    assert(saved->count == spo->mesh.count);
    assert(!memcmp(saved->vertices, spo->mesh.vertices, saved->count * sizeof(spo_vertex)));
    spo->intervals[0] = 100000000;
    spo->graph_serial = UINT64_MAX;
    assert(spo_layout(640, 480) == SPO_OK);
    assert(spo->mesh.count == saved->count);
    assert(memcmp(saved->graph_data, spo->mesh.graph_data, sizeof(saved->graph_data)));
    free(saved);
}
static void graph_ring(void) {
    reset();
    spo_overlay_settings settings = spo->overlay;
    settings.items = SPO_BIT(SPO_GRAPH);
    assert(spo_overlay_set(&settings) == SPO_OK);
    spo->history_count = spo->history_next = 120;
    spo->history_serial = 120;
    for (int i = 0; i < 120; i++)
        spo->intervals[i] = 16000000;
    spo->graph_range_at = spo_now();
    assert(spo_layout(640, 480) == SPO_OK);
    uint32_t epoch = spo->mesh.graph_epoch, version = spo->mesh.graph_version;
    uint32_t revision = spo->mesh.revision;
    uint8_t previous[120][4];
    memcpy(previous, spo->mesh.graph_data, sizeof(previous));
    spo->intervals[120] = 25000000;
    spo->history_count++;
    spo->history_next++;
    spo->history_serial++;
    assert(spo_layout(640, 480) == SPO_OK);
    assert(spo->mesh.graph_epoch == epoch && spo->mesh.graph_version == version + 1);
    assert(spo->mesh.graph_head == 1 && spo->mesh.revision == revision);
    assert(memcmp(previous[0], spo->mesh.graph_data[0], 4));
    assert(!memcmp(previous + 1, spo->mesh.graph_data + 1, 119 * 4));
    for (int i = 0; i < 600; i++) {
        spo->intervals[spo->history_next] = 16000000;
        spo->history_next = (spo->history_next + 1) % SPO_HISTORY;
        if (spo->history_count < SPO_HISTORY)
            spo->history_count++;
        spo->history_serial++;
        assert(spo_layout(640, 480) == SPO_OK);
    }
    assert(spo->mesh.graph_head == 1 && spo->mesh.graph_epoch == epoch);
    for (int i = 0; i < 120; i++)
        assert(!memcmp(previous[i], spo->mesh.graph_data[i], 4));
    uint32_t newest = spo->mesh.graph_head;
    spo->intervals[spo->history_next] = UINT64_MAX;
    spo->history_next = (spo->history_next + 1) % SPO_HISTORY;
    spo->history_serial++;
    spo->graph_range_at = spo_now();
    assert(spo_layout(640, 480) == SPO_OK);
    assert(spo->mesh.graph_data[newest][0] == 28 && spo->mesh.graph_data[newest][1] == 255 &&
           spo->mesh.graph_data[newest][2] == 90);
    settings.font_scale = 3;
    assert(spo_overlay_set(&settings) == SPO_OK);
    assert(spo_layout(640, 480) == SPO_OK);
    assert(spo->mesh.graph_epoch != epoch && spo->mesh.graph_scale == 42);
    assert(spo->mesh.graph_data[0][0] > previous[0][0]);
    spo_reset_timing();
    assert(spo_layout(640, 480) == SPO_OK && !spo->mesh.graph_count);
    for (int i = 0; i < 120; i++)
        assert(!spo->mesh.graph_data[i][0]);
}
static const spo_row *find_row(const char *label) {
    for (uint32_t i = 0; i < spo->row_count; i++)
        if (!strcmp(spo->rows[i].label, label))
            return &spo->rows[i];
    return NULL;
}
static void steady_history(double interval_ms) {
    spo->history_count = spo->history_next = 120;
    spo->history_serial = 120;
    for (int i = 0; i < 120; i++)
        spo->intervals[i] = (uint64_t)(interval_ms * 1e6);
}
static void relayout(void) {
    spo->formatted_at = 0;
    assert(spo_layout(640, 480) == SPO_OK);
}
static void colours(void) {
    spo_overlay_settings settings;
    reset();
    assert(spo_preset_set(4) == SPO_OK);
    for (int i = SPO_SYSTEM_CPU; i <= SPO_MEM_AVAILABLE; i++)
        spo->metrics[i] = (spo_metric){10, spo_now(), 1};
    steady_history(16.7);
    spo->frame_ms = 16.7;
    /* Rates within 2% of the target are green; 10% below is the yellow edge. */
    for (int i = 0; i < 5; i++) {
        static const double rates[] = {60.0, 59.94, 55.0, 50.0, 0.0};
        static const int tones[] = {SPO_TONE_GOOD, SPO_TONE_GOOD, SPO_TONE_WARN, SPO_TONE_BAD,
                                    SPO_TONE_VALUE};
        spo->fps = rates[i];
        relayout();
        assert(spo->target_fps == 60);
        assert(find_row("FPS")->tone[0] == tones[i] && find_row("FPS")->tone[1] == tones[i]);
    }
    steady_history(33.3);
    spo->fps = 29.9;
    relayout();
    assert(spo->target_fps == 30 && find_row("FPS")->tone[0] == SPO_TONE_GOOD);
    steady_history(8.3);
    spo->fps = 120;
    relayout();
    assert(spo->target_fps == 120);
    steady_history(16.4);
    relayout();
    assert(spo->target_fps == 60);
    assert(spo_target_fps_set(5) == SPO_INVALID && spo_target_fps_set(2000) == SPO_INVALID);
    steady_history(33.3);
    spo->fps = 30;
    assert(spo_target_fps_set(60) == SPO_OK);
    relayout();
    assert(spo->target_fps == 60 && find_row("FPS")->tone[0] == SPO_TONE_BAD);
    assert(spo_target_fps_set(0) == SPO_OK);
    relayout();
    assert(spo->target_fps == 30 && find_row("FPS")->tone[0] == SPO_TONE_GOOD);
    {
        static const double load[] = {84, 85, 94, 95}, heat[] = {69, 70, 79, 80},
                            charge[] = {21, 20, 11, 10};
        static const int ladder[] = {SPO_TONE_VALUE, SPO_TONE_WARN, SPO_TONE_WARN, SPO_TONE_BAD};
        for (int i = 0; i < 4; i++) {
            spo->metrics[SPO_SYSTEM_CPU].value = spo->metrics[SPO_GPU_LOAD].value = load[i];
            spo->metrics[SPO_TEMPERATURE].value = heat[i];
            spo->metrics[SPO_BATTERY].value = charge[i];
            relayout();
            assert(find_row("CPU")->tone[0] == ladder[i] && find_row("GPU")->tone[0] == ladder[i]);
            assert(find_row("TEMP")->tone[0] == ladder[i]);
            assert(find_row("BAT")->tone[0] == ladder[i]);
        }
    }
    spo->battery_charging = 1;
    relayout();
    assert(!strcmp(find_row("BAT")->cell[1], "CHG"));
    assert(find_row("SELF")->tone[0] == SPO_TONE_DIM && find_row("DRAW")->tone[0] == SPO_TONE_DIM);
    /* The graph bar colours follow the same thresholds. */
    settings = spo->overlay;
    settings.items = SPO_BIT(SPO_GRAPH);
    assert(spo_overlay_set(&settings) == SPO_OK);
    steady_history(16.7);
    spo->intervals[3] = 18400000;
    spo->intervals[4] = 31000000;
    spo->graph_serial = UINT64_MAX;
    spo->graph_range_at = spo_now();
    relayout();
    assert(spo->mesh.graph_data[2][1] == 98 && spo->mesh.graph_data[2][2] == 214);
    assert(spo->mesh.graph_data[3][1] == 248 && spo->mesh.graph_data[3][2] == 200);
    assert(spo->mesh.graph_data[4][1] == 255 && spo->mesh.graph_data[4][2] == 90);
}
static void sample_metrics(void) {
    static const double values[SPO_ITEM_COUNT] = {
        [SPO_SYSTEM_CPU] = 3,  [SPO_PROCESS_CPU] = 6,    [SPO_CPU_CLOCK] = 1512,
        [SPO_GPU_CLOCK] = 360, [SPO_TEMPERATURE] = 42,   [SPO_BATTERY] = 94,
        [SPO_RSS] = 44,        [SPO_MEM_AVAILABLE] = 796};
    for (int i = SPO_SYSTEM_CPU; i <= SPO_MEM_AVAILABLE; i++)
        if (i != SPO_GPU_LOAD)
            spo->metrics[i] = (spo_metric){values[i], spo_now(), 1};
    spo->battery_charging = 1;
    spo->fps = 60;
    spo->frame_ms = 16.7;
    steady_history(16.7);
    spo->stats.sample_frames = spo->sampling_stats.sample_frames = 1;
    spo->stats.draw_calls = 1;
    spo->sampling_stats.total_cpu_us_per_frame = 91;
    spo->sampling_stats.cpu_us_per_frame = 13;
}
static void layouts(void) {
    /* Panel areas of the previous layout at the default size, for the same readings. */
    static const int before[] = {0, 2760, 4860, 23184, 50400};
    spo_id id;
    spo_counter_desc counter = {sizeof(counter), "objects", "DRAWS", "", 0};
    spo_indicator_desc indicator = {sizeof(indicator), "shaders", "SHADERS"};
    reset();
    assert(spo_register_counter(&counter, &id) == SPO_OK && spo_counter_set(id, 1) == SPO_OK);
    assert(spo_register_indicator(&indicator, &id) == SPO_OK);
    assert(spo_indicator_set(id, 0) == SPO_OK);
    for (int preset = 1; preset <= 4; preset++) {
        assert(spo_preset_set((uint32_t)preset) == SPO_OK);
        sample_metrics();
        relayout();
        assert(spo->actual_scale == 2 && spo->mesh.font == 2);
        assert(spo->panel.w * spo->panel.h <= before[preset]);
        assert(spo->mesh.count % 6 == 0 && spo->mesh.count <= SPO_QUADS * 6);
        for (uint32_t i = 0; i < spo->mesh.count; i++) {
            const spo_vertex *v = &spo->mesh.vertices[i];
            assert(v->x >= 0 && v->x <= 640 && v->y >= 0 && v->y <= 480);
            if (v->u >= 0)
                assert(v->u >= 0 && v->u <= 1 && v->v >= 0 && v->v <= 1);
        }
        assert(preset == 3 ? !!spo->mesh.graph_scale : !spo->mesh.graph_scale || preset == 4);
        if (preset == 1)
            assert(spo->row_count == 1 && !strcmp(spo->rows[0].label, "FPS") &&
                   !spo->rows[0].cell[1][0]);
        if (preset == 2)
            assert(spo->row_count == 1 && !strcmp(spo->rows[0].cell[0], "60.0") &&
                   !strcmp(spo->rows[0].cell[1], "16.7 ms"));
        if (preset == 3)
            assert(find_row("FPS") && find_row("CPU") && !find_row("MEM") &&
                   !strcmp(find_row("CPU")->cell[0], "3%") && !find_row("CPU")->cell[1][0]);
        if (preset == 4) {
            assert(!strcmp(find_row("CPU")->cell[0], "3%") &&
                   !strcmp(find_row("CPU")->cell[1], "1512 MHz"));
            assert(!strcmp(find_row("GPU")->cell[1], "360 MHz") && !find_row("GPU")->cell[0][0]);
            assert(!strcmp(find_row("MEM")->cell[0], "44 MB") &&
                   !strcmp(find_row("MEM")->cell[1], "796 MB"));
            assert(!strcmp(find_row("TEMP")->cell[0], "42 C") &&
                   !strcmp(find_row("BAT")->cell[0], "94%"));
            assert(!strcmp(find_row("DRAWS")->cell[0], "1") &&
                   !strcmp(find_row("SHADERS")->cell[0], "OFF"));
            assert(!strcmp(find_row("SELF")->cell[0], "91 us") &&
                   !strcmp(find_row("DRAW")->cell[0], "1"));
        }
    }
    /* Fixed-width fields keep the panel still while the digits change. */
    assert(spo_preset_set(2) == SPO_OK);
    sample_metrics();
    spo->fps = 9.9;
    spo->frame_ms = 101.0;
    relayout();
    {
        int width = spo->panel.w;
        spo->fps = 120.0;
        spo->frame_ms = 8.3;
        relayout();
        assert(spo->panel.w == width);
        spo->fps = 60.0;
        spo->frame_ms = 16.7;
        relayout();
        assert(spo->panel.w == width);
    }
    /* Each size draws with its own font and falls back when it does not fit. */
    assert(spo_preset_set(4) == SPO_OK);
    sample_metrics();
    for (uint32_t size = 1; size <= 3; size++) {
        spo->overlay.font_scale = size;
        relayout();
        assert(spo->mesh.font == spo->actual_scale && spo->actual_scale <= size);
        assert(spo->panel.x >= 8 && spo->panel.x + spo->panel.w <= 632);
    }
    assert(spo->actual_scale == 3 || spo->panel.h > 464);
}
static void menu_layout(void) {
    reset();
    spo_set_open(1);
    for (int detail = 0; detail <= 5; detail++)
        for (int root = 0; root <= 1; root++) {
            spo->detail = detail;
            spo->root = detail ? 0 : root;
            spo->cursor = 0;
            assert(spo_layout(640, 480) == SPO_OK && spo->mesh.font == 2);
            assert(spo->panel.x >= 8 && spo->panel.x + spo->panel.w <= 632);
            assert(spo->panel.y >= 8 && spo->panel.y + spo->panel.h <= 472);
            assert(spo_layout(1280, 720) == SPO_OK && spo->mesh.font == 3);
        }
    spo_set_open(0);
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
    graph_ring();
    maximal_layout();
    colours();
    layouts();
    menu_layout();
    malformed_config();
    spo_shutdown();
    puts("core: input, anchors, presets, parsers, persistence, callbacks and timing passed");
    return 0;
}
