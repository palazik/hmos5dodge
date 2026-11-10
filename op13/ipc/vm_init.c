// SPDX-License-Identifier: Apache-2.0
/* RAM-only QEMU init; never installed on the phone or in the DSU package. */
#define main bootstrap_entry
#include "../dsu_bootstrap.c"
#undef main
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

static void finish(int status)
{
    printf("OP13_IPC_VM_RESULT=%s\n", status ? "FAIL" : "PASS");
    fflush(NULL);
    reboot(RB_POWER_OFF);
    for (;;) pause();
}

static void require(int ok, const char *operation)
{
    if (!ok) {
        printf("VM FAIL %s: errno=%d %s\n", operation, errno, strerror(errno));
        finish(1);
    }
}

static void test(const char *binary)
{
    pid_t child = fork();
    require(child >= 0, "fork test");
    if (!child) {
        execl(binary, binary, "/dev/access_token_id", "/dev/binder", NULL);
        _exit(127);
    }
    int status;
    require(waitpid(child, &status, 0) == child, "wait test");
    printf("VM TEST %s status=%d\n", binary, status);
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, binary);
}

int main(void)
{
    setbuf(stdout, NULL);
    require(getpid() == 1, "must be VM PID 1");
    struct utsname uts;
    require(uname(&uts) == 0, "uname");
    printf("OP13_IPC_VM kernel=%s\n", uts.release);
    require(mount("proc", "/proc", "proc", 0, NULL) == 0, "mount proc");
    require(mount("sysfs", "/sys", "sysfs", 0, NULL) == 0, "mount sysfs");
    require(mount("tmpfs", "/dev", "tmpfs", MS_NOSUID, "mode=0755") == 0, "mount RAM dev");
    require(mknod("/dev/null", S_IFCHR | 0666, makedev(1, 3)) == 0, "null");
    int fd = open("/op13_hmos_ipc.ko", O_RDONLY | O_CLOEXEC);
    require(fd >= 0, "module open");
    int result = syscall(SYS_finit_module, fd, "", 0);
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    require(result == 0, "normal finit_module with flags=0");
    root = "/";
    binder_namespace(); // Exercise the production DSU alias and API checks.
    require(device_count == 4, "four production IPC device-filter entries");
    struct stat token;
    require(stat("/dev/access_token_id", &token) == 0 && token.st_uid == 3020 &&
            token.st_gid == 3020, "native AccessToken owner/group");
    test("/token_contract");
    // Each contract uses a fresh context-manager endpoint.
    struct stat next;
    require(stat("/dev/hwbinder", &next) == 0, "second test endpoint");
    require(unlink("/dev/binder") == 0, "retire first test alias");
    require(mknod("/dev/binder", S_IFCHR | 0666, next.st_rdev) == 0 &&
            chmod("/dev/binder", 0666) == 0, "second Binder alias");
    test("/binder_nested");
    finish(0);
}
