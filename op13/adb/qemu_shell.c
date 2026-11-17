// SPDX-License-Identifier: Apache-2.0
// Test fixture only: execute the real ARM64 OHOS shell without global binfmt.
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    char **args = calloc((size_t)argc + 3, sizeof(char *));
    if (!args) return 125;
    args[0] = "/qemu";
    args[1] = "/system/bin/native-sh";
    for (int i = 1; i < argc; ++i) args[i + 1] = argv[i];
    execv(args[0], args);
    return 126;
}
