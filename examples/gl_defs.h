/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SPO_DEMO_GL_DEFS_H
#define SPO_DEMO_GL_DEFS_H
#include <stddef.h>
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
#define GL_VERSION 0x1f02
#define GL_RENDERER 0x1f01
#define GL_EXTENSIONS 0x1f03
#define GL_VERTEX_SHADER 0x8b31
#define GL_FRAGMENT_SHADER 0x8b30
#define GL_COMPILE_STATUS 0x8b81
#define GL_LINK_STATUS 0x8b82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88e4
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TRIANGLES 4
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_PACK_ALIGNMENT 0x0d05
#define GL_RGBA 0x1908
#define GL_TEXTURE_2D 0x0de1
#define GL_TEXTURE0 0x84c0
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812f
#endif
