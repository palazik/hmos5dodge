/* SPDX-License-Identifier: Apache-2.0
 * Offline test only. The real donor LoadPolicy and config parser run under
 * ARM64 emulation; only kernel-state/load calls are replaced with counters.
 * These test symbols are never linked into the bootstrap or ROM libraries.
 */
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int mode, mode_calls, policy_calls;
int security_getenforce(void) { return mode; }
int security_setenforce(int value)
{
    assert(value == 0 || value == 1);
    mode = value; ++mode_calls;
    return 0;
}
int security_load_policy(void *data, size_t length)
{
    assert(data && length > 1024 && *(uint32_t *)data == 0xf97cff8cU);
    ++policy_calls;
    return 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2 && access("/test/namespace-marker", F_OK) == 0);
    int expected = atoi(argv[1]);
    assert(expected == 0 || expected == 1);
    void *library = dlopen("libload_policy.z.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    int (*load)(void) = (int (*)(void))dlsym(library, "LoadPolicy");
    assert(load && load() == 0);
    assert(policy_calls == 1 && mode == expected && mode_calls == expected);
    printf("PASS actual donor LoadPolicy: mode=%d, mode calls=%d, policy calls=%d (kernel calls mocked only in test)\n",
        mode, mode_calls, policy_calls);
    dlclose(library);
    return 0;
}
