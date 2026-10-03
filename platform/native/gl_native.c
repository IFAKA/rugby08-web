/* gl_native.c -- desktop GL glue for third_party/d3d9-webgl (see platform/native/glshim/).
 *
 * d3d9-webgl's shaders are GLSL ES 1.00 (WebGL2 accepts them); macOS core profiles take GLSL
 * 1.50+ only. rt_gl_shader_source rewrites them to GLSL 4.10 core: a #version line, attribute/
 * varying -> in/out by stage, gl_FragColor -> a declared output, texture2D/textureCube -> texture.
 * GLSL ES 3.00 sources only get their version line swapped. */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *p; size_t n, cap; } buf_t;

static void put(buf_t *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

static int ident(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* Copy src replacing whole-word identifiers per the table. */
static void rewrite(buf_t *out, const char *src, const char *const (*map)[2], int nmap) {
    const char *s = src;
    while (*s) {
        if (ident(*s) && (s == src || !ident(s[-1]))) {
            const char *e = s;
            while (ident(*e)) e++;
            int k;
            for (k = 0; k < nmap; k++)
                if (strlen(map[k][0]) == (size_t)(e - s) && !strncmp(s, map[k][0], (size_t)(e - s))) break;
            if (k < nmap) put(out, map[k][1], strlen(map[k][1]));
            else put(out, s, (size_t)(e - s));
            s = e;
        } else {
            put(out, s++, 1);
        }
    }
}

static char *translate(GLenum stage, const char *src) {
    buf_t out = { 0 };
    const char *v = strstr(src, "#version 300 es");
    if (v) {                                   /* GLSL ES 3.00: version line only */
        put(&out, src, (size_t)(v - src));
        put(&out, "#version 410 core", 17);
        put(&out, v + 15, strlen(v + 15));
        return out.p;
    }
    static const char *const vs[][2] = { { "attribute", "in" }, { "varying", "out" }, { "texture2D", "texture" },
                                         { "textureCube", "texture" } };
    static const char *const fs[][2] = { { "varying", "in" }, { "gl_FragColor", "rt_FragColor" },
                                         { "texture2D", "texture" }, { "textureCube", "texture" } };
    put(&out, "#version 410 core\n", 18);
    if (stage == GL_FRAGMENT_SHADER) put(&out, "out vec4 rt_FragColor;\n", 23);
    if (stage == GL_FRAGMENT_SHADER) rewrite(&out, src, fs, 4);
    else rewrite(&out, src, vs, 4);
    return out.p;
}

void rt_gl_shader_source(GLuint shader, GLsizei count, const GLchar *const *src, const GLint *len) {
    GLint stage = 0;
    glGetShaderiv(shader, GL_SHADER_TYPE, &stage);
    buf_t all = { 0 };                         /* one string: a #version line must come first */
    for (GLsizei i = 0; i < count; i++) put(&all, src[i], len && len[i] >= 0 ? (size_t)len[i] : strlen(src[i]));
    char *t = translate((GLenum)stage, all.p ? all.p : "");
    const GLchar *one = t;
    glShaderSource(shader, 1, &one, NULL);
    free(t);
    free(all.p);
}
