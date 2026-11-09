/* SPDX-License-Identifier: Apache-2.0
 * Experimental Android first-stage -> full HarmonyOS second-stage handoff.
 * No bootloader/firmware writes or physical userdata fallback.
 * Initialize only an installer-wiped DSU allocation before mounting it.
 * A failed mount of an existing filesystem never triggers formatting.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/bpf.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <poll.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/klog.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *root = "/mnt/hmos-root";
static int kmsg = -1;
static int bootlog = -1;
static int metalog = -1;
static int native_log = -1;
static size_t metalog_bytes;
#define METALOG_LIMIT (1024U * 1024U)
#define NATIVE_LOG_LIMIT (4U * 1024U * 1024U)
struct device { unsigned major, minor; };
struct binder_device_request { char name[256]; uint32_t major, minor; };
#define BINDER_DEVICE_ADD _IOWR('b', 1, struct binder_device_request)
static struct device devices[128];
static size_t device_count;

static void save_kernel_tail(void)
{
    // Read-only syslog action 3: copy, do not clear or change kernel logging.
    // Preserve the final probe errors even when this phone leaves pstore empty.
    if (metalog < 0 || metalog_bytes + 128 >= METALOG_LIMIT) return;
    char *buffer = malloc(65536);
    if (!buffer) return;
    int count = klogctl(3, buffer, 65536);
    if (count > 0) {
        const char begin[] = "\n--- kernel tail at bootstrap stop ---\n";
        ssize_t written = write(metalog, begin, sizeof(begin) - 1);
        if (written > 0) metalog_bytes += written;
        size_t length = count;
        if (length > METALOG_LIMIT - metalog_bytes) length = METALOG_LIMIT - metalog_bytes;
        written = write(metalog, buffer + count - length, length);
        if (written > 0) metalog_bytes += written;
        fdatasync(metalog);
    }
    free(buffer);
}

static void log_line(const char *format, ...)
{
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    dprintf(2, "hmos-op13: %s\n", message);
    if (kmsg >= 0) dprintf(kmsg, "<3>hmos-op13: %s\n", message);
    if (bootlog >= 0) dprintf(bootlog, "%s\n", message);
    size_t length = strlen(message);
    if (metalog >= 0 && metalog_bytes + length + 1 <= METALOG_LIMIT) {
        if (dprintf(metalog, "%s\n", message) >= 0) metalog_bytes += length + 1;
        fdatasync(metalog);
    }
}

static __attribute__((noreturn)) void fatal(const char *operation)
{
    log_line("STOP %s: errno=%d (%s); no automatic reboot", operation, errno, strerror(errno));
    save_kernel_tail();
    if (bootlog >= 0) fdatasync(bootlog);
    if (getpid() != 1) exit(1);
    for (;;) pause();
}

static int read_text(const char *path, char *buffer, size_t capacity)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t size = read(fd, buffer, capacity - 1);
    int saved = errno;
    close(fd);
    errno = saved;
    if (size < 0) return -1;
    buffer[size] = 0;
    while (size && (buffer[size - 1] == '\n' || buffer[size - 1] == '\r')) buffer[--size] = 0;
    return 0;
}

static void directory(const char *path, mode_t mode)
{
    if (mkdir(path, mode) && errno != EEXIST) fatal(path);
    struct stat st;
    if (lstat(path, &st) || !S_ISDIR(st.st_mode)) { errno = ENOTDIR; fatal(path); }
}

static void rootpath(char *buffer, size_t capacity, const char *suffix)
{
    if (snprintf(buffer, capacity, "%s%s", root, suffix) >= (int)capacity) {
        errno = ENAMETOOLONG; fatal(suffix);
    }
}

static void root_directory(const char *suffix, mode_t mode)
{
    char path[512];
    rootpath(path, sizeof(path), suffix);
    directory(path, mode);
}

static void bind_path(const char *source, const char *target, bool readonly)
{
    if (mount(source, target, NULL, MS_BIND, NULL)) fatal(target);
    if (readonly && mount(NULL, target, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NODEV, NULL))
        fatal(target);
}

static void root_bind(const char *source, const char *suffix, bool readonly)
{
    char target[512];
    rootpath(target, sizeof(target), suffix);
    bind_path(source, target, readonly);
}

static void root_mount(const char *source, const char *suffix, const char *type,
                       unsigned long flags, const char *options)
{
    char target[512];
    rootpath(target, sizeof(target), suffix);
    if (mount(source, target, type, flags, options)) fatal(target);
}

static void pivot_native_root(void)
{
    // chroot alone leaves the mount namespace rooted at Android's first-stage
    // filesystem. Native mksandbox/setns restores that namespace root, hiding
    // our data, devices and overlays. Give the namespace its actual guest root.
    if (chdir(root) || syscall(__NR_pivot_root, ".", ".")) fatal("pivot native root");
    if (umount2(".", MNT_DETACH) || chdir("/")) fatal("detach old mount root");
    log_line("native mount root installed; old-root tree detached before namespace switches");
}

static void mount_native_selinux(void)
{
    // A new sysfs mount exposes the mountpoint directory, not selinuxfs.
    // The native policy loader needs the real API before library constructors
    // discover it. Do not change enforcement here or load policy over ADB.
    root_directory("/sys/fs/selinux", 0755);
    root_mount("selinuxfs", "/sys/fs/selinux", "selinuxfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    char path[512], value[32], enforce[8], extra;
    struct statfs fs;
    rootpath(path, sizeof(path), "/sys/fs/selinux");
    if (statfs(path, &fs) || (unsigned long)fs.f_type != 0xf97cff8cUL) {
        errno = ENODEV; fatal("native selinuxfs identity");
    }
    rootpath(path, sizeof(path), "/sys/fs/selinux/policyvers");
    unsigned version;
    if (read_text(path, value, sizeof(value)) || sscanf(value, "%u%c", &version, &extra) != 1 || version < 31) {
        errno = EINVAL; fatal("native SELinux policy-version API");
    }
    rootpath(path, sizeof(path), "/sys/fs/selinux/enforce");
    if (read_text(path, enforce, sizeof(enforce)) || (strcmp(enforce, "0") && strcmp(enforce, "1"))) {
        errno = EINVAL; fatal("native SELinux enforcement API");
    }
    log_line("native SELinux API mounted; kernel policy version=%u; enforcement=%s (unchanged)", version, enforce);
}

static void open_metadata_log(void)
{
    // A bounded diagnostic in the already mounted host metadata filesystem.
    // This mount is never exposed in the guest. Missing metadata is nonfatal.
    struct stat metadata, system;
    if (stat("/metadata", &metadata) || stat("/", &system) || metadata.st_dev == system.st_dev) return;
    int parent = open("/metadata", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parent < 0) return;
    if (mkdirat(parent, "hmos5dodge", 0700) && errno != EEXIST) { close(parent); return; }
    int directory_fd = openat(parent, "hmos5dodge", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(parent);
    if (directory_fd < 0) return;
    struct stat st;
    if (fstat(directory_fd, &st) || st.st_uid || (st.st_mode & 0777) != 0700) { close(directory_fd); return; }
    int fd = openat(directory_fd, "bootstrap.log", O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    close(directory_fd);
    if (fd < 0) return;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid || st.st_nlink != 1 ||
        (st.st_mode & 0777) != 0600 || st.st_size < 0 || st.st_size >= METALOG_LIMIT) { close(fd); return; }
    metalog = fd;
    metalog_bytes = st.st_size;
}

static void open_native_log(void)
{
    // Only an already validated metadata directory. Never expose this mount
    // or a directory descriptor to native init or its services.
    if (metalog < 0) return;
    int directory_fd = open("/metadata/hmos5dodge", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory_fd < 0) return;
    struct stat st;
    if (fstat(directory_fd, &st) || st.st_uid || (st.st_mode & 0777) != 0700) {
        close(directory_fd); return;
    }
    int fd = openat(directory_fd, "native-startup.log",
        O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    close(directory_fd);
    if (fd < 0) return;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid || st.st_nlink != 1 ||
        (st.st_mode & 0777) != 0600 || st.st_size < 0) { close(fd); return; }
    native_log = fd;
}

static bool append_native_log(int output, const char *text, size_t length, size_t *used, size_t limit)
{
    if (*used >= limit) return false;
    if (length > limit - *used) length = limit - *used;
    while (length) {
        ssize_t written = write(output, text, length);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        text += written; length -= written; *used += written;
    }
    return *used < limit;
}

static long long monotonic_ms(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time)) return -1;
    return (long long)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static void record_init_state(int output, size_t *used, size_t limit)
{
    const char *files[] = {"comm", "status", "wchan", "stack", "syscall", "cmdline", "mountinfo", "attr/current"};
    char path[128], buffer[4096], title[256];
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        snprintf(path, sizeof(path), "/proc/1/%s", files[i]);
        snprintf(title, sizeof(title), "\n--- PID 1 %s ---\n", files[i]);
        append_native_log(output, title, strlen(title), used, limit);
        int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        ssize_t length = fd < 0 ? -1 : read(fd, buffer, sizeof(buffer));
        int saved = errno;
        if (fd >= 0) close(fd);
        if (length < 0) {
            snprintf(buffer, sizeof(buffer), "unavailable: errno=%d\n", saved);
            length = strlen(buffer);
        } else if (!strcmp(files[i], "cmdline")) {
            for (ssize_t j = 0; j < length; ++j) if (!buffer[j]) buffer[j] = ' ';
        }
        append_native_log(output, buffer, length, used, limit);
    }
    ssize_t length = readlink("/proc/1/exe", buffer, sizeof(buffer));
    const char heading[] = "\n--- PID 1 executable ---\n";
    append_native_log(output, heading, sizeof(heading) - 1, used, limit);
    if (length > 0) append_native_log(output, buffer, length, used, limit);
    append_native_log(output, "\n", 1, used, limit);
}

static void native_record_loop(int kernel, int output, int exec_pipe, unsigned duration_ms, size_t limit)
{
    size_t used = 0, exec_bytes = 0;
    int exec_error = 0;
    const char begin[] = "HMOS native startup recorder revision 6; latest attempt only\n"
        "Bounded to 4 MiB / 180 seconds; no boot deadline or reboot action.\n";
    append_native_log(output, begin, sizeof(begin) - 1, &used, limit);
    long long start = monotonic_ms(), next_state = start + 1000;
    if (start < 0) return;
    struct pollfd inputs[] = {{kernel, POLLIN, 0}, {exec_pipe, POLLIN, 0}};
    while (used < limit) {
        long long now = monotonic_ms();
        if (now < 0 || now - start >= duration_ms) break;
        int remaining = duration_ms - (now - start);
        int ready = poll(inputs, 2, remaining < 250 ? remaining : 250);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) break;
        if (inputs[1].fd >= 0 && inputs[1].revents) {
            ssize_t count = read(inputs[1].fd, (char *)&exec_error + exec_bytes, sizeof(exec_error) - exec_bytes);
            if (count > 0) exec_bytes += count;
            if (exec_bytes == sizeof(exec_error) || count == 0) {
                char message[192];
                if (exec_bytes == sizeof(exec_error))
                    snprintf(message, sizeof(message), "execve FAILED: errno=%d (%s)\n", exec_error, strerror(exec_error));
                else if (exec_bytes)
                    snprintf(message, sizeof(message), "execve status incomplete; acceptance unknown\n");
                else
                    snprintf(message, sizeof(message), "execve status pipe closed without error; loader/framework success unverified\n");
                append_native_log(output, message, strlen(message), &used, limit);
                close(inputs[1].fd); inputs[1].fd = -1;
            } else if (count < 0 && errno != EAGAIN && errno != EINTR) {
                close(inputs[1].fd); inputs[1].fd = -1;
            }
        }
        if (inputs[0].fd >= 0 && inputs[0].revents) {
            for (unsigned batch = 0; batch < 32 && used < limit; ++batch) {
                char record[16384];
                ssize_t count = read(inputs[0].fd, record, sizeof(record));
                if (count > 0) {
                    append_native_log(output, record, count, &used, limit);
                    append_native_log(output, "\n", 1, &used, limit);
                } else if (count < 0 && errno == EPIPE) {
                    const char lost[] = "\n[kernel records overwritten before read]\n";
                    append_native_log(output, lost, sizeof(lost) - 1, &used, limit);
                } else {
                    if (!count || (errno != EAGAIN && errno != EINTR)) {
                        close(inputs[0].fd); inputs[0].fd = -1;
                    }
                    break;
                }
            }
        }
        now = monotonic_ms();
        if (now >= next_state) {
            record_init_state(output, &used, limit);
            next_state = now + 15000;
        }
        // Retain evidence after a forced reboot, rather than waiting until
        // the recorder exits. Only this fixed regular log file is written.
        if (fdatasync(output)) break;
    }
    fdatasync(output);
}

static int start_native_recorder(void)
{
    if (native_log < 0) { log_line("native startup recorder unavailable: metadata log not open"); return -1; }
    int input = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    int status_pipe[2];
    if (input < 0 || lseek(input, 0, SEEK_END) < 0 || pipe2(status_pipe, O_CLOEXEC | O_NONBLOCK)) {
        if (input >= 0) close(input);
        log_line("native startup recorder unavailable: errno=%d", errno);
        close(native_log); native_log = -1; return -1;
    }
    if (ftruncate(native_log, 0)) fatal("truncate latest native startup log");
    pid_t child = fork();
    if (child < 0) fatal("native startup recorder fork");
    if (!child) {
        // All sources are >= 3. After duplication only a read-only kernel
        // stream, one write-only regular log, and a read-only status pipe
        // survive. This process has already entered the guest mount root/filter.
        if (dup2(input, 0) < 0 || dup2(native_log, 1) < 0 || dup2(status_pipe[0], 2) < 0 ||
            syscall(__NR_close_range, 3U, ~0U, 0)) _exit(120);
        prctl(PR_SET_NAME, "hmos-native-log", 0, 0, 0);
        for (unsigned cap = 0; cap <= 63; ++cap) prctl(PR_CAPBSET_DROP, cap, 0, 0, 0);
        struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
        struct __user_cap_data_struct capabilities[2] = {{0}, {0}};
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) || syscall(__NR_capset, &header, capabilities)) _exit(121);
        native_record_loop(0, 1, 2, 180000, NATIVE_LOG_LIMIT);
        _exit(0);
    }
    close(input); close(status_pipe[0]); close(native_log); native_log = -1;
    log_line("native startup recorder started; /metadata/hmos5dodge/native-startup.log; at most 4 MiB / 180 seconds");
    return status_pipe[1];
}

static bool dm_name_valid(const char *name);

static bool dm_identity_matches_at(const char *device_directory, const char *expected)
{
    char path[768], name[128];
    if (snprintf(path, sizeof(path), "%s/dm/name", device_directory) >= (int)sizeof(path)) return false;
    if (!read_text(path, name, sizeof(name)) && !strcmp(name, expected)) return true;
    snprintf(path, sizeof(path), "%s/slaves", device_directory);
    DIR *slaves = opendir(path);
    if (!slaves) return false;
    unsigned count = 0, matching = 0;
    struct dirent *entry;
    while ((entry = readdir(slaves))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        ++count;
        snprintf(path, sizeof(path), "%s/slaves/%.128s/dm/name", device_directory, entry->d_name);
        if (!read_text(path, name, sizeof(name)) && !strcmp(name, expected)) ++matching;
    }
    closedir(slaves);
    return count == 1 && matching == 1;
}

static bool is_dsu_mount(const char *source, const char *expected)
{
    struct stat st;
    struct statvfs fs;
    if (stat(source, &st) || !S_ISDIR(st.st_mode) || statvfs(source, &fs) || !(fs.f_flag & ST_RDONLY)) {
        return false;
    }
    char path[512];
    snprintf(path, sizeof(path), "/sys/dev/block/%u:%u", major(st.st_dev), minor(st.st_dev));
    return dm_identity_matches_at(path, expected);
}

static dev_t find_exact_dm_at(const char *block_directory, const char *expected)
{
    DIR *directory_fd = opendir(block_directory);
    if (!directory_fd) return 0;
    dev_t found = 0;
    struct dirent *entry;
    while ((entry = readdir(directory_fd))) {
        if (!dm_name_valid(entry->d_name)) continue;
        char path[768], name[128], numbers[64], extra;
        snprintf(path, sizeof(path), "%s/%.64s/dm/name", block_directory, entry->d_name);
        if (read_text(path, name, sizeof(name)) || strcmp(name, expected)) continue;
        snprintf(path, sizeof(path), "%s/%.64s/dev", block_directory, entry->d_name);
        unsigned major_number, minor_number;
        if (read_text(path, numbers, sizeof(numbers)) ||
            sscanf(numbers, "%u:%u %c", &major_number, &minor_number, &extra) != 2 || !major_number) {
            closedir(directory_fd); errno = EINVAL; return 0;
        }
        if (found) { closedir(directory_fd); errno = EEXIST; return 0; }
        found = makedev(major_number, minor_number);
    }
    closedir(directory_fd);
    if (!found) errno = ENODEV;
    return found;
}

static void dsu_bind(const char *source, const char *target, const char *expected)
{
    if (is_dsu_mount(source, expected)) {
        root_bind(source, target, true);
        log_line("bound separate DSU partition %s from %s", expected, source);
        return;
    }
    // Some OEM first-stage implementations map extra images without mounting
    // them. Mount only the exact named DSU mapping, never a stock partition or
    // a same-filesystem placeholder directory. All eight parts remain required.
    dev_t device = find_exact_dm_at("/sys/block", expected);
    if (!device) fatal(expected);
    char node[512], destination[512];
    rootpath(node, sizeof(node), "/.dsu-mount-node");
    rootpath(destination, sizeof(destination), target);
    if (mknod(node, S_IFBLK | 0600, device)) fatal("temporary DSU read-only source");
    log_line("mounting mapped DSU partition %s (%u:%u)", expected, major(device), minor(device));
    int result = mount(node, destination, "erofs", MS_RDONLY | MS_NODEV | MS_NOSUID, NULL);
    if (result) {
        log_line("EROFS mount %s failed: errno=%d; trying ext4 without journal replay", expected, errno);
        result = mount(node, destination, "ext4", MS_RDONLY | MS_NODEV | MS_NOSUID, "noload");
    }
    int saved = errno;
    if (unlink(node)) fatal("remove temporary DSU source");
    errno = saved;
    if (result) fatal(expected);
    if (!is_dsu_mount(destination, expected)) { errno = ENODEV; fatal("mounted DSU identity verification"); }
    log_line("mounted separate DSU partition %s at %s", expected, target);
}

static bool dm_name_valid(const char *name)
{
    if (strncmp(name, "dm-", 3) || !name[3]) return false;
    for (const char *p = name + 3; *p; ++p) if (*p < '0' || *p > '9') return false;
    return true;
}

static dev_t find_dsu_data(void)
{
    DIR *dir = opendir("/sys/block");
    if (!dir) fatal("sys/block");
    struct dirent *entry;
    bool system_found = false;
    dev_t data = 0;
    while ((entry = readdir(dir))) {
        if (!dm_name_valid(entry->d_name)) continue;
        char path[512], name[128], numbers[64];
        snprintf(path, sizeof(path), "/sys/block/%.64s/dm/name", entry->d_name);
        if (read_text(path, name, sizeof(name))) continue;
        if (!strcmp(name, "system_gsi")) system_found = true;
        if (strcmp(name, "userdata_gsi")) continue;
        snprintf(path, sizeof(path), "/sys/block/%.64s/dev", entry->d_name);
        unsigned major_number, minor_number;
        if (read_text(path, numbers, sizeof(numbers)) || sscanf(numbers, "%u:%u", &major_number, &minor_number) != 2)
            fatal("userdata_gsi device identity");
        if (data) { errno = EEXIST; fatal("multiple userdata_gsi mappings"); }
        data = makedev(major_number, minor_number);
    }
    closedir(dir);
    if (!system_found || !data) { errno = ENODEV; fatal("exact system_gsi/userdata_gsi required"); }
    return data;
}

static bool module_path_valid(const char *path)
{
    const char *prefixes[] = {"/vendor_dlkm/lib/modules/", "/system_dlkm/lib/modules/", "/hmos/modules/"};
    bool prefix = false;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i)
        if (!strncmp(path, prefixes[i], strlen(prefixes[i]))) prefix = true;
    size_t length = strlen(path);
    if (!prefix || length < 4 || strcmp(path + length - 3, ".ko") || strstr(path, "..") || strstr(path, "//")) return false;
    for (const char *p = path; *p; ++p)
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.' || *p == '/')) return false;
    return true;
}

static int drm_boot_options(const char *cmdline, char *options, size_t capacity)
{
    if (!capacity) { errno = EINVAL; return -1; }
    options[0] = 0;
    size_t used = 0;
    int count = 0;
    const char *keys[] = {"msm_drm.dsi_display0=", "msm_drm.dsi_display1="};
    for (const char *p = cmdline; *p;) {
        while (*p == ' ' || *p == '\t' || *p == '\n') ++p;
        if (!*p) break;
        const char *start = p;
        bool quoted = false;
        while (*p && (quoted || (*p != ' ' && *p != '\t' && *p != '\n'))) {
            if (*p == '"') quoted = !quoted;
            ++p;
        }
        if (quoted) { errno = EINVAL; return -1; }
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
            size_t prefix = strlen(keys[i]);
            if ((size_t)(p - start) < prefix || strncmp(start, keys[i], prefix)) continue;
            // finit_module takes unprefixed parameter names. No shell runs.
            size_t length = p - start - strlen("msm_drm.");
            if (length + (used ? 1 : 0) >= capacity - used) { errno = E2BIG; return -1; }
            if (used) options[used++] = ' ';
            memcpy(options + used, start + strlen("msm_drm."), length);
            used += length;
            options[used] = 0;
            ++count;
        }
    }
    return count;
}

static void load_stock_modules(void)
{
    FILE *list = fopen("/hmos/modules/load-order", "re");
    if (!list) fatal("stock module order");
    char line[640], name[128], source[384], extra;
    while (fgets(line, sizeof(line), list)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        if (sscanf(line, "%127s %383s %c", name, source, &extra) != 2 || !module_path_valid(source)) {
            errno = EINVAL; fatal("invalid module source path");
        }
        for (const char *p = name; *p; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '_')) { errno = EINVAL; fatal("module name"); }
        char path[256];
        snprintf(path, sizeof(path), "/sys/module/%s", name);
        if (!access(path, F_OK)) continue;
        char mounted_source[768];
        const char *module_source = source;
        if (strncmp(source, "/hmos/modules/", 14)) {
            rootpath(mounted_source, sizeof(mounted_source), source);
            module_source = mounted_source;
        }
        int fd = open(module_source, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) fatal(module_source);
        char options[4096] = {0};
        if (!strcmp(name, "msm_drm")) {
            char cmdline[16384];
            if (read_text("/proc/cmdline", cmdline, sizeof(cmdline))) fatal("stock DRM boot arguments");
            if (strlen(cmdline) == sizeof(cmdline) - 1) { errno = E2BIG; fatal("truncated boot arguments"); }
            int selectors = drm_boot_options(cmdline, options, sizeof(options));
            if (selectors < 0) fatal("DRM panel parameter parsing");
            log_line("forwarding %d stock DSI panel selector(s) to msm_drm; values omitted", selectors);
        }
        int result = syscall(__NR_finit_module, fd, options, 0); // no force flags
        int saved = errno;
        close(fd);
        errno = saved;
        if (result && errno != EEXIST) fatal(name);
        log_line("loaded stock module %s", name);
    }
    fclose(list);
}

static bool device_numbers(const char *path, unsigned *major_number, unsigned *minor_number)
{
    char numbers[64], extra;
    unsigned maj, min;
    if (read_text(path, numbers, sizeof(numbers)) || sscanf(numbers, "%u:%u %c", &maj, &min, &extra) != 2 ||
        !maj || maj > 4095 || min > 1048575) return false;
    *major_number = maj; *minor_number = min;
    return true;
}

static bool wait_for_device_numbers(const char *path, unsigned *maj, unsigned *min, unsigned attempts)
{
    for (unsigned i = 0; i < attempts; ++i) {
        if (device_numbers(path, maj, min)) return true;
        if (i + 1 < attempts) {
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000};
            while (nanosleep(&delay, &delay) && errno == EINTR) {}
        }
    }
    errno = ENODEV;
    return false;
}

static void copy_device(const char *suffix, bool required)
{
    char original[256], target[512];
    snprintf(original, sizeof(original), "/dev/%s", suffix);
    struct stat st;
    if (stat(original, &st)) {
        char sysfile[256];
        const char *name = strrchr(suffix, '/');
        name = name ? name + 1 : suffix;
        const char *class_name = !strncmp(suffix, "dri/", 4) ? "drm" :
            !strncmp(suffix, "dma_heap/", 9) ? "dma_heap" :
            !strncmp(suffix, "input/", 6) ? "input" :
            !strcmp(suffix, "kgsl-3d0") ? "kgsl" : "misc";
        snprintf(sysfile, sizeof(sysfile), "/sys/class/%s/%s/dev", class_name, name);
        unsigned major_number = 0, minor_number = 0;
        if (required) log_line("waiting up to 10 seconds for required device %s", suffix);
        if (!wait_for_device_numbers(sysfile, &major_number, &minor_number, required ? 101 : 1)) {
            if (required) fatal(sysfile);
            return;
        }
        memset(&st, 0, sizeof(st));
        st.st_mode = S_IFCHR;
        st.st_rdev = makedev(major_number, minor_number);
    }
    if (!S_ISCHR(st.st_mode) || device_count == 128) { errno = EINVAL; fatal(original); }
    char relative[256];
    snprintf(relative, sizeof(relative), "/dev/%s", suffix);
    rootpath(target, sizeof(target), relative);
    if (mknod(target, S_IFCHR | 0666, st.st_rdev)) {
        struct stat existing;
        if (errno != EEXIST || lstat(target, &existing) || !S_ISCHR(existing.st_mode) || existing.st_rdev != st.st_rdev)
            fatal(target);
    }
    if (!strncmp(suffix, "dri/", 4)) {
        if (chown(target, 0, 1003) || chmod(target, 0660)) fatal("DRM device ownership");
    }
    devices[device_count++] = (struct device){major(st.st_rdev), minor(st.st_rdev)};
}

static void binder_namespace(void)
{
    root_directory("/dev/hmos-binderfs", 0700);
    root_mount("binder", "/dev/hmos-binderfs", "binder", MS_NOSUID | MS_NOEXEC, NULL);
    char control_path[512];
    rootpath(control_path, sizeof(control_path), "/dev/hmos-binderfs/binder-control");
    int control = open(control_path, O_RDWR | O_CLOEXEC);
    if (control < 0) fatal("private BinderFS control");
    const char *names[] = {"binder", "hwbinder", "vndbinder"};
    for (size_t i = 0; i < 3; ++i) {
        struct binder_device_request request = {0};
        snprintf(request.name, sizeof(request.name), "hmos-%s", names[i]);
        if (ioctl(control, BINDER_DEVICE_ADD, &request)) fatal("private BinderFS device");
        char source[512], suffix[256], alias[512];
        snprintf(suffix, sizeof(suffix), "/dev/hmos-binderfs/%s", request.name);
        rootpath(source, sizeof(source), suffix);
        snprintf(suffix, sizeof(suffix), "/dev/%s", names[i]);
        rootpath(alias, sizeof(alias), suffix);
        if (chmod(source, 0666)) fatal("BinderFS permissions");
        int placeholder = open(alias, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
        if (placeholder < 0) fatal(alias);
        close(placeholder);
        // A plain mknod of a BinderFS major/minor loses inode-private driver
        // data. Bind the actual BinderFS inode instead.
        bind_path(source, alias, false);
        if (device_count == 128) { errno = ENOSPC; fatal("device allowlist"); }
        devices[device_count++] = (struct device){request.major, request.minor};
    }
    close(control);
    // Guest applications only see the three native aliases. Keep control
    // inaccessible; the alias bind mounts hold their real BinderFS inodes.
    root_mount("tmpfs", "/dev/hmos-binderfs", "tmpfs", MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=0000");
}

static void device_namespace(void)
{
    binder_namespace();
    const char *base[] = {"null", "zero", "full", "random", "urandom", "tty", "kmsg",
                         "ptmx", "ashmem", "fuse", "sw_sync", "kgsl-3d0"};
    for (size_t i = 0; i < sizeof(base) / sizeof(base[0]); ++i)
        copy_device(base[i], !strcmp(base[i], "kgsl-3d0"));
    root_directory("/dev/dri", 0755);
    root_directory("/dev/dma_heap", 0755);
    root_directory("/dev/input", 0755);
    copy_device("dri/card0", true);
    copy_device("dri/renderD128", false);
    copy_device("dma_heap/system", true);
    copy_device("dma_heap/qcom,system", false);
    copy_device("dma_heap/qcom,display", false);
    DIR *inputs = opendir("/dev/input");
    struct dirent *entry;
    while (inputs && (entry = readdir(inputs))) {
        if (strncmp(entry->d_name, "event", 5)) continue;
        char relative[128];
        snprintf(relative, sizeof(relative), "input/%.64s", entry->d_name);
        copy_device(relative, false);
    }
    if (inputs) closedir(inputs);
    // Newly probed input modules may have sysfs entries before ueventd has
    // ever created Android /dev nodes. Enumerate those entries as well.
    inputs = opendir("/sys/class/input");
    while (inputs && (entry = readdir(inputs))) {
        if (strncmp(entry->d_name, "event", 5)) continue;
        char relative[128], guest[512], suffix[256];
        snprintf(relative, sizeof(relative), "input/%.64s", entry->d_name);
        snprintf(suffix, sizeof(suffix), "/dev/%s", relative);
        rootpath(guest, sizeof(guest), suffix);
        if (access(guest, F_OK)) copy_device(relative, false);
    }
    if (inputs) closedir(inputs);
    root_directory("/dev/pts", 0755);
    root_directory("/dev/shm", 01777);
    root_directory("/dev/socket", 0755);
    root_directory("/dev/unix/socket", 0755); // parent created by caller
    root_mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "newinstance,mode=0620,ptmxmode=0666");
}

#define INS(code_, dst_, src_, off_, imm_) ((struct bpf_insn){.code=(code_),.dst_reg=(dst_),.src_reg=(src_),.off=(off_),.imm=(imm_)})
static size_t build_device_filter(struct bpf_insn program[1024])
{
    size_t n = 0;
    program[n++] = INS(BPF_LDX | BPF_W | BPF_MEM, 2, 1, 0, 0);
    program[n++] = INS(BPF_ALU | BPF_AND | BPF_K, 2, 0, 0, 0xffff);
    program[n++] = INS(BPF_JMP | BPF_JEQ | BPF_K, 2, 0, 2, 2); // char only
    program[n++] = INS(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 0);
    program[n++] = INS(BPF_JMP | BPF_EXIT, 0, 0, 0, 0);
    program[n++] = INS(BPF_LDX | BPF_W | BPF_MEM, 2, 1, 4, 0); // major
    program[n++] = INS(BPF_LDX | BPF_W | BPF_MEM, 3, 1, 8, 0); // minor
    for (size_t i = 0; i < device_count; ++i) {
        program[n++] = INS(BPF_JMP | BPF_JNE | BPF_K, 2, 0, 3, devices[i].major);
        program[n++] = INS(BPF_JMP | BPF_JNE | BPF_K, 3, 0, 2, devices[i].minor);
        program[n++] = INS(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 1);
        program[n++] = INS(BPF_JMP | BPF_EXIT, 0, 0, 0, 0);
    }
    // PTYs created after devpts is mounted (Linux UNIX98 majors 136..143).
    for (unsigned major_number = 136; major_number <= 143; ++major_number) {
        program[n++] = INS(BPF_JMP | BPF_JNE | BPF_K, 2, 0, 2, major_number);
        program[n++] = INS(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 1);
        program[n++] = INS(BPF_JMP | BPF_EXIT, 0, 0, 0, 0);
    }
    program[n++] = INS(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 0);
    program[n++] = INS(BPF_JMP | BPF_EXIT, 0, 0, 0, 0);
    return n;
}

static void isolate_devices(void)
{
    // Attach above a child cgroup, then hide that ancestor in a cgroup namespace.
    // This keeps the filter effective if native init creates delegated children.
    directory("/mnt/hmos-cgroup", 0700);
    if (mount("cgroup2", "/mnt/hmos-cgroup", "cgroup2", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL))
        fatal("private cgroup2 mount");
    directory("/mnt/hmos-cgroup/hmos", 0700);
    directory("/mnt/hmos-cgroup/hmos/payload", 0700);
    int group = open("/mnt/hmos-cgroup/hmos", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (group < 0) fatal("device cgroup");
    struct bpf_insn program[1024];
    size_t n = build_device_filter(program);
    char verifier[16384] = {0};
    union bpf_attr load = {0};
    load.prog_type = BPF_PROG_TYPE_CGROUP_DEVICE;
    load.insn_cnt = n;
    load.insns = (uint64_t)(uintptr_t)program;
    load.license = (uint64_t)(uintptr_t)"Apache";
    load.log_buf = (uint64_t)(uintptr_t)verifier;
    load.log_size = sizeof(verifier);
    load.log_level = 1;
    int fd = syscall(__NR_bpf, BPF_PROG_LOAD, &load, sizeof(load));
    if (fd < 0) { log_line("BPF verifier: %s", verifier); fatal("device filter load"); }
    union bpf_attr attach = {0};
    attach.target_fd = group;
    attach.attach_bpf_fd = fd;
    attach.attach_type = BPF_CGROUP_DEVICE;
    if (syscall(__NR_bpf, BPF_PROG_ATTACH, &attach, sizeof(attach))) fatal("device filter attach");
    close(fd);
    close(group);
    int membership = open("/mnt/hmos-cgroup/hmos/payload/cgroup.procs", O_WRONLY | O_CLOEXEC);
    if (membership < 0 || write(membership, "1\n", 2) != 2) fatal("device cgroup membership");
    close(membership);
    if (unshare(CLONE_NEWCGROUP)) fatal("cgroup namespace");
    log_line("device filter attached: block devices denied; %zu character nodes allowed", device_count);
}

static void root_link(const char *target, const char *suffix)
{
    char path[512];
    rootpath(path, sizeof(path), suffix);
    if (symlink(target, path)) fatal(path);
}

static void mask_extra_init(void)
{
    FILE *list = fopen("/hmos/mask-init-dirs", "re");
    if (!list) fatal("init mask manifest");
    char suffix[384];
    while (fgets(suffix, sizeof(suffix), list)) {
        suffix[strcspn(suffix, "\r\n")] = 0;
        if (!suffix[0]) continue;
        bool prefix = !strncmp(suffix, "/sys_prod/", 10) || !strncmp(suffix, "/chip_prod/", 11) ||
            !strncmp(suffix, "/cust/", 6) || !strncmp(suffix, "/patch_hw/", 10);
        if (!prefix || strstr(suffix, "..") || !strstr(suffix, "/etc/init")) {
            errno = EINVAL; fatal("invalid init mask path");
        }
        root_bind("/hmos/empty", suffix, true);
    }
    fclose(list);
}

static void verify_data_node(int fd, dev_t expected)
{
    struct stat st;
    char path[256], name[128];
    if (fstat(fd, &st) || !S_ISBLK(st.st_mode) || st.st_rdev != expected) {
        errno = ENODEV; fatal("DSU data descriptor identity");
    }
    snprintf(path, sizeof(path), "/sys/dev/block/%u:%u/dm/name", major(expected), minor(expected));
    if (read_text(path, name, sizeof(name)) || strcmp(name, "userdata_gsi")) {
        errno = ENODEV; fatal("DSU data mapping changed");
    }
}

enum data_kind { DATA_READ_ERROR = -1, DATA_INSTALLER_WIPED, DATA_EXT4, DATA_F2FS, DATA_UNKNOWN };

static enum data_kind inspect_data_header(int fd)
{
    unsigned char header[4096];
    size_t offset = 0;
    while (offset < sizeof(header)) {
        ssize_t got = pread(fd, header + offset, sizeof(header) - offset, offset);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { if (!got) errno = EIO; return DATA_READ_ERROR; }
        offset += got;
    }
    // gsid Format() zeroes this entire header to mark a fresh allocation.
    bool wiped = true;
    for (size_t i = 0; i < sizeof(header); ++i) if (header[i]) { wiped = false; break; }
    if (wiped) return DATA_INSTALLER_WIPED;
    if (header[1080] == 0x53 && header[1081] == 0xef) return DATA_EXT4;
    if (header[1024] == 0x10 && header[1025] == 0x20 && header[1026] == 0xf5 && header[1027] == 0xf2)
        return DATA_F2FS;
    return DATA_UNKNOWN;
}

static void initialize_new_dsu_data(const char *node, dev_t expected)
{
    int fd = open(node, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) fatal("DSU data descriptor");
    verify_data_node(fd, expected);
    uint64_t bytes = 0;
    if (ioctl(fd, BLKGETSIZE64, &bytes) || bytes < 128ULL * 1024 * 1024 ||
        bytes > 512ULL * 1024 * 1024 * 1024) { errno = EINVAL; fatal("DSU data size"); }
    if (inspect_data_header(fd) != DATA_INSTALLER_WIPED) {
        errno = EEXIST; fatal("DSU header is not installer-wiped; no initialization");
    }
    verify_data_node(fd, expected);
    log_line("first-boot initialization of installer-wiped userdata_gsi; allocation remains %llu MiB",
             (unsigned long long)(bytes / 1024 / 1024));
    pid_t child = fork();
    if (child < 0) fatal("DSU formatter fork");
    if (!child) {
        if (syscall(__NR_close_range, 3U, ~0U, 0) || chdir(root) || chroot(".") || chdir("/")) _exit(125);
        char *const args[] = {"/system/bin/mke2fs", "-t", "ext4", "-b", "4096", "-I", "256", "-O", "encrypt,verity", "-F", "-m", "0",
                              "-L", "hmos-dsu", "/dev/block/userdata_gsi", NULL};
        char *const environment[] = {"PATH=/system/bin:/vendor/bin", NULL};
        execve(args[0], args, environment);
        _exit(126);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) {
        log_line("isolated mke2fs wait status=0x%x", status);
        errno = EIO; fatal("isolated DSU formatter");
    }
    verify_data_node(fd, expected);
    uint64_t after = 0;
    if (ioctl(fd, BLKGETSIZE64, &after) || after != bytes || inspect_data_header(fd) != DATA_EXT4) {
        errno = EIO; fatal("new DSU filesystem or unchanged allocation size verification");
    }
    log_line("new DSU ext4 verified; allocation size unchanged");
    close(fd);
}

int main(int argc, char **argv)
{
    if (getpid() != 1 || argc != 2 || strcmp(argv[1], "selinux_setup")) {
        fputs("This executable is only for Android DSU PID-1 handoff. No action taken.\n", stderr);
        return 2;
    }
    // Reserve stdio before opening diagnostic sources, so recorder fd
    // duplication cannot alias a source with its 0/1/2 destination.
    for (int fd = 0; fd <= 2; ++fd) {
        if (fcntl(fd, F_GETFD) < 0 && errno == EBADF) {
            int null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
            if (null_fd != fd) fatal("reserve standard descriptors");
        }
    }
    kmsg = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    umask(0);
    open_metadata_log();
    open_native_log();
    log_line("starting OP13 DSU handoff revision 6; stage=partition-setup; argv[1]=%s", argv[1]);
    dev_t data = find_dsu_data();
    if (unshare(CLONE_NEWNS)) fatal("private native mount namespace");
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) fatal("private mount propagation");
    directory(root, 0755);
    if (mount("tmpfs", root, "tmpfs", MS_NOSUID, "mode=0755")) fatal("new root");
    const char *dirs[] = {"/system", "/vendor", "/odm", "/vendor_dlkm", "/system_dlkm",
        "/chip_prod", "/sys_prod", "/cust", "/patch_hw", "/data", "/dev", "/proc", "/sys",
        "/mnt", "/tmp", "/log", "/config", "/storage", "/sec_storage", "/module_update",
        "/preload", "/version", "/updater", "/eng_chipset", "/eng_system"};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) root_directory(dirs[i], 0755);
    dsu_bind("/system", "/system", "system_gsi");
    root_bind("/system/bin/hmos-native-init", "/system/bin/init", true);
    dsu_bind("/vendor", "/vendor", "vendor_gsi");
    dsu_bind("/vendor_dlkm", "/vendor_dlkm", "vendor_dlkm_gsi");
    dsu_bind("/system_dlkm", "/system_dlkm", "system_dlkm_gsi");
    // Reuse Android's already mounted, intrinsically read-only OP13 ODM.
    // It is not supplied as a DSU partition and is never opened for writing.
    struct statvfs odm_flags;
    struct statfs odm_fs;
    struct stat odm_st, system_st;
    if (statvfs("/odm", &odm_flags) || !(odm_flags.f_flag & ST_RDONLY) ||
        statfs("/odm", &odm_fs) || (unsigned long)odm_fs.f_type != 0xe0f5e1e2UL ||
        stat("/odm", &odm_st) || stat("/system", &system_st) || odm_st.st_dev == system_st.st_dev) {
        errno = ENODEV; fatal("mounted read-only stock ODM EROFS required");
    }
    root_bind("/odm", "/odm", true);
    log_line("bound existing stock ODM read-only; no ODM image in DSU package");
    char target[512];
    dsu_bind("/chip_prod", "/chip_prod", "chip_prod_gsi");
    dsu_bind("/sys_prod", "/sys_prod", "sys_prod_gsi");
    dsu_bind("/cust", "/cust", "cust_gsi");
    dsu_bind("/patch_hw", "/patch_hw", "patch_hw_gsi");
    root_link("system/bin", "/bin");
    root_link("system/etc", "/etc");
    root_link("system/lib", "/lib");
    root_link("system/lib64", "/lib64");
    root_link("system/bin/init", "/init");
    root_link("vendor", "/chipset");
    root_mount("tmpfs", "/dev", "tmpfs", MS_NOSUID, "mode=0755");
    root_directory("/dev/block", 0700);
    root_directory("/dev/unix", 0755);
    // The native formatter runs before the complete device namespace exists.
    // It needs real random/null devices for UUID generation and normal stdio.
    const struct { const char *path; unsigned minor; } basics[] = {
        {"/dev/null", 3}, {"/dev/zero", 5}, {"/dev/random", 8}, {"/dev/urandom", 9}};
    for (size_t i = 0; i < sizeof(basics) / sizeof(basics[0]); ++i) {
        rootpath(target, sizeof(target), basics[i].path);
        if (mknod(target, S_IFCHR | 0666, makedev(1, basics[i].minor))) fatal(basics[i].path);
    }
    // Use only the exact DSU mapping. Decide first-boot initialization from
    // the installer header before mounting; mount errors never authorize mkfs.
    rootpath(target, sizeof(target), "/dev/block/userdata_gsi");
    if (mknod(target, S_IFBLK | 0600, data)) fatal("DSU data node");
    root_mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    root_mount("sysfs", "/sys", "sysfs", MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mount_native_selinux();
    char data_target[512];
    rootpath(data_target, sizeof(data_target), "/data");
    int data_fd = open(target, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (data_fd < 0) fatal("pin DSU data descriptor");
    verify_data_node(data_fd, data);
    log_line("stage=data-mount; exact userdata_gsi (%u:%u)", major(data), minor(data));
    enum data_kind data_kind = inspect_data_header(data_fd);
    if (data_kind == DATA_READ_ERROR) fatal("read DSU data header");
    if (data_kind == DATA_UNKNOWN) { errno = EINVAL; fatal("unrecognized DSU data header; no formatting"); }
    if (data_kind == DATA_INSTALLER_WIPED) {
        log_line("stage=first-data-initialization; Android DSU installer-wiped header detected");
        initialize_new_dsu_data(target, data);
        verify_data_node(data_fd, data);
        data_kind = DATA_EXT4;
    } else {
        log_line("existing DSU %s detected; mounting without formatting", data_kind == DATA_EXT4 ? "ext4" : "f2fs");
    }
    char pinned_data[64];
    snprintf(pinned_data, sizeof(pinned_data), "/proc/self/fd/%d", data_fd);
    const char *data_filesystem = data_kind == DATA_EXT4 ? "ext4" : "f2fs";
    if (mount(target, data_target, data_filesystem, MS_NOSUID | MS_NODEV, NULL)) {
        log_line("DSU %s mount by node failed: errno=%d; retrying same mapping by pinned descriptor", data_filesystem, errno);
        verify_data_node(data_fd, data);
        if (mount(pinned_data, data_target, data_filesystem, MS_NOSUID | MS_NODEV, NULL))
            fatal("DSU filesystem mount failed; no formatting on mount failure");
    }
    close(data_fd);
    if (unlink(target)) fatal("hide DSU block node");
    rootpath(target, sizeof(target), "/data/hmos-port");
    directory(target, 0700);
    rootpath(target, sizeof(target), "/data/hmos-port/bootstrap.log");
    bootlog = open(target, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (bootlog < 0) fatal("persistent bootstrap log");
    log_line("persistent data mounted exclusively from userdata_gsi");
    log_line("stage=stock-modules");
    load_stock_modules();
    log_line("stage=device-namespace");
    device_namespace();
    const char *brightness = "/sys/class/backlight/panel0-backlight/brightness";
    if (!access(brightness, F_OK)) {
        if (chown(brightness, 0, 1003) || chmod(brightness, 0660)) fatal("backlight ownership");
        root_bind(brightness, brightness, false);
    }
    // Curated startup files mask every auto-scanned vendor/chipset init tree.
    root_bind("/hmos/startup/init.cfg", "/system/etc/init.cfg", true);
    root_bind("/hmos/startup/system-init", "/system/etc/init", true);
    root_bind("/hmos/init-modules", "/system/lib64/init", true);
    root_bind("/hmos/startup/vendor-init", "/vendor/etc/init", true);
    mask_extra_init();
    root_bind("/hmos/startup/hdf_default.hcb", "/chip_prod/etc/hdfconfig/hdf_default.hcb", true);
    root_bind("/hmos/policy/policy.31", "/vendor/etc/selinux/prebuild_sepolicy/policy.31", true);
    root_bind("/hmos/policy/developer_policy", "/vendor/etc/selinux/prebuild_sepolicy/developer_policy", true);
    root_bind("/hmos/display/libdisplay_buffer_vdi_impl.z.so", "/vendor/lib64/libdisplay_buffer_vdi_impl.z.so", true);
    root_bind("/hmos/display/libdisplay_composer_vdi_impl.z.so", "/vendor/lib64/libdisplay_composer_vdi_impl.z.so", true);
    root_bind("/hmos/ui/display_manager_config.xml", "/sys_prod/etc/window/resources/display_manager_config.xml", true);
    root_bind("/hmos/ui/hgm_policy_config.xml", "/sys_prod/etc/graphic/hgm_policy_config.xml", true);
    log_line("stage=device-filter");
    isolate_devices();
    // No automatic critical-service reboot or raw-I/O/module privilege after
    // the curated stock module load. Native storage still retains SYS_ADMIN.
    if (prctl(PR_CAPBSET_DROP, CAP_SYS_BOOT, 0, 0, 0) ||
        prctl(PR_CAPBSET_DROP, CAP_SYS_RAWIO, 0, 0, 0) ||
        prctl(PR_CAPBSET_DROP, CAP_SYS_MODULE, 0, 0, 0)) fatal("capability bounding set");
    // Expose only delegated cgroup children. The device-filter ancestor is
    // outside this cgroup namespace and outside the guest's mount tree.
    root_directory("/dev/cgroup", 0755);
    root_mount("cgroup2", "/dev/cgroup", "cgroup2", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    log_line("executing full donor init --second-stage; framework/SceneBoard target");
    pivot_native_root();
    int handoff_status = start_native_recorder();
    // Only the close-on-exec error pipe may survive until execve. The native
    // graph inherits neither metadata logs nor old-root/block descriptors.
    kmsg = bootlog = metalog = -1;
    if (handoff_status >= 0 && handoff_status != 3) {
        if (dup3(handoff_status, 3, O_CLOEXEC) < 0) fatal("handoff status descriptor");
        handoff_status = 3;
    }
    if (syscall(__NR_close_range, handoff_status >= 0 ? 4U : 3U, ~0U, 0)) fatal("close inherited descriptors");
    kmsg = bootlog = metalog = -1;
    int null = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (null < 0) fatal("guest null device");
    if (dup2(null, 0) < 0) fatal("guest stdin");
    if (null > 2) close(null);
    // Adapted from the giuliac fork: keep native init diagnostics visible.
    // Reopen inside the guest after closing all old-root descriptors.
    int output = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (output < 0) output = open("/data/hmos-port/native-init.log", O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (output < 0) fatal("native init diagnostic stream");
    if (dup2(output, 1) < 0 || dup2(output, 2) < 0) fatal("guest diagnostic stdio");
    if (output > 2) close(output);
    // No old-root directory fd or block fd is inherited by the native graph.
    char *const init_args[] = {"/system/bin/init", "--second-stage", "0", NULL};
    char *const environment[] = {"PATH=/system/bin:/vendor/bin", "OHOS_MESA_SOFTWARE=true", NULL};
    execve(init_args[0], init_args, environment);
    int exec_error = errno;
    if (handoff_status >= 0) { write(handoff_status, &exec_error, sizeof(exec_error)); close(handoff_status); }
    kmsg = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    errno = exec_error;
    fatal("native init exec");
}
