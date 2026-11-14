/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
#include "spo.h"
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int no_vao, es3, fail_mode;
static int rendering, draw_calls, upload_calls, texture_calls, texture_width, font_upload;
static size_t upload_bytes;
static void draw_arrays(GLenum mode, GLint first, GLsizei count) {
    if (rendering)
        draw_calls++;
    glDrawArrays(mode, first, count);
}
static void buffer_subdata(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    if (rendering) {
        upload_calls++;
        upload_bytes += (size_t)size;
    }
    glBufferSubData(target, offset, size, data);
}
static void texture_subimage(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                             GLenum format, GLenum type, const void *data) {
    if (rendering) {
        texture_calls++;
        texture_width = w;
        assert((y == SPO_ATLAS_GRAPH_ROW && h == 1) ||
               (font_upload && ((x == 0 && w == SPO_ATLAS) ||
                                (x >= 0 && w > 0 && x + w <= SPO_ATLAS && y + h <= SPO_ATLAS_SOLID_Y - 1))));
    }
    glTexSubImage2D(target, level, x, y, w, h, format, type, data);
}
static void buffer_data(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    assert(!rendering);
    glBufferData(target, size, data, usage);
}
static void finish(void) {
    assert(!rendering);
    glFinish();
}
static spo_result frame(uint32_t width, uint32_t height) {
    rendering = 1;
    draw_calls = upload_calls = texture_calls = texture_width = 0;
    upload_bytes = 0;
    spo_result result = spo_frame(width, height);
    rendering = 0;
    assert(draw_calls <= 1);
    return result;
}
static GLenum fail_framebuffer(GLenum target) {
    (void)target;
    return GL_FRAMEBUFFER_UNSUPPORTED;
}
static GLuint fail_shader(GLenum type) {
    (void)type;
    return 0;
}
static void checked_attribute(GLuint index, GLenum name, GLfloat *out) {
    assert(!(es3 && name == GL_CURRENT_VERTEX_ATTRIB && (index == 1 || index == 2)));
    glGetVertexAttribfv(index, name, out);
}
static const GLubyte *get_string(GLenum name) {
    return no_vao && name == GL_EXTENSIONS ? (const GLubyte *)"" : glGetString(name);
}
static void *resolve(const char *name, void *user) {
    __eglMustCastToProperFunctionPointerType proc;
    void *out = NULL;
    (void)user;
    if (fail_mode == 1 && !strcmp(name, "glDrawArrays"))
        return NULL;
    if (fail_mode == 2 && !strcmp(name, "glCreateShader")) {
        GLuint (*fn)(GLenum) = fail_shader;
        memcpy(&out, &fn, sizeof(out));
        return out;
    }
    if (fail_mode == 3 && !strcmp(name, "glCheckFramebufferStatus")) {
        GLenum (*fn)(GLenum) = fail_framebuffer;
        memcpy(&out, &fn, sizeof(out));
        return out;
    }
    if (!strcmp(name, "glGetString")) {
        const GLubyte *(*fn)(GLenum) = get_string;
        memcpy(&out, &fn, sizeof(out));
        return out;
    }
    if (!strcmp(name, "glGetVertexAttribfv")) {
        void (*fn)(GLuint, GLenum, GLfloat *) = checked_attribute;
        memcpy(&out, &fn, sizeof(out));
        return out;
    }
#define INTERCEPT(symbol, function)                                                                \
    if (!strcmp(name, symbol)) {                                                                   \
        __typeof__(&function) fn = &function;                                                      \
        memcpy(&out, &fn, sizeof(out));                                                            \
        return out;                                                                                \
    }
    INTERCEPT("glDrawArrays", draw_arrays)
    INTERCEPT("glBufferSubData", buffer_subdata)
    INTERCEPT("glTexSubImage2D", texture_subimage)
    INTERCEPT("glBufferData", buffer_data)
    INTERCEPT("glFinish", finish)
#undef INTERCEPT
    proc = eglGetProcAddress(name);
    memcpy(&out, &proc, sizeof(out));
    return out;
}
typedef struct {
    GLint values[256];
    size_t count;
    GLfloat current[12];
    GLint signed_current[4];
    GLuint unsigned_current[4];
    GLint ubo, indexed_ubo;
    GLint64 ubo_range[2];
    void *pointers[3];
} audit;
static void take(audit *a) {
    static const GLenum names[] = {GL_CURRENT_PROGRAM,
                                   GL_ARRAY_BUFFER_BINDING,
                                   GL_ELEMENT_ARRAY_BUFFER_BINDING,
                                   GL_ACTIVE_TEXTURE,
                                   GL_BLEND_EQUATION_RGB,
                                   GL_BLEND_EQUATION_ALPHA,
                                   GL_BLEND_SRC_RGB,
                                   GL_BLEND_DST_RGB,
                                   GL_BLEND_SRC_ALPHA,
                                   GL_BLEND_DST_ALPHA,
                                   GL_DEPTH_FUNC,
                                   GL_DEPTH_WRITEMASK,
                                   GL_STENCIL_FUNC,
                                   GL_STENCIL_REF,
                                   GL_STENCIL_VALUE_MASK,
                                   GL_STENCIL_WRITEMASK,
                                   GL_STENCIL_FAIL,
                                   GL_STENCIL_PASS_DEPTH_FAIL,
                                   GL_STENCIL_PASS_DEPTH_PASS,
                                   GL_CULL_FACE_MODE,
                                   GL_FRONT_FACE,
                                   GL_FRAMEBUFFER_BINDING,
                                   GL_RENDERBUFFER_BINDING,
                                   GL_PACK_ALIGNMENT,
                                   GL_UNPACK_ALIGNMENT};
    static const GLenum enables[] = {GL_BLEND,           GL_DEPTH_TEST, GL_STENCIL_TEST,
                                     GL_SCISSOR_TEST,    GL_CULL_FACE,  GL_SAMPLE_ALPHA_TO_COVERAGE,
                                     GL_SAMPLE_COVERAGE, GL_DITHER};
    static const GLenum attrs[] = {
        GL_VERTEX_ATTRIB_ARRAY_ENABLED, GL_VERTEX_ATTRIB_ARRAY_SIZE,
        GL_VERTEX_ATTRIB_ARRAY_TYPE,    GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
        GL_VERTEX_ATTRIB_ARRAY_STRIDE,  GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING};
    size_t i, j;
    GLint active;
    memset(a, 0, sizeof(*a));
    if (es3) {
        glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &a->ubo);
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &a->indexed_ubo);
        glGetInteger64i_v(GL_UNIFORM_BUFFER_START, 0, &a->ubo_range[0]);
        glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, 0, &a->ubo_range[1]);
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        glGetIntegerv(names[i], &a->values[a->count++]);
    for (i = 0; i < sizeof(enables) / sizeof(enables[0]); i++)
        a->values[a->count++] = glIsEnabled(enables[i]);
    glGetIntegerv(GL_VIEWPORT, &a->values[a->count]);
    a->count += 4;
    glGetIntegerv(GL_SCISSOR_BOX, &a->values[a->count]);
    a->count += 4;
    glGetIntegerv(GL_COLOR_WRITEMASK, &a->values[a->count]);
    a->count += 4;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &a->values[a->count++]);
    if (es3)
        glGetIntegerv(GL_SAMPLER_BINDING, &a->values[a->count++]);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &a->values[a->count++]);
    if (es3)
        glGetIntegerv(GL_SAMPLER_BINDING, &a->values[a->count++]);
    glActiveTexture(GL_TEXTURE3);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &a->values[a->count++]);
    glActiveTexture((GLenum)active);
    for (i = 0; i < 3; i++) {
        for (j = 0; j < sizeof(attrs) / sizeof(attrs[0]); j++)
            glGetVertexAttribiv((GLuint)i, attrs[j], &a->values[a->count++]);
        glGetVertexAttribPointerv((GLuint)i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a->pointers[i]);
        if (es3 && i == 1)
            glGetVertexAttribIiv((GLuint)i, GL_CURRENT_VERTEX_ATTRIB, a->signed_current);
        else if (es3 && i == 2)
            glGetVertexAttribIuiv((GLuint)i, GL_CURRENT_VERTEX_ATTRIB, a->unsigned_current);
        else
            glGetVertexAttribfv((GLuint)i, GL_CURRENT_VERTEX_ATTRIB, &a->current[i * 4]);
    }
    if (es3) {
        static const GLenum extra[] = {
            GL_READ_FRAMEBUFFER_BINDING, GL_VERTEX_ARRAY_BINDING, GL_PIXEL_UNPACK_BUFFER_BINDING,
            GL_UNPACK_ROW_LENGTH,        GL_UNPACK_SKIP_ROWS,     GL_UNPACK_SKIP_PIXELS};
        for (i = 0; i < sizeof(extra) / sizeof(extra[0]); i++)
            glGetIntegerv(extra[i], &a->values[a->count++]);
        a->values[a->count++] = glIsEnabled(GL_RASTERIZER_DISCARD);
        if (strstr((const char *)glGetString(GL_VERSION), "3.2") ||
            strstr((const char *)glGetString(GL_VERSION), "3.1"))
            a->values[a->count++] = glIsEnabled(GL_SAMPLE_MASK);
    }
    assert(a->count < 256);
    assert(glGetError() == GL_NO_ERROR);
}
/* Every pixel of one native glyph cell must match the atlas source at 1:1. */
static void check_glyph(int size, char code, int x0, int y0) {
    const spo_font_info *f = spo_font_get((uint32_t)size);
    uint8_t band[SPO_ATLAS * 32 * 4], pixel[4];
    unsigned index = (unsigned)code - 32;
    int x, y;
    spo_font_band((uint32_t)size, (index / (unsigned)f->cols) * (unsigned)f->ch, (unsigned)f->ch,
                  band);
    for (y = 0; y < f->ch; y++)
        for (x = 0; x < f->cw; x++) {
            int on = band[((size_t)y * SPO_ATLAS + (index % (unsigned)f->cols) * (unsigned)f->cw +
                           (unsigned)x) *
                              4 +
                          3];
            glReadPixels(x0 + x, 480 - (y0 + y) - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            if (on)
                assert(pixel[0] > 135 && pixel[1] > 140 && pixel[2] > 150);
            else
                assert(pixel[0] < 110 && pixel[1] < 110 && pixel[2] < 110);
        }
}
static GLuint shader(GLenum type, const char *text) {
    GLint ok;
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &text, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    assert(ok);
    return s;
}
int main(int argc, char **argv) {
    int major = argc > 1 ? atoi(argv[1]) : 2;
    EGLint count = 0;
    EGLDisplay d;
    EGLConfig cfg;
    EGLSurface surface;
    EGLContext ctx;
    EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, major, EGL_NONE};
    EGLint config_attrs[] = {EGL_SURFACE_TYPE,
                             EGL_PBUFFER_BIT,
                             EGL_RENDERABLE_TYPE,
                             major == 3 ? 0x40 : EGL_OPENGL_ES2_BIT,
                             EGL_RED_SIZE,
                             8,
                             EGL_GREEN_SIZE,
                             8,
                             EGL_BLUE_SIZE,
                             8,
                             EGL_NONE};
    EGLint surface_attrs[] = {EGL_WIDTH, 640, EGL_HEIGHT, 480, EGL_NONE};
    spo_config config = {0};
    spo_overlay_settings o = {0};
    GLuint tex[3], buf[3], ubo[2] = {0}, vao = 0, sampler = 0, fb, program, vs, fs;
    void (*gen_vao)(GLsizei, GLuint *) = NULL;
    void (*bind_vao)(GLuint) = NULL;
    void (*delete_vao)(GLsizei, const GLuint *) = NULL;
    audit before, after;
    uint8_t pixel[4];
    int i;
    no_vao = argc > 2;
    es3 = major == 3;
    d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) {
        puts("No EGL runtime/display");
        return 77;
    }
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    assert(eglChooseConfig(d, config_attrs, &cfg, 1, &count) && count);
    surface = eglCreatePbufferSurface(d, cfg, surface_attrs);
    assert(surface != EGL_NO_SURFACE);
    ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, context_attrs);
    assert(ctx != EGL_NO_CONTEXT);
    assert(eglMakeCurrent(d, surface, surface, ctx));
    printf("%s; no-VAO path=%d\n", glGetString(GL_VERSION), no_vao);
    vs = shader(GL_VERTEX_SHADER, "attribute vec2 p;void main(){gl_Position=vec4(p,0.,1.);}");
    fs = shader(GL_FRAGMENT_SHADER,
                "precision mediump float;void main(){gl_FragColor=vec4(.2,.3,.4,1.);}");
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glUseProgram(program);
    glGenTextures(3, tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[0], 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glClearColor(.5f, .5f, .5f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex[2]);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, tex[2]);
    if (!no_vao) {
        __eglMustCastToProperFunctionPointerType p =
            eglGetProcAddress(es3 ? "glGenVertexArrays" : "glGenVertexArraysOES");
        memcpy(&gen_vao, &p, sizeof(p));
        p = eglGetProcAddress(es3 ? "glBindVertexArray" : "glBindVertexArrayOES");
        memcpy(&bind_vao, &p, sizeof(p));
        p = eglGetProcAddress(es3 ? "glDeleteVertexArrays" : "glDeleteVertexArraysOES");
        memcpy(&delete_vao, &p, sizeof(p));
        if (gen_vao && bind_vao && delete_vao) {
            gen_vao(1, &vao);
            bind_vao(vao);
        }
    }
    glGenBuffers(3, buf);
    glBindBuffer(GL_ARRAY_BUFFER, buf[0]);
    glBufferData(GL_ARRAY_BUFFER, 256, NULL, GL_STATIC_DRAW);
    for (i = 0; i < 3; i++) {
        glVertexAttribPointer((GLuint)i, 2 + i % 2, GL_FLOAT, (GLboolean)(i % 2), 32,
                              (void *)(uintptr_t)(i * 8));
        glVertexAttrib4f((GLuint)i, .1f * (float)i, .2f, .3f, 1);
        if (i % 2)
            glEnableVertexAttribArray((GLuint)i);
        else
            glDisableVertexAttribArray((GLuint)i);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf[1]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 64, NULL, GL_STATIC_DRAW);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
    glBlendFuncSeparate(GL_DST_COLOR, GL_SRC_COLOR, GL_ZERO, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_NOTEQUAL, 3, 0x3f);
    glStencilMask(0x7f);
    glStencilOp(GL_REPLACE, GL_INCR, GL_DECR);
    glEnable(GL_SCISSOR_TEST);
    glScissor(31, 23, 17, 29);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glFrontFace(GL_CW);
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glEnable(GL_SAMPLE_COVERAGE);
    glSampleCoverage(.25f, GL_TRUE);
    glEnable(GL_DITHER);
    glViewport(17, 19, 100, 120);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    if (es3) {
        GLint alignment;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
        glGenBuffers(2, ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo[0]);
        glBufferData(GL_UNIFORM_BUFFER, alignment + 1024, NULL, GL_DYNAMIC_DRAW);
        glBindBufferRange(GL_UNIFORM_BUFFER, 0, ubo[0], alignment, 512);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo[1]);
        glBufferData(GL_UNIFORM_BUFFER, 2048, NULL, GL_DYNAMIC_DRAW);
        glVertexAttribI4i(1, 16777217, -2147483647, 123, 1);
        glVertexAttribI4ui(2, 4294967295u, 4000000001u, 17, 1);
        glGenSamplers(1, &sampler);
        glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glBindSampler(0, sampler);
        glBindSampler(1, sampler);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buf[2]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, 256, NULL, GL_STATIC_DRAW);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 16);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
        glEnable(GL_RASTERIZER_DISCARD);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
    assert(glGetError() == GL_NO_ERROR);
    take(&before);
    config.struct_size = sizeof(config);
    config.abi_version = SPO_ABI_VERSION;
    config.get_proc_address = resolve;
    config.port_name = "GL isolation test";
    for (fail_mode = 1; fail_mode <= 3; fail_mode++) {
        assert(spo_init(&config) == SPO_GL_UNAVAILABLE);
        take(&after);
        assert(!memcmp(&before, &after, sizeof(audit)));
    }
    fail_mode = 0;
    assert(spo_init(&config) == SPO_OK);
    take(&after);
    assert(!memcmp(&before, &after, sizeof(audit)));
    for (i = 0; i < 8; i++) {
        char key[16];
        spo_id id;
        spo_counter_desc counter = {sizeof(counter), key, "COUNTERXYZ", "msec", 3};
        spo_indicator_desc indicator = {sizeof(indicator), key, "STATE01234567890"};
        snprintf(key, sizeof(key), "counter%d", i);
        assert(spo_register_counter(&counter, &id) == SPO_OK);
        assert(spo_counter_set(id, 99999.123) == SPO_OK);
        assert(spo_register_indicator(&indicator, &id) == SPO_OK);
        assert(spo_indicator_set(id, 0) == SPO_OK);
    }
    assert(spo_preset_set(4) == SPO_OK);
    o.struct_size = sizeof(o);
    assert(spo_overlay_get(&o) == SPO_OK);
    o.items = 1u << SPO_FPS;
    assert(spo_overlay_set(&o) == SPO_OK);
    for (i = 0; i < 4; i++) {
        assert(frame(640, 480) == SPO_OK);
        take(&after);
        assert(!memcmp(&before, &after, sizeof(audit)));
    }
    if (es3) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo[0]);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo[1]);
        take(&before);
        assert(before.ubo_range[0] == 0 && before.ubo_range[1] == 0);
        assert(frame(640, 480) == SPO_OK);
        take(&after);
        assert(!memcmp(&before, &after, sizeof(audit)));
    }
    spo_reset_timing();
    assert(frame(640, 480) == SPO_OK);
    {
        spo_stats stats = {0};
        stats.struct_size = sizeof(stats);
        assert(spo_get_stats(&stats) == SPO_OK);
        assert(stats.vertices > 341 * 6);
        assert(stats.draw_calls == 1);
    }
    {
        /* Counter rows follow the FPS row: labels start 16 px apart at the default size. */
        check_glyph(2, 'F', 12, 12);
        check_glyph(2, 'C', 12, 28);
        check_glyph(2, 'S', 12, 12 + 16 * 9);
    }
    assert(frame(640, 480) == SPO_OK);
    assert(draw_calls == 1 && upload_calls == 0);
    assert(texture_calls == 0);
    glReadPixels(10, 469, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(glGetError() == GL_NO_ERROR);
    assert(pixel[0] < 110 && pixel[1] < 110 && pixel[2] < 110 && pixel[3] > 200);
    assert(spo_preset_set(0) == SPO_OK);
    o.struct_size = sizeof(o);
    assert(spo_overlay_get(&o) == SPO_OK);
    o.items = 1u << SPO_GRAPH;
    assert(spo_overlay_set(&o) == SPO_OK);
    spo_reset_timing();
    spo->history_count = spo->history_next = 120;
    spo->history_serial = 120;
    for (i = 0; i < 120; i++)
        spo->intervals[i] = 16666000;
    spo->graph_range_at = spo_now();
    assert(frame(640, 480) == SPO_OK);
    take(&after);
    assert(!memcmp(&before, &after, sizeof(audit)));
    glReadPixels(137, 480 - 33 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[1] > 160 && pixel[1] > pixel[0] + 30);
    glReadPixels(137, 480 - 18 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] < 130 && pixel[1] < 130);
    spo->intervals[60] = 100000000;
    spo->bar_tone[60] = 0;
    spo->graph_serial = UINT64_MAX;
    spo->last_stamp = 0;
    assert(frame(640, 480) == SPO_OK);
    glReadPixels(141, 480 - 16 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] > 200 && pixel[1] < 140 && pixel[2] < 140);
    assert(frame(640, 480) == SPO_OK);
    assert(draw_calls == 1 && upload_calls == 0);
    assert(texture_calls == 1 && texture_width == 1);
    /* Every bar reaches the screen as the ring turns. */
    spo_reset_timing();
    for (i = 0; i < 520; i++) {
        spo_history_push(16666000);
        spo->last_stamp = 0;
        assert(frame(640, 480) == SPO_OK);
        if (i > 130 && i % 3 == 0) {
            uint8_t row[240 * 4];
            glReadPixels(12, 480 - 39 - 1, 240, 1, GL_RGBA, GL_UNSIGNED_BYTE, row);
            for (int column = 0; column < 120; column++)
                assert(row[(column * 2 + 1) * 4 + 1] > 160 && row[(column * 2) * 4 + 1] > 160);
        }
    }
    spo->mesh.graph_version++;
    spo->mesh.graph_head = (spo->mesh.graph_head + 1) % 120;
    rendering = 1;
    texture_calls = upload_calls = draw_calls = 0;
    uint32_t draws = 0;
    assert(spo_backend_draw(&spo->mesh, 640, 480, &draws) == SPO_OK);
    rendering = 0;
    assert(draws == 1 && draw_calls == 1 && !texture_calls && !upload_calls);
    assert(glGetError() == GL_NO_ERROR);
    assert(spo_preset_set(0) == SPO_OK);
    take(&before);
    assert(frame(640, 480) == SPO_OK);
    take(&after);
    assert(!memcmp(&before, &after, sizeof(audit)));
    o.struct_size = sizeof(o);
    assert(spo_overlay_get(&o) == SPO_OK);
    o.items = 1u << SPO_FPS;
    spo->counter_mask = spo->indicator_mask = 0;
    for (i = 0; i < 4; i++) {
        static const uint32_t sizes[] = {3, 4, 1, 2};
        const spo_font_info *f = spo_font_get(sizes[i]);
        o.font_scale = sizes[i];
        assert(spo_overlay_set(&o) == SPO_OK);
        font_upload = 1;
        assert(frame(640, 480) == SPO_OK);
        font_upload = 0;
        assert(texture_calls > 0);
        check_glyph((int)sizes[i], 'F', 8 + f->cw / 2, 8 + f->ch / 4);
        assert(frame(640, 480) == SPO_OK && texture_calls == 0);
    }
    o.font_scale = 4;
    assert(spo_overlay_set(&o) == SPO_OK);
    font_upload = 1;
    assert(frame(640, 480) == SPO_OK);
    font_upload = 0;
    spo_shutdown();
    take(&after);
    assert(!memcmp(&before, &after, sizeof(audit)));
    glDeleteProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteFramebuffers(1, &fb);
    glDeleteTextures(3, tex);
    glDeleteBuffers(3, buf);
    if (es3)
        glDeleteBuffers(2, ubo);
    if (vao)
        delete_vao(1, &vao);
    if (sampler)
        glDeleteSamplers(1, &sampler);
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(d, ctx);
    eglDestroySurface(d, surface);
    eglTerminate(d);
    puts("GL state, host objects, nondefault framebuffer and overlay pixels passed");
    return 0;
}
