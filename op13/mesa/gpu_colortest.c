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
#include <stdbool.h>
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
FN(void *, eglGetProcAddress, (const char *));
FN(void *, eglCreateImageKHR, (EGLDisplay, EGLContext, EGLenum, EGLClientBuffer, const EGLint *));
FN(void, glEGLImageTargetTexture2DOES, (GLenum, void *));

#define LOAD(name) p_##name = sym(#name)

enum { SIZE = 512 };
static int failures;

static const uint8_t RED[4] = {255, 0, 0, 255}, BLUE[4] = {0, 0, 255, 255};

static void check(const char *name, const uint8_t want[4])
{
    static uint8_t got[SIZE * SIZE * 4];
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
    static uint8_t data[SIZE * SIZE * 4];
    for (int i = 0; pixel && i < SIZE * SIZE; ++i) memcpy(data + i * 4, pixel, 4);
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

/* render_service path: GPU rendering into / sampling from a HarmonyOS native
 * buffer imported with EGL_NATIVE_BUFFER_OHOS, checked by mapping the buffer
 * on the CPU (the composer scans these bytes out as DRM ABGR8888 = R,G,B,A). */
#define EGL_NATIVE_BUFFER_OHOS 0x34E1
typedef struct { int32_t width, height, format, usage, stride; } NbConfig;
static void native_buffer_tests(EGLDisplay dpy)
{
    void *nb = dlopen("/system/lib64/chipset-pub-sdk/libnative_buffer.so", RTLD_NOW);
    void *nw = dlopen("/system/lib64/chipset-pub-sdk/libnative_window.so", RTLD_NOW);
    if (!nb || !nw) { printf("SKIP native buffer: %s\n", dlerror()); return; }
    void *(*alloc)(const NbConfig *) = dlsym(nb, "OH_NativeBuffer_Alloc");
    int (*map)(void *, void **) = dlsym(nb, "OH_NativeBuffer_Map");
    int (*unmap)(void *) = dlsym(nb, "OH_NativeBuffer_Unmap");
    void (*getconfig)(void *, NbConfig *) = dlsym(nb, "OH_NativeBuffer_GetConfig");
    void *(*wrap)(void *) = dlsym(nw, "OH_NativeWindow_CreateNativeWindowBufferFromNativeBuffer");
    p_eglCreateImageKHR = p_eglGetProcAddress("eglCreateImageKHR");
    p_glEGLImageTargetTexture2DOES = p_eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!alloc || !map || !unmap || !wrap || !p_eglCreateImageKHR || !p_glEGLImageTargetTexture2DOES) {
        printf("SKIP native buffer: missing entry points\n"); return;
    }
    const NbConfig want = {SIZE, SIZE, 12 /* RGBA_8888 */, (1 << 0) | (1 << 1) | (1 << 3) | (1 << 8) | (1 << 9), 0};
    for (int pass = 0; pass < 2; ++pass) {
        void *buffer = alloc(&want);
        if (!buffer) { printf("FAIL OH_NativeBuffer_Alloc\n"); ++failures; return; }
        NbConfig got = want;
        if (getconfig) getconfig(buffer, &got);
        void *image = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_OHOS, wrap(buffer), NULL);
        if (!image) { printf("FAIL eglCreateImageKHR(EGL_NATIVE_BUFFER_OHOS) 0x%x\n", p_eglGetError()); ++failures; return; }
        GLuint tex;
        p_glGenTextures(1, &tex);
        p_glBindTexture(GL_TEXTURE_2D, tex);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        p_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
        uint8_t *bytes = NULL;
        if (pass == 0) {
            /* GPU writes red into the native buffer; check the raw bytes. */
            target(tex);
            p_glClearColor(1, 0, 0, 1); p_glClear(GL_COLOR_BUFFER_BIT); p_glFinish();
            if (map(buffer, (void **)&bytes) || !bytes) { printf("FAIL map\n"); ++failures; return; }
            const uint8_t *px = bytes + (SIZE / 2) * got.stride + (SIZE / 2) * 4;
            bool ok = px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255;
            if (!ok) ++failures;
            printf("%s %-40s want ff0000ff got %02x%02x%02x%02x (stride %d)\n", ok ? "PASS" : "FAIL",
                   "GPU clear red -> native RGBA_8888 bytes", px[0], px[1], px[2], px[3], got.stride);
        } else {
            /* CPU writes blue as R,G,B,A bytes; the GPU samples it. */
            if (map(buffer, (void **)&bytes) || !bytes) { printf("FAIL map\n"); ++failures; return; }
            for (int y = 0; y < SIZE; ++y)
                for (int x = 0; x < SIZE; ++x) memcpy(bytes + y * got.stride + x * 4, BLUE, 4);
            unmap(buffer);
            target(texture(GL_RGBA, NULL));
            draw(tex);
            check("sample native RGBA_8888 (blue) -> RGBA", BLUE);
        }
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    /* Use Mesa's own libraries, not the HarmonyOS GL wrapper: the wrapper's GL
     * entry points dispatch through per-thread hooks that only its own EGL
     * MakeCurrent installs (glGetString returned NULL and the test crashed). */
    egl_lib = dlopen("/vendor/lib64/chipsetsdk/libEGL_impl.so", RTLD_NOW | RTLD_GLOBAL);
    gl_lib = dlopen("/vendor/lib64/chipsetsdk/libGLESv2_impl.so", RTLD_NOW | RTLD_GLOBAL);
    if (!egl_lib || !gl_lib) { printf("FAIL cannot load Mesa libEGL_impl/libGLESv2_impl: %s\n", dlerror()); return 2; }
    LOAD(eglGetDisplay); LOAD(eglInitialize); LOAD(eglChooseConfig); LOAD(eglCreatePbufferSurface);
    LOAD(eglCreateContext); LOAD(eglMakeCurrent); LOAD(eglGetError); LOAD(glGetString); LOAD(glClearColor);
    LOAD(glClear); LOAD(glReadPixels); LOAD(glGenTextures); LOAD(glBindTexture); LOAD(glTexImage2D);
    LOAD(glTexParameteri); LOAD(glGenFramebuffers); LOAD(glBindFramebuffer); LOAD(glFramebufferTexture2D);
    LOAD(glCheckFramebufferStatus); LOAD(glCreateShader); LOAD(glShaderSource); LOAD(glCompileShader);
    LOAD(glCreateProgram); LOAD(glAttachShader); LOAD(glBindAttribLocation); LOAD(glLinkProgram); LOAD(glUseProgram);
    LOAD(glVertexAttribPointer); LOAD(glEnableVertexAttribArray); LOAD(glDrawArrays); LOAD(glViewport); LOAD(glFinish);
    LOAD(glGetError); LOAD(eglGetProcAddress);

    /* The HarmonyOS wrapper opens the OHOS platform explicitly. */
    EGLDisplay (*get_platform_display)(EGLenum, void *, const EGLAttrib *) =
        (EGLDisplay (*)(EGLenum, void *, const EGLAttrib *))p_eglGetProcAddress("eglGetPlatformDisplay");
    EGLDisplay dpy = get_platform_display ? get_platform_display(0x34E0 /* EGL_PLATFORM_OHOS_KHR */, EGL_DEFAULT_DISPLAY, NULL)
                                          : p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    printf("display: %s\n", get_platform_display ? "EGL_PLATFORM_OHOS_KHR" : "default");
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
    const char *renderer = (const char *)p_glGetString(GL_RENDERER);
    printf("EGL %d.%d renderer: %s | %s\n", major, minor, renderer ? renderer : "(null)",
           p_glGetString(GL_VERSION) ? (const char *)p_glGetString(GL_VERSION) : "(null)");
    if (!renderer) { printf("FAIL no current GL context\n"); return 2; }
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

    native_buffer_tests(dpy);

    printf("RESULT %s (%d failing)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
