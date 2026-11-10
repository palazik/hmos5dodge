// SPDX-License-Identifier: Apache-2.0
// Optional Huawei private-gralloc ABI imported by the donor libgralloc_priv.
// Its five methods are static (verified against the donor's AArch64 callers).
// Our DRM allocator emits standard OHOS BufferHandles, not Huawei PrivHandles.
// Do not read that incompatible layout, invent a metadata fd, or report success.
// Standard IDisplayBufferVdi metadata operations remain separate.
#define SWEET_LOG_TAG "OP13Gralloc"
#include <cstdint>
#include <mutex>
#include <vector>
#include "sweet_log.h"

namespace gralloc {
struct PrivHandle;

class GrallocAttrBufferMgr {
public:
    static int32_t AllocAttrBuffer(PrivHandle &);
    static int32_t SetMetadata(PrivHandle &, uint32_t, const std::vector<uint8_t> &);
    static int32_t GetMetadata(PrivHandle &, uint32_t, std::vector<uint8_t> &);
    static int32_t ListMetadataKeys(PrivHandle &, std::vector<uint32_t> &);
    static int32_t EraseMetadataKey(PrivHandle &, uint32_t);
};

static int32_t UnsupportedPrivateMetadata()
{
    static std::once_flag warning;
    std::call_once(warning, [] {
        SLOGE("Huawei private gralloc metadata is unsupported by the OP13 DRM allocator");
    });
    return -5; // DISPLAY_NOT_SUPPORT, not successful allocation or metadata access
}

int32_t GrallocAttrBufferMgr::AllocAttrBuffer(PrivHandle &)
{
    return UnsupportedPrivateMetadata();
}
int32_t GrallocAttrBufferMgr::SetMetadata(PrivHandle &, uint32_t, const std::vector<uint8_t> &)
{
    return UnsupportedPrivateMetadata();
}
int32_t GrallocAttrBufferMgr::GetMetadata(PrivHandle &, uint32_t, std::vector<uint8_t> &)
{
    return UnsupportedPrivateMetadata();
}
int32_t GrallocAttrBufferMgr::ListMetadataKeys(PrivHandle &, std::vector<uint32_t> &)
{
    return UnsupportedPrivateMetadata();
}
int32_t GrallocAttrBufferMgr::EraseMetadataKey(PrivHandle &, uint32_t)
{
    return UnsupportedPrivateMetadata();
}
} // namespace gralloc
