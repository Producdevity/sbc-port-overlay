/* SPDX-License-Identifier: MIT */
#ifndef SPO_INTERNAL_H
#define SPO_INTERNAL_H
#include "spo.h"
#include <stddef.h>
#include <stdint.h>
#define SPO_BIT(i) (UINT32_C(1) << (i))
#define SPO_HISTORY 512
#define SPO_QUADS 1024
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
    uint32_t count, revision;
    uint8_t graph_data[120][4];
    uint32_t graph_head, graph_count, graph_scale, graph_epoch, graph_version;
    uint32_t font;
} spo_mesh;
#define SPO_ATLAS 256
#define SPO_ATLAS_GRAPH_ROW (SPO_ATLAS - 1)
#define SPO_ATLAS_SOLID_X 255
#define SPO_ATLAS_SOLID_Y 254
typedef struct {
    int cw, ch, cols, grid_h;
} spo_font_info;
enum {
    SPO_TONE_VALUE,
    SPO_TONE_LABEL,
    SPO_TONE_DIM,
    SPO_TONE_GOOD,
    SPO_TONE_WARN,
    SPO_TONE_BAD,
    SPO_TONE_ACCENT,
    SPO_TONE_COUNT
};
typedef struct {
    char label[17], cell[3][24];
    uint8_t tone[3], reserve[3], span;
} spo_row;
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
    uint32_t id, used, held, armed, guide, ready;
    int32_t axis[SPO_AXIS_COUNT], guide_axis[SPO_AXIS_COUNT];
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
    int open, drain, tab, cursor, scroll;
    uint64_t intervals[SPO_HISTORY], last_stamp, window_ns, refresh_stamp, history_serial;
    uint32_t history_count, history_next, window_frames;
    double fps, frame_ms, low_1, graph_max;
    uint32_t fps_low, fps_high, fps_config_low, fps_config_high;
    uint64_t tone_since;
    uint8_t tone_shown, tone_pending;
    int tone_ready;
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
    spo_row rows[40];
    uint32_t row_count, header_rows, formatted_valid;
    spo_rect graph_rect;
    uint64_t graph_range_at;
    uint64_t graph_serial;
    double graph_pixel_range, graph_pixel_factor;
    int graph_pixel_scale;
    double graph_label_range, graph_label_high;
    char graph_label[32];
    uint64_t formatted_at, format_checked_at;
    spo_overlay_settings formatted_settings;
    uint32_t formatted_counters, formatted_indicators;
    spo_stats stats;
    uint64_t stats_window;
    double cpu_sum, wall_sum, cpu_max;
    uint32_t stats_frames, frame_reads;
    void *renderer;
} spo_context;
extern spo_context *spo;
uint64_t spo_now(void);
uint64_t spo_cpu_now(void);
int spo_text_copy(char *, size_t, const char *);
int spo_key_valid(const char *);
spo_result spo_fail(spo_result);
void spo_apply_preset(uint32_t);
void spo_overlay_sync(void);
uint32_t spo_preset_items(uint32_t);
void spo_numeric_refresh(uint64_t);
enum { SPO_ROW_NONE, SPO_ROW_HEADING, SPO_ROW_TOGGLE, SPO_ROW_CHOICE, SPO_ROW_SLIDER, SPO_ROW_ACTION };
#define SPO_MENU_ROWS 10
typedef struct {
    int kind;
    char label[40], value[40];
    uint8_t tone, enabled;
} spo_menu_row;
void spo_menu_button(spo_button);
int spo_menu_rows(void);
int spo_menu_tabs(void);
const char *spo_menu_tab_label(int);
void spo_menu_describe(int, spo_menu_row *);
const char *spo_menu_help(int);
int spo_menu_row_kind(int);
int spo_menu_repeats(spo_button);
void spo_menu_home(void);
void spo_menu_scroll_fit(void);
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
const spo_font_info *spo_font_get(uint32_t);
void spo_font_band(uint32_t, uint32_t, uint32_t, uint8_t *);
enum {
    SPO_GLYPH_DPAD_UD,
    SPO_GLYPH_DPAD_LR,
    SPO_GLYPH_SOUTH,
    SPO_GLYPH_EAST,
    SPO_GLYPH_L1,
    SPO_GLYPH_R1,
    SPO_GLYPH_MENU,
    SPO_GLYPH_COUNT
};
/* A controller glyph in the atlas; yoff is its top edge relative to a line of text. */
typedef struct {
    int x, y, w, h, yoff;
} spo_glyph;
const spo_glyph *spo_glyph_get(uint32_t, int);
void spo_glyph_raster(uint32_t, int, uint8_t *);
spo_result spo_backend_init(void);
void spo_backend_shutdown(void);
spo_result spo_backend_draw(const spo_mesh *, uint32_t, uint32_t, uint32_t *);
#define SPO_HOLD_NS UINT64_C(1000000000)
void spo_history_push(uint64_t);
void spo_colours_step(uint64_t);
#endif
