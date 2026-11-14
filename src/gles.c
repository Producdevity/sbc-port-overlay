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
    X(void, TexSubImage2D,                                                                         \
      (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *))               \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                         \
    X(void, GenFramebuffers, (GLsizei, GLuint *))                                                  \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *))                                         \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                     \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                                    \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                             \
    X(void, GenRenderbuffers, (GLsizei, GLuint *))                                                 \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint *))                                        \
    X(void, BindRenderbuffer, (GLenum, GLuint))                                                    \
    X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                               \
    X(void, Finish, (void))                                                                        \
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
    GLuint program, texture, buffer, vao;
    GLint viewport_uniform, texture_uniform, graph_uniform;
    uint32_t graph_head, graph_count, graph_scale, graph_epoch, graph_version;
    int es3, sample_mask, unpack_subimage;
    uint32_t width, height, stride, slot, attribute_slot;
    uint32_t cached_revision, slot_count[3];
    spo_mesh cached_mesh;
    uint32_t packed[SPO_QUADS * 4];
    union {
        spo_vertex vertices[3][SPO_QUADS * 6];
        uint32_t quads[3][SPO_QUADS * 4];
    } uploaded;
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
        if (state[1])
            g->BindBuffer(0x88ec, 0);
    }
    if (g->unpack_subimage) {
        g->GetIntegerv(0x0cf2, &state[2]);
        g->GetIntegerv(0x0cf3, &state[3]);
        g->GetIntegerv(0x0cf4, &state[4]);
        if (state[2])
            g->PixelStorei(0x0cf2, 0);
        if (state[3])
            g->PixelStorei(0x0cf3, 0);
        if (state[4])
            g->PixelStorei(0x0cf4, 0);
    }
}
static void unpack_restore(spo_gl *g, const GLint state[5]) {
    if (g->es3 && state[1])
        g->BindBuffer(0x88ec, (GLuint)state[1]);
    if (g->unpack_subimage) {
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
static int warmup(spo_gl *, gl_state *);
spo_result spo_backend_init(const spo_atlas *atlas) {
    spo_gl *g = calloc(1, sizeof(*g));
    gl_state state;
    int major = 2, minor = 0, unpack_saved = 0;
    const char *version, *extensions;
    GLuint vertex = 0, fragment = 0;
    GLint ok = 0, unpack[5] = {0};
    const char *vs =
        "attribute vec2 p;attribute vec2 uv;attribute vec4 color;uniform vec2 size;varying vec2 "
        "t;varying vec4 c;void "
        "main(){gl_Position=vec4(p.x*2.0/size.x-1.0,1.0-p.y*2.0/size.y,0.0,1.0);t=uv;c=color;}";
    const char *fragment_body =
        "void main(){vec3 rgb=c.rgb;float a;if(t.x<0.){float column=floor(-t.x-1.);"
        "if(column<120.-graph.y)discard;"
        "float slot=mod(column+graph.x,120.);"
        "vec4 d=SAMPLE(atlas,vec2((slot+.5)/128.,63.5/64.));"
        "float h=floor(d.r*255.+.5);if(h<=(20.-t.y)*graph.z)discard;"
        "if(d.g>.5&&t.y<1.)rgb=vec3(240.,242.,244.)/255.;a=c.a;"
        "}else{a=SAMPLE(atlas,t).a*c.a;}OUTPUT=vec4(rgb*a,a);}";
    char fragment_source[2048];
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
    g->unpack_subimage = g->es3 || extension(extensions, "GL_EXT_unpack_subimage") ||
                         strstr(spo->renderer_name, "gl4es") != NULL;
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
#undef EXTRA
    }
    capture(g, &state);
    if (g->GenVertexArrays)
        g->GenVertexArrays(1, &g->vao);
    if (g->GenVertexArrays && !g->vao)
        goto resource_failure;
    if (g->es3) {
        const char *vertex3 = "#version 300 es\nprecision highp float;precision highp int;"
                              "layout(std140) uniform Mesh {uvec4 quads[1024];};uniform vec2 size;"
                              "out mediump vec2 t;out lowp vec4 c;"
                              "void main(){uvec4 q=quads[gl_VertexID/6];int k=gl_VertexID%6;"
                              "vec2 corner=vec2(k==2||k==4||k==5?1.:0.,k==1||k==2||k==4?1.:0.);"
                              "vec2 a=vec2(q.x&65535u,q.x>>16),b=vec2(q.y&65535u,q.y>>16);"
                              "vec2 p=mix(a,b,corner);"
                              "if(q.z==0xffffffffu)t=mix(vec2(-1.,0.),vec2(-121.,20.),corner);"
                              "else t=mix(vec2(q.z&255u,(q.z>>8)&255u),"
                              "vec2((q.z>>16)&255u,q.z>>24),corner)/vec2(256.,128.);"
                              "c=vec4(q.w&255u,(q.w>>8)&255u,(q.w>>16)&255u,q.w>>24)/255.;"
                              "gl_Position=vec4(p.x*2./size.x-1.,1.-p.y*2./size.y,0.,1.);}";
        snprintf(
            fragment_source, sizeof(fragment_source),
            "#version 300 es\nprecision mediump float;uniform sampler2D atlas;uniform vec3 graph;"
            "in mediump vec2 t;in lowp vec4 c;out vec4 output_color;\n"
            "#define SAMPLE texture\n#define OUTPUT output_color\n%s",
            fragment_body);
        /* Enabled ES3 arrays can destroy unqueryable typed current attributes. */
        vertex = shader(g, 0x8b31, vertex3);
    } else {
        snprintf(fragment_source, sizeof(fragment_source),
                 "precision mediump float;uniform sampler2D atlas;uniform vec3 graph;"
                 "varying vec2 t;varying vec4 c;\n"
                 "#define SAMPLE texture2D\n#define OUTPUT gl_FragColor\n%s",
                 fragment_body);
        vertex = shader(g, 0x8b31, vs);
    }
    fragment = shader(g, 0x8b30, fragment_source);
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
    g->graph_uniform = g->GetUniformLocation(g->program, "graph");
    g->cached_revision = UINT32_MAX;
    g->attribute_slot = UINT32_MAX;
    g->graph_epoch = UINT32_MAX;
    g->graph_count = UINT32_MAX;
    g->stride = (uint32_t)sizeof(spo->mesh.vertices);
    if (g->es3) {
        GLint alignment = 0;
        GLuint block = g->GetUniformBlockIndex(g->program, "Mesh");
        g->GetIntegerv(0x8a34, &alignment);
        if (alignment <= 0 || alignment > 1048576 || block == 0xffffffffu)
            goto resource_failure;
        g->stride = (16384u + (uint32_t)alignment - 1) / (uint32_t)alignment * (uint32_t)alignment;
        g->UniformBlockBinding(g->program, block, 0);
    }
    g->GenBuffers(1, &g->buffer);
    if (!g->buffer)
        goto resource_failure;
    g->BindBuffer(g->es3 ? 0x8a11 : ARRAY_BUFFER, g->buffer);
    g->BufferData(g->es3 ? 0x8a11 : ARRAY_BUFFER, (GLsizeiptr)g->stride * 3, NULL, 0x88e8);
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
    if (!warmup(g, &state))
        goto resource_failure;
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
        if (g->buffer)
            g->DeleteBuffers(1, &g->buffer);
        if (g->vao)
            g->DeleteVertexArrays(1, &g->vao);
    }
    free(g);
    spo->renderer = NULL;
}
static void prepare(spo_gl *g, gl_state *state, uint32_t width, uint32_t height) {
    int i, n = g->sample_mask ? 9 : g->es3 ? 8 : 7;
    for (i = 0; i < n; i++)
        if (state->enabled[i] != (i == 0)) {
            state->enables_changed |= 1 << i;
            if (i == 0)
                g->Enable(enables[i]);
            else
                g->Disable(enables[i]);
        }
    if (state->eq_rgb != 0x8006 || state->eq_alpha != 0x8006) {
        state->equation_changed = 1;
        g->BlendEquationSeparate(0x8006, 0x8006);
    }
    if (state->src_rgb != 1 || state->src_alpha != 1 || state->dst_rgb != 0x0303 ||
        state->dst_alpha != 0x0303) {
        state->function_changed = 1;
        g->BlendFuncSeparate(1, 0x0303, 1, 0x0303);
    }
    if (state->viewport[0] || state->viewport[1] || state->viewport[2] != (GLint)width ||
        state->viewport[3] != (GLint)height) {
        state->viewport_changed = 1;
        g->Viewport(0, 0, (GLsizei)width, (GLsizei)height);
    }
    if (!state->color[0] || !state->color[1] || !state->color[2] || !state->color[3]) {
        state->color_changed = 1;
        g->ColorMask(1, 1, 1, 1);
    }
    g->UseProgram(g->program);
    if (g->width != width || g->height != height) {
        g->Uniform2f(g->viewport_uniform, (float)width, (float)height);
        g->width = width;
        g->height = height;
    }
    g->BindTexture(TEXTURE_2D, g->texture);
    if (g->es3 && state->sampler)
        g->BindSampler(0, 0);
    if (g->BindVertexArray)
        g->BindVertexArray(g->vao);
}
static uint32_t pair(float a, float b) {
    return (uint32_t)a | ((uint32_t)b << 16);
}
static void pack_mesh(spo_gl *g, const spo_mesh *mesh) {
    for (uint32_t i = 0; i < mesh->count; i += 6) {
        const spo_vertex *a = mesh->vertices + i, *b = a + 2;
        if (i < g->cached_mesh.count && !memcmp(a, g->cached_mesh.vertices + i, sizeof(*a)) &&
            !memcmp(b, g->cached_mesh.vertices + i + 2, sizeof(*b)))
            continue;
        uint32_t *out = g->packed + i / 6 * 4;
        out[0] = pair(a->x, a->y);
        out[1] = pair(b->x, b->y);
        out[2] = a->u < 0 ? UINT32_MAX
                          : (uint32_t)(a->u * 256) | ((uint32_t)(a->v * 128) << 8) |
                                ((uint32_t)(b->u * 256) << 16) | ((uint32_t)(b->v * 128) << 24);
        out[3] = (uint32_t)a->color[0] | ((uint32_t)a->color[1] << 8) |
                 ((uint32_t)a->color[2] << 16) | ((uint32_t)a->color[3] << 24);
    }
}
static void upload_mesh(spo_gl *g, const spo_mesh *mesh) {
    if (mesh->revision != g->cached_revision || mesh->count != g->cached_mesh.count) {
        if (mesh->count != g->cached_mesh.count ||
            memcmp(mesh->vertices, g->cached_mesh.vertices, mesh->count * sizeof(spo_vertex))) {
            g->slot = (g->slot + 1) % 3;
            uint32_t unit = g->es3 ? 16 : 6 * sizeof(spo_vertex), count = mesh->count / 6;
            if (g->es3)
                pack_mesh(g, mesh);
            const uint8_t *data =
                g->es3 ? (const uint8_t *)g->packed : (const uint8_t *)mesh->vertices;
            uint8_t *old = g->es3 ? (uint8_t *)g->uploaded.quads[g->slot]
                                  : (uint8_t *)g->uploaded.vertices[g->slot];
            uint32_t first = 0, end = count;
            while (first < count && first < g->slot_count[g->slot] &&
                   !memcmp(data + first * unit, old + first * unit, unit))
                first++;
            while (end > first && end <= g->slot_count[g->slot] &&
                   !memcmp(data + (end - 1) * unit, old + (end - 1) * unit, unit))
                end--;
            if (first < end) {
                GLenum target = g->es3 ? 0x8a11 : ARRAY_BUFFER;
                g->BindBuffer(target, g->buffer);
                g->BufferSubData(target, (GLintptr)g->slot * g->stride + first * unit,
                                 (GLsizeiptr)(end - first) * unit, data + first * unit);
                memcpy(old + first * unit, data + first * unit, (end - first) * unit);
            }
            g->slot_count[g->slot] = count;
            memcpy(g->cached_mesh.vertices, mesh->vertices, mesh->count * sizeof(spo_vertex));
            g->cached_mesh.count = mesh->count;
        }
        g->cached_revision = mesh->revision;
    }
    if (g->es3) {
        g->BindBufferRange(0x8a11, 0, g->buffer, (GLintptr)g->slot * g->stride, 16384);
    } else if (!g->BindVertexArray || g->attribute_slot != g->slot) {
        g->BindBuffer(ARRAY_BUFFER, g->buffer);
        uintptr_t base = (uintptr_t)g->slot * g->stride;
        g->VertexAttribPointer(0, 2, 0x1406, 0, sizeof(spo_vertex),
                               (void *)(base + offsetof(spo_vertex, x)));
        g->VertexAttribPointer(1, 2, 0x1406, 0, sizeof(spo_vertex),
                               (void *)(base + offsetof(spo_vertex, u)));
        g->VertexAttribPointer(2, 4, 0x1401, 1, sizeof(spo_vertex),
                               (void *)(base + offsetof(spo_vertex, color)));
        for (int i = 0; i < 3; i++)
            g->EnableVertexAttribArray((GLuint)i);
        g->attribute_slot = g->slot;
    }
}
static void upload_graph(spo_gl *g, const spo_mesh *mesh) {
    if (!mesh->graph_scale)
        return;
    if (g->graph_epoch != mesh->graph_epoch || g->graph_version != mesh->graph_version) {
        GLint unpack[5] = {0};
        int single = g->graph_epoch == mesh->graph_epoch &&
                     mesh->graph_version == g->graph_version + 1 &&
                     mesh->graph_head == (g->graph_head + 1) % 120;
        uint32_t first = single ? (mesh->graph_head + 119) % 120 : 0;
        unpack_capture(g, unpack);
        g->TexSubImage2D(TEXTURE_2D, 0, (GLint)first, 63, single ? 1 : 120, 1, 0x1908, 0x1401,
                         mesh->graph_data[first]);
        unpack_restore(g, unpack);
        g->graph_epoch = mesh->graph_epoch;
        g->graph_version = mesh->graph_version;
    }
    if (g->graph_head != mesh->graph_head || g->graph_count != mesh->graph_count ||
        g->graph_scale != mesh->graph_scale) {
        g->Uniform3f(g->graph_uniform, (float)mesh->graph_head, (float)mesh->graph_count,
                     (float)mesh->graph_scale);
        g->graph_head = mesh->graph_head;
        g->graph_count = mesh->graph_count;
        g->graph_scale = mesh->graph_scale;
    }
}
static int warmup(spo_gl *g, gl_state *state) {
    GLint draw = 0, read = 0, renderbuffer = 0;
    GLuint framebuffer = 0, color = 0;
    int ok = 0;
    g->GetIntegerv(0x8ca6, &draw);
    if (g->es3)
        g->GetIntegerv(0x8caa, &read);
    else
        read = draw;
    g->GetIntegerv(0x8ca7, &renderbuffer);
    g->GenFramebuffers(1, &framebuffer);
    g->GenRenderbuffers(1, &color);
    if (!framebuffer || !color)
        goto done;
    g->BindFramebuffer(0x8d40, framebuffer);
    g->BindRenderbuffer(0x8d41, color);
    g->RenderbufferStorage(0x8d41, g->es3 ? 0x8058 : 0x8056, 1, 1);
    g->FramebufferRenderbuffer(0x8d40, 0x8ce0, 0x8d41, color);
    if (g->CheckFramebufferStatus(0x8d40) != 0x8cd5)
        goto done;
    prepare(g, state, 1, 1);
    g->Uniform3f(g->graph_uniform, 0, 120, 1);
    if (g->es3) {
        const uint32_t quads[8] = {0, 0x00010001, 0x7fff7fff, UINT32_MAX,
                                   0, 0x00010001, UINT32_MAX, UINT32_MAX};
        g->BindBuffer(0x8a11, g->buffer);
        g->BufferSubData(0x8a11, 0, sizeof(quads), quads);
        g->BindBufferRange(0x8a11, 0, g->buffer, 0, 16384);
    } else {
        spo_vertex vertices[12];
        static const float corners[6][2] = {{0, 0}, {0, 1}, {1, 1}, {0, 0}, {1, 1}, {1, 0}};
        for (int i = 0; i < 12; i++) {
            float x = corners[i % 6][0], y = corners[i % 6][1];
            vertices[i] = (spo_vertex){x,
                                       y,
                                       i < 6 ? 127.5f / 128.0f : -1 - x * 120,
                                       i < 6 ? 63.5f / 64.0f : y * 20,
                                       {255, 255, 255, 255}};
        }
        g->BindBuffer(ARRAY_BUFFER, g->buffer);
        g->BufferSubData(ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        g->VertexAttribPointer(0, 2, 0x1406, 0, sizeof(spo_vertex),
                               (void *)offsetof(spo_vertex, x));
        g->VertexAttribPointer(1, 2, 0x1406, 0, sizeof(spo_vertex),
                               (void *)offsetof(spo_vertex, u));
        g->VertexAttribPointer(2, 4, 0x1401, 1, sizeof(spo_vertex),
                               (void *)offsetof(spo_vertex, color));
        for (int i = 0; i < 3; i++)
            g->EnableVertexAttribArray((GLuint)i);
        g->attribute_slot = 0;
    }
    g->DrawArrays(0x0004, 0, 12);
    g->Finish();
    ok = 1;
done:
    if (g->es3) {
        g->BindFramebuffer(0x8ca9, (GLuint)draw);
        g->BindFramebuffer(0x8ca8, (GLuint)read);
    } else
        g->BindFramebuffer(0x8d40, (GLuint)draw);
    g->BindRenderbuffer(0x8d41, (GLuint)renderbuffer);
    if (framebuffer)
        g->DeleteFramebuffers(1, &framebuffer);
    if (color)
        g->DeleteRenderbuffers(1, &color);
    return ok;
}
spo_result spo_backend_draw(const spo_mesh *mesh, uint32_t width, uint32_t height, uint32_t *draws) {
    spo_gl *g = spo->renderer;
    gl_state state;
    if (!g)
        return SPO_GL_UNAVAILABLE;
    if (mesh->count > SPO_QUADS * 6 || mesh->count % 6)
        return SPO_CAPACITY;
    capture(g, &state);
    prepare(g, &state, width, height);
    upload_mesh(g, mesh);
    upload_graph(g, mesh);
    g->DrawArrays(0x0004, 0, (GLsizei)mesh->count);
    *draws = 1;
    restore(g, &state);
    return SPO_OK;
}
