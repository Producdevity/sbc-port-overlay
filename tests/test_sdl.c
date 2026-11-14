/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "spo.h"
#include <SDL.h>
#include <assert.h>
#include <string.h>
static int button(Uint32 type, Uint8 value) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.cbutton.which = 7;
    event.cbutton.button = value;
    return spo_handle_sdl_event(&event);
}
int main(void) {
    spo_config config = {0};
    SDL_Event event;
    config.struct_size = sizeof(config);
    config.abi_version = SPO_ABI_VERSION;
    config.flags = SPO_INIT_NO_RENDER | SPO_INIT_NO_SAMPLER_THREAD;
    assert(spo_init(&config) == SPO_OK);
    button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_START);
    button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_START);
    assert(!spo_menu_is_open());
    button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_GUIDE);
    button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_START);
    button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_GUIDE);
    button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_START);
    assert(!spo_menu_is_open());
    button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_GUIDE);
    assert(button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_GUIDE));
    assert(spo_menu_is_open());
    memset(&event, 0, sizeof(event));
    event.type = SDL_JOYBUTTONDOWN;
    assert(spo_handle_sdl_event(&event));
#if SDL_VERSION_ATLEAST(2, 0, 14)
    event.type = SDL_CONTROLLERSENSORUPDATE;
    assert(spo_handle_sdl_event(&event));
    event.type = SDL_CONTROLLERTOUCHPADMOTION;
    assert(spo_handle_sdl_event(&event));
#endif
    event.type = SDL_QUIT;
    assert(!spo_handle_sdl_event(&event));
    button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_B);
    assert(button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_B));
    assert(!spo_input_captured());
    event.type = SDL_JOYBUTTONDOWN;
    assert(!spo_handle_sdl_event(&event));
    spo_shutdown();
    return 0;
}
