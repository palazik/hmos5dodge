// SPDX-License-Identifier: Apache-2.0
// Load the actual donor DSO with NOW relocation, then exercise its public ABI.
// No DRM access, real buffer allocation, device node or phone is involved.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <vector>

template<typename T> T Symbol(void *library, const char *name)
{
    void *address = dlsym(library, name);
    if (!address) {
        fprintf(stderr, "missing %s: %s\n", name, dlerror());
    }
    assert(address);
    return reinterpret_cast<T>(address);
}

int main()
{
    void *library = dlopen("libgralloc_priv.z.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "donor gralloc load failed: %s\n", dlerror());
        return 1;
    }
    // Layout checks in the unmodified donor public wrappers use offsets 0x30,
    // 0x34 and 0x84.  This is an isolated synthetic handle, never a DMA buffer.
    alignas(16) unsigned char handle[0x138] = {};
    auto word = [&](size_t offset, uint32_t value) { memcpy(handle + offset, &value, sizeof(value)); };
    word(0x30, 0); // reserveFds
    word(0x34, 64); // Huawei private handle reserveInts
    word(0x84, 0x03141592); // private handle magic
    auto isPrivate = Symbol<bool (*)(const void *)>(library,
        "_ZN7gralloc14GrallocPrivItf12IsPrivHandleEP12BufferHandle");
    assert(isPrivate(handle));
    // Bit 31 makes the donor wrapper propagate allocation failure instead of
    // treating the attribute buffer as optional.  Never report allocation success.
    word(0x38, 1u << 31);
    auto alloc = Symbol<int (*)(void *)>(library,
        "_ZN7gralloc14GrallocPrivItf15AllocAttrBufferEP12BufferHandle");
    assert(alloc(handle) == -1);
    std::vector<uint8_t> bytes = {3, 1, 4};
    auto set = Symbol<int (*)(const void *, uint32_t, const std::vector<uint8_t> &)>(library,
        "_ZN7gralloc14GrallocPrivItf11SetMetadataEPK12BufferHandlejRKNSt3__h6vectorIhNS4_9allocatorIhEEEE");
    auto get = Symbol<int (*)(const void *, uint32_t, std::vector<uint8_t> &)>(library,
        "_ZN7gralloc14GrallocPrivItf11GetMetadataEPK12BufferHandlejRNSt3__h6vectorIhNS4_9allocatorIhEEEE");
    auto list = Symbol<int (*)(const void *, std::vector<uint32_t> &)>(library,
        "_ZN7gralloc14GrallocPrivItf16ListMetadataKeysEPK12BufferHandleRNSt3__h6vectorIjNS4_9allocatorIjEEEE");
    auto erase = Symbol<int (*)(const void *, uint32_t)>(library,
        "_ZN7gralloc14GrallocPrivItf16EraseMetadataKeyEPK12BufferHandlej");
    std::vector<uint32_t> keys = {42};
    assert(set(handle, 42, bytes) == -5);
    assert(get(handle, 42, bytes) == -5 && bytes == std::vector<uint8_t>({3, 1, 4}));
    assert(list(handle, keys) == -5 && keys == std::vector<uint32_t>({42}));
    assert(erase(handle, 42) == -5);
    word(0x34, 0); // ordinary OHOS BufferHandle: not a Huawei private handle
    assert(!isPrivate(handle));
    assert(set(handle, 42, bytes) == -4);
    dlclose(library);
    puts("PASS donor gralloc NOW relocation; all five private operations reject unsupported layout; ordinary handle rejected by private API");
    return 0;
}
