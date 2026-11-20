/* SPDX-License-Identifier: MIT */
/* The second screen of a two-panel handheld, as a second SDL window. Included by sdl2.c so a port
   that compiles that one file gets it. The window has its own GL context, shared with the game's,
   and its own thread: the game thread only copies a mesh, never swaps or waits for it. */
#include "internal.h"
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

enum { SCREEN_NONE, SCREEN_OPENING, SCREEN_LIVE, SCREEN_FAILED };
#define SCREEN_FINGERS 10
#define SCREEN_VALIDATE_MS 3000u
#define SCREEN_IDLE_MS 1500u

typedef struct {
    int state, detected, forced, port_owned;
    spo_screen_desc desc;
    /* Everything below is cleared when the window goes. */
    int stop, gl_failed;
    SDL_Window *window, *game_window;
    SDL_GLContext context, game_context;
    Uint32 window_id, opened_at, last_submit, last_draw;
    SDL_Thread *thread;
    SDL_mutex *lock;
    SDL_cond *wake;
    uint32_t width, height, flags;
    int logical_w, logical_h;
    spo_mesh *pending, *working;
    uint32_t serial, seen, signature[8];
    int has_mesh, invalid, menu;
    Uint32 last_content;
    SDL_FingerID fingers[SCREEN_FINGERS];
    int mouse_down;
} screen_state;
static screen_state screen;
/* SDL_TouchFingerEvent gained windowID in 2.0.12 by appending a field; a port built against older
   headers still runs on a newer library, so read it through this layout when the library has it. */
typedef struct {
    Uint32 type, timestamp;
    SDL_TouchID touchId;
    SDL_FingerID fingerId;
    float x, y, dx, dy, pressure;
    Uint32 windowID;
} finger_event;
static int finger_windows(void) {
    SDL_version v;
    SDL_GetVersion(&v);
    return SDL_VERSIONNUM(v.major, v.minor, v.patch) >= SDL_VERSIONNUM(2, 0, 12);
}

static void debug(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void debug(const char *format, ...) {
    va_list args;
    if (!getenv("SPO_SCREEN_DEBUG"))
        return;
    fputs("spo-screen: ", stderr);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}
static int env_equals(const char *name, const char *a, const char *b) {
    const char *v = getenv(name);
    return v && (!strcmp(v, a) || (b && !strcmp(v, b)));
}
/* Connected internal panels. A connector's kind is part of its name (card0-DSI-1); which one is
   which output is the compositor's business. */
static int internal_panels(void) {
    DIR *dir = opendir("/sys/class/drm");
    struct dirent *e;
    int panels = 0;
    if (!dir)
        return 0;
    while ((e = readdir(dir))) {
        const char *kind = strchr(e->d_name, '-');
        char path[320], state[32] = "";
        FILE *f;
        if (!kind || strncmp(e->d_name, "card", 4))
            continue;
        kind++;
        if (strncmp(kind, "DSI-", 4) && strncmp(kind, "eDP-", 4) && strncmp(kind, "LVDS-", 5) &&
            strncmp(kind, "DPI-", 4))
            continue;
        snprintf(path, sizeof(path), "/sys/class/drm/%s/status", e->d_name);
        f = fopen(path, "r");
        if (!f)
            continue;
        if (fgets(state, sizeof(state), f) && strncmp(state, "disconnected", 12))
            panels++;
        fclose(f);
    }
    closedir(dir);
    return panels;
}
static int detect(void) {
    const char *driver = SDL_GetCurrentVideoDriver();
    if (env_equals("SPO_SECOND_SCREEN", "off", "0"))
        return 0;
    if (env_equals("SPO_SECOND_SCREEN", "force", "1")) {
        screen.forced = 1;
        return 1;
    }
    /* KMSDRM presents one window; the others route windows by title or by output. */
    if (!driver || (strcmp(driver, "wayland") && strcmp(driver, "x11")))
        return 0;
    {
        int panels = internal_panels();
        debug("driver %s, %d internal panels, DEVICE_HAS_DUAL_SCREEN=%s", driver, panels,
              getenv("DEVICE_HAS_DUAL_SCREEN") ? getenv("DEVICE_HAS_DUAL_SCREEN") : "");
        return env_equals("DEVICE_HAS_DUAL_SCREEN", "Y", "y") ||
               env_equals("DEVICE_HAS_DUAL_SCREEN", "true", "1") || panels >= 2;
    }
}
static uint32_t interval_ms(void) {
    if (screen.desc.draw)
        return 1000u / (screen.desc.max_hz ? screen.desc.max_hz : 30u);
    return screen.menu ? 33u : 100u;
}
static void *screen_proc(const char *name, void *user) {
    (void)user;
    return SDL_GL_GetProcAddress(name);
}
static int render_thread(void *unused) {
    typedef void (*fn_viewport)(int, int, int, int);
    typedef void (*fn_color)(float, float, float, float);
    typedef void (*fn_clear)(unsigned);
    typedef void (*fn_disable)(unsigned);
    fn_viewport viewport;
    fn_color clear_color;
    fn_clear clear;
    fn_disable disable;
    spo_gl *gl = NULL;
    int first = 1;
    (void)unused;
    if (SDL_GL_MakeCurrent(screen.window, screen.context) != 0) {
        screen.gl_failed = 1;
        return 0;
    }
    SDL_GL_SetSwapInterval(0);
    viewport = (fn_viewport)SDL_GL_GetProcAddress("glViewport");
    clear_color = (fn_color)SDL_GL_GetProcAddress("glClearColor");
    clear = (fn_clear)SDL_GL_GetProcAddress("glClear");
    disable = (fn_disable)SDL_GL_GetProcAddress("glDisable");
    if (!viewport || !clear_color || !clear || !disable) {
        screen.gl_failed = 1;
        return 0;
    }
    gl = spo_gl_create(screen_proc, NULL, 2);
    if (!gl && !screen.desc.draw)
        screen.gl_failed = 1;
    for (;;) {
        spo_screen_info info;
        spo_screen_desc desc;
        uint32_t draws = 0;
        int hud, w, h;
        SDL_LockMutex(screen.lock);
        while (!screen.stop) {
            Uint32 now = SDL_GetTicks();
            int need = first || screen.serial != screen.seen || screen.invalid;
            Uint32 wait = 250, due = screen.last_draw + interval_ms();
            if (need && (Sint32)(now - due) >= 0)
                break;
            if (need)
                wait = due - now;
            SDL_CondWaitTimeout(screen.wake, screen.lock, wait);
        }
        if (screen.stop) {
            SDL_UnlockMutex(screen.lock);
            break;
        }
        if (screen.serial != screen.seen) {
            spo_mesh *swap = screen.working;
            screen.working = screen.pending;
            screen.pending = swap;
            screen.seen = screen.serial;
        }
        screen.invalid = 0;
        first = 0;
        hud = screen.has_mesh;
        desc = screen.desc;
        w = (int)screen.width;
        h = (int)screen.height;
        info = (spo_screen_info){sizeof(info), screen.width, screen.height, screen.flags};
        SDL_UnlockMutex(screen.lock);
        screen.last_draw = SDL_GetTicks();
        disable(0x0c11);
        viewport(0, 0, w, h);
        clear_color(0, 0, 0, 1);
        clear(0x4000);
        if (desc.draw)
            desc.draw(&info, desc.user);
        if (gl && hud && screen.working->count)
            (void)spo_gl_draw(gl, screen.working, (uint32_t)w, (uint32_t)h, &draws);
        SDL_GL_SwapWindow(screen.window);
    }
    spo_gl_destroy(gl, 0);
    SDL_GL_MakeCurrent(NULL, NULL);
    return 0;
}

/* Output power. The compositor may power the second output on when the window appears (ROCKNIX's
   sway does, by window title) and nothing powers it off again when the window goes. Before the
   window opens, the outputs that are off are read from sway; when it closes, a small detached
   process puts exactly those back, whether the game closes the screen, exits, is killed or
   crashes: it waits on a pipe whose write end only this process holds. */
static int watch_fd = -1;
static char restore_socket[108], restore_command[1024];
static int sway_socket(char *path, size_t size) {
    const char *env = getenv("SWAYSOCK"), *runtime = getenv("XDG_RUNTIME_DIR");
    DIR *dir;
    struct dirent *e;
    if (env && *env && strlen(env) < size) {
        memcpy(path, env, strlen(env) + 1);
        return 1;
    }
    if (!runtime || !(dir = opendir(runtime)))
        return 0;
    while ((e = readdir(dir)))
        if (!strncmp(e->d_name, "sway-ipc.", 9) && strstr(e->d_name, ".sock") &&
            strlen(runtime) + strlen(e->d_name) + 2 < size) {
            snprintf(path, size, "%s/%s", runtime, e->d_name);
            closedir(dir);
            return 1;
        }
    closedir(dir);
    return 0;
}
static int ipc_connect(const char *path) {
    struct sockaddr_un address;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}
static int ipc_send(int fd, uint32_t type, const char *payload, uint32_t length) {
    unsigned char header[14];
    memcpy(header, "i3-ipc", 6);
    memcpy(header + 6, &length, 4);
    memcpy(header + 10, &type, 4);
    return write(fd, header, sizeof(header)) == (ssize_t)sizeof(header) &&
           (!length || write(fd, payload, length) == (ssize_t)length);
}
static int read_all(int fd, void *out, size_t size) {
    size_t done = 0;
    while (done < size) {
        ssize_t n = read(fd, (char *)out + done, size - done);
        if (n <= 0)
            return 0;
        done += (size_t)n;
    }
    return 1;
}
/* Names of the outputs sway reports with "power": false. Each output is an object at depth 2 of the
   array; strings are skipped so a quoted brace cannot confuse the depth. */
static int outputs_off(const char *json, size_t length, char names[][64], int max) {
    int depth = 0, count = 0, have_name = 0;
    char name[64] = "";
    size_t i;
    for (i = 0; i < length; i++) {
        char c = json[i];
        if (c == '"') {
            size_t start = ++i, key_end;
            while (i < length && json[i] != '"')
                i += json[i] == '\\' ? 2 : 1;
            key_end = i;
            size_t colon = i + 1;
            while (colon < length && (json[colon] == ' ' || json[colon] == '\n' || json[colon] == '\t'))
                colon++;
            if (depth == 2 && colon < length && json[colon] == ':') {
                size_t n = key_end - start, at = colon + 1;
                const char *v;
                while (at < length && (json[at] == ' ' || json[at] == '\n' || json[at] == '\t'))
                    at++;
                v = json + at;
                if (n == 4 && !strncmp(json + start, "name", 4) && *v == '"') {
                    const char *e = memchr(v + 1, '"', length - at - 1);
                    if (e && e - v - 1 < (long)sizeof(name)) {
                        memcpy(name, v + 1, (size_t)(e - v - 1));
                        name[e - v - 1] = 0;
                        have_name = 1;
                    }
                } else if (n == 5 && !strncmp(json + start, "power", 5) &&
                           at + 5 <= length && !strncmp(v, "false", 5) && have_name && count < max) {
                    snprintf(names[count++], 64, "%s", name);
                }
            }
        } else if (c == '{' || c == '[')
            depth++;
        else if (c == '}' || c == ']') {
            depth--;
            if (depth == 1)
                have_name = 0;
        }
    }
    return count;
}
static void watchdog_start(void) {
    char names[8][64];
    char *reply = NULL;
    uint32_t length = 0;
    int fd, count, i, pipes[2];
    size_t used = 0;
    pid_t pid;
    unsigned char header[14];
    if (watch_fd >= 0 || !sway_socket(restore_socket, sizeof(restore_socket)))
        return;
    fd = ipc_connect(restore_socket);
    if (fd < 0)
        return;
    if (!ipc_send(fd, 3, "", 0) || !read_all(fd, header, sizeof(header)) ||
        memcmp(header, "i3-ipc", 6)) {
        close(fd);
        return;
    }
    memcpy(&length, header + 6, 4);
    if (length > (1u << 20) || !(reply = malloc(length + 1)) || !read_all(fd, reply, length)) {
        free(reply);
        close(fd);
        return;
    }
    close(fd);
    count = outputs_off(reply, length, names, 8);
    free(reply);
    for (i = 0; i < count; i++) {
        const char *p;
        for (p = names[i]; *p; p++)
            if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') ||
                  (*p >= 'A' && *p <= 'Z') || *p == '-' || *p == '_' || *p == '.'))
                break;
        if (*p || used + strlen(names[i]) + 24 > sizeof(restore_command))
            continue;
        used += (size_t)snprintf(restore_command + used, sizeof(restore_command) - used,
                                 "%soutput %s power off", used ? "; " : "", names[i]);
    }
    debug("outputs off before opening: %d, restore command \"%s\"", count, restore_command);
    if (!used || pipe(pipes) != 0)
        return;
    fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
    pid = fork();
    if (pid < 0) {
        close(pipes[0]);
        close(pipes[1]);
        return;
    }
    if (pid == 0) {
        /* Only system calls from here: the game's other threads may hold any lock. */
        pid_t second = fork();
        if (second != 0)
            _exit(0);
        {
            char c = 0;
            struct timespec pause = {0, 80 * 1000 * 1000};
            int n, f;
            setsid();
            signal(SIGTERM, SIG_IGN);
            signal(SIGHUP, SIG_IGN);
            signal(SIGINT, SIG_IGN);
            signal(SIGPIPE, SIG_IGN);
            for (f = 3; f < 1024; f++)
                if (f != pipes[0])
                    close(f);
            n = (int)read(pipes[0], &c, 1);
            if (n == 1 && c == 'N')
                _exit(0);
            nanosleep(&pause, NULL);
            f = ipc_connect(restore_socket);
            if (f >= 0) {
                char scratch[256];
                if (ipc_send(f, 0, restore_command, (uint32_t)strlen(restore_command)))
                    (void)!read(f, scratch, sizeof(scratch));
                close(f);
            }
            _exit(0);
        }
    }
    close(pipes[0]);
    waitpid(pid, NULL, 0);
    watch_fd = pipes[1];
}
/* Called after the window is gone: the helper puts the outputs back and exits. */
static void watchdog_release(void) {
    if (watch_fd < 0)
        return;
    close(watch_fd);
    watch_fd = -1;
}
static void teardown(void) {
    if (screen.thread) {
        SDL_LockMutex(screen.lock);
        screen.stop = 1;
        SDL_CondSignal(screen.wake);
        SDL_UnlockMutex(screen.lock);
        SDL_WaitThread(screen.thread, NULL);
    }
    if (screen.context)
        SDL_GL_DeleteContext(screen.context);
    if (screen.window)
        SDL_DestroyWindow(screen.window);
    if (screen.wake)
        SDL_DestroyCond(screen.wake);
    if (screen.lock)
        SDL_DestroyMutex(screen.lock);
    free(screen.pending);
    free(screen.working);
    watchdog_release();
    memset(&screen.stop, 0, sizeof(screen) - offsetof(screen_state, stop));
    screen.state = SCREEN_NONE;
}
static void fail(void) {
    teardown();
    screen.state = SCREEN_FAILED;
}
static int open_window(void) {
    static const int attributes[] = {SDL_GL_CONTEXT_MAJOR_VERSION,
                                     SDL_GL_CONTEXT_MINOR_VERSION,
                                     SDL_GL_CONTEXT_PROFILE_MASK,
                                     SDL_GL_CONTEXT_FLAGS,
                                     SDL_GL_DEPTH_SIZE,
                                     SDL_GL_STENCIL_SIZE,
                                     SDL_GL_MULTISAMPLEBUFFERS,
                                     SDL_GL_MULTISAMPLESAMPLES,
                                     SDL_GL_SHARE_WITH_CURRENT_CONTEXT};
    int saved[sizeof(attributes) / sizeof(attributes[0])], i, display, other = -1, count;
    SDL_Rect bounds = {0, 0, 0, 0};
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS;
    const char *title = getenv("SPO_SCREEN_TITLE");
    int width, height, x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
    Uint32 started = SDL_GetTicks();
    screen.game_window = SDL_GL_GetCurrentWindow();
    screen.game_context = SDL_GL_GetCurrentContext();
    if (!screen.game_window || !screen.game_context) {
        screen.state = SCREEN_FAILED;
        return 0;
    }
    for (i = 0; i < (int)(sizeof(attributes) / sizeof(attributes[0])); i++)
        if (SDL_GL_GetAttribute((SDL_GLattr)attributes[i], &saved[i]) != 0)
            saved[i] = 0;
    /* The game's context decides the API; the screen needs no depth or multisampling. */
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    SDL_SetHint("SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN", "1");
    SDL_SetHint("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0");
    display = SDL_GetWindowDisplayIndex(screen.game_window);
    count = SDL_GetNumVideoDisplays();
    for (i = 0; i < count; i++)
        if (i != display && !SDL_GetDisplayBounds(i, &bounds)) {
            other = i;
            break;
        }
    SDL_GetWindowSize(screen.game_window, &width, &height);
    if (other >= 0) {
        /* The compositor already shows a second output: ask for it by index. */
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        x = SDL_WINDOWPOS_UNDEFINED_DISPLAY(other);
        y = x;
        width = bounds.w;
        height = bounds.h;
    }
    watchdog_start();
    screen.window = SDL_CreateWindow(title && *title ? title : "Secondary", x, y, width, height,
                                     flags | SDL_WINDOW_HIDDEN);
    if (screen.window)
        screen.context = SDL_GL_CreateContext(screen.window);
    SDL_GL_MakeCurrent(screen.game_window, screen.game_context);
    for (i = 0; i < (int)(sizeof(attributes) / sizeof(attributes[0])); i++)
        SDL_GL_SetAttribute((SDL_GLattr)attributes[i], saved[i]);
    if (!screen.window || !screen.context)
        goto failed;
    screen.window_id = SDL_GetWindowID(screen.window);
    screen.lock = SDL_CreateMutex();
    screen.wake = SDL_CreateCond();
    screen.pending = calloc(1, sizeof(spo_mesh));
    screen.working = calloc(1, sizeof(spo_mesh));
    if (!screen.lock || !screen.wake || !screen.pending || !screen.working)
        goto failed;
    SDL_GetWindowSize(screen.window, &screen.logical_w, &screen.logical_h);
    SDL_GL_GetDrawableSize(screen.window, &width, &height);
    screen.width = (uint32_t)width;
    screen.height = (uint32_t)height;
    screen.thread = SDL_CreateThread(render_thread, "spo-screen", NULL);
    if (!screen.thread)
        goto failed;
    SDL_ShowWindow(screen.window);
    debug("window %u %dx%d (drawable %ux%u), game display %d of %d, other %d", screen.window_id,
          screen.logical_w, screen.logical_h, screen.width, screen.height, display, count, other);
    screen.opened_at = SDL_GetTicks();
    screen.last_content = screen.opened_at;
    debug("opened in %u ms", screen.opened_at - started);
    screen.state = screen.forced ? SCREEN_LIVE : SCREEN_OPENING;
    return 1;
failed:
    fail();
    return 0;
}
/* A window the compositor placed on the game's own output would cover the game: it stays out of
   use until it is seen on another output, and is dropped when that does not happen. */
static void poll_state(void) {
    if (screen.state == SCREEN_OPENING) {
        int mine = SDL_GetWindowDisplayIndex(screen.window),
            game = SDL_GetWindowDisplayIndex(screen.game_window);
        if (mine >= 0 && game >= 0 && mine != game) {
            screen.state = SCREEN_LIVE;
            debug("live on display %d (game %d) after %u ms, %ux%u", mine, game,
                  SDL_GetTicks() - screen.opened_at, screen.width, screen.height);
        } else if (SDL_GetTicks() - screen.opened_at > SCREEN_VALIDATE_MS) {
            debug("not on another display (%d, game %d); closing", mine, game);
            fail();
        }
    }
    if (screen.state == SCREEN_LIVE && screen.gl_failed)
        fail();
}
static int sink_available(void) {
    if (screen.state == SCREEN_FAILED)
        return 0;
    if (!screen.detected)
        screen.detected = detect() ? 1 : 2;
    return screen.detected == 1;
}
static int sink_routed(int show, uint32_t *width, uint32_t *height) {
    if (screen.state == SCREEN_NONE && show)
        open_window();
    poll_state();
    if (screen.state != SCREEN_LIVE || (screen.desc.flags & SPO_SCREEN_NO_OVERLAY))
        return 0;
    SDL_LockMutex(screen.lock);
    *width = screen.width;
    *height = screen.height;
    screen.menu = spo_menu_is_open();
    SDL_UnlockMutex(screen.lock);
    return 1;
}
static void sink_submit(const spo_mesh *mesh) {
    uint32_t sig[8];
    Uint32 now;
    if (screen.state != SCREEN_LIVE && screen.state != SCREEN_OPENING)
        return;
    if (!mesh) {
        if (!screen.port_owned)
            teardown();
        return;
    }
    now = SDL_GetTicks();
    if (mesh->count)
        screen.last_content = now;
    else if (!screen.port_owned && now - screen.last_content > SCREEN_IDLE_MS) {
        /* Nothing to show for a while: give the output back. It opens again when something is. */
        debug("nothing shown for %u ms, closing", now - screen.last_content);
        teardown();
        return;
    }
    sig[0] = mesh->revision;
    sig[1] = mesh->count;
    sig[2] = mesh->graph_version;
    sig[3] = mesh->graph_head;
    sig[4] = mesh->graph_count;
    sig[5] = mesh->graph_scale;
    sig[6] = mesh->graph_epoch;
    sig[7] = mesh->font;
    now = SDL_GetTicks();
    if (screen.has_mesh && !memcmp(sig, screen.signature, sizeof(sig)))
        return;
    if (screen.has_mesh && now - screen.last_submit < 33u)
        return;
    SDL_LockMutex(screen.lock);
    memcpy(screen.pending->vertices, mesh->vertices, mesh->count * sizeof(spo_vertex));
    memcpy(&screen.pending->count, &mesh->count, sizeof(spo_mesh) - offsetof(spo_mesh, count));
    screen.serial++;
    screen.has_mesh = 1;
    SDL_CondSignal(screen.wake);
    SDL_UnlockMutex(screen.lock);
    memcpy(screen.signature, sig, sizeof(sig));
    screen.last_submit = now;
}
static void sink_close(void) {
    teardown();
    memset(&screen.desc, 0, sizeof(screen.desc));
    screen.port_owned = 0;
    if (screen.state == SCREEN_FAILED)
        screen.state = SCREEN_NONE;
}
static const spo_sink sink = {sink_available, sink_routed, sink_submit, sink_close};
static void attach(void) {
    /* A new spo_init starts from a clean screen. */
    teardown();
    memset(&screen, 0, sizeof(screen));
    spo_sink_set(&sink);
}
__attribute__((constructor)) static void screen_register(void) {
    spo_attach_hook(attach);
}
spo_result spo_screen_open(const spo_screen_desc *desc) {
    if (!desc || desc->struct_size != sizeof(*desc))
        return SPO_ABI;
    if (!spo)
        return SPO_NOT_INITIALIZED;
    if (!sink_available())
        return SPO_UNSUPPORTED;
    if (screen.lock)
        SDL_LockMutex(screen.lock);
    screen.desc = *desc;
    if (screen.lock)
        SDL_UnlockMutex(screen.lock);
    screen.port_owned = 1;
    if (screen.state == SCREEN_NONE)
        open_window();
    poll_state();
    if (screen.state != SCREEN_OPENING && screen.state != SCREEN_LIVE) {
        memset(&screen.desc, 0, sizeof(screen.desc));
        screen.port_owned = 0;
        return SPO_UNSUPPORTED;
    }
    return SPO_OK;
}
void spo_screen_close(void) {
    if (!spo)
        return;
    sink_close();
}
spo_result spo_screen_info_get(spo_screen_info *info) {
    if (!info || info->struct_size != sizeof(*info))
        return SPO_INVALID;
    if (!spo)
        return SPO_NOT_INITIALIZED;
    poll_state();
    if (screen.state != SCREEN_LIVE)
        return SPO_UNSUPPORTED;
    SDL_LockMutex(screen.lock);
    info->width = screen.width;
    info->height = screen.height;
    info->flags = screen.flags;
    SDL_UnlockMutex(screen.lock);
    return SPO_OK;
}
void spo_screen_invalidate(void) {
    if (screen.state != SCREEN_LIVE || !screen.lock)
        return;
    SDL_LockMutex(screen.lock);
    screen.invalid = 1;
    SDL_CondSignal(screen.wake);
    SDL_UnlockMutex(screen.lock);
}
static void touch(uint32_t finger, uint32_t phase, float x, float y) {
    int menu;
    SDL_LockMutex(screen.lock);
    if (!(screen.flags & SPO_SCREEN_TOUCH))
        debug("first touch on the screen at %.2f %.2f", (double)x, (double)y);
    screen.flags |= SPO_SCREEN_TOUCH;
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    y = y < 0 ? 0 : y > 1 ? 1 : y;
    menu = spo_menu_is_open();
    SDL_UnlockMutex(screen.lock);
    if (menu) {
        /* One finger drives the menu; the layout is in the screen's own pixels. */
        if (finger == 0)
            (void)spo_ui_pointer(phase, (int32_t)(x * (float)screen.width),
                                 (int32_t)(y * (float)screen.height));
    } else if (screen.desc.touch)
        screen.desc.touch(finger, phase, x, y, screen.desc.user);
}
static uint32_t finger_slot(SDL_FingerID id, int create) {
    uint32_t i;
    for (i = 0; i < SCREEN_FINGERS; i++)
        if (screen.fingers[i] == id + 1)
            return i;
    if (create)
        for (i = 0; i < SCREEN_FINGERS; i++)
            if (!screen.fingers[i]) {
                screen.fingers[i] = id + 1;
                return i;
            }
    return SCREEN_FINGERS;
}
static void debug_event(const SDL_Event *event) {
    if (event->type == SDL_FINGERDOWN && finger_windows()) {
        const finger_event *f = (const finger_event *)event;
        debug("finger down window %u (screen %u) touch %lld at %.2f %.2f", f->windowID,
              screen.window_id, (long long)f->touchId, (double)f->x, (double)f->y);
    }
}
/* Events of the screen's own window. Returns 1 when the event was one of its own, and sets
 *consumed when the overlay keeps it from the game. */
static int screen_event(const SDL_Event *event, int32_t *consumed) {
    Uint32 id = 0;
    if (screen.state != SCREEN_LIVE && screen.state != SCREEN_OPENING)
        return 0;
    switch (event->type) {
    case SDL_WINDOWEVENT:
        id = event->window.windowID;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        id = event->button.windowID;
        break;
    case SDL_MOUSEMOTION:
        id = event->motion.windowID;
        break;
    case SDL_FINGERDOWN:
    case SDL_FINGERUP:
    case SDL_FINGERMOTION:
        if (!finger_windows())
            return 0;
        id = ((const finger_event *)event)->windowID;
        break;
    default:
        return 0;
    }
    if (!id || id != screen.window_id)
        return 0;
    *consumed = 0;
    if (event->type == SDL_WINDOWEVENT) {
        if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
            event->window.event == SDL_WINDOWEVENT_RESIZED) {
            int w, h;
            SDL_GL_GetDrawableSize(screen.window, &w, &h);
            SDL_LockMutex(screen.lock);
            SDL_GetWindowSize(screen.window, &screen.logical_w, &screen.logical_h);
            screen.width = (uint32_t)w;
            screen.height = (uint32_t)h;
            screen.invalid = 1;
            SDL_CondSignal(screen.wake);
            SDL_UnlockMutex(screen.lock);
        }
        return 1;
    }
    if (screen.state != SCREEN_LIVE)
        return 1;
    if (event->type == SDL_FINGERDOWN || event->type == SDL_FINGERUP ||
        event->type == SDL_FINGERMOTION) {
        const finger_event *f = (const finger_event *)event;
        uint32_t slot = finger_slot(f->fingerId, event->type == SDL_FINGERDOWN);
        uint32_t phase = event->type == SDL_FINGERDOWN ? SPO_TOUCH_DOWN
                         : event->type == SDL_FINGERUP ? SPO_TOUCH_UP
                                                       : SPO_TOUCH_MOVE;
        if (slot < SCREEN_FINGERS) {
            touch(slot, phase, f->x, f->y);
            if (phase == SPO_TOUCH_UP)
                screen.fingers[slot] = 0;
        }
        *consumed = spo_input_captured();
        return 1;
    }
    /* A touch panel's pointer repeats the finger events above. */
    if ((event->type == SDL_MOUSEMOTION ? event->motion.which : event->button.which) ==
        SDL_TOUCH_MOUSEID)
        return 1;
    if (screen.logical_w > 0 && screen.logical_h > 0) {
        float x, y;
        if (event->type == SDL_MOUSEMOTION) {
            if (!screen.mouse_down)
                return 1;
            x = (float)event->motion.x / (float)screen.logical_w;
            y = (float)event->motion.y / (float)screen.logical_h;
            touch(0, SPO_TOUCH_MOVE, x, y);
        } else if (event->button.button == SDL_BUTTON_LEFT) {
            x = (float)event->button.x / (float)screen.logical_w;
            y = (float)event->button.y / (float)screen.logical_h;
            screen.mouse_down = event->type == SDL_MOUSEBUTTONDOWN;
            touch(0, screen.mouse_down ? SPO_TOUCH_DOWN : SPO_TOUCH_UP, x, y);
        }
    }
    *consumed = spo_input_captured();
    return 1;
}
