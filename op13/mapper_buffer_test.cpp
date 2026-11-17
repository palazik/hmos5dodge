// SPDX-License-Identifier: Apache-2.0
// Exercise the actual donor mapper through its HDI ABI, using a memfd fixture.
// This verifies CPU mapping/lifetime, not DMA-BUF allocation or display scanout.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "display/buffer/v1_2/imapper.h"

using OHOS::HDI::Base::NativeBuffer;
using OHOS::sptr;
using Mapper = OHOS::HDI::Display::Buffer::V1_2::IMapper;

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    void *hdi = dlopen("libhdi.z.so", RTLD_NOW | RTLD_LOCAL);
    assert(hdi);
    auto load = reinterpret_cast<void *(*)(const char *, const char *)>(dlsym(hdi, "LoadHdiImpl"));
    assert(load);
    for (const char *descriptor : {"ohos.hdi.display.buffer.v1_0.IMapper", "ohos.hdi.display.buffer.v1_2.IMapper"}) {
        auto *mapper = static_cast<Mapper *>(load(descriptor, "service"));
        assert(mapper);
        uint32_t major = 0, minor = 0;
        assert(mapper->GetVersion(major, minor) == 0 && major == 1);
        printf("mapper %s reports version %u.%u\n", descriptor, major, minor);
        auto *handle = static_cast<BufferHandle *>(calloc(1, sizeof(BufferHandle)));
        assert(handle);
        handle->fd = memfd_create("op13-mapper-fixture", MFD_CLOEXEC);
        assert(handle->fd >= 0 && ftruncate(handle->fd, 4096) == 0);
        handle->size = 4096;
        handle->width = 16;
        handle->height = 16;
        handle->stride = 64;
        sptr<NativeBuffer> wrapped = new NativeBuffer;
        wrapped->SetBufferHandle(handle, false);
        assert(mapper->Mmap(wrapped) == 0 && handle->virAddr);
        memcpy(handle->virAddr, "mapper-ABI", 11);
        char bytes[11] = {};
        assert(pread(handle->fd, bytes, sizeof(bytes), 0) == sizeof(bytes));
        assert(memcmp(bytes, "mapper-ABI", sizeof(bytes)) == 0);
        assert(mapper->Unmap(wrapped) == 0 && handle->virAddr == nullptr);
        printf("PASS native mapper CPU mapping/readback/unmap: %s\n", descriptor);
        assert(mapper->FreeMem(wrapped) != 0 && wrapped->GetBufferHandle() == handle);
        int savedFd = handle->fd;
        assert(fcntl(savedFd, F_GETFD) >= 0);
        wrapped->SetBufferHandle(handle, true);
        assert(mapper->FreeMem(wrapped) == 0);
        assert(wrapped->GetBufferHandle() == nullptr);
        errno = 0;
        assert(fcntl(savedFd, F_GETFD) < 0 && errno == EBADF);
        wrapped = nullptr;
        printf("PASS native mapper handle release: %s\n", descriptor);
    }
    return 0;
}
