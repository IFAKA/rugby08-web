/* Native stand-in for <GLES3/gl3.h>: macOS OpenGL 4.1 core, plus the WebGL extension enums
 * d3d9-webgl uses, plus a glShaderSource that turns "#version 300 es" into "#version 410 core"
 * (platform/native/gl_native.c). */
#pragma once
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifdef __cplusplus
extern "C" {
#endif
void rt_gl_shader_source(GLuint shader, GLsizei count, const GLchar *const *src, const GLint *len);
#ifdef __cplusplus
}
#endif
#define glShaderSource rt_gl_shader_source
