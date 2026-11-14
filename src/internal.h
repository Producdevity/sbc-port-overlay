/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SPO_INTERNAL_H
#define SPO_INTERNAL_H
#include "spo.h"
#include <stddef.h>
#include <stdint.h>
#define SPO_BIT(i) (UINT32_C(1) << (i))
#define SPO_HISTORY 512
#define SPO_QUADS 2048
#define SPO_FDS 24
#define SPO_INI_ENTRIES 256
typedef struct {
    float x, y, u, v;
    uint8_t color[4];
} spo_vertex;
typedef struct {
    int x, y, w, h;
} spo_rect;
typedef struct {
    spo_vertex vertices[SPO_QUADS * 6];
    uint32_t count;
    uint32_t graph_words[32], graph_scale;
} spo_mesh;
typedef struct {
    const uint8_t *rgba;
    uint32_t width, height;
} spo_atlas;
typedef struct {
    char key[33], label[25];
    uint32_t count, first;
} spo_page;
typedef struct {
    char key[33], label[25], help[81], choices[16][25];
    spo_option_desc desc;
    int32_t value;
} spo_option;
typedef struct {
    char key[33], label[11], unit[5];
    double value;
    uint32_t decimals, enabled, valid;
} spo_counter;
typedef struct {
    char key[33], label[17];
    uint32_t value, enabled, valid;
} spo_indicator;
typedef struct {
    char section[65], key[65], value[161];
} spo_ini_entry;
typedef struct {
    uint32_t id, used, held, axes, armed, guide, ready;
    int32_t axis[SPO_AXIS_COUNT];
    uint64_t repeat_at;
    spo_button repeat_button;
} spo_pad;
typedef struct {
    int fd, kind;
    uint32_t needs, failures;
    uint64_t due;
} spo_source;
typedef struct {
    double value;
    uint64_t stamp;
    int valid;
} spo_metric;
typedef struct spo_sampler spo_sampler;
typedef struct {
    spo_config config;
    char path[4096], name[81], version[81], renderer_name[32];
    spo_overlay_settings overlay;
    spo_page pages[8];
    spo_option options[64];
    spo_counter counters[8];
    spo_indicator indicators[8];
    uint32_t page_count, option_count, counter_count, indicator_count;
    uint32_t counter_mask, indicator_mask;
    int frozen, lost, dirty, saving, in_callback;
    spo_result error;
    spo_pad pads[4];
    int open, drain, root, detail, cursor, editing;
    uint64_t intervals[SPO_HISTORY], last_stamp, window_ns, refresh_stamp;
    uint32_t history_count, history_next, window_frames;
    double fps, frame_ms, low_1, graph_max;
    uint64_t graph_high_at;
    spo_sampler *sampler;
    uint32_t sampling_sequence, requested_items, available_items;
    spo_metric metrics[SPO_ITEM_COUNT];
    spo_sampling_stats sampling_stats;
    uint64_t cost_sampler_cpu, cost_sampler_elapsed;
    double cost_frame_ns, cost_render_percent;
    spo_ini_entry ini[SPO_INI_ENTRIES];
    uint32_t ini_count;
    char ini_buffer[65537];
    spo_mesh mesh;
    uint32_t base_count;
    uint32_t mesh_width, mesh_height;
    uint64_t mesh_formatted_at;
    int cached_graph_y;
    spo_rect panel;
    uint32_t actual_scale;
    int battery_charging;
    char lines[40][64];
    uint32_t line_count, header_lines, formatted_valid;
    uint64_t graph_range_at;
    uint64_t graph_ns[SPO_HISTORY];
    uint8_t graph_pixels[SPO_HISTORY];
    double graph_pixel_range, graph_pixel_factor;
    int graph_pixel_scale;
    double graph_label_range;
    char graph_label[32];
    uint64_t formatted_at;
    spo_overlay_settings formatted_settings;
    uint32_t formatted_counters, formatted_indicators;
    spo_stats stats;
    uint64_t stats_window;
    double cpu_sum, wall_sum, cpu_max;
    uint32_t stats_frames, frame_reads;
    uint8_t atlas_pixels[128 * 64 * 4];
    void *renderer;
} spo_context;
extern spo_context *spo;
uint64_t spo_now(void);
uint64_t spo_cpu_now(void);
int spo_text_copy(char *, size_t, const char *);
int spo_key_valid(const char *);
spo_result spo_fail(spo_result);
void spo_apply_preset(uint32_t);
void spo_numeric_refresh(uint64_t);
void spo_menu_button(spo_button);
int spo_menu_rows(void);
spo_option *spo_menu_option(int);
void spo_input_tick(uint64_t);
void spo_set_open(int);
spo_result spo_config_load(void);
int spo_ini_get(const char *, const char *, int32_t *);
int spo_ini_has(const char *, const char *);
int spo_ini_put(const char *, const char *, int32_t);
spo_result spo_metrics_init(void);
void spo_metrics_close(void);
void spo_metrics_collect(uint64_t);
void spo_metrics_request(void);
void spo_metrics_reset(void);
void spo_metrics_cost(void);
int spo_metrics_available(int);
int spo_parse_cpu(const char *, uint64_t *, uint64_t *);
int spo_parse_process(const char *, uint64_t *, uint64_t *);
int spo_parse_mem(const char *, double *);
int spo_parse_number(const char *, double *);
int spo_parse_gpu_load(const char *, double *);
spo_result spo_layout(uint32_t, uint32_t);
spo_rect spo_anchor(uint32_t, uint32_t, int, int, int);
void spo_font_atlas(uint8_t *);
spo_result spo_backend_init(const spo_atlas *);
void spo_backend_shutdown(void);
spo_result spo_backend_draw(const spo_mesh *, uint32_t, uint32_t, uint32_t, uint32_t *);
const char *spo_item_name(uint32_t);
#endif
