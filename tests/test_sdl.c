/* SPDX-License-Identifier: MIT */
#include "spo.h"
#include <SDL.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
static int pad_button(SDL_JoystickID which, Uint32 type, Uint8 value) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.cbutton.which = which;
    event.cbutton.button = value;
    return spo_handle_sdl_event(&event);
}
static int button(Uint32 type, Uint8 value) {
    return pad_button(7, type, value);
}
#if SDL_VERSION_ATLEAST(2, 0, 14)
static void added_before_open(void) {
    SDL_Event event;
    SDL_JoystickID id;
    int index;
    assert(SDL_Init(SDL_INIT_JOYSTICK) == 0);
    index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 0, 16, 0);
    assert(index >= 0);
    id = SDL_JoystickGetDeviceInstanceID(index);
    assert(id >= 0 && !SDL_GameControllerFromInstanceID(id));
    memset(&event, 0, sizeof(event));
    event.type = SDL_CONTROLLERDEVICEADDED;
    event.cdevice.which = index;
    assert(!spo_handle_sdl_event(&event));
    pad_button(id, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_GUIDE);
    pad_button(id, SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_GUIDE);
    assert(spo_menu_is_open());
    pad_button(id, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_GUIDE);
    pad_button(id, SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_GUIDE);
    assert(!spo_menu_is_open());
    SDL_JoystickDetachVirtual(index);
    SDL_Quit();
}
#endif
static int mapped(Uint32 type, Uint8 value) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.cbutton.which = 7;
    event.cbutton.button = value;
    event.cbutton.state = type == SDL_CONTROLLERBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    spo_map_sdl_event(&event);
    assert(event.type == type && event.cbutton.which == 7);
    assert(event.cbutton.state == (type == SDL_CONTROLLERBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED));
    return event.cbutton.button;
}
static spo_config face_config(const char *path) {
    spo_config config = {0};
    config.struct_size = sizeof(config);
    config.abi_version = SPO_ABI_VERSION;
    config.flags = SPO_INIT_NO_RENDER | SPO_INIT_NO_SAMPLER_THREAD;
    config.config_path = path;
    return config;
}
static void face_buttons(void) {
    char path[] = "/tmp/spo-sdl-face-XXXXXX";
    int fd = mkstemp(path), i;
    spo_config config = face_config(path);
    SDL_Event event;
    assert(fd >= 0);
    /* As set: no event changes. */
    assert(write(fd, "[spo]\nface_buttons=0\n", 21) == 21);
    close(fd);
    assert(spo_init(&config) == SPO_OK && !spo_face_buttons_swapped());
    for (i = 0; i < 21; i++)
        assert(mapped(SDL_CONTROLLERBUTTONDOWN, (Uint8)i) == i &&
               mapped(SDL_CONTROLLERBUTTONUP, (Uint8)i) == i);
    spo_map_sdl_event(NULL);
    spo_shutdown();
    /* Swapped: A with B and X with Y, on press and release; nothing else moves. */
    fd = open(path, O_WRONLY | O_TRUNC);
    assert(fd >= 0 && write(fd, "[spo]\nface_buttons=1\n", 21) == 21);
    close(fd);
    assert(spo_init(&config) == SPO_OK && spo_face_buttons_swapped());
    assert(mapped(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A) == SDL_CONTROLLER_BUTTON_B);
    assert(mapped(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_B) == SDL_CONTROLLER_BUTTON_A);
    assert(mapped(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_X) == SDL_CONTROLLER_BUTTON_Y);
    assert(mapped(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_Y) == SDL_CONTROLLER_BUTTON_X);
    for (i = 4; i < 21; i++)
        assert(mapped(SDL_CONTROLLERBUTTONDOWN, (Uint8)i) == i);
    /* Other events are left alone. */
    memset(&event, 0, sizeof(event));
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.axis = 1;
    event.caxis.value = 1234;
    spo_map_sdl_event(&event);
    assert(event.caxis.axis == 1 && event.caxis.value == 1234);
    memset(&event, 0, sizeof(event));
    event.type = SDL_JOYBUTTONDOWN;
    spo_map_sdl_event(&event);
    assert(event.jbutton.button == 0);
    /* The overlay reads the events as the system reports them: Guide still opens the menu,
       and the menu's Back (B, East) still closes it. */
    pad_button(7, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_GUIDE);
    pad_button(7, SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_GUIDE);
    assert(spo_menu_is_open());
    pad_button(7, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_B);
    pad_button(7, SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_B);
    assert(!spo_menu_is_open());
    spo_shutdown();
    unlink(path);
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
#if SDL_VERSION_ATLEAST(2, 0, 14)
    added_before_open();
#endif
    spo_shutdown();
    face_buttons();
    return 0;
}
