/* SPDX-License-Identifier: MIT */
#include "spo.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <string.h>

/* Without a screen window, events are none of the overlay's business. */
int main(void) {
    spo_config config = {0};
    SDL_Event e;
    config.struct_size = sizeof(config);
    config.abi_version = SPO_ABI_VERSION;
    config.flags = SPO_INIT_NO_RENDER | SPO_INIT_NO_SAMPLER_THREAD;
    assert(spo_init(&config) == SPO_OK);
    assert(!spo_handle_sdl3_screen_event(NULL));
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_FINGER_DOWN;
    e.tfinger.windowID = 5;
    assert(!spo_handle_sdl3_screen_event(&e));
    e.type = SDL_EVENT_WINDOW_RESIZED;
    e.window.windowID = 5;
    assert(!spo_handle_sdl3_screen_event(&e));
    e.type = SDL_EVENT_MOUSE_MOTION;
    assert(!spo_handle_sdl3_screen_event(&e));
    spo_shutdown();
    return 0;
}
