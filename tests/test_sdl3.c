/* SPDX-License-Identifier: MIT */
#include "spo.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int mapped(Uint32 type, Uint8 value) {
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.gbutton.which = 3;
    e.gbutton.button = value;
    e.gbutton.down = type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    spo_map_sdl3_event(&e);
    assert(e.type == type && e.gbutton.which == 3);
    assert(e.gbutton.down == (type == SDL_EVENT_GAMEPAD_BUTTON_DOWN));
    return e.gbutton.button;
}
static void face_buttons(void) {
    char path[] = "/tmp/spo-sdl3-face-XXXXXX";
    int fd = mkstemp(path), i;
    spo_config config = {0};
    SDL_Event e;
    assert(fd >= 0);
    config.struct_size = sizeof(config);
    config.abi_version = SPO_ABI_VERSION;
    config.flags = SPO_INIT_NO_RENDER | SPO_INIT_NO_SAMPLER_THREAD;
    config.config_path = path;
    assert(write(fd, "[spo]\nface_buttons=0\n", 21) == 21);
    close(fd);
    assert(spo_init(&config) == SPO_OK);
    for (i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
        assert(mapped(SDL_EVENT_GAMEPAD_BUTTON_DOWN, (Uint8)i) == i &&
               mapped(SDL_EVENT_GAMEPAD_BUTTON_UP, (Uint8)i) == i);
    spo_map_sdl3_event(NULL);
    spo_shutdown();
    fd = open(path, O_WRONLY | O_TRUNC);
    assert(fd >= 0 && write(fd, "[spo]\nface_buttons=1\n", 21) == 21);
    close(fd);
    assert(spo_init(&config) == SPO_OK && spo_face_buttons_swapped());
    assert(mapped(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_SOUTH) ==
           SDL_GAMEPAD_BUTTON_EAST);
    assert(mapped(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_EAST) ==
           SDL_GAMEPAD_BUTTON_SOUTH);
    assert(mapped(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_WEST) ==
           SDL_GAMEPAD_BUTTON_NORTH);
    assert(mapped(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_NORTH) ==
           SDL_GAMEPAD_BUTTON_WEST);
    for (i = SDL_GAMEPAD_BUTTON_BACK; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
        assert(mapped(SDL_EVENT_GAMEPAD_BUTTON_DOWN, (Uint8)i) == i);
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    e.gaxis.axis = 1;
    e.gaxis.value = 1234;
    spo_map_sdl3_event(&e);
    assert(e.gaxis.axis == 1 && e.gaxis.value == 1234);
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    spo_map_sdl3_event(&e);
    assert(e.jbutton.button == 0);
    spo_shutdown();
    unlink(path);
}
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
    face_buttons();
    return 0;
}
