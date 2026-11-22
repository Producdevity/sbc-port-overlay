/* SPDX-License-Identifier: MIT */
/* SDL3 counterpart of sdl2.c for the second screen only: a host that maps its own controller events
   (as Starlet's does) calls spo_handle_sdl3_screen_event with every event. spo_map_sdl3_event
   applies "Face buttons" to a gamepad event. */
#include "spo.h"
#include <SDL3/SDL.h>
#define SPO_SDL3 1
#include "screen.c"
int32_t spo_handle_sdl3_screen_event(const union SDL_Event *event) {
    int32_t consumed = 0;
    if (!event)
        return 0;
    debug_event(event);
    return screen_event(event, &consumed) ? consumed : 0;
}
void spo_map_sdl3_event(union SDL_Event *event) {
    if (event && (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                  event->type == SDL_EVENT_GAMEPAD_BUTTON_UP))
        event->gbutton.button = (Uint8)spo_face_button(event->gbutton.button);
}
