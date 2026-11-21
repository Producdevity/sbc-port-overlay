/* SPDX-License-Identifier: MIT */
/* An SDL2 application that does not link the overlay, run with libspo_preload.so in LD_PRELOAD:
   the shim must swallow the menu's input and save a setting declared in a settings file. */
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void swap(SDL_Window *w) {
    SDL_GL_SwapWindow(w);
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
    char text[512];
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
    unlink(config);
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
    assert(strstr(text, "[port.t]") && strstr(text, "k=1"));
    unlink(config);
    unlink(settings);
    SDL_Quit();
    return 0;
}
