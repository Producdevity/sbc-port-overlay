/* SPDX-License-Identifier: MIT */
#include "spo.h"
#include <SDL.h>
static const spo_button buttons[] = {
    SPO_SOUTH, SPO_EAST, SPO_WEST,    SPO_NORTH,   SPO_SELECT,  SPO_GUIDE,   SPO_START,
    SPO_L3,    SPO_R3,   SPO_L1,      SPO_R1,      SPO_UP,      SPO_DOWN,    SPO_LEFT,
    SPO_RIGHT, SPO_MISC, SPO_PADDLE1, SPO_PADDLE2, SPO_PADDLE3, SPO_PADDLE4, SPO_TOUCHPAD};
static const spo_axis axes[] = {SPO_LX, SPO_LY, SPO_RX, SPO_RY, SPO_LT, SPO_RT};
static void sync_controller(SDL_JoystickID id) {
    SDL_GameController *controller = SDL_GameControllerFromInstanceID(id);
    SDL_version version;
    uint32_t held = 0, i, count;
    int32_t values[SPO_AXIS_COUNT] = {0};
    if (!controller) {
        spo_input_reset((uint32_t)id);
        return;
    }
    SDL_GetVersion(&version);
    count = SDL_VERSIONNUM(version.major, version.minor, version.patch) >= SDL_VERSIONNUM(2, 0, 14)
                ? 21
                : 15;
    for (i = 0; i < count; i++)
        if (SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)i))
            held |= 1u << buttons[i];
    for (i = 0; i < SPO_AXIS_COUNT; i++)
        values[axes[i]] = SDL_GameControllerGetAxis(controller, (SDL_GameControllerAxis)i);
    spo_input_sync((uint32_t)id, held, values);
}
int32_t spo_handle_sdl_event(const union SDL_Event *event) {
    if (!event)
        return 0;
    switch (event->type) {
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        return spo_handle_input((uint32_t)event->cbutton.which,
                                event->cbutton.button < sizeof(buttons) / sizeof(buttons[0])
                                    ? buttons[event->cbutton.button]
                                    : SPO_OTHER,
                                event->type == SDL_CONTROLLERBUTTONDOWN);
    case SDL_CONTROLLERAXISMOTION:
        if (event->caxis.axis >= sizeof(axes) / sizeof(axes[0]))
            return spo_input_captured();
        return spo_handle_axis((uint32_t)event->caxis.which, axes[event->caxis.axis],
                               event->caxis.value);
    case SDL_CONTROLLERDEVICEREMOVED:
        spo_input_reset((uint32_t)event->cdevice.which);
        return 0;
    case SDL_CONTROLLERDEVICEADDED: {
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(event->cdevice.which);
        if (id >= 0)
            sync_controller(id);
        return 0;
    }
    case SDL_CONTROLLERDEVICEREMAPPED:
        sync_controller(event->cdevice.which);
        return 0;
#if SDL_VERSION_ATLEAST(2, 0, 14)
    case SDL_CONTROLLERTOUCHPADDOWN:
    case SDL_CONTROLLERTOUCHPADUP:
    case SDL_CONTROLLERTOUCHPADMOTION:
    case SDL_CONTROLLERSENSORUPDATE:
        return spo_input_captured();
#endif
    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            spo_input_reset(SPO_ALL_CONTROLLERS);
        return 0;
    case SDL_JOYAXISMOTION:
    case SDL_JOYBALLMOTION:
    case SDL_JOYHATMOTION:
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        return spo_input_captured();
    default:
        return 0;
    }
}
