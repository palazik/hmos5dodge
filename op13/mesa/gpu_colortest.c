// SPDX-License-Identifier: Apache-2.0
/*
 * op13-gpu-colortest: check RGBA/BGRA channel order on the OP13 Adreno 830.
 *
 * Revision 28/29 phones show blue and pink swapped in the UI and in the boot
 * video, which render_service composites with GLES (freedreno/KGSL) into one
 * RGBA_8888 client layer.  Skia keeps decoded images and offscreen layers in
 * BGRA (kN32) textures when GL_EXT_texture_format_BGRA8888 is present.  This
 * test renders and samples known colors through RGBA and BGRA textures and
 * reads the results back, so a wrong swap in the GPU path is visible without
 * looking at the screen.  Run as root over ADB; it changes no state.
 *
 * Output: one line per case, "PASS"/"FAIL", expected and actual RGBA bytes.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

static void *egl_lib, *gl_lib;
static void *sym(const char *name)
{
    void *f = gl_lib ? dlsym(gl_lib, name) : NULL;
    if (!f && egl_lib) f = dlsym(egl_lib, name);
    if (!f) { fprintf(stderr, "missing symbol %s\n", name); }
    return f;
}

#define FN(ret, name, args) static ret(*p_##name) args
FN(EGLDisplay, eglGetDisplay, (EGLNativeDisplayType));
FN(EGLBoolean, eglInitialize, (EGLDisplay, EGLint *, EGLint *));
FN(EGLBoolean, eglChooseConfig, (EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *));
FN(EGLSurface, eglCreatePbufferSurface, (EGLDisplay, EGLConfig, const EGLint *));
FN(EGLContext, eglCreateContext, (EGLDisplay, EGLConfig, EGLContext, const EGLint *));
FN(EGLBoolean, eglMakeCurrent, (EGLDisplay, EGLSurface, EGLSurface, EGLContext));
FN(EGLint, eglGetError, (void));
FN(const GLubyte *, glGetString, (GLenum));
FN(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat));
FN(void, glClear, (GLbitfield));
FN(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));
FN(void, glGenTextures, (GLsizei, GLuint *));
FN(void, glBindTexture, (GLenum, GLuint));
FN(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *));
FN(void, glTexParameteri, (GLenum, GLenum, GLint));
FN(void, glGenFramebuffers, (GLsizei, GLuint *));
FN(void, glBindFramebuffer, (GLenum, GLuint));
FN(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint));
FN(GLenum, glCheckFramebufferStatus, (GLenum));
FN(GLuint, glCreateShader, (GLenum));
FN(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *));
FN(void, glCompileShader, (GLuint));
FN(GLuint, glCreateProgram, (void));
FN(void, glAttachShader, (GLuint, GLuint));
FN(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *));
FN(void, glLinkProgram, (GLuint));
FN(void, glUseProgram, (GLuint));
FN(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *));
FN(void, glEnableVertexAttribArray, (GLuint));
FN(void, glDrawArrays, (GLenum, GLint, GLsizei));
FN(void, glViewport, (GLint, GLint, GLsizei, GLsizei));
FN(void, glFinish, (void));
FN(GLenum, glGetError, (void));

#define LOAD(name) p_##name = sym(#name)

enum { SIZE = 8 };
static int failures;

static const uint8_t RED[4] = {255, 0, 0, 255}, BLUE[4] = {0, 0, 255, 255};

static void check(const char *name, const uint8_t want[4])
{
    uint8_t got[SIZE * SIZE * 4];
    memset(got, 0x5a, sizeof(got));
    p_glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, got);
    const uint8_t *px = got + (SIZE / 2 * SIZE + SIZE / 2) * 4;
    int ok = abs(px[0] - want[0]) < 4 && abs(px[1] - want[1]) < 4 && abs(px[2] - want[2]) < 4 && abs(px[3] - want[3]) < 4;
    if (!ok) ++failures;
    printf("%s %-40s want %02x%02x%02x%02x got %02x%02x%02x%02x gl_error=0x%x\n", ok ? "PASS" : "FAIL", name,
           want[0], want[1], want[2], want[3], px[0], px[1], px[2], px[3], p_glGetError());
}

static GLuint texture(GLenum format, const uint8_t *pixel)
{
    uint8_t data[SIZE * SIZE * 4];
    for (int i = 0; i < SIZE * SIZE; ++i) memcpy(data + i * 4, pixel, 4);
    GLuint t;
    p_glGenTextures(1, &t);
    p_glBindTexture(GL_TEXTURE_2D, t);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    p_glTexImage2D(GL_TEXTURE_2D, 0, format, SIZE, SIZE, 0, format, GL_UNSIGNED_BYTE, pixel ? data : NULL);
    return t;
}

static GLuint target(GLuint tex)
{
    GLuint fbo;
    p_glGenFramebuffers(1, &fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum status = p_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) printf("note: framebuffer status 0x%x\n", status);
    p_glViewport(0, 0, SIZE, SIZE);
    return fbo;
}

static void draw(GLuint tex)
{
    static GLuint program;
    if (!program) {
        const char *vs = "attribute vec2 p; varying vec2 uv; void main(){ uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }";
        const char *fs = "precision mediump float; varying vec2 uv; uniform sampler2D s; void main(){ gl_FragColor = texture2D(s, uv); }";
        GLuint v = p_glCreateShader(GL_VERTEX_SHADER), f = p_glCreateShader(GL_FRAGMENT_SHADER);
        p_glShaderSource(v, 1, &vs, NULL); p_glCompileShader(v);
        p_glShaderSource(f, 1, &fs, NULL); p_glCompileShader(f);
        program = p_glCreateProgram();
        p_glAttachShader(program, v); p_glAttachShader(program, f);
        p_glBindAttribLocation(program, 0, "p");
        p_glLinkProgram(program);
    }
    static const GLfloat quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    p_glUseProgram(program);
    p_glBindTexture(GL_TEXTURE_2D, tex);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    p_glEnableVertexAttribArray(0);
    p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    p_glFinish();
}

int main(void)
{
    egl_lib = dlopen("libEGL.so", RTLD_NOW);
    gl_lib = dlopen("libGLESv2.so", RTLD_NOW);
    if (!egl_lib || !gl_lib) { printf("FAIL cannot load libEGL/libGLESv2: %s\n", dlerror()); return 2; }
    LOAD(eglGetDisplay); LOAD(eglInitialize); LOAD(eglChooseConfig); LOAD(eglCreatePbufferSurface);
    LOAD(eglCreateContext); LOAD(eglMakeCurrent); LOAD(eglGetError); LOAD(glGetString); LOAD(glClearColor);
    LOAD(glClear); LOAD(glReadPixels); LOAD(glGenTextures); LOAD(glBindTexture); LOAD(glTexImage2D);
    LOAD(glTexParameteri); LOAD(glGenFramebuffers); LOAD(glBindFramebuffer); LOAD(glFramebufferTexture2D);
    LOAD(glCheckFramebufferStatus); LOAD(glCreateShader); LOAD(glShaderSource); LOAD(glCompileShader);
    LOAD(glCreateProgram); LOAD(glAttachShader); LOAD(glBindAttribLocation); LOAD(glLinkProgram); LOAD(glUseProgram);
    LOAD(glVertexAttribPointer); LOAD(glEnableVertexAttribArray); LOAD(glDrawArrays); LOAD(glViewport); LOAD(glFinish);
    LOAD(glGetError);

    EGLDisplay dpy = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0, n = 0;
    if (!p_eglInitialize(dpy, &major, &minor)) { printf("FAIL eglInitialize 0x%x\n", p_eglGetError()); return 2; }
    const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    if (!p_eglChooseConfig(dpy, attrs, &config, 1, &n) || n < 1) { printf("FAIL eglChooseConfig\n"); return 2; }
    const EGLint pb[] = {EGL_WIDTH, SIZE, EGL_HEIGHT, SIZE, EGL_NONE};
    const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLSurface surface = p_eglCreatePbufferSurface(dpy, config, pb);
    EGLContext context = p_eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attrs);
    if (!p_eglMakeCurrent(dpy, surface, surface, context)) { printf("FAIL eglMakeCurrent 0x%x\n", p_eglGetError()); return 2; }
    printf("EGL %d.%d renderer: %s | %s\n", major, minor, p_glGetString(GL_RENDERER), p_glGetString(GL_VERSION));
    const char *ext = (const char *)p_glGetString(GL_EXTENSIONS);
    printf("GL_EXT_texture_format_BGRA8888: %s\n", ext && strstr(ext, "GL_EXT_texture_format_BGRA8888") ? "yes" : "no");

    const uint8_t red_bgra[4] = {0, 0, 255, 255};  /* red stored as B,G,R,A */
    const uint8_t blue_bgra[4] = {255, 0, 0, 255};

    p_glClearColor(1, 0, 0, 1); p_glClear(GL_COLOR_BUFFER_BIT); p_glFinish();
    check("clear red -> pbuffer", RED);

    target(texture(GL_RGBA, NULL));
    p_glClearColor(1, 0, 0, 1); p_glClear(GL_COLOR_BUFFER_BIT); p_glFinish();
    check("clear red -> RGBA texture", RED);

    target(texture(GL_BGRA_EXT, NULL));
    p_glClearColor(1, 0, 0, 1); p_glClear(GL_COLOR_BUFFER_BIT); p_glFinish();
    check("clear red -> BGRA texture", RED);

    GLuint rgba_src = texture(GL_RGBA, RED), bgra_src = texture(GL_BGRA_EXT, red_bgra), bgra_blue = texture(GL_BGRA_EXT, blue_bgra);
    target(texture(GL_RGBA, NULL));
    draw(rgba_src);
    check("sample RGBA upload -> RGBA target", RED);
    draw(bgra_src);
    check("sample BGRA upload (red) -> RGBA target", RED);
    draw(bgra_blue);
    check("sample BGRA upload (blue) -> RGBA target", BLUE);

    GLuint bgra_rt = texture(GL_BGRA_EXT, NULL);
    target(bgra_rt);
    p_glClearColor(0, 0, 1, 1); p_glClear(GL_COLOR_BUFFER_BIT); p_glFinish();
    target(texture(GL_RGBA, NULL));
    draw(bgra_rt);
    check("render blue into BGRA, sample -> RGBA", BLUE);

    printf("RESULT %s (%d failing)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
