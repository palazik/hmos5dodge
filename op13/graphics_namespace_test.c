/* SPDX-License-Identifier: Apache-2.0
 * Static host fixture: exercise the production graphics bind and root switch,
 * then exec the real native loader test through QEMU inside that guest root.
 */
#define main bootstrap_entry
#include "dsu_bootstrap.c"
#undef main
#include <assert.h>

int main(int argc, char **argv)
{
    assert(argc >= 4);
    struct statfs fs;
    assert(!statfs("/", &fs) && (unsigned long)fs.f_type == 0x01021994UL);
    assert(!access("/test/graphics-namespace-fixture", F_OK));
    bool nested = !strncmp(argv[1], "service-", 8);
    bool expose = nested || !strcmp(argv[1], "expose");
    assert(expose || !strcmp(argv[1], "omit"));
    root = "/guest";
    directory(root, 0755);
    assert(!mount("tmpfs", root, "tmpfs", MS_NOSUID, "mode=0755"));
    // Recursive binds are fixture-only: its synthetic /system and /vendor
    // consist of several read-only library/config mounts, unlike real EROFS.
    const char *paths[] = {"/system", "/vendor", "/proc", "/dev", "/test"};
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        root_directory(paths[i], 0755);
        char target[512];
        rootpath(target, sizeof(target), paths[i]);
        assert(!mount(paths[i], target, NULL, MS_BIND | MS_REC, NULL));
    }
    root_link("system/lib", "/lib");
    root_link("system/lib64", "/lib64");
    root_link("system/etc", "/etc");
    assert(access("/guest/hmos/display", F_OK) < 0 && errno == ENOENT);
    if (expose) expose_graphics_runtime();
    pivot_native_root();
    // Mirror the native sandbox's namespace restoration after the handoff.
    int original = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
    assert(original >= 0 && !unshare(CLONE_NEWNS) && !setns(original, CLONE_NEWNS));
    close(original);
    if (nested) {
        bool service_expose = !strcmp(argv[1], "service-expose");
        assert(service_expose || !strcmp(argv[1], "service-omit"));
        root = "/native-service";
        root_directory("", 0755);
        assert(!mount("tmpfs", root, "tmpfs", MS_NOSUID, "mode=0755"));
        for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
            root_directory(paths[i], 0755);
            char target[512];
            rootpath(target, sizeof(target), paths[i]);
            assert(!mount(paths[i], target, NULL, MS_BIND | MS_REC, NULL));
        }
        root_link("system/lib", "/lib");
        root_link("system/lib64", "/lib64");
        root_link("system/etc", "/etc");
        if (service_expose) {
            root_directory("/hmos", 0755);
            root_directory("/hmos/display", 0755);
            char target[512];
            rootpath(target, sizeof(target), "/hmos/display");
            // Exact flags in the appended native sandbox JSON entry; this
            // source is the read-only bind installed by production above.
            assert(!mount("/hmos/display", target, NULL, MS_BIND | MS_REC | MS_PRIVATE, NULL));
        }
        pivot_native_root();
        expose = service_expose;
    }
    if (expose) {
        struct statvfs flags;
        assert(!statvfs("/hmos/display", &flags) && (flags.f_flag & ST_RDONLY));
        int writable = open("/hmos/display/forbidden-test-write", O_WRONLY | O_CREAT | O_EXCL, 0600);
        assert(writable < 0 && errno == EROFS);
        assert(access("/hmos/modules", F_OK) < 0 && errno == ENOENT);
    } else {
        assert(access("/hmos/display", F_OK) < 0 && errno == ENOENT);
    }
    // argv[2] is the native program; argv[3] its expectation. No phone device
    // nodes exist in this isolated fixture. Both interpreters are local files.
    char *arguments[32] = {"/test/qemu"};
    assert(argc - 1 < 32);
    for (int i = 2; i < argc; ++i) arguments[i - 1] = argv[i];
    execv(arguments[0], arguments);
    perror("fixture native exec");
    return 1;
}
