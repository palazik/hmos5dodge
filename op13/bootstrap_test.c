/* SPDX-License-Identifier: Apache-2.0
 * Tests execute pure guard/scanner functions only. No mount, BPF syscall,
 * device operation, formatter or native init is executed by this program.
 */
#define main bootstrap_entry
#include "dsu_bootstrap.c"
#undef main
#include <assert.h>

static void fixture_path(char path[512], const char *base, const char *suffix)
{
    assert(snprintf(path, 512, "%s/%s", base, suffix) < 512);
}

static void fixture_directory(const char *base, const char *suffix)
{
    char path[512]; fixture_path(path, base, suffix); assert(!mkdir(path, 0700));
}

static void fixture_text(const char *base, const char *suffix, const char *text)
{
    char path[512]; fixture_path(path, base, suffix);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    assert(fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    assert(!close(fd));
}

static void mapper_fixtures(void)
{
    char directory[] = "/tmp/hmos-mapper-XXXXXX", path[512];
    assert(mkdtemp(directory));
    fixture_directory(directory, "dm-0"); fixture_directory(directory, "dm-0/dm");
    fixture_text(directory, "dm-0/dm/name", "system_gsi\n");
    fixture_text(directory, "dm-0/dev", "254:42\n");
    assert(find_exact_dm_at(directory, "system_gsi") == makedev(254, 42));
    assert(!find_exact_dm_at(directory, "userdata_gsi") && errno == ENODEV);
    fixture_directory(directory, "dm-1"); fixture_directory(directory, "dm-1/dm");
    fixture_text(directory, "dm-1/dm/name", "system_gsi\n");
    fixture_text(directory, "dm-1/dev", "254:43\n");
    assert(!find_exact_dm_at(directory, "system_gsi") && errno == EEXIST);
    fixture_text(directory, "dm-1/dm/name", "userdata_backup\n");
    assert(!find_exact_dm_at(directory, "userdata_gsi") && errno == ENODEV);
    assert(find_exact_dm_at(directory, "system_gsi") == makedev(254, 42));
    fixture_path(path, directory, "dm-0");
    assert(dm_identity_matches_at(path, "system_gsi"));
    assert(!dm_identity_matches_at(path, "vendor_gsi"));
    fixture_directory(directory, "dm-2"); fixture_directory(directory, "dm-2/dm");
    fixture_directory(directory, "dm-2/slaves");
    fixture_text(directory, "dm-2/dm/name", "verity-layer\n");
    fixture_path(path, directory, "dm-2/slaves/dm-0"); assert(!symlink("../../dm-0", path));
    fixture_path(path, directory, "dm-2");
    assert(dm_identity_matches_at(path, "system_gsi"));
    fixture_path(path, directory, "dm-2/slaves/dm-1"); assert(!symlink("../../dm-1", path));
    fixture_path(path, directory, "dm-2");
    assert(!dm_identity_matches_at(path, "system_gsi")); // mixed backing devices rejected
    const char *files[] = {"dm-0/dm/name", "dm-0/dev", "dm-1/dm/name", "dm-1/dev",
        "dm-2/dm/name", "dm-2/slaves/dm-0", "dm-2/slaves/dm-1"};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        fixture_path(path, directory, files[i]); assert(!unlink(path));
    }
    const char *dirs[] = {"dm-0/dm", "dm-0", "dm-1/dm", "dm-1", "dm-2/slaves", "dm-2/dm", "dm-2"};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        fixture_path(path, directory, dirs[i]); assert(!rmdir(path));
    }
    assert(!rmdir(directory));
}

static void delayed_device_fixture(void)
{
    char directory[] = "/tmp/hmos-device-ready-XXXXXX", path[512];
    assert(mkdtemp(directory));
    fixture_path(path, directory, "dev");
    unsigned maj = 0, min = 0;
    fixture_text(directory, "dev", "226:0 junk\n");
    assert(!device_numbers(path, &maj, &min));
    fixture_text(directory, "dev", "0:0\n");
    assert(!device_numbers(path, &maj, &min));
    assert(!unlink(path));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 50000000};
        nanosleep(&delay, NULL);
        fixture_text(directory, "dev", "226:0\n");
        _exit(0);
    }
    assert(wait_for_device_numbers(path, &maj, &min, 10));
    assert(maj == 226 && min == 0);
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(!unlink(path));
    assert(!wait_for_device_numbers(path, &maj, &min, 2) && errno == ENODEV);
    assert(!rmdir(directory));
}

static void recorder_fixture(bool exec_failed, size_t limit)
{
    char path[] = "/tmp/hmos-recorder-XXXXXX";
    int output = mkstemp(path), kernel[2], status[2];
    assert(output >= 0); unlink(path);
    assert(!pipe2(kernel, O_NONBLOCK | O_CLOEXEC));
    assert(!pipe2(status, O_NONBLOCK | O_CLOEXEC));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(kernel[0]); close(status[0]);
        const char message[] = "6,12,1234,-;[pid=1][Init] boot job pre-init start";
        assert(write(kernel[1], message, sizeof(message) - 1) == sizeof(message) - 1);
        if (exec_failed) {
            execl("/this-hmos-recorder-test-executable-does-not-exist", "missing", NULL);
            int error = errno;
            assert(write(status[1], &error, 1) == 1); // partial pipe reads are valid
            struct timespec delay = {.tv_nsec = 20000000};
            nanosleep(&delay, NULL);
            assert(write(status[1], (char *)&error + 1, sizeof(error) - 1) == sizeof(error) - 1);
            close(status[1]);
        } else {
            // Use a real successful exec to exercise O_CLOEXEC EOF, rather
            // than pretending a text message proves loader acceptance.
            execl("/bin/true", "true", NULL);
            _exit(119);
        }
        close(kernel[1]); _exit(0);
    }
    close(kernel[1]); close(status[1]);
    int result;
    if (limit <= 512)
        assert(waitpid(child, &result, 0) == child && WIFEXITED(result) && WEXITSTATUS(result) == 0);
    native_record_loop(kernel[0], output, status[0], 150, limit);
    close(kernel[0]); close(status[0]);
    if (limit > 512)
        assert(waitpid(child, &result, 0) == child && WIFEXITED(result) && WEXITSTATUS(result) == 0);
    struct stat st;
    assert(!fstat(output, &st) && st.st_size <= (off_t)limit);
    char text[4096] = {0};
    assert(pread(output, text, sizeof(text) - 1, 0) == st.st_size);
    if (limit > 512) {
        assert(strstr(text, "boot job pre-init start"));
        assert(strstr(text, exec_failed ? "execve FAILED: errno=2" : "status pipe closed without error"));
    } else assert(st.st_size == (off_t)limit);
    close(output);
}

static int evaluate(const struct bpf_insn *instructions, size_t count,
                    uint32_t type, uint32_t major_number, uint32_t minor_number)
{
    uint64_t registers[11] = {0};
    uint32_t context[] = {type, major_number, minor_number};
    size_t pc = 0, steps = 0;
    while (pc < count && steps++ < 2048) {
        const struct bpf_insn *instruction = instructions + pc++;
        unsigned op = instruction->code;
        if (op == (BPF_LDX | BPF_W | BPF_MEM)) {
            assert(instruction->src_reg == 1 && instruction->off % 4 == 0);
            assert(instruction->off >= 0 && instruction->off <= 8);
            registers[instruction->dst_reg] = context[instruction->off / 4];
        } else if (op == (BPF_ALU | BPF_AND | BPF_K)) {
            registers[instruction->dst_reg] = (uint32_t)registers[instruction->dst_reg] & instruction->imm;
        } else if (op == (BPF_ALU64 | BPF_MOV | BPF_K)) {
            registers[instruction->dst_reg] = instruction->imm;
        } else if (op == (BPF_JMP | BPF_JEQ | BPF_K)) {
            if (registers[instruction->dst_reg] == (uint32_t)instruction->imm) pc += instruction->off;
        } else if (op == (BPF_JMP | BPF_JNE | BPF_K)) {
            if (registers[instruction->dst_reg] != (uint32_t)instruction->imm) pc += instruction->off;
        } else if (op == (BPF_JMP | BPF_EXIT)) {
            return registers[0];
        } else {
            assert(!"unexpected BPF instruction");
        }
    }
    assert(!"filter did not terminate");
    return -1;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--formatted-image")) {
        int image = open(argv[2], O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        struct stat st;
        assert(image >= 0 && !fstat(image, &st) && S_ISREG(st.st_mode));
        assert(inspect_data_header(image) == DATA_EXT4);
        close(image);
        puts("PASS real native-formatter output is recognized as existing ext4, not fresh data");
        return 0;
    }
    assert(argc == 1);
    mapper_fixtures();
    delayed_device_fixture();
    assert(preserve_boot_permissive("0", "console=tty0 androidboot.selinux=permissive"));
    assert(!preserve_boot_permissive("1", "console=tty0"));
    assert(!preserve_boot_permissive("0", "console=tty0 enforcing=1"));
    assert(preserve_boot_permissive("0", "console=tty0 enforcing=0"));
    assert(preserve_boot_permissive("0", "console=tty0 androidboot.enforcing=1"));
    recorder_fixture(false, 4095);
    recorder_fixture(true, 4095);
    recorder_fixture(false, 32);
    char options[256];
    assert(drm_boot_options("console=tty0 msm_drm.dsi_display0=qcom,panel_A:config0: msm_drm.modeset=0 unrelated.key=x",
        options, sizeof(options)) == 1);
    assert(!strcmp(options, "dsi_display0=qcom,panel_A:config0:"));
    assert(drm_boot_options("msm_drm.dsi_display0=\"panel with spaces\" msm_drm.dsi_display1=secondary",
        options, sizeof(options)) == 2);
    assert(!strcmp(options, "dsi_display0=\"panel with spaces\" dsi_display1=secondary"));
    assert(drm_boot_options("msm_drm.dsi_display00=bad other.dsi_display0=bad", options, sizeof(options)) == 0);
    assert(!options[0]);
    assert(drm_boot_options("msm_drm.dsi_display0=toolong", options, 8) == -1 && errno == E2BIG);
    assert(drm_boot_options("msm_drm.dsi_display0=\"unterminated", options, sizeof(options)) == -1 && errno == EINVAL);
    assert(dm_name_valid("dm-0") && dm_name_valid("dm-987"));
    assert(!dm_name_valid("dm-") && !dm_name_valid("userdata") && !dm_name_valid("dm-1/../sda"));
    assert(module_path_valid("/vendor_dlkm/lib/modules/msm_drm.ko"));
    assert(module_path_valid("/system_dlkm/lib/modules/kernel/crypto/example.ko"));
    assert(module_path_valid("/hmos/modules/qcom_scm.ko"));
    assert(!module_path_valid("/dev/block/sda"));
    assert(!module_path_valid("/vendor_dlkm/lib/modules/../../bad.ko"));
    assert(!module_path_valid("/vendor_dlkm/lib/modules//bad.ko"));
    assert(!module_path_valid("/vendor_dlkm/lib/modules/example.so"));
    devices[0] = (struct device){226, 0};
    devices[1] = (struct device){511, 7};
    device_count = 2;
    struct bpf_insn program[1024];
    size_t count = build_device_filter(program);
    for (uint32_t access = 1; access <= 7; ++access) {
        assert(evaluate(program, count, (access << 16) | 2, 226, 0) == 1);
        assert(evaluate(program, count, (access << 16) | 2, 511, 7) == 1);
        assert(evaluate(program, count, (access << 16) | 2, 511, 8) == 0);
        assert(evaluate(program, count, (access << 16) | 2, 1, 1) == 0); // /dev/mem
        assert(evaluate(program, count, (access << 16) | 2, 10, 237) == 0); // loop-control
        assert(evaluate(program, count, (access << 16) | 2, 136, 200) == 1);
        assert(evaluate(program, count, (access << 16) | 2, 143, 200) == 1);
        assert(evaluate(program, count, (access << 16) | 2, 144, 0) == 0);
        for (uint32_t major_number = 0; major_number <= 511; ++major_number) {
            assert(evaluate(program, count, (access << 16) | 1, major_number, 0) == 0);
            assert(evaluate(program, count, (access << 16) | 1, major_number, 7) == 0);
        }
    }
    char path[] = "/tmp/hmos-data-header-XXXXXX";
    int file = mkstemp(path);
    assert(file >= 0);
    unlink(path);
    assert(!ftruncate(file, 131073));
    assert(inspect_data_header(file) == DATA_INSTALLER_WIPED);
    assert(pwrite(file, "X", 1, 131072) == 1);
    assert(inspect_data_header(file) == DATA_INSTALLER_WIPED); // normal DSU allocation need not have a zero tail
    assert(pwrite(file, "X", 1, 4095) == 1);
    assert(inspect_data_header(file) == DATA_UNKNOWN); // partial/unknown header must not initialize
    assert(pwrite(file, "\0", 1, 4095) == 1);
    assert(pwrite(file, "\x53\xef", 2, 1080) == 2);
    assert(inspect_data_header(file) == DATA_EXT4); // otherwise-zero prefix is still an existing FS
    assert(pwrite(file, "\0\0", 2, 1080) == 2);
    assert(pwrite(file, "\x10\x20\xf5\xf2", 4, 1024) == 4);
    assert(inspect_data_header(file) == DATA_F2FS);
    assert(pwrite(file, "\0\0\0\0", 4, 1024) == 4);
    assert(pwrite(file, "X", 1, 0) == 1);
    assert(inspect_data_header(file) == DATA_UNKNOWN);
    assert(!ftruncate(file, 4095));
    assert(inspect_data_header(file) == DATA_READ_ERROR && errno == EIO);
    close(file);
    assert(inspect_data_header(-1) == DATA_READ_ERROR && errno == EBADF);
    puts("PASS exact mapper identities, duplicate/mixed mapping rejection, device-filter decisions and DSU wiped/ext4/f2fs/unknown header classification; kernel mounts/attachment not tested");
    return 0;
}
