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
#define SPO_VERSION "0.2.1"
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
    SPO_BUSY = -12,
    SPO_UNSUPPORTED = -13
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
/* The "Face buttons" option of the Overlay tab. Swapped exchanges A with B and X with Y for this
   process, for ports that expect SDL's positional layout (A at the bottom) on a system whose
   controller mapping puts A on the right. The overlay's own menu does not use it. */
SPO_API int32_t spo_face_buttons_swapped(void);
/* Maps an SDL controller button number: 0 (A) and 1 (B) exchange, 2 (X) and 3 (Y) exchange, and
   every other number, or any number while the option is off, comes back unchanged. The numbers
   are the same for SDL_GameControllerButton and SDL_GamepadButton, and the mapping is its own
   inverse. Any thread. */
SPO_API uint32_t spo_face_button(uint32_t);
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
SPO_API spo_result spo_notice(const char *);
union SDL_Event;
SPO_API int32_t spo_handle_sdl_event(const union SDL_Event *);
/* Applies "Face buttons" to an SDL2 SDL_CONTROLLERBUTTONDOWN or UP event in place. Call it on
   events spo_handle_sdl_event did not consume, after that call. Other events are left alone. */
SPO_API void spo_map_sdl_event(union SDL_Event *);
/* SDL3 hosts: src/sdl3.c has the second screen only. Pass it every event; the result is nonzero
   when the overlay keeps the event from the game. Controller events go to spo_handle_input,
   spo_handle_axis and spo_input_sync as for any host without src/sdl2.c. */
SPO_API int32_t spo_handle_sdl3_screen_event(const union SDL_Event *);
/* SDL3 counterpart of spo_map_sdl_event for SDL_EVENT_GAMEPAD_BUTTON_DOWN and UP. */
SPO_API void spo_map_sdl3_event(union SDL_Event *);
/* Second screen, provided by the SDL adapter (src/sdl2.c). The overlay opens it by itself on a
   device with two display panels and draws there instead of over the game; these calls are for a
   port that wants to draw its own content on it. */
enum { SPO_TOUCH_DOWN, SPO_TOUCH_MOVE, SPO_TOUCH_UP };
enum { SPO_SCREEN_TOUCH = 1, SPO_SCREEN_NO_OVERLAY = 2 };
typedef struct spo_screen_info {
    uint32_t struct_size, width, height, flags; /* flags: SPO_SCREEN_TOUCH once a touch arrived */
} spo_screen_info;
/* Runs on the screen's own thread with its GL context current; that context shares objects with
   the game's. The target is cleared first; the overlay draws afterwards. Flush the game's context
   before the screen reads what it wrote. */
typedef void (*spo_screen_draw_fn)(const spo_screen_info *, void *);
/* Called from spo_handle_sdl_event; x and y run 0..1 across the screen. */
typedef void (*spo_screen_touch_fn)(uint32_t finger, uint32_t phase, float x, float y, void *);
typedef struct spo_screen_desc {
    uint32_t struct_size, flags; /* SPO_SCREEN_NO_OVERLAY keeps the overlay on the game screen */
    spo_screen_draw_fn draw;
    spo_screen_touch_fn touch;
    void *user;
    uint32_t max_hz; /* draw rate cap, 0 for 30 */
} spo_screen_desc;
/* After spo_init, on the thread where the game's GL context is current. SPO_UNSUPPORTED: there is
   no second screen, or SPO_SECOND_SCREEN=off, or it could not be opened. */
SPO_API spo_result spo_screen_open(const spo_screen_desc *);
SPO_API void spo_screen_close(void);
SPO_API spo_result spo_screen_info_get(spo_screen_info *);
SPO_API void spo_screen_invalidate(void); /* any thread: draw again soon */
#ifdef __cplusplus
}
#endif
#endif
