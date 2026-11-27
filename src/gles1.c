/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLenum, GLuint;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat;
#define PROCS1(X)                                                                                  \
    X(const GLubyte *, GetString, (GLenum))                                                        \
    X(void, GetIntegerv, (GLenum, GLint *))                                                        \
    X(void, GetFloatv, (GLenum, GLfloat *))                                                        \
    X(void, GetBooleanv, (GLenum, GLboolean *))                                                    \
    X(void, GetPointerv, (GLenum, void **))                                                        \
    X(void, GetTexEnviv, (GLenum, GLenum, GLint *))                                                \
    X(GLboolean, IsEnabled, (GLenum))                                                              \
    X(void, Enable, (GLenum))                                                                      \
    X(void, Disable, (GLenum))                                                                     \
    X(void, EnableClientState, (GLenum))                                                           \
    X(void, DisableClientState, (GLenum))                                                          \
    X(void, ActiveTexture, (GLenum))                                                               \
    X(void, ClientActiveTexture, (GLenum))                                                         \
    X(void, BindTexture, (GLenum, GLuint))                                                         \
    X(void, GenTextures, (GLsizei, GLuint *))                                                      \
    X(void, DeleteTextures, (GLsizei, const GLuint *))                                             \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                                \
    X(void, TexImage2D,                                                                            \
      (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))               \
    X(void, TexSubImage2D,                                                                         \
      (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *))               \
    X(void, TexEnvi, (GLenum, GLenum, GLint))                                                      \
    X(void, BindBuffer, (GLenum, GLuint))                                                          \
    X(void, BlendFunc, (GLenum, GLenum))                                                           \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                            \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                               \
    X(void, ShadeModel, (GLenum))                                                                  \
    X(void, MatrixMode, (GLenum))                                                                  \
    X(void, LoadIdentity, (void))                                                                  \
    X(void, LoadMatrixf, (const GLfloat *))                                                        \
    X(void, Orthof, (GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat))                        \
    X(void, Color4f, (GLfloat, GLfloat, GLfloat, GLfloat))                                         \
    X(void, VertexPointer, (GLint, GLenum, GLsizei, const void *))                                 \
    X(void, ColorPointer, (GLint, GLenum, GLsizei, const void *))                                  \
    X(void, TexCoordPointer, (GLint, GLenum, GLsizei, const void *))                               \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))
#define DECL(ret, name, args) ret(*name) args;
enum { MAX_UNITS = 8, MAX_ENABLES = 20, GRAPH_COLUMNS = 120 };
struct spo_gl1 {
    PROCS1(DECL)
    GLuint texture;
    uint32_t font;
    GLint units;
    uint32_t enable_count;
    GLenum enables[MAX_ENABLES];
    spo_vertex out[(SPO_QUADS + GRAPH_COLUMNS) * 6];
};
#undef DECL
typedef struct spo_gl1 spo_gl1;
enum {
    TEXTURE_2D = 0x0de1,
    TEXTURE0 = 0x84c0,
    ARRAY_BUFFER = 0x8892,
    TEXTURE_ENV = 0x2300,
    MODELVIEW = 0x1700,
    PROJECTION = 0x1701,
    TEXTURE = 0x1702,
    ENV_PARAMS = 11
};
/* Fixed-function state that alters a textured, vertex-coloured triangle draw. */
static const GLenum base_enables[] = {0x0be2, 0x0b71, 0x0b44, 0x0c11, 0x0b90, 0x0bc0,
                                      0x0b60, 0x0b50, 0x0bf2, 0x8037, 0x809e, 0x80a0};
static const GLenum env_names[ENV_PARAMS] = {0x2200, 0x8571, 0x8572, 0x8580, 0x8590, 0x8588,
                                             0x8598, 0x8589, 0x8599, 0x8573, 0x0d1c};
/* COMBINE: colour from the vertex, alpha = atlas alpha * vertex alpha, scale 1. */
static const GLint env_values[ENV_PARAMS] = {0x8570, 0x1e01, 0x2100, 0x8577, 0x0300, 0x1702,
                                             0x0302, 0x8577, 0x0302, 1,      1};
typedef struct {
    GLint enabled, size, type, stride, buffer;
    void *pointer;
} array_state;
typedef struct {
    GLint active, client_active, texture, buffer, matrix_mode, shade, src, dst;
    GLint viewport[4], env[ENV_PARAMS];
    GLboolean color[4], enabled[MAX_ENABLES], unit_texture[MAX_UNITS];
    GLfloat matrices[3][16], current[4];
    array_state arrays[3];
} gl1_state;
static const GLenum array_enums[3][6] = {
    {0x8074, 0x807a, 0x807b, 0x807c, 0x8896, 0x808e},
    {0x8076, 0x8081, 0x8082, 0x8083, 0x8898, 0x8090},
    {0x8078, 0x8088, 0x8089, 0x808a, 0x889a, 0x8092},
};
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
static int load(spo_get_proc_fn get_proc, void *user, void *out, size_t size, const char *name) {
    void *p = get_proc(name, user);
    if (!p || size != sizeof(p))
        return 0;
    memcpy(out, &p, size);
    return 1;
}
static void capture(spo_gl1 *g, gl1_state *s) {
    static const GLenum matrices[3] = {0x0ba6, 0x0ba7, 0x0ba8};
    g->GetIntegerv(0x84e0, &s->active);
    g->GetIntegerv(0x84e1, &s->client_active);
    for (GLint unit = 1; unit < g->units; unit++) {
        g->ActiveTexture(TEXTURE0 + (GLenum)unit);
        s->unit_texture[unit] = g->IsEnabled(TEXTURE_2D);
    }
    g->ActiveTexture(TEXTURE0);
    s->unit_texture[0] = g->IsEnabled(TEXTURE_2D);
    g->GetIntegerv(0x8069, &s->texture);
    for (int i = 0; i < ENV_PARAMS; i++)
        g->GetTexEnviv(TEXTURE_ENV, env_names[i], &s->env[i]);
    g->GetIntegerv(0x0ba0, &s->matrix_mode);
    for (int i = 0; i < 3; i++)
        g->GetFloatv(matrices[i], s->matrices[i]);
    g->ClientActiveTexture(TEXTURE0);
    for (int i = 0; i < 3; i++) {
        array_state *a = &s->arrays[i];
        a->enabled = g->IsEnabled(array_enums[i][0]);
        g->GetIntegerv(array_enums[i][1], &a->size);
        g->GetIntegerv(array_enums[i][2], &a->type);
        g->GetIntegerv(array_enums[i][3], &a->stride);
        g->GetIntegerv(array_enums[i][4], &a->buffer);
        g->GetPointerv(array_enums[i][5], &a->pointer);
    }
    g->GetIntegerv(0x8894, &s->buffer);
    g->GetIntegerv(0x0b54, &s->shade);
    g->GetIntegerv(0x0be1, &s->src);
    g->GetIntegerv(0x0be0, &s->dst);
    g->GetIntegerv(0x0ba2, s->viewport);
    g->GetBooleanv(0x0c23, s->color);
    g->GetFloatv(0x0b00, s->current);
    for (uint32_t i = 0; i < g->enable_count; i++)
        s->enabled[i] = g->IsEnabled(g->enables[i]);
}
static void restore(spo_gl1 *g, const gl1_state *s) {
    static const GLenum modes[3] = {MODELVIEW, PROJECTION, TEXTURE};
    for (uint32_t i = 0; i < g->enable_count; i++)
        (s->enabled[i] ? g->Enable : g->Disable)(g->enables[i]);
    g->Color4f(s->current[0], s->current[1], s->current[2], s->current[3]);
    g->ColorMask(s->color[0], s->color[1], s->color[2], s->color[3]);
    g->Viewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    g->BlendFunc((GLenum)s->src, (GLenum)s->dst);
    g->ShadeModel((GLenum)s->shade);
    for (int i = 0; i < 3; i++) {
        const array_state *a = &s->arrays[i];
        g->BindBuffer(ARRAY_BUFFER, (GLuint)a->buffer);
        if (i == 0)
            g->VertexPointer(a->size, (GLenum)a->type, a->stride, a->pointer);
        else if (i == 1)
            g->ColorPointer(a->size, (GLenum)a->type, a->stride, a->pointer);
        else
            g->TexCoordPointer(a->size, (GLenum)a->type, a->stride, a->pointer);
        (a->enabled ? g->EnableClientState : g->DisableClientState)(array_enums[i][0]);
    }
    g->BindBuffer(ARRAY_BUFFER, (GLuint)s->buffer);
    g->ClientActiveTexture((GLenum)s->client_active);
    for (int i = 0; i < 3; i++) {
        g->MatrixMode(modes[i]);
        g->LoadMatrixf(s->matrices[i]);
    }
    g->MatrixMode((GLenum)s->matrix_mode);
    for (int i = 0; i < ENV_PARAMS; i++)
        g->TexEnvi(TEXTURE_ENV, env_names[i], s->env[i]);
    g->BindTexture(TEXTURE_2D, (GLuint)s->texture);
    (s->unit_texture[0] ? g->Enable : g->Disable)(TEXTURE_2D);
    for (GLint unit = 1; unit < g->units; unit++) {
        g->ActiveTexture(TEXTURE0 + (GLenum)unit);
        (s->unit_texture[unit] ? g->Enable : g->Disable)(TEXTURE_2D);
    }
    g->ActiveTexture((GLenum)s->active);
}
static void upload_font(spo_gl1 *g, uint32_t size) {
    uint8_t band[SPO_ATLAS * 16 * 4];
    const spo_font_info *f = spo_font_get(size);
    for (uint32_t y = 0; y < (uint32_t)f->grid_h; y += 16) {
        uint32_t rows = (uint32_t)f->grid_h - y < 16 ? (uint32_t)f->grid_h - y : 16;
        spo_font_band(size, y, rows, band);
        g->TexSubImage2D(TEXTURE_2D, 0, 0, (GLint)y, SPO_ATLAS, (GLsizei)rows, 0x1908, 0x1401,
                         band);
    }
    for (uint32_t id = 0; id < SPO_GLYPH_COUNT; id++) {
        const spo_glyph *glyph = spo_glyph_get(size, (int)id);
        uint8_t pixels[96 * 32 * 4];
        spo_glyph_raster(size, (int)id, pixels);
        g->TexSubImage2D(TEXTURE_2D, 0, glyph->x, glyph->y, glyph->w, glyph->h, 0x1908, 0x1401,
                         pixels);
    }
    g->font = size < 1 ? 1 : size > 4 ? 4 : size;
}
static spo_gl1 *gl1_create(spo_get_proc_fn get_proc, void *user, uint32_t font,
                           char *renderer, size_t renderer_size) {
    static const uint8_t solid[16] = {255, 255, 255, 255, 255, 255, 255, 255,
                                      255, 255, 255, 255, 255, 255, 255, 255};
    spo_gl1 *g = calloc(1, sizeof(*g));
    const char *version, *extensions;
    int major = 0, minor = 0;
    GLint texture = 0, active = 0, clip_planes = 0;
    if (!g)
        return NULL;
#define LOAD(ret, name, args)                                                                      \
    if (!load(get_proc, user, &g->name, sizeof(g->name), "gl" #name))                              \
        goto unavailable;
    PROCS1(LOAD)
#undef LOAD
    version = (const char *)g->GetString(0x1f02);
    if (!version || !strstr(version, "OpenGL ES-C") ||
        sscanf(strstr(version, "OpenGL ES-C") + 13, "%d.%d", &major, &minor) != 2 || major != 1 ||
        minor < 1)
        goto unavailable;
    snprintf(renderer, renderer_size, "GLES %d.%d", major, minor);
    extensions = (const char *)g->GetString(0x1f03);
    g->GetIntegerv(0x84e2, &g->units);
    g->units = g->units < 1 ? 1 : g->units > MAX_UNITS ? MAX_UNITS : g->units;
    g->GetIntegerv(0x0d32, &clip_planes);
    clip_planes = clip_planes < 0 ? 0 : clip_planes > 6 ? 6 : clip_planes;
    memcpy(g->enables, base_enables, sizeof(base_enables));
    g->enable_count = sizeof(base_enables) / sizeof(base_enables[0]);
    for (GLint i = 0; i < clip_planes; i++)
        g->enables[g->enable_count++] = 0x3000 + (GLenum)i;
    if (extension(extensions, "GL_OES_texture_cube_map")) {
        g->enables[g->enable_count++] = 0x8513;
        g->enables[g->enable_count++] = 0x8d60;
    }
    if (extension(extensions, "GL_OES_matrix_palette"))
        g->enables[g->enable_count++] = 0x8840;
    g->GetIntegerv(0x84e0, &active);
    g->ActiveTexture(TEXTURE0);
    g->GetIntegerv(0x8069, &texture);
    g->GenTextures(1, &g->texture);
    if (!g->texture)
        goto unavailable_bound;
    g->BindTexture(TEXTURE_2D, g->texture);
    g->TexParameteri(TEXTURE_2D, 0x2801, 0x2600);
    g->TexParameteri(TEXTURE_2D, 0x2800, 0x2600);
    g->TexParameteri(TEXTURE_2D, 0x2802, 0x812f);
    g->TexParameteri(TEXTURE_2D, 0x2803, 0x812f);
    g->TexImage2D(TEXTURE_2D, 0, 0x1908, SPO_ATLAS, SPO_ATLAS, 0, 0x1908, 0x1401, NULL);
    g->TexSubImage2D(TEXTURE_2D, 0, SPO_ATLAS_SOLID_X - 1, SPO_ATLAS_SOLID_Y - 1, 2, 2, 0x1908,
                     0x1401, solid);
    upload_font(g, font);
    g->BindTexture(TEXTURE_2D, (GLuint)texture);
    g->ActiveTexture((GLenum)active);
    return g;
unavailable_bound:
    g->BindTexture(TEXTURE_2D, (GLuint)texture);
    g->ActiveTexture((GLenum)active);
unavailable:
    free(g);
    return NULL;
}
static void gl1_destroy(spo_gl1 *g, int lost) {
    if (!g)
        return;
    if (!lost && g->texture)
        g->DeleteTextures(1, &g->texture);
    free(g);
}
static void graph_bars(spo_gl1 *g, const spo_mesh *mesh, const spo_vertex *quad, uint32_t *n) {
    const spo_vertex *a = quad, *b = quad + 2;
    float width = b->x - a->x, height = b->y - a->y;
    const float u = (SPO_ATLAS_SOLID_X - 0.5f) / SPO_ATLAS, v = (SPO_ATLAS_SOLID_Y - 0.5f) / SPO_ATLAS;
    uint32_t count = mesh->graph_count > GRAPH_COLUMNS ? GRAPH_COLUMNS : mesh->graph_count;
    if (!mesh->graph_scale)
        return;
    for (uint32_t column = GRAPH_COLUMNS - count; column < GRAPH_COLUMNS; column++) {
        uint32_t slot = (column + mesh->graph_head) % GRAPH_COLUMNS;
        const uint8_t *d = mesh->graph_data[slot];
        float fraction = (float)d[0] / (float)mesh->graph_scale;
        if (!d[0])
            continue;
        fraction = fraction > 1 ? 1 : fraction;
        float x0 = a->x + width * (float)column / GRAPH_COLUMNS;
        float x1 = a->x + width * (float)(column + 1) / GRAPH_COLUMNS;
        float y0 = b->y - height * fraction, y1 = b->y;
        const float corners[6][2] = {{x0, y0}, {x0, y1}, {x1, y1}, {x0, y0}, {x1, y1}, {x1, y0}};
        for (int k = 0; k < 6; k++)
            g->out[(*n)++] =
                (spo_vertex){corners[k][0], corners[k][1], u, v, {d[1], d[2], d[3], a->color[3]}};
    }
}
static spo_result gl1_draw(spo_gl1 *g, const spo_mesh *mesh, uint32_t width, uint32_t height,
                           uint32_t *draws) {
    static const GLenum modes[3] = {MODELVIEW, PROJECTION, TEXTURE};
    gl1_state state;
    uint32_t n = 0;
    if (mesh->count > SPO_QUADS * 6 || mesh->count % 6)
        return SPO_CAPACITY;
    for (uint32_t i = 0; i < mesh->count; i += 6) {
        if (mesh->vertices[i].u < 0)
            graph_bars(g, mesh, mesh->vertices + i, &n);
        else {
            memcpy(g->out + n, mesh->vertices + i, 6 * sizeof(spo_vertex));
            n += 6;
        }
    }
    capture(g, &state);
    for (uint32_t i = 0; i < g->enable_count; i++)
        (i == 0 ? g->Enable : g->Disable)(g->enables[i]);
    for (GLint unit = 1; unit < g->units; unit++) {
        if (!state.unit_texture[unit])
            continue;
        g->ActiveTexture(TEXTURE0 + (GLenum)unit);
        g->Disable(TEXTURE_2D);
    }
    g->ActiveTexture(TEXTURE0);
    g->Enable(TEXTURE_2D);
    g->BindTexture(TEXTURE_2D, g->texture);
    if (mesh->font != g->font && mesh->font >= 1 && mesh->font <= 4)
        upload_font(g, mesh->font);
    for (int i = 0; i < ENV_PARAMS; i++)
        g->TexEnvi(TEXTURE_ENV, env_names[i], env_values[i]);
    g->BlendFunc(0x0302, 0x0303);
    g->ShadeModel(0x1d01);
    g->ColorMask(1, 1, 1, 1);
    g->Viewport(0, 0, (GLsizei)width, (GLsizei)height);
    for (int i = 0; i < 3; i++) {
        g->MatrixMode(modes[i]);
        g->LoadIdentity();
    }
    g->MatrixMode(PROJECTION);
    g->Orthof(0, (GLfloat)width, (GLfloat)height, 0, -1, 1);
    g->BindBuffer(ARRAY_BUFFER, 0);
    g->VertexPointer(2, 0x1406, sizeof(spo_vertex), &g->out[0].x);
    g->TexCoordPointer(2, 0x1406, sizeof(spo_vertex), &g->out[0].u);
    g->ColorPointer(4, 0x1401, sizeof(spo_vertex), g->out[0].color);
    for (int i = 0; i < 3; i++)
        g->EnableClientState(array_enums[i][0]);
    if (n)
        g->DrawArrays(0x0004, 0, (GLsizei)n);
    *draws = n ? 1 : 0;
    restore(g, &state);
    return SPO_OK;
}
spo_result spo_gl1_backend_init(void) {
    spo_gl1 *g = gl1_create(spo->config.get_proc_address, spo->config.gl_user,
                            spo->overlay.font_scale, spo->renderer_name,
                            sizeof(spo->renderer_name));
    if (!g)
        return SPO_GL_UNAVAILABLE;
    spo->renderer = g;
    spo->renderer_gles1 = 1;
    return SPO_OK;
}
void spo_gl1_backend_shutdown(void) {
    gl1_destroy(spo->renderer, spo->lost);
    spo->renderer = NULL;
    spo->renderer_gles1 = 0;
}
spo_result spo_gl1_backend_draw(const spo_mesh *mesh, uint32_t width, uint32_t height,
                                uint32_t *draws) {
    return gl1_draw(spo->renderer, mesh, width, height, draws);
}
