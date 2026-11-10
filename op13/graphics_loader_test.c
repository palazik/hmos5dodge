// SPDX-License-Identifier: Apache-2.0
// Run under the donor loader configuration; no host LD_LIBRARY_PATH override.
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    assert(argc >= 2);
    setvbuf(stdout, NULL, _IONBF, 0);
    void *library = dlopen("libdrm.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        const char *error = dlerror();
        fprintf(stderr, "DRM load failure: %s\n", error);
        if (!strcmp(argv[1], "expect-stock-collision") && strstr(error, "__errno")) {
            puts("PASS baseline reproduces Android/Bionic DRM collision under native loader paths");
            return 0;
        }
        return 1;
    }
    int missing = !strcmp(argv[1], "expect-missing-bridges");
    assert(missing || !strcmp(argv[1], "expect-native"));
    void *symbol = dlsym(library, "drmGetVersion");
    assert(symbol);
    Dl_info info;
    assert(dladdr(symbol, &info) && info.dli_fname);
    printf("resolved DRM provider: %s\n", info.dli_fname);
    assert(!strcmp(info.dli_fname, "/hmos/display/libdrm.so"));
    void *(*getVersion)(int) = (void *(*)(int))symbol;
    int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    assert(fd >= 0);
    errno = 0;
    assert(getVersion(fd) == NULL && errno == ENOTTY);
    close(fd);
    dlclose(library);
    puts("PASS native DRM relocation, provider path and real ENOTTY handling; no display device used");
    for (int i = 2; i < argc; ++i) {
        printf("loading display dependency: %s\n", argv[i]);
        void *dependency = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL);
        if (!dependency) {
            const char *error = dlerror();
            fprintf(stderr, "display dependency failed: %s: %s\n", argv[i], error);
            // Donor musl's final strerror can reflect errno from its logger.
            // Check which root DSO failed; the fixture asserts it is absent.
            char expected[256];
            snprintf(expected, sizeof(expected), "Error loading shared library %s:", argv[i]);
            if (missing && strstr(error, expected)) {
                printf("PASS baseline reproduces missing HDF implementation: %s\n", argv[i]);
                continue;
            }
            return 1;
        }
        assert(!missing);
        printf("PASS display dependency relocation: %s\n", argv[i]);
    }
    if (!missing && argc > 2) {
        void *hdi = dlopen("libhdi.z.so", RTLD_NOW | RTLD_LOCAL);
        assert(hdi);
        void *(*load)(const char *, const char *) = dlsym(hdi, "LoadHdiImpl");
        assert(load);
        const char *descriptors[] = {"ohos.hdi.display.buffer.v1_0.IAllocator",
            "ohos.hdi.display.composer.v1_2.IDisplayComposer"};
        for (unsigned i = 0; i < sizeof(descriptors) / sizeof(descriptors[0]); ++i) {
            printf("constructing native HDI implementation: %s\n", descriptors[i]);
            assert(load(descriptors[i], "service"));
            printf("PASS native LoadHdiImpl returned an implementation: %s\n", descriptors[i]);
        }
    }
    return 0;
}
