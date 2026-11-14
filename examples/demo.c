/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gl_defs.h"
#include "spo.h"
#include <SDL.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__linux__)
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#define FUNCS(X)                                                                                   \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                      \
    X(void, Clear, (GLbitfield))                                                                   \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                            \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                               \
    X(GLuint, CreateShader, (GLenum))                                                              \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))                 \
    X(void, CompileShader, (GLuint))                                                               \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *))                                                \
    X(void, DeleteShader, (GLuint))                                                                \
    X(GLuint, CreateProgram, (void))                                                               \
    X(void, AttachShader, (GLuint, GLuint))                                                        \
    X(void, LinkProgram, (GLuint))                                                                 \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *))                                               \
    X(void, DeleteProgram, (GLuint))                                                               \
    X(void, UseProgram, (GLuint))                                                                  \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *))                                  \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *))                                         \
    X(void, Uniform1f, (GLint, GLfloat))                                                           \
    X(void, GenBuffers, (GLsizei, GLuint *))                                                       \
    X(void, BindBuffer, (GLenum, GLuint))                                                          \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum))                                \
    X(void, DeleteBuffers, (GLsizei, const GLuint *))                                              \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))        \
    X(void, EnableVertexAttribArray, (GLuint))                                                     \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                                  \
    X(const GLubyte *, GetString, (GLenum))                                                        \
    X(void, GetIntegerv, (GLenum, GLint *))                                                        \
    X(void, PixelStorei, (GLenum, GLint))                                                          \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))
#define FIELD(ret, name, args) ret(*name) args;
static struct { FUNCS(FIELD) } gl;
#undef FIELD
static volatile sig_atomic_t stopped;
static int paused;
static int reset_scene;
static spo_sampling_stats sampling_begin;
static double warm_cpu_sum, warm_cpu_max;
static size_t warm_cpu_count;
static void stop(int sig) {
    (void)sig;
    stopped = 1;
}
static double seconds(clockid_t id) {
    struct timespec t;
    clock_gettime(id, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
static void *get_proc(const char *name, void *user) {
    (void)user;
    return SDL_GL_GetProcAddress(name);
}
static void pause_game(uint32_t on, void *user) {
    (void)user;
    paused = (int)on;
}
static spo_result changed(spo_id id, int32_t value, void *user) {
    (void)id;
    *(int *)user = value;
    return SPO_OK;
}
static spo_result vsync_changed(spo_id id, int32_t value, void *user) {
    (void)id;
    (void)user;
    return value == SDL_GL_GetSwapInterval() || SDL_GL_SetSwapInterval(value) == 0 ? SPO_OK
                                                                                   : SPO_REJECTED;
}
static void scene_reset(spo_id id, void *user) {
    (void)id;
    (void)user;
    reset_scene = 1;
}
static void pressed(spo_button button) {
    spo_handle_input(0, button, 1);
    spo_handle_input(0, button, 0);
}
static GLuint shader(GLenum type, const char *source) {
    GLuint s = gl.CreateShader(type);
    GLint ok;
    gl.ShaderSource(s, 1, &source, NULL);
    gl.CompileShader(s);
    gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        gl.DeleteShader(s);
        return 0;
    }
    return s;
}
static int compare(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static void summary(const char *name, double *samples, size_t count) {
    size_t i;
    double sum = 0;
    if (!count) {
        printf("%s,unavailable\n", name);
        return;
    }
    for (i = 0; i < count; i++)
        sum += samples[i];
    qsort(samples, count, sizeof(*samples), compare);
    printf("%s,%zu,%.3f,%.3f,%.3f,%.3f\n", name, count, sum / (double)count,
           samples[(count - 1) * 95 / 100], samples[(count - 1) * 99 / 100], samples[count - 1]);
}
static void report(double *cpu, double *wall, double *gpu, size_t count, size_t gpu_count,
                   double elapsed, double process_cpu) {
    size_t i;
    double cpu_sum = 0;
    spo_stats stats = {0};
    spo_sampling_stats sampling = {0};
    for (i = 0; i < count; i++)
        cpu_sum += cpu[i];
    stats.struct_size = sizeof(stats);
    spo_get_stats(&stats);
    sampling.struct_size = sizeof(sampling);
    spo_get_sampling_stats(&sampling);
    printf("measurement_phase,steady_state\nwarmup_seconds,2\n");
    printf("metric,samples,mean_us,p95_us,p99_us,max_us\n");
    summary("cpu", cpu, count);
    summary("wall", wall, count);
    summary("gpu_query", gpu, gpu_count);
    printf("elapsed_s,%.6f\nfps,%.6f\nprocess_cpu_percent,%.6f\noverlay_cpu_percent,%.6f\n"
           "draw_calls,%u\nvertices,%u\n",
           elapsed, (double)count / elapsed, process_cpu / elapsed * 100.,
           cpu_sum / (elapsed * 1e6) * 100., stats.draw_calls, stats.vertices);
    uint64_t sample_cpu = sampling.cpu_ns_total - sampling_begin.cpu_ns_total;
    uint64_t sample_elapsed = sampling.elapsed_ns - sampling_begin.elapsed_ns;
    double sample_percent =
        sample_elapsed ? 100.0 * (double)sample_cpu / (double)sample_elapsed : 0;
    double render_percent = cpu_sum / (elapsed * 1e6) * 100.;
    printf("sampler_threaded,%u\nsampler_nice,%d\nsampler_policy,%d\n"
           "sampler_cpu_us,%.3f\nsampler_window_s,%.6f\nsampler_cycles,%llu\nsampler_reads,%llu\n"
           "sampler_core_percent,%.6f\nlibrary_core_percent,%.6f\nsampler_cpu_us_per_frame,%.3f\n",
           sampling.threaded, sampling.nice_value, sampling.scheduler_policy,
           (double)sample_cpu / 1000., (double)sample_elapsed / 1e9,
           (unsigned long long)(sampling.cycles - sampling_begin.cycles),
           (unsigned long long)(sampling.reads - sampling_begin.reads), sample_percent,
           render_percent + sample_percent,
           count ? sample_percent / 100. * elapsed * 1e6 / count : 0);
    printf("warmup_cpu_samples,%zu\nwarmup_cpu_mean_us,%.3f\nwarmup_cpu_max_us,%.3f\n",
           warm_cpu_count, warm_cpu_count ? warm_cpu_sum / warm_cpu_count : 0, warm_cpu_max);
    fflush(stdout);
}
static void select_view(int preset, int menu) {
    static int selected_root;
    if (spo_menu_is_open())
        pressed(SPO_GUIDE);
    spo_preset_set((uint32_t)preset);
    if (menu >= 0) {
        pressed(SPO_GUIDE);
        if (menu != selected_root) {
            pressed(SPO_UP);
            for (int i = 0; i < (menu - selected_root + 3) % 3; i++)
                pressed(SPO_RIGHT);
            pressed(SPO_DOWN);
            selected_root = menu;
        }
    }
    spo_reset_timing();
}
static int capture(const char *path, int width, int height) {
    size_t row = (size_t)width * 4;
    uint8_t *pixels = malloc(row * (size_t)height), *temp = malloc(row);
    SDL_Surface *surface;
    int y, ok;
    if (!pixels || !temp) {
        free(pixels);
        free(temp);
        return 0;
    }
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (y = 0; y < height / 2; y++) {
        uint8_t *a = pixels + (size_t)y * row, *b = pixels + (size_t)(height - y - 1) * row;
        memcpy(temp, a, row);
        memcpy(a, b, row);
        memcpy(b, temp, row);
    }
    surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, 32, (int)row,
                                                 SDL_PIXELFORMAT_ABGR8888);
    ok = surface && SDL_SaveBMP(surface, path) == 0;
    SDL_FreeSurface(surface);
    free(pixels);
    free(temp);
    return ok;
}
static int grab_input(const char *node) {
#if defined(__linux__)
    DIR *directory = opendir("/proc/self/fd");
    struct dirent *entry;
    int grabbed = 0;
    if (!directory || !node) {
        if (directory)
            closedir(directory);
        return 0;
    }
    while ((entry = readdir(directory))) {
        char path[64], target[512];
        ssize_t length;
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;
        int fd = atoi(entry->d_name);
        snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
        length = readlink(path, target, sizeof(target) - 1);
        if (length < 0)
            continue;
        target[length] = 0;
        if (!strcmp(target, node) && ioctl(fd, EVIOCGRAB, 1) == 0) {
            grabbed = 1;
            break;
        }
    }
    closedir(directory);
    return grabbed;
#else
    (void)node;
    return 0;
#endif
}
int main(int argc, char **argv) {
    SDL_Window *window = NULL;
    SDL_GLContext context = NULL;
    SDL_GameController *pads[4] = {0};
    const size_t capacity = 262144;
    double *cpu = NULL, *wall = NULL, *gpu = NULL;
    double duration = 15, start, process_start, measured_start = 0, process_end = 0;
    size_t count = 0, gpu_count = 0;
    int preset = 4, uncapped = 0, menu = -1, major = 2, width = 640, height = 480, result = 1, i,
        work = 24, palette = 0, frame_failed = 0, maxitems = 0, capture_mode = 0, suite = 0,
        phase = 0, phases = 1, fps_limit = 0, trace_input = 0;
    double deadline = 0;
    double previous = 0, scene_time = 0;
    const char *shot = NULL, *gl_library = NULL;
    const char *capture_dir = NULL;
    const char *input_node = NULL;
    char capture_path[4096];
    GLuint program = 0, buffer = 0, vs = 0, fs = 0;
    GLint time_uniform, work_uniform, palette_uniform;
    spo_id page, counter, indicator;
    const GLfloat triangle[] = {-1, -1, 3, -1, -1, 3};
    spo_config config = {0};
    void (*GenQueries)(GLsizei, GLuint *) = NULL, (*DeleteQueries)(GLsizei, const GLuint *) = NULL;
    void (*BeginQuery)(GLenum, GLuint) = NULL, (*EndQuery)(GLenum) = NULL;
    void (*GetQueryObjectuiv)(GLuint, GLenum, GLuint *) = NULL;
    void (*GetQueryObjectui64v)(GLuint, GLenum, uint64_t *) = NULL;
    void (*GetQueryiv)(GLenum, GLenum, GLint *) = NULL;
    GLuint queries[4] = {0};
    int pending[4] = {0}, query_valid[4] = {0}, query_slot = 0;
    sampling_begin.struct_size = sizeof(sampling_begin);
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            duration = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--preset") && i + 1 < argc)
            preset = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--work") && i + 1 < argc)
            work = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc)
            fps_limit = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace-input"))
            trace_input = 1;
        else if (!strcmp(argv[i], "--input-node") && i + 1 < argc)
            input_node = argv[++i];
        else if (!strcmp(argv[i], "--menu") && i + 1 < argc)
            menu = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--capture") && i + 1 < argc) {
            shot = argv[++i];
            capture_mode = 1;
        } else if (!strcmp(argv[i], "--gles3"))
            major = 3;
        else if (!strcmp(argv[i], "--suite"))
            suite = 1;
        else if (!strcmp(argv[i], "--capture-dir") && i + 1 < argc) {
            capture_dir = argv[++i];
            capture_mode = 1;
            suite = 1;
        } else if (!strcmp(argv[i], "--uncapped"))
            uncapped = 1;
        else if (!strcmp(argv[i], "--max-items"))
            maxitems = 1;
        else if (!strcmp(argv[i], "--gl4es") && i + 1 < argc)
            gl_library = argv[++i];
        else {
            fprintf(
                stderr,
                "Usage: spo_demo [--seconds 15] [--preset 0..4] [--uncapped] [--gles3] "
                "[--menu 0..2] [--capture file.bmp] [--max-items] [--gl4es library] "
                "[--suite] [--capture-dir directory] [--work 1..24] [--fps 60] [--trace-input]\n");
            return 2;
        }
    }
    if (!(duration >= 3 && duration <= 480) || preset < 0 || preset > 4 || menu < -1 || menu > 2 ||
        work < 1 || work > 24)
        return 2;
    if ((fps_limit != 0 && fps_limit != 60) || (trace_input && suite))
        return 2;
    if (trace_input)
        capture_mode = 1;
    phases = suite ? (capture_mode ? 8 : 9) : 1;
    if (duration * phases > 480 || (suite && shot) || (capture_mode && suite && !capture_dir))
        return 2;
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    cpu = calloc(capacity, sizeof(double));
    wall = calloc(capacity, sizeof(double));
    gpu = calloc(capacity, sizeof(double));
    if (!cpu || !wall || !gpu)
        goto done;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        goto done;
    }
    if (gl_library && SDL_GL_LoadLibrary(gl_library)) {
        fprintf(stderr, "GL wrapper: %s\n", SDL_GetError());
        goto done;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, gl_library
                                                         ? SDL_GL_CONTEXT_PROFILE_COMPATIBILITY
                                                         : SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, gl_library ? 2 : major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gl_library ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window =
        SDL_CreateWindow("sbc-port-overlay demo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window || (context = SDL_GL_CreateContext(window)) == NULL) {
        fprintf(stderr, "GL context: %s\n", SDL_GetError());
        goto done;
    }
    SDL_GL_SetSwapInterval(uncapped ? 0 : 1);
#define LOAD(ret, name, args)                                                                      \
    do {                                                                                           \
        void *p = SDL_GL_GetProcAddress("gl" #name);                                               \
        if (!p)                                                                                    \
            goto done;                                                                             \
        memcpy(&gl.name, &p, sizeof(p));                                                           \
    } while (0);
    FUNCS(LOAD)
#undef LOAD
    printf("GL,%s\nRENDERER,%s\n", gl.GetString(GL_VERSION), gl.GetString(GL_RENDERER));
    printf("preset,%d\nvsync,%d\n", preset, SDL_GL_GetSwapInterval());
    printf("pacing_fps,%d\nshader_work,%d\n", fps_limit, work);
    vs = shader(GL_VERTEX_SHADER,
                "attribute vec2 p;varying vec2 uv;void main(){uv=p;gl_Position=vec4(p,0.,1.);}");
    fs = shader(GL_FRAGMENT_SHADER, "precision mediump float;uniform float time;uniform float "
                                    "work;uniform float palette;varying vec2 uv;void main(){vec3 "
                                    "c=vec3(uv*.12+.2,.26)+vec3(palette*.04,0.,0.);for(int "
                                    "i=0;i<24;i++){if(float(i)>=work)break;float "
                                    "f=float(i)+1.;c+=sin(vec3(uv.x*f+time,uv.y*f-time,(uv.x+uv.y)*"
                                    "f))*.008;}gl_FragColor=vec4(c,1.);}");
    if (!vs || !fs)
        goto done;
    program = gl.CreateProgram();
    gl.AttachShader(program, vs);
    gl.AttachShader(program, fs);
    gl.BindAttribLocation(program, 0, "p");
    gl.LinkProgram(program);
    {
        GLint ok;
        gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok)
            goto done;
    }
    time_uniform = gl.GetUniformLocation(program, "time");
    work_uniform = gl.GetUniformLocation(program, "work");
    palette_uniform = gl.GetUniformLocation(program, "palette");
    gl.GenBuffers(1, &buffer);
    gl.BindBuffer(GL_ARRAY_BUFFER, buffer);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    config.struct_size = sizeof(config);
    config.abi_version = SPO_ABI_VERSION;
    config.get_proc_address = get_proc;
    config.port_name = "Overlay demo";
    config.port_version = "1.0";
    config.on_pause = pause_game;
    if (spo_init(&config) != SPO_OK) {
        fprintf(stderr, "spo_init failed\n");
        goto done;
    }
    {
        spo_page_desc p = {sizeof(p), "graphics", "Graphics"};
        spo_option_desc o = {0};
        spo_id option;
        const char *choices[] = {"Ocean", "Forest", "Amber"};
        spo_counter_desc c = {sizeof(c), "draws", "DRAWS", "", 0};
        spo_indicator_desc ind = {sizeof(ind), "shaders", "SHADERS"};
        if (spo_register_page(&p, &page) != SPO_OK)
            goto done;
        o.struct_size = sizeof(o);
        o.page = page;
        o.type = SPO_TOGGLE;
        o.key = "vsync";
        o.label = "VSync";
        o.default_value = SDL_GL_GetSwapInterval() == 0 ? 0 : 1;
        o.on_change = vsync_changed;
        if (spo_register_option(&o, &option) != SPO_OK)
            goto done;
        o.type = SPO_CHOICE;
        o.key = "palette";
        o.label = "Palette";
        o.default_value = 0;
        o.choices = choices;
        o.choice_count = 3;
        o.on_change = changed;
        o.user = &palette;
        if (spo_register_option(&o, &option) != SPO_OK)
            goto done;
        o.type = SPO_INTEGER;
        o.key = "work";
        o.label = "Shader work";
        o.minimum = 1;
        o.maximum = 24;
        o.step = 1;
        o.default_value = work;
        o.on_change = changed;
        o.user = &work;
        o.choice_count = 0;
        o.choices = NULL;
        o.help = "GPU shader iterations: 1..24";
        if (spo_register_option(&o, &option) != SPO_OK)
            goto done;
        o.type = SPO_ACTION;
        o.key = "reset";
        o.label = "Reset scene";
        o.on_change = NULL;
        o.on_action = scene_reset;
        o.help = "Restart the scene animation";
        if (spo_register_option(&o, &option) != SPO_OK ||
            spo_register_counter(&c, &counter) != SPO_OK ||
            spo_register_indicator(&ind, &indicator) != SPO_OK)
            goto done;
        if (maxitems)
            for (i = 1; i < 8; i++) {
                char key[16], label[11];
                spo_id id;
                snprintf(key, sizeof(key), "counter%d", i);
                snprintf(label, sizeof(label), "COUNT%d", i);
                c.key = key;
                c.label = label;
                if (spo_register_counter(&c, &id) != SPO_OK)
                    goto done;
                spo_counter_set(id, 1234.0 * i);
                snprintf(key, sizeof(key), "indicator%d", i);
                snprintf(label, sizeof(label), "STATE%d", i);
                ind.key = key;
                ind.label = label;
                if (spo_register_indicator(&ind, &id) != SPO_OK)
                    goto done;
                spo_indicator_set(id, (uint32_t)(i % 2));
            }
    }
    spo_counter_set(counter, 1);
    spo_indicator_set(indicator, 0);
    if (suite) {
        preset = 0;
        menu = -1;
    }
    select_view(preset, menu);
    if (capture_dir) {
        snprintf(capture_path, sizeof(capture_path), "%s/preset-0.bmp", capture_dir);
        shot = capture_path;
    }
    printf("phase,%d,preset,%d,menu,%d\n", phase, preset, menu);
    {
        const char *extensions = (const char *)gl.GetString(GL_EXTENSIONS);
        if (!capture_mode && extensions && strstr(extensions, "GL_EXT_disjoint_timer_query")) {
#define QUERY(name, symbol)                                                                        \
    do {                                                                                           \
        void *p = SDL_GL_GetProcAddress(symbol);                                                   \
        if (p)                                                                                     \
            memcpy(&name, &p, sizeof(p));                                                          \
    } while (0)
            QUERY(GenQueries, "glGenQueriesEXT");
            QUERY(DeleteQueries, "glDeleteQueriesEXT");
            QUERY(BeginQuery, "glBeginQueryEXT");
            QUERY(EndQuery, "glEndQueryEXT");
            QUERY(GetQueryObjectuiv, "glGetQueryObjectuivEXT");
            QUERY(GetQueryObjectui64v, "glGetQueryObjectui64vEXT");
            QUERY(GetQueryiv, "glGetQueryivEXT");
#undef QUERY
            if (GenQueries && DeleteQueries && BeginQuery && EndQuery && GetQueryObjectuiv &&
                GetQueryObjectui64v && GetQueryiv) {
                GLint bits = 0;
                GetQueryiv(0x88bf, 0x8864, &bits);
                if (bits > 0)
                    GenQueries(4, queries);
            }
        }
    }
    for (i = 0; i < SDL_NumJoysticks() && i < 4; i++)
        if (SDL_IsGameController(i)) {
            pads[i] = SDL_GameControllerOpen(i);
            if (pads[i]) {
                char *mapping = SDL_GameControllerMapping(pads[i]);
                printf("controller,%d,%s,%s\n", i, SDL_GameControllerName(pads[i]),
                       mapping ? mapping : "unmapped");
                SDL_free(mapping);
            }
        }
    printf("video_driver,%s\n", SDL_GetCurrentVideoDriver());
    if (trace_input) {
        /* Grab SDL's client so the suspended frontend cannot queue test events. */
        if (!grab_input(input_node)) {
            fprintf(stderr, "Could not exclusively grab the SDL input client\n");
            goto done;
        }
        printf("input_grab,1\n");
    }
    start = seconds(CLOCK_MONOTONIC);
    deadline = start;
    previous = start;
    process_start = seconds(CLOCK_PROCESS_CPUTIME_ID);
    while (!stopped) {
        SDL_Event event;
        double now = seconds(CLOCK_MONOTONIC), a, b, c, d;
        int query = 0;
        if (now - start >= duration) {
            if ((capture_mode && shot) || (!capture_mode && !measured_start)) {
                fprintf(stderr, "Phase ended without a complete capture/measurement\n");
                frame_failed = 1;
                break;
            }
            process_end = seconds(CLOCK_PROCESS_CPUTIME_ID);
            if (!capture_mode)
                report(cpu, wall, gpu, count, gpu_count, now - measured_start,
                       process_end - process_start);
            if (++phase == phases)
                break;
            preset = capture_mode ? (phase < 5 ? phase : 4) : (phase % 2 ? (phase + 1) / 2 : 0);
            menu = capture_mode && phase >= 5 ? phase - 5 : -1;
            select_view(preset, menu);
            if (capture_dir) {
                if (menu < 0)
                    snprintf(capture_path, sizeof(capture_path), "%s/preset-%d.bmp", capture_dir,
                             preset);
                else
                    snprintf(capture_path, sizeof(capture_path), "%s/menu-%d.bmp", capture_dir,
                             menu);
                shot = capture_path;
            }
            printf("phase,%d,preset,%d,menu,%d\n", phase, preset, menu);
            count = gpu_count = 0;
            warm_cpu_sum = warm_cpu_max = 0;
            warm_cpu_count = 0;
            measured_start = 0;
            memset(query_valid, 0, sizeof(query_valid));
            start = now;
        }
        if (now - start >= 2 && !measured_start) {
            measured_start = now;
            process_start = seconds(CLOCK_PROCESS_CPUTIME_ID);
            spo_get_sampling_stats(&sampling_begin);
        }
        if (!paused)
            scene_time += now - previous;
        previous = now;
        if (reset_scene) {
            scene_time = 0;
            reset_scene = 0;
        }
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
                stopped = 1;
            if (event.type == SDL_CONTROLLERDEVICEADDED) {
                SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(event.cdevice.which);
                int found = 0;
                for (i = 0; i < 4; i++)
                    if (pads[i] &&
                        SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id)
                        found = 1;
                if (!found)
                    for (i = 0; i < 4; i++)
                        if (!pads[i] && SDL_IsGameController(event.cdevice.which)) {
                            pads[i] = SDL_GameControllerOpen(event.cdevice.which);
                            break;
                        }
            }
            if (event.type == SDL_CONTROLLERDEVICEREMOVED)
                for (i = 0; i < 4; i++)
                    if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) ==
                                       event.cdevice.which) {
                        SDL_GameControllerClose(pads[i]);
                        pads[i] = NULL;
                    }
            {
                int before = spo_menu_is_open();
                int consumed = spo_handle_sdl_event(&event);
                if (trace_input && (event.type == SDL_CONTROLLERBUTTONDOWN ||
                                    event.type == SDL_CONTROLLERBUTTONUP))
                    printf("input,%s,%d,consumed,%d,menu_before,%d,menu_after,%d\n",
                           SDL_GameControllerGetStringForButton(
                               (SDL_GameControllerButton)event.cbutton.button),
                           event.type == SDL_CONTROLLERBUTTONDOWN, consumed, before,
                           spo_menu_is_open());
            }
        }
        SDL_GL_GetDrawableSize(window, &width, &height);
        gl.Viewport(0, 0, width, height);
        gl.ColorMask(1, 1, 1, 1);
        gl.ClearColor(.1f, .15f, .2f, 1);
        gl.Clear(GL_COLOR_BUFFER_BIT);
        gl.UseProgram(program);
        gl.Uniform1f(time_uniform, (GLfloat)scene_time);
        gl.Uniform1f(work_uniform, (GLfloat)work);
        gl.Uniform1f(palette_uniform, (GLfloat)palette);
        gl.BindBuffer(GL_ARRAY_BUFFER, buffer);
        gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, 0, NULL);
        gl.EnableVertexAttribArray(0);
        gl.DrawArrays(GL_TRIANGLES, 0, 3);
        if (queries[0]) {
            GLuint available = 0;
            GLint disjoint = 0;
            gl.GetIntegerv(0x8fbb, &disjoint);
            if (disjoint)
                for (i = 0; i < 4; i++)
                    query_valid[i] = 0;
            if (pending[query_slot]) {
                GetQueryObjectuiv(queries[query_slot], 0x8867, &available);
                if (available) {
                    uint64_t ns;
                    GetQueryObjectui64v(queries[query_slot], 0x8866, &ns);
                    if (!disjoint && query_valid[query_slot] && now - start >= 2 &&
                        gpu_count < capacity)
                        gpu[gpu_count++] = (double)ns / 1000.;
                    pending[query_slot] = 0;
                }
            }
            if (!pending[query_slot]) {
                BeginQuery(0x88bf, queries[query_slot]);
                query_valid[query_slot] = !disjoint;
                query = 1;
            }
        }
        a = seconds(CLOCK_THREAD_CPUTIME_ID);
        b = seconds(CLOCK_MONOTONIC);
        spo_counter_set(counter, 1);
        spo_indicator_set(indicator, 0);
        if (spo_frame((uint32_t)width, (uint32_t)height) != SPO_OK) {
            fprintf(stderr, "spo_frame=%d\n", spo_last_error());
            frame_failed = 1;
            break;
        }
        c = seconds(CLOCK_THREAD_CPUTIME_ID);
        d = seconds(CLOCK_MONOTONIC);
        if (query) {
            EndQuery(0x88bf);
            pending[query_slot] = 1;
        }
        query_slot = (query_slot + 1) % 4;
        if (now - start >= 2) {
            if (count == capacity) {
                fprintf(stderr, "Benchmark sample capacity exceeded\n");
                frame_failed = 1;
                break;
            }
            cpu[count] = (c - a) * 1e6;
            wall[count] = (d - b) * 1e6;
            count++;
        } else {
            double cost = (c - a) * 1e6;
            warm_cpu_count++;
            warm_cpu_sum += cost;
            if (cost > warm_cpu_max)
                warm_cpu_max = cost;
        }
        if (shot && now - start >= 2) {
            if (!capture(shot, width, height)) {
                fprintf(stderr, "Capture failed\n");
                goto done;
            }
            shot = NULL;
            if (!suite)
                stopped = 1;
        }
        SDL_GL_SwapWindow(window);
        if (fps_limit) {
            struct timespec delay;
            double current = seconds(CLOCK_MONOTONIC);
            deadline += 1.0 / fps_limit;
            if (deadline < current)
                deadline = current;
            double remaining = deadline - current;
            if (remaining > 0) {
                delay.tv_sec = (time_t)remaining;
                delay.tv_nsec = (long)((remaining - delay.tv_sec) * 1e9);
                while (nanosleep(&delay, &delay) && errno == EINTR && !stopped) {
                }
            }
        }
    }
    process_end = seconds(CLOCK_PROCESS_CPUTIME_ID);
    if (capture_mode) {
        printf("capture_mode,untimed\n");
        result = frame_failed ? 1 : 0;
        goto done;
    }
    if (phase < phases && measured_start)
        report(cpu, wall, gpu, count, gpu_count, seconds(CLOCK_MONOTONIC) - measured_start,
               process_end - process_start);
    result = frame_failed ? 1 : 0;
done:
    spo_shutdown();
    for (i = 0; i < 4; i++)
        if (pads[i])
            SDL_GameControllerClose(pads[i]);
    if (context) {
        if (queries[0] && DeleteQueries)
            DeleteQueries(4, queries);
        if (buffer)
            gl.DeleteBuffers(1, &buffer);
        if (program)
            gl.DeleteProgram(program);
        if (vs)
            gl.DeleteShader(vs);
        if (fs)
            gl.DeleteShader(fs);
        SDL_GL_DeleteContext(context);
    }
    if (window)
        SDL_DestroyWindow(window);
    SDL_Quit();
    free(cpu);
    free(wall);
    free(gpu);
    return result;
}
