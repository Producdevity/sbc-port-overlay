/* SPDX-License-Identifier: MIT */
/* An SDL2 application that does not link the overlay, run with libspo_preload.so in LD_PRELOAD:
   the shim must swallow the menu's input, save a setting declared in a settings file and apply
   "Face buttons" to events and controller state, once per read. */
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void swap(SDL_Window *w) {
    SDL_GL_SwapWindow(w);
}
static void push_button(SDL_GameControllerButton b, int up) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = up ? SDL_CONTROLLERBUTTONUP : SDL_CONTROLLERBUTTONDOWN;
    e.cbutton.which = 1;
    e.cbutton.button = (Uint8)b;
    assert(SDL_PushEvent(&e) == 1);
}
/* SDL 2.26 can return 0 from one SDL_PollEvent that follows a SDL_WaitEvent, with an event
   queued. */
static int poll_event(SDL_Event *e) {
    int tries;
    for (tries = 0; tries < 5; tries++)
        if (SDL_PollEvent(e))
            return 1;
    return 0;
}
/* Poll and wait read the same queue; each event is swapped once, whichever call reads it. */
static void face_buttons(void) {
    SDL_Event e;
    int i;
    for (i = 0; i < 2; i++) {
        push_button(SDL_CONTROLLER_BUTTON_A, i);
        assert(poll_event(&e) && e.type == (i ? SDL_CONTROLLERBUTTONUP : SDL_CONTROLLERBUTTONDOWN));
        assert(e.cbutton.button == SDL_CONTROLLER_BUTTON_B);
        push_button(SDL_CONTROLLER_BUTTON_Y, i);
        assert(SDL_WaitEventTimeout(&e, 1000) && e.cbutton.button == SDL_CONTROLLER_BUTTON_X);
        push_button(SDL_CONTROLLER_BUTTON_X, i);
        assert(SDL_WaitEvent(&e) && e.cbutton.button == SDL_CONTROLLER_BUTTON_Y);
        push_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, i);
        assert(poll_event(&e) && e.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    }
#if SDL_VERSION_ATLEAST(2, 0, 14)
    {
        SDL_GameController *c;
        SDL_Joystick *j;
        int index;
        assert(SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0);
        index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 16, 0);
        assert(index >= 0);
        c = SDL_GameControllerOpen(index);
        j = SDL_GameControllerGetJoystick(c);
        assert(c && j);
        assert(SDL_JoystickSetVirtualButton(j, 0, 1) == 0); /* the physical A position */
        SDL_GameControllerUpdate();
        assert(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A) == 0);
        assert(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B) == 1);
        assert(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X) == 0);
        assert(SDL_JoystickSetVirtualButton(j, 0, 0) == 0);
        assert(SDL_JoystickSetVirtualButton(j, 3, 1) == 0); /* Y */
        SDL_GameControllerUpdate();
        assert(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y) == 0);
        assert(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X) == 1);
        SDL_GameControllerClose(c);
        SDL_JoystickDetachVirtual(index);
    }
#endif
}
static void pad(SDL_GameControllerButton b) {
    SDL_Event e;
    int i;
    for (i = 0; i < 2; i++) {
        memset(&e, 0, sizeof e);
        e.type = i ? SDL_CONTROLLERBUTTONUP : SDL_CONTROLLERBUTTONDOWN;
        e.cbutton.which = 1;
        e.cbutton.button = (Uint8)b;
        assert(SDL_PushEvent(&e) == 1);
    }
}
int main(void) {
    char config[] = "/tmp/spo-shim-XXXXXX", settings[] = "/tmp/spo-shim-XXXXXX";
    char text[2048];
    SDL_Window *window;
    SDL_Event e;
    FILE *f;
    int fd, seen_user = 0, leaked = 0, i;
    fd = mkstemp(config);
    assert(fd >= 0);
    close(fd);
    fd = mkstemp(settings);
    assert(fd >= 0);
    f = fdopen(fd, "w");
    fputs("[page t]\nlabel = Test\n[option t.k]\ntype = toggle\nlabel = Knob\ndefault = 0\n", f);
    fclose(f);
    f = fopen(config, "w");
    assert(f);
    fputs("[spo]\nformat=1\nface_buttons=1\n", f);
    fclose(f);
    setenv("SPO_CONFIG", config, 1);
    setenv("SPO_SETTINGS", settings, 1);
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    window = SDL_CreateWindow("shim", 0, 0, 320, 240, SDL_WINDOW_OPENGL);
    assert(window && SDL_GL_CreateContext(window));
    for (i = 0; i < 3; i++)
        swap(window);
    pad(SDL_CONTROLLER_BUTTON_GUIDE);
    pad(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    pad(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    pad(SDL_CONTROLLER_BUTTON_A);
    pad(SDL_CONTROLLER_BUTTON_GUIDE);
    memset(&e, 0, sizeof e);
    e.type = SDL_USEREVENT;
    assert(SDL_PushEvent(&e) == 1);
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_USEREVENT)
            seen_user = 1;
        /* The first Guide press is passed on until its release shows it was a tap. */
        if ((e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) &&
            e.cbutton.button != SDL_CONTROLLER_BUTTON_GUIDE)
            leaked++;
    }
    assert(seen_user && !leaked);
    f = fopen(config, "r");
    assert(f);
    memset(text, 0, sizeof text);
    assert(fread(text, 1, sizeof text - 1, f) > 0);
    fclose(f);
    assert(strstr(text, "[port.t]") && strstr(text, "k=1") && strstr(text, "face_buttons=1"));
    face_buttons();
    unlink(config);
    unlink(settings);
    SDL_Quit();
    return 0;
}
