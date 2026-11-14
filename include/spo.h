/* SPDX-License-Identifier: MIT */
#ifndef SPO_H
#define SPO_H
#include <stdint.h>
#if defined(_WIN32) && defined(SPO_SHARED)
#define SPO_API __declspec(dllexport)
#elif defined(__GNUC__)
#define SPO_API __attribute__((visibility("default")))
#else
#define SPO_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define SPO_ABI_VERSION 1u
#define SPO_VERSION "0.1.0"
#define SPO_ALL_CONTROLLERS UINT32_MAX
#define SPO_INIT_NO_RENDER 1u
#define SPO_INIT_NO_SAMPLER_THREAD 2u
typedef int32_t spo_result;
typedef uint32_t spo_id;
typedef uint32_t spo_button;
typedef uint32_t spo_axis;
typedef uint32_t spo_option_type;
enum {
    SPO_OK = 0,
    SPO_INVALID = -1,
    SPO_ABI = -2,
    SPO_NOT_INITIALIZED = -3,
    SPO_ALREADY_INITIALIZED = -4,
    SPO_CAPACITY = -5,
    SPO_REGISTRATION_CLOSED = -6,
    SPO_IO = -7,
    SPO_GL_UNAVAILABLE = -8,
    SPO_CONTEXT_LOST = -9,
    SPO_REJECTED = -10,
    SPO_THREAD_UNAVAILABLE = -11,
    SPO_BUSY = -12
};
enum {
    SPO_GUIDE,
    SPO_UP,
    SPO_DOWN,
    SPO_LEFT,
    SPO_RIGHT,
    SPO_SOUTH,
    SPO_EAST,
    SPO_WEST,
    SPO_NORTH,
    SPO_START,
    SPO_SELECT,
    SPO_L1,
    SPO_R1,
    SPO_L2,
    SPO_R2,
    SPO_L3,
    SPO_R3,
    SPO_MISC,
    SPO_PADDLE1,
    SPO_PADDLE2,
    SPO_PADDLE3,
    SPO_PADDLE4,
    SPO_TOUCHPAD,
    SPO_OTHER,
    SPO_BUTTON_COUNT
};
enum { SPO_LX, SPO_LY, SPO_RX, SPO_RY, SPO_LT, SPO_RT, SPO_AXIS_COUNT };
enum { SPO_TOGGLE, SPO_CHOICE, SPO_INTEGER, SPO_ACTION };
enum {
    SPO_FPS,
    SPO_FRAME_TIME,
    SPO_GRAPH,
    SPO_LOW_1,
    SPO_SYSTEM_CPU,
    SPO_PROCESS_CPU, /* unused; the slot keeps the other values stable */
    SPO_GPU_LOAD,
    SPO_CPU_CLOCK,
    SPO_GPU_CLOCK,
    SPO_TEMPERATURE,
    SPO_BATTERY,
    SPO_RSS,
    SPO_RAM_USED,
    SPO_SELF_COST,
    SPO_CHARGING,
    SPO_ITEM_COUNT
};
enum {
    SPO_TOP_LEFT,
    SPO_TOP_CENTER,
    SPO_TOP_RIGHT,
    SPO_CENTER_LEFT,
    SPO_CENTER,
    SPO_CENTER_RIGHT,
    SPO_BOTTOM_LEFT,
    SPO_BOTTOM_CENTER,
    SPO_BOTTOM_RIGHT
};
typedef void *(*spo_get_proc_fn)(const char *, void *);
typedef void (*spo_pause_fn)(uint32_t, void *);
typedef spo_result (*spo_change_fn)(spo_id, int32_t, void *);
typedef void (*spo_action_fn)(spo_id, void *);
typedef struct spo_config {
    uint32_t struct_size, abi_version;
    spo_get_proc_fn get_proc_address;
    void *gl_user;
    const char *config_path, *port_name, *port_version;
    spo_pause_fn on_pause;
    void *pause_user;
    uint32_t flags;
} spo_config;
typedef struct spo_page_desc {
    uint32_t struct_size;
    const char *key, *label;
} spo_page_desc;
typedef struct spo_option_desc {
    uint32_t struct_size;
    spo_id page;
    spo_option_type type;
    const char *key, *label, *help;
    int32_t default_value, minimum, maximum, step;
    const char *const *choices;
    uint32_t choice_count;
    spo_change_fn on_change;
    spo_action_fn on_action;
    void *user;
} spo_option_desc;
typedef struct spo_counter_desc {
    uint32_t struct_size;
    const char *key, *label, *unit;
    uint32_t decimals;
} spo_counter_desc;
typedef struct spo_indicator_desc {
    uint32_t struct_size;
    const char *key, *label;
} spo_indicator_desc;
typedef struct spo_stats {
    uint32_t struct_size, sample_frames;
    double cpu_us_mean, cpu_us_max, wall_us_mean;
    uint32_t draw_calls, vertices, telemetry_reads;
} spo_stats;
typedef struct spo_sampling_stats {
    uint32_t struct_size, threaded, source_count, sample_frames;
    int32_t nice_value, scheduler_policy;
    uint64_t cpu_ns_total, elapsed_ns, cycles, reads, cycle_cpu_ns_max;
    double cpu_us_per_frame, total_cpu_us_per_frame, cpu_percent, total_cpu_percent;
} spo_sampling_stats;
typedef struct spo_overlay_settings {
    uint32_t struct_size, preset, custom, anchor, font_scale;
    uint32_t background_opacity, text_opacity, graph_samples, items;
} spo_overlay_settings;
SPO_API spo_result spo_init(const spo_config *);
SPO_API void spo_shutdown(void);
SPO_API spo_result spo_register_page(const spo_page_desc *, spo_id *);
SPO_API spo_result spo_register_option(const spo_option_desc *, spo_id *);
SPO_API spo_result spo_register_counter(const spo_counter_desc *, spo_id *);
SPO_API spo_result spo_register_indicator(const spo_indicator_desc *, spo_id *);
SPO_API spo_result spo_option_get(spo_id, int32_t *);
SPO_API spo_result spo_option_set(spo_id, int32_t);
SPO_API spo_result spo_counter_set(spo_id, double);
SPO_API spo_result spo_indicator_set(spo_id, uint32_t);
SPO_API int32_t spo_handle_input(uint32_t, spo_button, uint32_t);
SPO_API int32_t spo_handle_axis(uint32_t, spo_axis, int32_t);
SPO_API void spo_input_reset(uint32_t);
SPO_API spo_result spo_input_sync(uint32_t, uint32_t, const int32_t[SPO_AXIS_COUNT]);
SPO_API int32_t spo_menu_is_open(void);
SPO_API int32_t spo_input_captured(void);
SPO_API spo_result spo_frame(uint32_t, uint32_t);
SPO_API void spo_reset_timing(void);
SPO_API void spo_context_lost(void);
SPO_API spo_result spo_get_stats(spo_stats *);
SPO_API spo_result spo_sample(void);
SPO_API spo_result spo_get_sampling_stats(spo_sampling_stats *);
SPO_API int32_t spo_config_dirty(void);
SPO_API spo_result spo_config_save(void);
SPO_API spo_result spo_last_error(void);
SPO_API const char *spo_version(void);
SPO_API spo_result spo_overlay_get(spo_overlay_settings *);
SPO_API spo_result spo_overlay_set(const spo_overlay_settings *);
SPO_API spo_result spo_preset_set(uint32_t);
SPO_API spo_result spo_target_fps_set(uint32_t);
union SDL_Event;
SPO_API int32_t spo_handle_sdl_event(const union SDL_Event *);
#ifdef __cplusplus
}
#endif
#endif
