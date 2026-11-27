/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
#include "spo.h"
#include <EGL/egl.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static int draws;
static void draw_arrays(GLenum mode, GLint first, GLsizei count) {
    draws++;
    glDrawArrays(mode, first, count);
}
static void *resolve(const char *name, void *user) {
    __eglMustCastToProperFunctionPointerType proc;
    void *out = NULL;
    (void)user;
    if (!strcmp(name, "glDrawArrays")) {
        void (*fn)(GLenum, GLint, GLsizei) = draw_arrays;
        memcpy(&out, &fn, sizeof(out));
        return out;
    }
    proc = eglGetProcAddress(name);
    memcpy(&out, &proc, sizeof(out));
    return out;
}
typedef struct {
    GLint values[64];
    GLfloat floats[68];
    GLboolean flags[32];
    void *pointers[3];
} audit;
static void take(audit *a) {
    static const GLenum ints[] = {
        GL_ACTIVE_TEXTURE,         GL_TEXTURE_BINDING_2D,
        GL_ARRAY_BUFFER_BINDING,   GL_MATRIX_MODE,                 GL_SHADE_MODEL,
        GL_BLEND_SRC,              GL_BLEND_DST,                   GL_VERTEX_ARRAY_SIZE,
        GL_VERTEX_ARRAY_TYPE,      GL_VERTEX_ARRAY_STRIDE,         GL_VERTEX_ARRAY_BUFFER_BINDING,
        GL_COLOR_ARRAY_SIZE,       GL_COLOR_ARRAY_TYPE,            GL_COLOR_ARRAY_STRIDE,
        GL_COLOR_ARRAY_BUFFER_BINDING, GL_TEXTURE_COORD_ARRAY_SIZE, GL_TEXTURE_COORD_ARRAY_TYPE,
        GL_TEXTURE_COORD_ARRAY_STRIDE, GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING};
    static const GLenum env[] = {GL_TEXTURE_ENV_MODE, GL_COMBINE_RGB,   GL_COMBINE_ALPHA,
                                 GL_SRC0_RGB,         GL_OPERAND0_RGB,  GL_SRC0_ALPHA,
                                 GL_OPERAND0_ALPHA,   GL_SRC1_ALPHA,    GL_OPERAND1_ALPHA,
                                 GL_RGB_SCALE,        GL_ALPHA_SCALE};
    static const GLenum caps[] = {GL_BLEND,        GL_DEPTH_TEST,          GL_CULL_FACE,
                                  GL_SCISSOR_TEST, GL_STENCIL_TEST,        GL_ALPHA_TEST,
                                  GL_FOG,          GL_LIGHTING,            GL_COLOR_LOGIC_OP,
                                  GL_POLYGON_OFFSET_FILL, GL_SAMPLE_ALPHA_TO_COVERAGE,
                                  GL_SAMPLE_COVERAGE, GL_CLIP_PLANE0,      GL_VERTEX_ARRAY,
                                  GL_COLOR_ARRAY,  GL_TEXTURE_COORD_ARRAY};
    size_t n = 0, b = 0;
    GLint active, client;
    memset(a, 0, sizeof(*a));
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &client);
    for (int unit = 0; unit < 2; unit++) {
        glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
        a->flags[b++] = glIsEnabled(GL_TEXTURE_2D);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &a->values[n++]);
        glGetFloatv(GL_TEXTURE_MATRIX, a->floats + unit * 16);
    }
    glActiveTexture(GL_TEXTURE0);
    for (size_t i = 0; i < sizeof(env) / sizeof(env[0]); i++)
        glGetTexEnviv(GL_TEXTURE_ENV, env[i], &a->values[n++]);
    glActiveTexture((GLenum)active);
    glClientActiveTexture(GL_TEXTURE0);
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &a->pointers[0]);
    glGetPointerv(GL_COLOR_ARRAY_POINTER, &a->pointers[1]);
    glGetPointerv(GL_TEXTURE_COORD_ARRAY_POINTER, &a->pointers[2]);
    for (size_t i = 0; i < sizeof(ints) / sizeof(ints[0]); i++)
        glGetIntegerv(ints[i], &a->values[n++]);
    glClientActiveTexture((GLenum)client);
    a->values[n++] = client;
    glGetIntegerv(GL_VIEWPORT, &a->values[n]);
    n += 4;
    glGetFloatv(GL_MODELVIEW_MATRIX, a->floats + 32);
    glGetFloatv(GL_PROJECTION_MATRIX, a->floats + 48);
    glGetFloatv(GL_CURRENT_COLOR, a->floats + 64);
    glGetBooleanv(GL_COLOR_WRITEMASK, &a->flags[b]);
    b += 4;
    for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); i++)
        a->flags[b++] = glIsEnabled(caps[i]);
    assert(n <= 64 && b <= 32);
}
static int changed(const uint8_t *pixels, int x0, int y0, int x1, int y1) {
    int count = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const uint8_t *p = pixels + ((size_t)y * 640 + (size_t)x) * 4;
            count += p[0] != 128 || p[1] != 128 || p[2] != 128;
        }
    return count;
}
int main(void) {
    static uint8_t pixels[640 * 480 * 4];
    static const GLfloat colors[16] = {0};
    EGLint count = 0;
    EGLDisplay d;
    EGLConfig cfg;
    EGLSurface surface;
    EGLContext ctx;
    EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE};
    EGLint config_attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                             EGL_OPENGL_ES_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE,
                             8, EGL_NONE};
    EGLint surface_attrs[] = {EGL_WIDTH, 640, EGL_HEIGHT, 480, EGL_NONE};
    spo_config config = {0};
    spo_overlay_settings o = {0};
    GLuint tex[2], buf;
    audit before, after;
    d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) {
        puts("No EGL runtime/display");
        return 77;
    }
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    if (!eglChooseConfig(d, config_attrs, &cfg, 1, &count) || !count) {
        puts("No GLES1 config");
        return 77;
    }
    surface = eglCreatePbufferSurface(d, cfg, surface_attrs);
    assert(surface != EGL_NO_SURFACE);
    ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, context_attrs);
    assert(ctx != EGL_NO_CONTEXT);
    assert(eglMakeCurrent(d, surface, surface, ctx));
    printf("%s\n", glGetString(GL_VERSION));
    glClearColor(128 / 255.0f, 128 / 255.0f, 128 / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    /* Game state the overlay must leave as it found it. */
    glGenTextures(2, tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_TEXTURE);
    glScalef(2, 3, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glDisable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_ADD);
    glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 2);
    glTranslatef(.25f, .5f, 0);
    glMatrixMode(GL_PROJECTION);
    glOrthof(-3, 5, -7, 9, -1, 2);
    glMatrixMode(GL_MODELVIEW);
    glRotatef(30, 0, 0, 1);
    glMatrixMode(GL_TEXTURE);
    glGenBuffers(1, &buf);
    glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, 256, NULL, GL_STATIC_DRAW);
    glVertexPointer(3, GL_SHORT, 12, (void *)8);
    glEnableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glColorPointer(4, GL_FLOAT, 16, colors);
    glEnableClientState(GL_COLOR_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);
    glTexCoordPointer(3, GL_FLOAT, 20, colors + 2);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE1);
    glBindBuffer(GL_ARRAY_BUFFER, buf);
    glColor4f(.1f, .2f, .3f, .4f);
    glShadeModel(GL_FLAT);
    glBlendFunc(GL_ONE, GL_ONE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_FOG);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, .9f);
    glEnable(GL_CLIP_PLANE0);
    glViewport(17, 19, 100, 120);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    assert(glGetError() == GL_NO_ERROR);
    take(&before);
    config.struct_size = sizeof(config);
    config.abi_version = SPO_ABI_VERSION;
    config.get_proc_address = resolve;
    config.port_name = "GLES1 test";
    config.port_author = "Tester";
    assert(spo_init(&config) == SPO_OK);
    assert(!strcmp(spo->renderer_name, "GLES 1.1"));
    take(&after);
    assert(!memcmp(&before, &after, sizeof(audit)));
    assert(glGetError() == GL_NO_ERROR);
    o.struct_size = sizeof(o);
    assert(spo_overlay_get(&o) == SPO_OK);
    o.preset = 4;
    o.items = (1u << SPO_FPS) | (1u << SPO_GRAPH);
    assert(spo_overlay_set(&o) == SPO_OK);
    for (int i = 0; i < 40; i++) {
        struct timespec pause = {0, (i % 3 + 1) * 4000000};
        nanosleep(&pause, NULL);
        draws = 0;
        assert(spo_frame(640, 480) == SPO_OK);
        assert(draws <= 1);
        take(&after);
        assert(!memcmp(&before, &after, sizeof(audit)));
        assert(glGetError() == GL_NO_ERROR);
    }
    assert(draws == 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    printf("changed: total=%d right=%d\n", changed(pixels, 0, 0, 640, 480),
           changed(pixels, 320, 0, 640, 480));
    assert(changed(pixels, 0, 0, 640, 480) > 200);
    spo_shutdown();
    take(&after);
    assert(!memcmp(&before, &after, sizeof(audit)));
    assert(glGetError() == GL_NO_ERROR);
    puts("gles1: draw, graph and state isolation passed");
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(d, ctx);
    eglDestroySurface(d, surface);
    eglTerminate(d);
    return 0;
}
