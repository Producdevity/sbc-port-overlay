/* SPDX-License-Identifier: MIT */
/* The second screen as a second SDL window. Runs under Xvfb with Mesa; SPO_SECOND_SCREEN=force
   stands in for the DRM panel check, which a container cannot satisfy. */
#include "spo.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <stdint.h>

typedef unsigned int GLuint;
typedef unsigned char GLboolean;
static void (*gen_textures)(int, GLuint *);
static GLboolean (*is_texture)(GLuint);
static unsigned (*get_error)(void);
static void (*flush)(void);
static void (*bind_texture)(unsigned, GLuint);

static SDL_Window *game;
static SDL_GLContext game_context;
static GLuint shared_texture;
static SDL_atomic_t draws, shared_seen, errors, other_thread, wrong_context;
static SDL_atomic_t touches, last_phase;
static SDL_threadID main_thread;
static float last_x, last_y;
static uint32_t last_width, last_height;

static void *proc(const char *name, void *user) {
    (void)user;
    return SDL_GL_GetProcAddress(name);
}
static void draw(const spo_screen_info *info, void *user) {
    (void)user;
    SDL_AtomicAdd(&draws, 1);
    last_width = info->width;
    last_height = info->height;
    if (SDL_ThreadID() != main_thread)
        SDL_AtomicAdd(&other_thread, 1);
    if (SDL_GL_GetCurrentContext() == game_context)
        SDL_AtomicAdd(&wrong_context, 1);
    if (is_texture(shared_texture))
        SDL_AtomicAdd(&shared_seen, 1);
    if (get_error())
        SDL_AtomicAdd(&errors, 1);
}
static void touched(uint32_t finger, uint32_t phase, float x, float y, void *user) {
    (void)user;
    assert(finger == 0);
    last_x = x;
    last_y = y;
    SDL_AtomicSet(&last_phase, (int)phase);
    SDL_AtomicAdd(&touches, 1);
}
static void wait_for(SDL_atomic_t *value, int at_least) {
    for (int i = 0; i < 400 && SDL_AtomicGet(value) < at_least; i++)
        SDL_Delay(10);
    assert(SDL_AtomicGet(value) >= at_least);
}
static Uint32 secondary_id(void) {
    for (Uint32 id = 1; id < 16; id++) {
        SDL_Window *window = SDL_GetWindowFromID(id);
        if (window && window != game && !strcmp(SDL_GetWindowTitle(window), "Secondary"))
            return id;
    }
    return 0;
}
/* The second window's pixels as the X server holds them: how many are not black. */
static long lit_pixels(Uint32 id) {
    SDL_SysWMinfo wm;
    XImage *image;
    long lit = 0;
    int w, h;
    SDL_Window *window = SDL_GetWindowFromID(id);
    SDL_VERSION(&wm.version);
    assert(window && SDL_GetWindowWMInfo(window, &wm) && wm.subsystem == SDL_SYSWM_X11);
    SDL_GetWindowSize(window, &w, &h);
    image = XGetImage(wm.info.x11.display, wm.info.x11.window, 0, 0, (unsigned)w, (unsigned)h,
                      AllPlanes, ZPixmap);
    assert(image);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            lit += XGetPixel(image, x, y) & 0xffffff ? 1 : 0;
    XDestroyImage(image);
    return lit;
}
static int finger(Uint32 type, Uint32 window, float x, float y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.tfinger.windowID = window;
    event.tfinger.fingerId = 5;
    event.tfinger.x = x;
    event.tfinger.y = y;
    return spo_handle_sdl_event(&event);
}

/* A stand-in for sway's IPC socket: DSI-2 is on, DSI-1 is off; RUN_COMMAND payloads are recorded. */
static char sway_path[108];
static int sway_listener;
static char commands[8][128];
static volatile int command_count;
static const char outputs_json[] =
    "[\n  {\n    \"id\": 4,\n    \"type\": \"output\",\n    \"name\": \"DSI-2\",\n    \"nodes\": [],\n"
    "    \"modes\": [\n      {\n        \"width\": 640\n      }\n    ],\n    \"active\": true,\n"
    "    \"power\": true\n  },\n  {\n    \"id\": 5,\n    \"type\": \"output\",\n    \"name\": \"DSI-1\",\n"
    "    \"nodes\": [],\n    \"window\": null,\n    \"active\": true,\n    \"power\": false\n  }\n]";
static void *sway_server(void *unused) {
    (void)unused;
    for (;;) {
        int fd = accept(sway_listener, NULL, NULL);
        unsigned char header[14];
        if (fd < 0)
            return NULL;
        while (read(fd, header, 14) == 14) {
            uint32_t length, type;
            char payload[256] = "";
            memcpy(&length, header + 6, 4);
            memcpy(&type, header + 10, 4);
            if (length && read(fd, payload, length < 255 ? length : 255) <= 0)
                break;
            if (type == 3) {
                uint32_t n = (uint32_t)strlen(outputs_json);
                memcpy(header + 6, &n, 4);
                write(fd, header, 14);
                write(fd, outputs_json, n);
            } else {
                static const char ok[] = "[{\"success\":true}]";
                uint32_t n = sizeof(ok) - 1;
                if (command_count < 8)
                    snprintf(commands[command_count++], sizeof(commands[0]), "%s", payload);
                memcpy(header + 6, &n, 4);
                write(fd, header, 14);
                write(fd, ok, n);
            }
        }
        close(fd);
    }
}
static void start_fake_sway(void) {
    struct sockaddr_un address;
    pthread_t thread;
    snprintf(sway_path, sizeof(sway_path), "/tmp/spo-sway-%d.sock", (int)getpid());
    unlink(sway_path);
    sway_listener = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", sway_path);
    assert(bind(sway_listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    assert(listen(sway_listener, 4) == 0);
    pthread_create(&thread, NULL, sway_server, NULL);
    setenv("SWAYSOCK", sway_path, 1);
}
static void wait_commands(int at_least) {
    for (int i = 0; i < 300 && command_count < at_least; i++)
        SDL_Delay(10);
    assert(command_count >= at_least);
}
static spo_config config(void) {
    spo_config c;
    memset(&c, 0, sizeof(c));
    c.struct_size = sizeof(c);
    c.abi_version = SPO_ABI_VERSION;
    c.get_proc_address = proc;
    c.config_path = NULL;
    c.port_name = "Screen test";
    c.port_version = "1";
    c.flags = SPO_INIT_NO_SAMPLER_THREAD;
    return c;
}
static void tap_guide(void) {
    spo_handle_input(1, SPO_GUIDE, 1);
    spo_handle_input(1, SPO_GUIDE, 0);
}
static void unsupported(const char *mode) {
    spo_config c = config();
    spo_screen_desc desc = {sizeof(desc), 0, draw, touched, NULL, 0};
    spo_screen_info info = {sizeof(info), 0, 0, 0};
    if (mode)
        setenv("SPO_SECOND_SCREEN", mode, 1);
    else
        unsetenv("SPO_SECOND_SCREEN");
    assert(spo_init(&c) == SPO_OK);
    assert(spo_screen_open(&desc) == SPO_UNSUPPORTED);
    assert(spo_screen_info_get(&info) == SPO_UNSUPPORTED);
    assert(!secondary_id());
    spo_preset_set(4);
    for (int i = 0; i < 5; i++)
        assert(spo_frame(640, 480) == SPO_OK);
    {
        spo_stats stats = {sizeof(stats)};
        assert(spo_get_stats(&stats) == SPO_OK && stats.draw_calls == 1 && stats.vertices > 0);
    }
    assert(!secondary_id());
    spo_shutdown();
}
int main(int argc, char **argv) {
    spo_config c = config();
    spo_screen_desc desc = {sizeof(desc), 0, draw, touched, NULL, 30};
    spo_screen_info info = {sizeof(info), 0, 0, 0};
    Uint32 id;
    spo_stats stats = {sizeof(stats)};
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    game = SDL_CreateWindow("Game", 0, 0, 640, 480, SDL_WINDOW_OPENGL);
    assert(game);
    game_context = SDL_GL_CreateContext(game);
    assert(game_context);
    main_thread = SDL_ThreadID();
    gen_textures = (void (*)(int, GLuint *))SDL_GL_GetProcAddress("glGenTextures");
    is_texture = (GLboolean(*)(GLuint))SDL_GL_GetProcAddress("glIsTexture");
    get_error = (unsigned (*)(void))SDL_GL_GetProcAddress("glGetError");
    flush = (void (*)(void))SDL_GL_GetProcAddress("glFlush");
    bind_texture = (void (*)(unsigned, GLuint))SDL_GL_GetProcAddress("glBindTexture");
    assert(bind_texture && gen_textures && is_texture && get_error && flush);
    gen_textures(1, &shared_texture);
    assert(shared_texture);
    bind_texture(0x0de1, shared_texture); /* a name exists once it is bound */
    flush();
    if (argc > 1 && !strcmp(argv[1], "crash")) {
        /* Dies with the screen open: only the helper process can put the output back. */
        setenv("SPO_SECOND_SCREEN", "force", 1);
        assert(spo_init(&c) == SPO_OK && spo_screen_open(&desc) == SPO_OK);
        SDL_Delay(300);
        _exit(0);
    }
    /* A single-screen device, and a screen switched off, keep the overlay over the game. */
    unsupported(NULL);
    unsupported("off");
    start_fake_sway();
    setenv("SPO_SECOND_SCREEN", "force", 1);
    assert(spo_init(&c) == SPO_OK);
    assert(spo_screen_open(&desc) == SPO_OK);
    id = secondary_id();
    assert(id);
    assert(spo_screen_info_get(&info) == SPO_OK && info.width > 0 && info.height > 0);
    wait_for(&draws, 1);
    assert(SDL_AtomicGet(&other_thread) == SDL_AtomicGet(&draws));
    assert(SDL_AtomicGet(&wrong_context) == 0);
    assert(SDL_AtomicGet(&shared_seen) == SDL_AtomicGet(&draws));
    assert(last_width == info.width && last_height == info.height);
    /* The port asks for another draw. */
    {
        int before = SDL_AtomicGet(&draws);
        spo_screen_invalidate();
        wait_for(&draws, before + 1);
    }
    /* The game's own context is current again on this thread. */
    assert(SDL_GL_GetCurrentContext() == game_context);
    /* The overlay goes to the second screen: the game draws nothing, and does not wait. */
    spo_preset_set(4);
    for (int i = 0; i < 30; i++) {
        assert(spo_frame(640, 480) == SPO_OK);
        SDL_GL_SwapWindow(game);
        SDL_PumpEvents();
    }
    assert(spo_get_stats(&stats) == SPO_OK && stats.draw_calls == 0 && stats.vertices > 0);
    assert(SDL_AtomicGet(&errors) == 0);
    SDL_Delay(400);
    assert(lit_pixels(id) > 200); /* the FPS panel is on the second window */
    spo_preset_set(0);
    for (int i = 0; i < 5; i++)
        assert(spo_frame(640, 480) == SPO_OK);
    SDL_Delay(400);
    assert(lit_pixels(id) == 0);
    spo_preset_set(4);
    for (int i = 0; i < 5; i++)
        assert(spo_frame(640, 480) == SPO_OK);
    /* Touch reaches the port in normalized coordinates, and only from the second window. */
    assert(finger(SDL_FINGERDOWN, id, 0.5f, 0.25f) == 0);
    wait_for(&touches, 1);
    assert(SDL_AtomicGet(&last_phase) == SPO_TOUCH_DOWN && last_x == 0.5f && last_y == 0.25f);
    assert(finger(SDL_FINGERUP, id, 0.5f, 0.25f) == 0);
    assert(SDL_AtomicGet(&last_phase) == SPO_TOUCH_UP);
    assert((spo_screen_info_get(&info), info.flags & SPO_SCREEN_TOUCH));
    {
        int before = SDL_AtomicGet(&touches);
        finger(SDL_FINGERDOWN, SDL_GetWindowID(game), 0.1f, 0.1f);
        assert(SDL_AtomicGet(&touches) == before);
    }
    /* With the menu open the second screen's touches drive the menu, not the port. */
    tap_guide();
    assert(spo_menu_is_open());
    for (int i = 0; i < 10; i++)
        assert(spo_frame(640, 480) == SPO_OK);
    {
        int before = SDL_AtomicGet(&touches);
        assert(finger(SDL_FINGERDOWN, id, 0.001f, 0.001f) == 1);
        finger(SDL_FINGERUP, id, 0.001f, 0.001f);
        assert(SDL_AtomicGet(&touches) == before);
        assert(!spo_menu_is_open());
    }
    /* Closing the screen takes the window with it. */
    spo_screen_close();
    assert(!secondary_id());
    assert(spo_screen_info_get(&info) == SPO_UNSUPPORTED);
    /* The output that was off when the screen opened is switched off again, and only that one. */
    wait_commands(1);
    assert(!strcmp(commands[0], "output DSI-1 power off"));
    spo_shutdown();
    /* The overlay alone: the window opens when something is shown and goes when nothing is. */
    assert(spo_init(&c) == SPO_OK);
    spo_preset_set(4);
    for (int i = 0; i < 5; i++)
        assert(spo_frame(640, 480) == SPO_OK);
    assert(secondary_id());
    spo_preset_set(0);
    for (int i = 0; i < 40 && secondary_id(); i++) {
        assert(spo_frame(640, 480) == SPO_OK);
        SDL_Delay(50);
    }
    assert(!secondary_id());
    wait_commands(2);
    assert(!strcmp(commands[1], "output DSI-1 power off"));
    spo_preset_set(4);
    for (int i = 0; i < 5; i++)
        assert(spo_frame(640, 480) == SPO_OK);
    assert(secondary_id());
    spo_shutdown();
    assert(!secondary_id());
    wait_commands(3);
    /* A game that dies with the screen open. */
    {
        pid_t pid = fork();
        int status = 0;
        if (pid == 0) {
            execl("/proc/self/exe", argv[0], "crash", (char *)NULL);
            _exit(127);
        }
        assert(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
               WEXITSTATUS(status) == 0);
        wait_commands(4);
        assert(!strcmp(commands[3], "output DSI-1 power off") && command_count == 4);
    }
    unlink(sway_path);
    assert(SDL_GL_GetCurrentContext() == game_context);
    SDL_GL_DeleteContext(game_context);
    SDL_DestroyWindow(game);
    SDL_Quit();
    puts("screen: second window, shared context, touch and overlay routing passed");
    return 0;
}
