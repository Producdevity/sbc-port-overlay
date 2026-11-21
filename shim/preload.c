/* SPDX-License-Identifier: MIT */
/* LD_PRELOAD shim: draws the overlay in an SDL2 OpenGL or GLES application that does not link
   the library, and feeds it the application's events.
   Environment: SPO_CONFIG (settings file path), SPO_SETTINGS (declaration file, see docs),
   SPO_PORT_NAME, SPO_PORT_VERSION. */
#define _GNU_SOURCE
#include "settings.h"
#include <SDL.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

#define EXPORT __attribute__((visibility("default")))
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define NEXT(type, name) \
    static type fn; \
    if (!fn) \
        fn = (type)dlsym(RTLD_NEXT, name)

typedef void (*swap_fn)(SDL_Window *);
typedef int (*poll_fn)(SDL_Event *);
typedef int (*wait_fn)(SDL_Event *, int);
typedef Uint8 (*button_fn)(SDL_GameController *, SDL_GameControllerButton);
typedef Sint16 (*axis_fn)(SDL_GameController *, SDL_GameControllerAxis);
typedef const Uint8 *(*keys_fn)(int *);

static SDL_Window *game_window;
static pthread_t game_thread;
static int state; /* 0 not tried, 1 active, -1 failed */
static void (*get_integerv)(unsigned, int *);
static void (*bind_framebuffer)(unsigned, unsigned);

static void *resolve(const char *name, void *user) {
    (void)user;
    return SDL_GL_GetProcAddress(name);
}
static void save_config(void) {
    if (state == 1 && spo_config_dirty())
        spo_config_save();
}
static void start(SDL_Window *window) {
    spo_config config = {0};
    const char *settings = getenv("SPO_SETTINGS"), *name = getenv("SPO_PORT_NAME");
    state = -1;
    get_integerv = (void (*)(unsigned, int *))SDL_GL_GetProcAddress("glGetIntegerv");
    bind_framebuffer = (void (*)(unsigned, unsigned))SDL_GL_GetProcAddress("glBindFramebuffer");
    if (!get_integerv || !bind_framebuffer)
        return;
    config.struct_size = sizeof(config);
    config.abi_version = SPO_ABI_VERSION;
    config.get_proc_address = resolve;
    config.config_path = getenv("SPO_CONFIG");
    config.port_name = name ? name : "Port";
    config.port_version = getenv("SPO_PORT_VERSION");
    if (spo_init(&config) != SPO_OK)
        return;
    if (settings)
        spo_settings_load(settings);
    game_window = window;
    game_thread = pthread_self();
    atexit(save_config);
    state = 1;
}
EXPORT void SDL_GL_SwapWindow(SDL_Window *window) {
    NEXT(swap_fn, "SDL_GL_SwapWindow");
    if (!state && SDL_GL_GetCurrentContext())
        start(window);
    if (state == 1 && window == game_window && pthread_equal(pthread_self(), game_thread) &&
        SDL_GL_GetCurrentContext()) {
        int w = 0, h = 0, fbo = 0;
        SDL_GL_GetDrawableSize(window, &w, &h);
        get_integerv(GL_FRAMEBUFFER_BINDING, &fbo);
        bind_framebuffer(GL_FRAMEBUFFER, 0);
        spo_frame((uint32_t)w, (uint32_t)h);
        bind_framebuffer(GL_FRAMEBUFFER, (unsigned)fbo);
    }
    fn(window);
}
/* A consumed event is dropped and the next one is read. */
EXPORT int SDL_PollEvent(SDL_Event *event) {
    int r;
    NEXT(poll_fn, "SDL_PollEvent");
    while ((r = fn(event)) && event && state == 1 && spo_handle_sdl_event(event))
        ;
    return r;
}
EXPORT int SDL_WaitEventTimeout(SDL_Event *event, int timeout) {
    int r;
    Uint32 end = SDL_GetTicks() + (Uint32)timeout;
    NEXT(wait_fn, "SDL_WaitEventTimeout");
    while ((r = fn(event, timeout)) && event && state == 1 && spo_handle_sdl_event(event)) {
        if (timeout < 0)
            continue;
        if ((Sint32)(end - SDL_GetTicks()) <= 0)
            return 0;
        timeout = (int)(end - SDL_GetTicks());
    }
    return r;
}
EXPORT int SDL_WaitEvent(SDL_Event *event) {
    return SDL_WaitEventTimeout(event, -1);
}
/* The menu owns the controller while it is open; applications that read state instead of
   events must not see it. */
EXPORT Uint8 SDL_GameControllerGetButton(SDL_GameController *c, SDL_GameControllerButton b) {
    NEXT(button_fn, "SDL_GameControllerGetButton");
    return state == 1 && spo_input_captured() ? 0 : fn(c, b);
}
EXPORT Sint16 SDL_GameControllerGetAxis(SDL_GameController *c, SDL_GameControllerAxis a) {
    NEXT(axis_fn, "SDL_GameControllerGetAxis");
    return state == 1 && spo_input_captured() ? 0 : fn(c, a);
}
EXPORT const Uint8 *SDL_GetKeyboardState(int *count) {
    static const Uint8 none[SDL_NUM_SCANCODES];
    const Uint8 *keys;
    NEXT(keys_fn, "SDL_GetKeyboardState");
    keys = fn(count);
    return state == 1 && spo_input_captured() ? none : keys;
}
