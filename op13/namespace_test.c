/* SPDX-License-Identifier: Apache-2.0
 * Run only inside validate_components.py's disposable bubblewrap namespace.
 * Uses tmpfs fixture data: no phone, block device, BPF or policy-load calls.
 */
#define main bootstrap_entry
#include "dsu_bootstrap.c"
#undef main
#include <assert.h>

static void fixture_file(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    assert(fd >= 0 && write(fd, "fixture", 7) == 7);
    assert(!close(fd));
}

static void namespace_round_trip(bool pivot)
{
    if (pivot) pivot_native_root();
    else assert(!chdir(root) && !chroot(".") && !chdir("/"));
    assert(!access("/data/dsu-fixture", F_OK));
    int original = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
    assert(original >= 0);
    assert(!unshare(CLONE_NEWNS));
    assert(!setns(original, CLONE_NEWNS));
    close(original);
    if (!pivot) {
        // Reproduce revision 5: restoring the namespace escapes the chroot,
        // loses its data mount and exposes the original namespace root.
        assert(!access("/old-root-fixture", F_OK));
        assert(access("/data/dsu-fixture", F_OK) < 0 && errno == ENOENT);
        return;
    }
    assert(access("/old-root-fixture", F_OK) < 0 && errno == ENOENT);
    assert(!access("/data/dsu-fixture", F_OK));
    fixture_file("/data/writable-after-setns");
    struct stat st;
    assert(!stat("/dev", &st) && S_ISDIR(st.st_mode));
    assert(!access("/dev/guest-device-tree-fixture", F_OK));
    // Repeat the second default-namespace restoration used by mksandbox.
    original = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
    assert(original >= 0 && !unshare(CLONE_NEWNS) && !setns(original, CLONE_NEWNS));
    close(original);
    assert(!access("/data/writable-after-setns", F_OK));
    assert(!access("/dev/guest-device-tree-fixture", F_OK));
}

int main(void)
{
    // Refuse direct execution on the user's filesystem. The validator gives
    // us an otherwise empty tmpfs root and this explicit read-only marker.
    struct statfs fs;
    assert(!statfs("/", &fs) && (unsigned long)fs.f_type == 0x01021994UL);
    assert(!access("/hmos-namespace-test", F_OK));
    root = "/guest";
    fixture_file("/old-root-fixture");
    directory(root, 0755);
    assert(!mount("tmpfs", root, "tmpfs", MS_NOSUID, "mode=0755"));
    root_directory("/data", 0700);
    root_mount("tmpfs", "/data", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0700");
    root_directory("/dev", 0700);
    root_mount("tmpfs", "/dev", "tmpfs", MS_NOSUID, "mode=0700");
    root_directory("/proc", 0755);
    root_mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    fixture_file("/guest/data/dsu-fixture");
    fixture_file("/guest/dev/guest-device-tree-fixture");
    for (unsigned pivot = 0; pivot < 2; ++pivot) {
        pid_t child = fork();
        assert(child >= 0);
        if (!child) {
            assert(!unshare(CLONE_NEWNS));
            assert(!mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL));
            namespace_round_trip(pivot);
            _exit(0);
        }
        int status;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    }
    puts("PASS chroot failure reproduced; pivot_root retains guest data/devices across repeated setns");
    return 0;
}
