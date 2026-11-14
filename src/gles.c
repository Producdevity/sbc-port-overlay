/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat;
typedef ptrdiff_t GLsizeiptr, GLintptr;
#define PROCS(X)                                                                                   \
    X(const GLubyte *, GetString, (GLenum))                                                        \
    X(void, GetIntegerv, (GLenum, GLint *))                                                        \
    X(void, GetBooleanv, (GLenum, GLboolean *))                                                    \
    X(GLboolean, IsEnabled, (GLenum))                                                              \
    X(void, Enable, (GLenum))                                                                      \
    X(void, Disable, (GLenum))                                                                     \
    X(void, ActiveTexture, (GLenum))                                                               \
    X(void, BindTexture, (GLenum, GLuint))                                                         \
    X(void, GenTextures, (GLsizei, GLuint *))                                                      \
    X(void, DeleteTextures, (GLsizei, const GLuint *))                                             \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                                \
    X(void, TexImage2D,                                                                            \
      (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))               \
    X(void, PixelStorei, (GLenum, GLint))                                                          \
    X(void, GenBuffers, (GLsizei, GLuint *))                                                       \
    X(void, DeleteBuffers, (GLsizei, const GLuint *))                                              \
    X(void, BindBuffer, (GLenum, GLuint))                                                          \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum))                                \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *))                           \
    X(GLuint, CreateShader, (GLenum))                                                              \
    X(void, ShaderSource, (GLuint, GLsizei, const char *const *, const GLint *))                   \
    X(void, CompileShader, (GLuint))                                                               \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *))                                                \
    X(void, DeleteShader, (GLuint))                                                                \
    X(GLuint, CreateProgram, (void))                                                               \
    X(void, AttachShader, (GLuint, GLuint))                                                        \
    X(void, BindAttribLocation, (GLuint, GLuint, const char *))                                    \
    X(void, LinkProgram, (GLuint))                                                                 \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *))                                               \
    X(void, DeleteProgram, (GLuint))                                                               \
    X(void, UseProgram, (GLuint))                                                                  \
    X(GLint, GetUniformLocation, (GLuint, const char *))                                           \
    X(void, Uniform1i, (GLint, GLint))                                                             \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                                  \
    X(void, GetVertexAttribiv, (GLuint, GLenum, GLint *))                                          \
    X(void, GetVertexAttribfv, (GLuint, GLenum, GLfloat *))                                        \
    X(void, GetVertexAttribPointerv, (GLuint, GLenum, void **))                                    \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))        \
    X(void, VertexAttrib4fv, (GLuint, const GLfloat *))                                            \
    X(void, EnableVertexAttribArray, (GLuint))                                                     \
    X(void, DisableVertexAttribArray, (GLuint))                                                    \
    X(void, BlendEquationSeparate, (GLenum, GLenum))                                               \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                   \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                            \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                             \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                               \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))
#define DECL(ret, name, args) ret(*name) args;
typedef struct {
    PROCS(DECL)
    void (*BindVertexArray)(GLuint);
    void (*GenVertexArrays)(GLsizei, GLuint *);
    void (*DeleteVertexArrays)(GLsizei, const GLuint *);
    void (*BindSampler)(GLuint, GLuint);
    void (*GetIntegeri_v)(GLenum, GLuint, GLint *);
    void (*GetInteger64i_v)(GLenum, GLuint, int64_t *);
    void (*BindBufferBase)(GLenum, GLuint, GLuint);
    void (*BindBufferRange)(GLenum, GLuint, GLuint, GLintptr, GLsizeiptr);
    GLuint (*GetUniformBlockIndex)(GLuint, const char *);
    void (*UniformBlockBinding)(GLuint, GLuint, GLuint);
    void (*Uniform4uiv)(GLint, GLsizei, const GLuint *);
    GLuint program, texture, buffers[3], vao;
    GLint viewport_uniform, texture_uniform;
    GLint graph_uniform, graph_scale_uniform, graph_slot_uniform, graph_count_uniform;
    int graph_slot, graph_count;
    uint32_t graph_words[32], graph_scale;
    int es3, sample_mask, slot;
    uint32_t width, height;
    uint32_t ubo_stride;
    spo_mesh cached_mesh;
    GLfloat packed[SPO_QUADS * 12];
    int cached_slot;
    uint32_t prefix_count, prefix_version, slot_version[3];
} spo_gl;
_Static_assert(sizeof(spo_context) + sizeof(spo_gl) < 1024 * 1024, "CPU storage budget");
#undef DECL
enum { ARRAY_BUFFER = 0x8892, TEXTURE_2D = 0x0de1, TEXTURE0 = 0x84c0 };
typedef struct {
    GLint enabled, size, type, normalized, stride, buffer;
    void *pointer;
    GLfloat current[4];
} attrib_state;
typedef struct {
    GLint program, buffer, vao, active, texture, sampler;
    GLint ubo, indexed_ubo;
    int64_t ubo_start, ubo_size;
    GLint eq_rgb, eq_alpha, src_rgb, dst_rgb, src_alpha, dst_alpha;
    GLint viewport[4];
    GLboolean color[4], enabled[10];
    int viewport_changed, color_changed, equation_changed, function_changed, enables_changed;
    attrib_state attrs[3];
} gl_state;
static const GLenum enables[] = {0x0be2, 0x0b71, 0x0b90, 0x0c11, 0x0b44,
                                 0x809e, 0x80a0, 0x8c89, 0x8e51};
static int extension(const char *all, const char *needle) {
    size_t n = strlen(needle);
    const char *p = all;
    if (!p)
        return 0;
    while ((p = strstr(p, needle))) {
        if ((p == all || p[-1] == ' ') && (!p[n] || p[n] == ' '))
            return 1;
        p += n;
    }
    return 0;
}
static int load(void *out, size_t size, const char *name) {
    void *p = spo->config.get_proc_address(name, spo->config.gl_user);
    if (!p || size != sizeof(p))
        return 0;
    memcpy(out, &p, size);
    return 1;
}
static void capture(spo_gl *g, gl_state *s) {
    int i, n = g->sample_mask ? 9 : g->es3 ? 8 : 7;
    s->viewport_changed = s->color_changed = s->equation_changed = s->function_changed =
        s->enables_changed = 0;
    g->GetIntegerv(0x8b8d, &s->program);
    if (!g->es3)
        g->GetIntegerv(0x8894, &s->buffer);
    g->GetIntegerv(0x84e0, &s->active);
    if (s->active != TEXTURE0)
        g->ActiveTexture(TEXTURE0);
    g->GetIntegerv(0x8069, &s->texture);
    if (g->es3) {
        g->GetIntegerv(0x8919, &s->sampler);
        g->GetIntegerv(0x8a28, &s->ubo);
        g->GetIntegeri_v(0x8a28, 0, &s->indexed_ubo);
        s->ubo_start = s->ubo_size = 0;
        if (s->indexed_ubo) {
            g->GetInteger64i_v(0x8a29, 0, &s->ubo_start);
            g->GetInteger64i_v(0x8a2a, 0, &s->ubo_size);
        }
    }
    if (g->BindVertexArray)
        g->GetIntegerv(0x85b5, &s->vao);
    g->GetIntegerv(0x8009, &s->eq_rgb);
    g->GetIntegerv(0x883d, &s->eq_alpha);
    g->GetIntegerv(0x80c9, &s->src_rgb);
    g->GetIntegerv(0x80c8, &s->dst_rgb);
    g->GetIntegerv(0x80cb, &s->src_alpha);
    g->GetIntegerv(0x80ca, &s->dst_alpha);
    g->GetIntegerv(0x0ba2, s->viewport);
    g->GetBooleanv(0x0c23, s->color);
    for (i = 0; i < n; i++)
        s->enabled[i] = g->IsEnabled(enables[i]);
    for (i = 0; !g->es3 && i < 3; i++) {
        attrib_state *a = &s->attrs[i];
        g->GetVertexAttribfv((GLuint)i, 0x8626, a->current);
        if (g->BindVertexArray)
            continue;
        g->GetVertexAttribiv((GLuint)i, 0x8622, &a->enabled);
        g->GetVertexAttribiv((GLuint)i, 0x8623, &a->size);
        g->GetVertexAttribiv((GLuint)i, 0x8625, &a->type);
        g->GetVertexAttribiv((GLuint)i, 0x886a, &a->normalized);
        g->GetVertexAttribiv((GLuint)i, 0x8624, &a->stride);
        g->GetVertexAttribiv((GLuint)i, 0x889f, &a->buffer);
        g->GetVertexAttribPointerv((GLuint)i, 0x8645, &a->pointer);
    }
}
static void restore(spo_gl *g, const gl_state *s) {
    int i, n = g->sample_mask ? 9 : g->es3 ? 8 : 7;
    if (g->BindVertexArray)
        g->BindVertexArray((GLuint)s->vao);
    for (i = 0; !g->es3 && i < 3; i++) {
        const attrib_state *a = &s->attrs[i];
        if (!g->BindVertexArray) {
            g->BindBuffer(ARRAY_BUFFER, (GLuint)a->buffer);
            g->VertexAttribPointer((GLuint)i, a->size, (GLenum)a->type, (GLboolean)a->normalized,
                                   a->stride, a->pointer);
            if (a->enabled)
                g->EnableVertexAttribArray((GLuint)i);
            else
                g->DisableVertexAttribArray((GLuint)i);
        }
        g->VertexAttrib4fv((GLuint)i, a->current);
    }
    if (!g->es3)
        g->BindBuffer(ARRAY_BUFFER, (GLuint)s->buffer);
    else {
        if (s->ubo_size)
            g->BindBufferRange(0x8a11, 0, (GLuint)s->indexed_ubo, (GLintptr)s->ubo_start,
                               (GLsizeiptr)s->ubo_size);
        else
            g->BindBufferBase(0x8a11, 0, (GLuint)s->indexed_ubo);
        if (s->ubo != s->indexed_ubo)
            g->BindBuffer(0x8a11, (GLuint)s->ubo);
    }
    g->UseProgram((GLuint)s->program);
    g->BindTexture(TEXTURE_2D, (GLuint)s->texture);
    if (g->es3 && s->sampler) {
        g->BindSampler(0, (GLuint)s->sampler);
    }
    if (s->active != TEXTURE0)
        g->ActiveTexture((GLenum)s->active);
    if (s->equation_changed)
        g->BlendEquationSeparate((GLenum)s->eq_rgb, (GLenum)s->eq_alpha);
    if (s->function_changed)
        g->BlendFuncSeparate((GLenum)s->src_rgb, (GLenum)s->dst_rgb, (GLenum)s->src_alpha,
                             (GLenum)s->dst_alpha);
    if (s->viewport_changed)
        g->Viewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    if (s->color_changed)
        g->ColorMask(s->color[0], s->color[1], s->color[2], s->color[3]);
    for (i = 0; i < n; i++) {
        if (!(s->enables_changed & (1 << i)))
            continue;
        if (s->enabled[i])
            g->Enable(enables[i]);
        else
            g->Disable(enables[i]);
    }
}
static GLuint shader(spo_gl *g, GLenum type, const char *source) {
    GLint ok = 0;
    GLuint s = g->CreateShader(type);
    if (!s)
        return 0;
    g->ShaderSource(s, 1, &source, NULL);
    g->CompileShader(s);
    g->GetShaderiv(s, 0x8b81, &ok);
    if (!ok) {
        g->DeleteShader(s);
        return 0;
    }
    return s;
}
static void unpack_capture(spo_gl *g, GLint state[5]) {
    /* All multirow uploads have an eight-byte-aligned pitch; the tail is one row. */
    if (g->es3) {
        g->GetIntegerv(0x88ef, &state[1]);
        g->GetIntegerv(0x0cf2, &state[2]);
        g->GetIntegerv(0x0cf3, &state[3]);
        g->GetIntegerv(0x0cf4, &state[4]);
        if (state[1])
            g->BindBuffer(0x88ec, 0);
        if (state[2])
            g->PixelStorei(0x0cf2, 0);
        if (state[3])
            g->PixelStorei(0x0cf3, 0);
        if (state[4])
            g->PixelStorei(0x0cf4, 0);
    }
}
static void unpack_restore(spo_gl *g, const GLint state[5]) {
    if (g->es3) {
        if (state[1])
            g->BindBuffer(0x88ec, (GLuint)state[1]);
        if (state[2])
            g->PixelStorei(0x0cf2, state[2]);
        if (state[3])
            g->PixelStorei(0x0cf3, state[3]);
        if (state[4])
            g->PixelStorei(0x0cf4, state[4]);
    }
}
static void texture_parameters(spo_gl *g) {
    g->TexParameteri(TEXTURE_2D, 0x2801, 0x2600);
    g->TexParameteri(TEXTURE_2D, 0x2800, 0x2600);
    g->TexParameteri(TEXTURE_2D, 0x2802, 0x812f);
    g->TexParameteri(TEXTURE_2D, 0x2803, 0x812f);
}
spo_result spo_backend_init(const spo_atlas *atlas) {
    spo_gl *g = calloc(1, sizeof(*g));
    gl_state state;
    int major = 2, minor = 0, i, unpack_saved = 0;
    const char *version, *extensions;
    GLuint vertex = 0, fragment = 0;
    GLint ok = 0, unpack[5] = {0};
    const char *vs =
        "attribute vec2 p;attribute vec2 uv;attribute vec4 color;uniform vec2 size;varying vec2 "
        "t;varying vec4 c;void "
        "main(){gl_Position=vec4(p.x*2.0/size.x-1.0,1.0-p.y*2.0/size.y,0.0,1.0);t=uv;c=color;}";
    const char *fs =
        "precision mediump float;uniform sampler2D atlas;varying vec2 t;varying vec4 c;void "
        "main(){float a=texture2D(atlas,t).a*c.a;gl_FragColor=vec4(c.rgb*a,a);}";
    if (!g)
        return SPO_CAPACITY;
    spo->renderer = g;
#define LOAD(ret, name, args)                                                                      \
    if (!load(&g->name, sizeof(g->name), "gl" #name))                                              \
        goto unavailable;
    PROCS(LOAD)
#undef LOAD
    version = (const char *)g->GetString(0x1f02);
    if (!version)
        goto unavailable;
    if (strstr(version, "OpenGL ES")) {
        const char *p = strstr(version, "OpenGL ES") + 9;
        while (*p && (*p < '0' || *p > '9'))
            p++;
        if (sscanf(p, "%d.%d", &major, &minor) != 2 || major < 2)
            goto unavailable;
    }
    g->es3 = major >= 3;
    if (strstr(version, "gl4es") || strstr(version, "GL4ES"))
        spo_text_copy(spo->renderer_name, sizeof(spo->renderer_name), "gl4es (GLES2)");
    else
        snprintf(spo->renderer_name, sizeof(spo->renderer_name), "GLES %d.%d", major, minor);
    g->sample_mask = major > 3 || (major == 3 && minor >= 1);
    extensions = (const char *)g->GetString(0x1f03);
    if (g->es3 || extension(extensions, "GL_OES_vertex_array_object")) {
        const char *suffix = g->es3 ? "" : "OES";
        char name[64];
        snprintf(name, sizeof(name), "glBindVertexArray%s", suffix);
        if (!load(&g->BindVertexArray, sizeof(g->BindVertexArray), name))
            goto unavailable;
        snprintf(name, sizeof(name), "glGenVertexArrays%s", suffix);
        if (!load(&g->GenVertexArrays, sizeof(g->GenVertexArrays), name))
            goto unavailable;
        snprintf(name, sizeof(name), "glDeleteVertexArrays%s", suffix);
        if (!load(&g->DeleteVertexArrays, sizeof(g->DeleteVertexArrays), name))
            goto unavailable;
    }
    if (g->es3 && !load(&g->BindSampler, sizeof(g->BindSampler), "glBindSampler"))
        goto unavailable;
    if (g->es3) {
#define EXTRA(name)                                                                                \
    if (!load(&g->name, sizeof(g->name), "gl" #name))                                              \
    goto unavailable
        EXTRA(GetIntegeri_v);
        EXTRA(GetInteger64i_v);
        EXTRA(BindBufferBase);
        EXTRA(BindBufferRange);
        EXTRA(GetUniformBlockIndex);
        EXTRA(UniformBlockBinding);
        EXTRA(Uniform4uiv);
#undef EXTRA
    }
    capture(g, &state);
    if (g->GenVertexArrays)
        g->GenVertexArrays(1, &g->vao);
    if (g->GenVertexArrays && !g->vao)
        goto resource_failure;
    if (g->es3) {
        const char *vertex3 =
            "#version 300 es\nprecision highp float;precision highp int;"
            "layout(std140) uniform Mesh {vec4 quads[1023];};uniform vec2 size;"
            "uniform highp uvec4 graph_data[8];uniform int graph_scale;uniform int "
            "graph_slot;uniform int graph_count;"
            "out mediump vec2 t;out lowp vec4 c;"
            "void main(){int q=gl_VertexID/6;int k=gl_VertexID%6;"
            "vec2 corner=vec2(k==2||k==4||k==5?1.:0.,k==1||k==2||k==4?1.:0.);vec2 p;"
            "if(graph_slot>=0&&q>=graph_slot&&q<graph_slot+graph_count){"
            "int column=(q-graph_slot)%120;int w=column>>2;"
            "uint b=(graph_data[w>>2][w&3]>>uint((column&3)*8))&255u;"
            "int n=graph_slot*3;bool cap=q>=graph_slot+120;"
            "float h=cap?((b&128u)!=0u?float(graph_scale):0.):float(b&127u);"
            "float top=cap?quads[n].y:quads[n].w-h;"
            "p=vec2(quads[n].x+float(column)*float(graph_scale),top)+"
            "corner*vec2(float(graph_scale),h);"
            "t=vec2(127.5/128.,63.5/64.);c=quads[n+2];"
            "if(cap)c.rgb=vec3(240.,242.,244.)/255.;"
            "}else{if(graph_slot>=0&&q>=graph_slot+graph_count)q-=graph_count-1;int n=q*3;"
            "p=mix(quads[n].xy,quads[n].zw,corner);"
            "t=mix(quads[n+1].xy,quads[n+1].zw,corner);c=quads[n+2];}"
            "gl_Position=vec4(p.x*2./size.x-1.,1.-p.y*2./size.y,0.,1.);}";
        const char *fragment3 =
            "#version 300 es\nprecision mediump float;uniform sampler2D atlas;"
            "in mediump vec2 t;in lowp vec4 c;out vec4 output_color;"
            "void main(){float a=texture(atlas,t).a*c.a;output_color=vec4(c.rgb*a,a);}";
        /* ES3 cannot query the type of current generic attributes; avoid using them. */
        vertex = shader(g, 0x8b31, vertex3);
        fragment = shader(g, 0x8b30, fragment3);
    } else {
        vertex = shader(g, 0x8b31, vs);
        fragment = shader(g, 0x8b30, fs);
    }
    if (vertex && fragment) {
        g->program = g->CreateProgram();
    }
    if (g->program) {
        g->AttachShader(g->program, vertex);
        g->AttachShader(g->program, fragment);
        g->BindAttribLocation(g->program, 0, "p");
        g->BindAttribLocation(g->program, 1, "uv");
        g->BindAttribLocation(g->program, 2, "color");
        g->LinkProgram(g->program);
        g->GetProgramiv(g->program, 0x8b82, &ok);
    }
    if (vertex)
        g->DeleteShader(vertex);
    if (fragment)
        g->DeleteShader(fragment);
    if (!ok)
        goto resource_failure;
    g->viewport_uniform = g->GetUniformLocation(g->program, "size");
    g->texture_uniform = g->GetUniformLocation(g->program, "atlas");
    if (g->es3) {
        g->graph_uniform = g->GetUniformLocation(g->program, "graph_data[0]");
        g->graph_scale_uniform = g->GetUniformLocation(g->program, "graph_scale");
        g->graph_slot_uniform = g->GetUniformLocation(g->program, "graph_slot");
        g->graph_count_uniform = g->GetUniformLocation(g->program, "graph_count");
        g->graph_slot = -2;
    }
    {
        GLenum target = g->es3 ? 0x8a11 : ARRAY_BUFFER;
        GLsizeiptr bytes = (GLsizeiptr)sizeof(spo->mesh.vertices);
        if (g->es3) {
            GLint alignment = 0;
            GLuint block = g->GetUniformBlockIndex(g->program, "Mesh");
            g->GetIntegerv(0x8a34, &alignment);
            if (alignment <= 0 || alignment > 1048576 || block == 0xffffffffu)
                goto resource_failure;
            g->ubo_stride =
                (16368u + (uint32_t)alignment - 1) / (uint32_t)alignment * (uint32_t)alignment;
            bytes = (GLsizeiptr)(g->ubo_stride * ((SPO_QUADS + 340) / 341));
            g->UniformBlockBinding(g->program, block, 0);
        }
        g->GenBuffers(3, g->buffers);
        if (!g->buffers[0] || !g->buffers[1] || !g->buffers[2])
            goto resource_failure;
        for (i = 0; i < 3; i++) {
            g->BindBuffer(target, g->buffers[i]);
            g->BufferData(target, bytes, NULL, 0x88e8);
        }
    }
    unpack_capture(g, unpack);
    unpack_saved = 1;
    g->GenTextures(1, &g->texture);
    if (!g->texture)
        goto resource_failure;
    g->BindTexture(TEXTURE_2D, g->texture);
    texture_parameters(g);
    g->TexImage2D(TEXTURE_2D, 0, 0x1908, (GLsizei)atlas->width, (GLsizei)atlas->height, 0, 0x1908,
                  0x1401, atlas->rgba);
    unpack_restore(g, unpack);
    unpack_saved = 0;
    g->UseProgram(g->program);
    g->Uniform1i(g->texture_uniform, 0);
    restore(g, &state);
    return SPO_OK;
resource_failure:
    if (unpack_saved)
        unpack_restore(g, unpack);
    restore(g, &state);
    spo_backend_shutdown();
    return SPO_GL_UNAVAILABLE;
unavailable:
    free(g);
    spo->renderer = NULL;
    return SPO_GL_UNAVAILABLE;
}
void spo_backend_shutdown(void) {
    spo_gl *g = spo->renderer;
    if (!g)
        return;
    if (!spo->lost) {
        if (g->program)
            g->DeleteProgram(g->program);
        if (g->texture)
            g->DeleteTextures(1, &g->texture);
        if (g->buffers[0] || g->buffers[1] || g->buffers[2])
            g->DeleteBuffers(3, g->buffers);
        if (g->vao)
            g->DeleteVertexArrays(1, &g->vao);
    }
    free(g);
    spo->renderer = NULL;
}
static int graph_rectangle(spo_mesh *out, const spo_vertex *style, float x, float y, float w,
                           float h, int cap) {
    if (h <= 0)
        return 1;
    if (out->count + 6 > SPO_QUADS * 6)
        return 0;
    spo_vertex a = *style, b, c, d;
    a.x = x;
    a.y = y;
    a.u = 127.5f / 128.0f;
    a.v = 63.5f / 64.0f;
    if (cap) {
        a.color[0] = 240;
        a.color[1] = 242;
        a.color[2] = 244;
    }
    b = a;
    b.y += h;
    c = b;
    c.x += w;
    d = a;
    d.x += w;
    spo_vertex *vertices = out->vertices + out->count;
    vertices[0] = a;
    vertices[1] = b;
    vertices[2] = c;
    vertices[3] = a;
    vertices[4] = c;
    vertices[5] = d;
    out->count += 6;
    return 1;
}
static int expand_graph(const spo_mesh *mesh, spo_mesh *out) {
    out->count = 0;
    for (uint32_t first = 0; first < mesh->count; first += 6) {
        const spo_vertex *quad = mesh->vertices + first;
        if (quad->u >= 0) {
            if (out->count + 6 > SPO_QUADS * 6)
                return 0;
            memcpy(out->vertices + out->count, quad, 6 * sizeof(spo_vertex));
            out->count += 6;
        } else {
            for (uint32_t column = 0; column < 120; column++) {
                uint32_t value = (mesh->graph_words[column / 4] >> ((column % 4) * 8)) & 255;
                float height = (float)(value & 127), scale = (float)mesh->graph_scale;
                float x = quad->x + (float)column * scale;
                if (!graph_rectangle(out, quad, x, quad[2].y - height, scale, height, 0))
                    return 0;
                if ((value & 128) && !graph_rectangle(out, quad, x, quad->y, scale, scale, 1))
                    return 0;
            }
        }
    }
    return 1;
}
spo_result spo_backend_draw(const spo_mesh *mesh, uint32_t prefix, uint32_t width, uint32_t height,
                            uint32_t *draws) {
    spo_gl *g = spo->renderer;
    gl_state state;
    int i, n;
    if (!g)
        return SPO_GL_UNAVAILABLE;
    if (!g->es3 && mesh->graph_scale) {
        if (!expand_graph(mesh, &g->cached_mesh))
            return SPO_CAPACITY;
        mesh = &g->cached_mesh;
    }
    capture(g, &state);
    n = g->sample_mask ? 9 : g->es3 ? 8 : 7;
    for (i = 0; i < n; i++)
        if (state.enabled[i] != (i == 0)) {
            state.enables_changed |= 1 << i;
            if (i == 0)
                g->Enable(enables[i]);
            else
                g->Disable(enables[i]);
        }
    if (state.eq_rgb != 0x8006 || state.eq_alpha != 0x8006) {
        state.equation_changed = 1;
        g->BlendEquationSeparate(0x8006, 0x8006);
    }
    if (state.src_rgb != 1 || state.src_alpha != 1 || state.dst_rgb != 0x0303 ||
        state.dst_alpha != 0x0303) {
        state.function_changed = 1;
        g->BlendFuncSeparate(1, 0x0303, 1, 0x0303);
    }
    if (state.viewport[0] || state.viewport[1] || state.viewport[2] != (GLint)width ||
        state.viewport[3] != (GLint)height) {
        state.viewport_changed = 1;
        g->Viewport(0, 0, (GLsizei)width, (GLsizei)height);
    }
    if (!state.color[0] || !state.color[1] || !state.color[2] || !state.color[3]) {
        state.color_changed = 1;
        g->ColorMask(1, 1, 1, 1);
    }
    g->UseProgram(g->program);
    if (g->width != width || g->height != height) {
        g->Uniform2f(g->viewport_uniform, (float)width, (float)height);
        g->width = width;
        g->height = height;
    }
    g->BindTexture(TEXTURE_2D, g->texture);
    if (g->es3 && state.sampler)
        g->BindSampler(0, 0);
    if (g->BindVertexArray)
        g->BindVertexArray(g->vao);
    if (g->es3) {
        uint32_t first, j, batch = 0;
        int graph_count = 120;
        if (mesh->graph_scale) {
            for (j = 0; j < 30; j++)
                if (mesh->graph_words[j] & UINT32_C(0x80808080))
                    graph_count = 240;
            if (g->graph_count != graph_count) {
                g->Uniform1i(g->graph_count_uniform, graph_count);
                g->graph_count = graph_count;
            }
            if (memcmp(g->graph_words, mesh->graph_words, sizeof(g->graph_words))) {
                memcpy(g->graph_words, mesh->graph_words, sizeof(g->graph_words));
                g->Uniform4uiv(g->graph_uniform, 8, mesh->graph_words);
            }
            if (g->graph_scale != mesh->graph_scale) {
                g->graph_scale = mesh->graph_scale;
                g->Uniform1i(g->graph_scale_uniform, (GLint)mesh->graph_scale);
            }
        }
        int prefix_changed =
            prefix != g->prefix_count ||
            memcmp(mesh->vertices, g->cached_mesh.vertices, prefix * sizeof(spo_vertex));
        int changed = prefix_changed || mesh->count != g->cached_mesh.count ||
                      memcmp(mesh->vertices + prefix, g->cached_mesh.vertices + prefix,
                             (mesh->count - prefix) * sizeof(spo_vertex));
        if (prefix_changed) {
            g->prefix_count = prefix;
            g->prefix_version++;
        }
        int slot = changed ? g->slot : g->cached_slot;
        if (changed) {
            for (j = 0; j < mesh->count / 6; j++) {
                const spo_vertex *a = mesh->vertices + j * 6, *b = a + 2;
                if (j * 6 < g->cached_mesh.count &&
                    !memcmp(a, g->cached_mesh.vertices + j * 6, sizeof(*a)) &&
                    !memcmp(b, g->cached_mesh.vertices + j * 6 + 2, sizeof(*b)))
                    continue;
                GLfloat *out = g->packed + j * 12;
                out[0] = a->x;
                out[1] = a->y;
                out[2] = b->x;
                out[3] = b->y;
                out[4] = a->u;
                out[5] = a->v;
                out[6] = b->u;
                out[7] = b->v;
                for (int channel = 0; channel < 4; channel++)
                    out[8 + channel] = (float)a->color[channel] / 255.0f;
            }
            g->cached_mesh.count = mesh->count;
            uint32_t copy_from = prefix_changed ? 0 : prefix;
            memcpy(g->cached_mesh.vertices + copy_from, mesh->vertices + copy_from,
                   (mesh->count - copy_from) * sizeof(spo_vertex));
            g->cached_slot = slot;
            g->BindBuffer(0x8a11, g->buffers[slot]);
        }
        for (first = 0; first < mesh->count; first += 341 * 6, batch++) {
            uint32_t quads = (mesh->count - first) / 6;
            if (quads > 341)
                quads = 341;
            uint32_t upload_first = 0;
            if (g->slot_version[slot] == g->prefix_version && prefix > first) {
                upload_first = (prefix - first) / 6;
                if (upload_first > quads)
                    upload_first = quads;
            }
            if (changed && upload_first < quads)
                g->BufferSubData(0x8a11, (GLintptr)(batch * g->ubo_stride + upload_first * 48),
                                 (GLsizeiptr)((quads - upload_first) * 48),
                                 g->packed + (first / 6 + upload_first) * 12);
            g->BindBufferRange(0x8a11, 0, g->buffers[slot], (GLintptr)(batch * g->ubo_stride),
                               16368);
            int graph_slot = -1;
            if (mesh->graph_scale)
                for (j = 0; j < quads; j++)
                    if (mesh->vertices[first + j * 6].u < 0) {
                        graph_slot = (int)j;
                        break;
                    }
            if (g->graph_slot != graph_slot) {
                g->Uniform1i(g->graph_slot_uniform, graph_slot);
                g->graph_slot = graph_slot;
            }
            g->DrawArrays(0x0004, 0,
                          (GLsizei)((quads + (graph_slot >= 0 ? graph_count - 1 : 0)) * 6));
            (*draws)++;
        }
        if (changed)
            g->slot = (g->slot + 1) % 3;
        g->slot_version[slot] = g->prefix_version;
    } else {
        g->BindBuffer(ARRAY_BUFFER, g->buffers[g->slot]);
        g->slot = (g->slot + 1) % 3;
        g->BufferSubData(ARRAY_BUFFER, 0, (GLsizeiptr)(mesh->count * sizeof(spo_vertex)),
                         mesh->vertices);
        g->VertexAttribPointer(0, 2, 0x1406, 0, sizeof(spo_vertex),
                               (const void *)(uintptr_t)offsetof(spo_vertex, x));
        g->VertexAttribPointer(1, 2, 0x1406, 0, sizeof(spo_vertex),
                               (const void *)(uintptr_t)offsetof(spo_vertex, u));
        g->VertexAttribPointer(2, 4, 0x1401, 1, sizeof(spo_vertex),
                               (const void *)(uintptr_t)offsetof(spo_vertex, color));
        for (i = 0; i < 3; i++)
            g->EnableVertexAttribArray((GLuint)i);
    }
    if (!g->es3) {
        g->DrawArrays(0x0004, 0, (GLsizei)mesh->count);
        *draws = 1;
    }
    restore(g, &state);
    return SPO_OK;
}
