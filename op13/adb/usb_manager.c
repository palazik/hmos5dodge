// SPDX-License-Identifier: Apache-2.0
// Started only inside the isolated, user-selected HMOS DSU guest.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define GADGET "/config/usb_gadget/hmos5dodge"
#define FFS "/dev/usb-ffs/adb"

static bool directory(const char *path)
{
    if (mkdir(path, 0755) == 0) return true;
    struct stat st;
    return errno == EEXIST && !lstat(path, &st) && S_ISDIR(st.st_mode);
}

static bool write_value(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    size_t count = strlen(value);
    ssize_t written;
    do written = write(fd, value, count); while (written < 0 && errno == EINTR);
    int saved = errno;
    close(fd);
    errno = saved;
    return written == (ssize_t)count;
}

static bool read_value(const char *path, char *value, size_t size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    ssize_t count = read(fd, value, size - 1);
    close(fd);
    if (count < 0) return false;
    value[count] = 0;
    value[strcspn(value, "\r\n")] = 0;
    return true;
}

static bool controller(char *name, size_t size)
{
    // Revision 26: log what exists and prefer the Qualcomm dwc3 controller
    // (Android uses a600000.dwc3).  It may register late, so wait up to 30 s.
    for (unsigned attempt = 0; attempt < 60; ++attempt) {
        DIR *dir = opendir("/sys/class/udc");
        if (!dir) { if (!attempt) fprintf(stderr, "ADB: /sys/class/udc: %s\n", strerror(errno)); usleep(500000); continue; }
        struct dirent *entry;
        char first[256] = {0};
        bool found = false;
        unsigned count = 0;
        while ((entry = readdir(dir))) {
            if (entry->d_name[0] == '.' || strlen(entry->d_name) >= size) continue;
            if (!attempt || attempt % 20 == 0) fprintf(stderr, "ADB: UDC candidate %s\n", entry->d_name);
            if (!count++) snprintf(first, sizeof(first), "%s", entry->d_name);
            if (!found && strstr(entry->d_name, "dwc3")) { snprintf(name, size, "%s", entry->d_name); found = true; }
        }
        closedir(dir);
        if (!found && count == 1) { snprintf(name, size, "%s", first); found = true; }
        if (found) { fprintf(stderr, "ADB: using UDC %s (%u present)\n", name, count); return true; }
        if (!attempt) {
            fprintf(stderr, "ADB: %u UDC(s) present, none usable yet; waiting\n", count);
            // Revision 27: on Qualcomm the dwc3 UDC only exists in device role.
            // ColorOS shows a600000.ssusb-role-switch = device; nothing sets it
            // in the guest, so request the device role on every role switch.
            DIR *roles = opendir("/sys/class/usb_role");
            struct dirent *role;
            while (roles && (role = readdir(roles))) {
                if (role->d_name[0] == '.') continue;
                char path[512], current[64] = {0};
                snprintf(path, sizeof(path), "/sys/class/usb_role/%s/role", role->d_name);
                if (!read_value(path, current, sizeof(current))) continue;
                bool ok = !strcmp(current, "device") || write_value(path, "device");
                fprintf(stderr, "ADB: role switch %s was %s -> device %s\n", role->d_name, current,
                        ok ? "ok" : strerror(errno));
            }
            if (roles) closedir(roles);
        }
        usleep(500000);
    }
    return false;
}

static void drop_daemon_privileges(void)
{
    // Authenticated shells stay UID 0 for guest diagnostics with only read-only
    // capabilities: DAC_READ_SEARCH (service-owned logs) and SYSLOG (kernel
    // log).  No mount/module/reboot/raw-I/O/write-override capability.
    for (int cap = 0; cap <= 63; ++cap)
        if (cap != CAP_DAC_READ_SEARCH && cap != CAP_SYSLOG &&
            prctl(PR_CAPBSET_DROP, cap, 0, 0, 0) && errno != EINVAL) _exit(120);
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct data[2] = {{0}, {0}};
    data[0].effective = data[0].permitted = data[0].inheritable = 1U << CAP_DAC_READ_SEARCH;
    data[1].effective = data[1].permitted = data[1].inheritable = 1U << (CAP_SYSLOG - 32);
    if (syscall(SYS_capset, &header, data) || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) _exit(121);
}

static pid_t start_daemon(void)
{
    pid_t child = fork();
    if (child) return child;
    drop_daemon_privileges();
    char *const args[] = {"/system/bin/hmos-adbd", NULL};
    char *const env[] = {"PATH=/system/bin:/vendor/bin", "HOME=/data", "TMPDIR=/data/local/tmp", NULL};
    execve(args[0], args, env);
    perror("hmos-adbd exec");
    _exit(122);
}

static int configure(void)
{
    char udc[256] = {0}, previous_path[512] = {0}, previous_udc[256] = {0};
    pid_t daemon = -1;
    bool unbound = false;
    if (!controller(udc, sizeof(udc))) {
        fprintf(stderr, "ADB: no usable USB device controller after 30 s\n");
        return 1;
    }
    if (!directory("/config") ||
        (mount("none", "/config", "configfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) && errno != EBUSY))
        goto fail;
    if (!directory(GADGET)) goto fail;
    // Construct our own function instance; never rewrite stock g1 descriptors.
    if (!write_value(GADGET "/idVendor", "0x18d1") ||
        !write_value(GADGET "/idProduct", "0x4ee7") ||
        !write_value(GADGET "/bcdUSB", "0x0200") ||
        !directory(GADGET "/strings/0x409") ||
        !write_value(GADGET "/strings/0x409/serialnumber", "hmos5dodge") ||
        !write_value(GADGET "/strings/0x409/manufacturer", "hmos5dodge") ||
        !write_value(GADGET "/strings/0x409/product", "OP13 HMOS5 diagnostic ADB") ||
        !directory(GADGET "/configs/b.1") ||
        !write_value(GADGET "/configs/b.1/MaxPower", "500") ||
        !directory(GADGET "/functions/ffs.hmos_adb") ||
        !directory("/dev/usb-ffs") || !directory(FFS) ||
        mount("hmos_adb", FFS, "functionfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL)) goto fail;
    // The descriptors must be submitted before attaching the function/UDC.
    daemon = start_daemon();
    if (daemon < 0) goto fail;
    bool ready = false;
    for (unsigned i = 0; i < 100; ++i) {
        int status;
        if (waitpid(daemon, &status, WNOHANG) == daemon) { daemon = -1; errno = ECHILD; goto fail; }
        if (!access(FFS "/ep1", F_OK) && !access(FFS "/ep2", F_OK)) { ready = true; break; }
        usleep(100000);
    }
    if (!ready) { errno = ETIMEDOUT; goto fail; }
    if (symlink(GADGET "/functions/ffs.hmos_adb", GADGET "/configs/b.1/f1")) goto fail;
    DIR *gadgets = opendir("/config/usb_gadget");
    if (!gadgets) goto fail;
    struct dirent *entry;
    unsigned bound = 0;
    while ((entry = readdir(gadgets))) {
        if (entry->d_name[0] == '.' || !strcmp(entry->d_name, "hmos5dodge")) continue;
        char path[512], value[256];
        snprintf(path, sizeof(path), "/config/usb_gadget/%s/UDC", entry->d_name);
        if (read_value(path, value, sizeof(value)) && value[0]) {
            ++bound;
            strcpy(previous_path, path);
            strcpy(previous_udc, value);
        }
    }
    closedir(gadgets);
    if (bound > 1 || (bound && strcmp(previous_udc, udc))) { errno = EBUSY; goto fail; }
    if (bound) {
        if (!write_value(previous_path, "\n")) goto fail;
        unbound = true;
    }
    if (!write_value(GADGET "/UDC", udc)) goto fail;
    fprintf(stderr, "ADB: USB-only authenticated daemon attached to %s; serial=hmos5dodge\n", udc);
    // ADB is diagnostic: its exit never kills native init or reboots the phone.
    for (unsigned restarts = 0; restarts < 3; ++restarts) {
        int status;
        while (waitpid(daemon, &status, 0) < 0) if (errno != EINTR) goto fail;
        fprintf(stderr, "ADB: daemon exited status=%d, restart=%u\n", status, restarts + 1);
        sleep(1);
        daemon = start_daemon();
        if (daemon < 0) goto fail;
    }
    errno = ECHILD;
fail:
    fprintf(stderr, "ADB: unavailable: %s (HMOS startup continues)\n", strerror(errno));
    if (daemon > 0) { kill(daemon, SIGTERM); while (waitpid(daemon, NULL, 0) < 0 && errno == EINTR) {} }
    write_value(GADGET "/UDC", "\n");
    if (unbound && !write_value(previous_path, previous_udc)) perror("ADB restore prior UDC binding");
    return 1;
}

int main(void)
{
    umask(0077);
    struct rlimit limit = {2 * 1024 * 1024, 2 * 1024 * 1024};
    if (setrlimit(RLIMIT_FSIZE, &limit)) return 1;
    signal(SIGXFSZ, SIG_IGN);
    if (setsid() < 0) return 1;
    int log = open("/data/hmos-port/adb.log", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct stat st;
    if (log < 0 || fstat(log, &st) || !S_ISREG(st.st_mode) || st.st_uid || st.st_nlink != 1) return 1;
    if (fchmod(log, 0600) || dup2(log, 1) < 0 || dup2(log, 2) < 0) return 1;
    close(log);
    int null = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (null < 0 || dup2(null, 0) < 0) return 1;
    if (null > 2) close(null);
    fprintf(stderr, "ADB: native USB manager starting inside HMOS DSU\n");
    return configure();
}
