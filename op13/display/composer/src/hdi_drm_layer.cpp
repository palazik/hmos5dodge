/*
 * Copyright (c) 2021-2023 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <linux/dma-buf.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <drm_fourcc.h>
#include <sys/ioctl.h>
#include "hdi_drm_layer.h"
#include <mutex>
#include <vector>
#include <sys/stat.h>
#include <cinttypes>
#include <cerrno>
#include "display_log.h"
#include "drm_device.h"

namespace OHOS {
namespace HDI {
namespace DISPLAY {
DrmGemBuffer::DrmGemBuffer(int drmFd, HdiLayerBuffer &hdl) : mDrmFd(drmFd)
{
    DISPLAY_LOGD();
    Init(mDrmFd, hdl);
}

/*
 * sweet: render_service cycles through the same 3-4 buffers. Importing every
 * frame (PRIME import + AddFB2, RmFB + GEM close two frames later) costs time
 * on the commit path, so framebuffers are cached by dma-buf identity and only
 * released when the cache is full.
 */
namespace {
struct FbCacheEntry {
    uint64_t ino;
    uint32_t w, h, stride, fmt;
    uint32_t gem, fb;
    int refs;
    uint64_t lastUse;
};
std::mutex g_fbCacheLock;
std::vector<FbCacheEntry> g_fbCache;
uint64_t g_fbUse;
constexpr size_t FB_CACHE_MAX = 8;

void ReleaseEntry(int drmFd, const FbCacheEntry &e)
{
    drmModeRmFB(drmFd, e.fb);
    struct drm_gem_close gemClose = { 0 };
    gemClose.handle = e.gem;
    drmIoctl(drmFd, DRM_IOCTL_GEM_CLOSE, &gemClose);
}

/*
 * Revision 31: scanout channel order.  render_service's own screenshot of
 * revision 30 has correct colours (blue wallpaper, orange Video icon) and the
 * GPU writes R,G,B,A bytes into RGBA_8888 buffers, yet the panel shows red and
 * blue exchanged (light-blue wallpaper as peach/pink, orange as blue) while
 * the buffer is scanned out as DRM ABGR8888.  So scan RGBA/RGBX buffers out as
 * ARGB/XRGB8888, which cancels the exchange.
 *
 * /dev/shm/op13-scanout (re-read once a second) overrides this for on-device
 * checks: "native" = plain table mapping, "swap" = the default above, or a
 * four-character DRM fourcc such as "XR24" used for every 32-bit RGB buffer.
 */
uint32_t ScanoutFormat(uint32_t native)
{
    /* Revision 32: native by default; the panel path rotates channels, which the
     * composer now undoes with the DSPP PCC matrix (hdi_drm_composition.cpp). */
    static uint32_t overrideMode = 0; /* 0 native, 1 swap, 2 explicit */
    static uint32_t explicitFourcc = 0;
    static int64_t lastCheck = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (now - lastCheck > 1000) {
        lastCheck = now;
        uint32_t mode = 0, fourcc = 0;
        char text[16] = {0};
        int fd = open("/dev/shm/op13-scanout", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            ssize_t n = read(fd, text, sizeof(text) - 1);
            close(fd);
            if (n > 0) {
                text[strcspn(text, "\r\n ")] = 0;
                if (!strcmp(text, "native")) {
                    mode = 0;
                } else if (!strcmp(text, "swap")) {
                    mode = 1;
                } else if (strlen(text) == 4) {
                    mode = 2;
                    fourcc = fourcc_code(text[0], text[1], text[2], text[3]);
                }
            }
        }
        if (mode != overrideMode || fourcc != explicitFourcc) {
            DISPLAY_LOGI("scanout channel order: %{public}s", mode == 0 ? "native" : mode == 1 ? "swap R/B" : text);
        }
        overrideMode = mode;
        explicitFourcc = fourcc;
    }
    bool rgb32 = native == DRM_FORMAT_ABGR8888 || native == DRM_FORMAT_XBGR8888 ||
                 native == DRM_FORMAT_ARGB8888 || native == DRM_FORMAT_XRGB8888;
    if (!rgb32 || overrideMode == 0) {
        return native;
    }
    if (overrideMode == 2 && explicitFourcc != 0) {
        return explicitFourcc;
    }
    switch (native) {
        case DRM_FORMAT_ABGR8888: return DRM_FORMAT_ARGB8888;
        case DRM_FORMAT_XBGR8888: return DRM_FORMAT_XRGB8888;
        case DRM_FORMAT_ARGB8888: return DRM_FORMAT_ABGR8888;
        case DRM_FORMAT_XRGB8888: return DRM_FORMAT_XBGR8888;
        default: return native;
    }
}
} // namespace

void DrmGemBuffer::Init(int drmFd, HdiLayerBuffer &hdl)
{
    int ret;
    const int MAX_COUNT = 4;
    uint32_t pitches[MAX_COUNT] = {0};
    uint32_t gemHandles[MAX_COUNT] = {0};
    uint32_t offsets[MAX_COUNT] = {0};
    DISPLAY_CHK_RETURN_NOT_VALUE((drmFd < 0), DISPLAY_LOGE("can not init drmfd %{public}d", drmFd));
    /* System-heap buffers are CPU-cached: clean the CPU cache before the
     * display controller reads this frame (no-op for uncached buffers). */
    struct dma_buf_sync sync = {DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW};
    if (ioctl(hdl.GetFb(), DMA_BUF_IOCTL_SYNC, &sync) != 0) {
        static bool logged;
        if (!logged) { logged = true; DISPLAY_LOGE("dma-buf sync before scanout failed: %{public}d", errno); }
    }
    mDrmFormat = ScanoutFormat(DrmDevice::ConvertToDrmFormat(static_cast<PixelFormat>(hdl.GetFormat())));
    struct stat st;
    uint64_t ino = (fstat(hdl.GetFb(), &st) == 0) ? (uint64_t)st.st_ino : 0;

    std::lock_guard<std::mutex> lock(g_fbCacheLock);
    if (ino != 0) {
        for (auto &e : g_fbCache) {
            if (e.ino == ino && e.w == (uint32_t)hdl.GetWight() && e.h == (uint32_t)hdl.GetHeight() &&
                e.stride == (uint32_t)hdl.GetStride() && e.fmt == mDrmFormat) {
                e.refs++;
                e.lastUse = ++g_fbUse;
                mGemHandle = e.gem;
                mFdId = e.fb;
                mCached = true;
                return;
            }
        }
    }
    ret = drmPrimeFDToHandle(drmFd, hdl.GetFb(), &mGemHandle);
    DISPLAY_CHK_RETURN_NOT_VALUE((ret != 0), DISPLAY_LOGE("can not get handle errno %{public}d", errno));

    pitches[0] = hdl.GetStride();
    gemHandles[0] = mGemHandle;
    offsets[0] = 0;
    ret = drmModeAddFB2(drmFd, hdl.GetWight(), hdl.GetHeight(), mDrmFormat, gemHandles, pitches, offsets, &mFdId, 0);
    DISPLAY_CHK_RETURN_NOT_VALUE((ret != 0), DISPLAY_LOGE("can not add fb errno %{public}d", errno));
    if (ino == 0) {
        return;
    }
    if (g_fbCache.size() >= FB_CACHE_MAX) {
        /* drop the least recently used framebuffer nobody holds */
        size_t victim = g_fbCache.size();
        for (size_t i = 0; i < g_fbCache.size(); i++) {
            if (g_fbCache[i].refs == 0 && (victim == g_fbCache.size() ||
                g_fbCache[i].lastUse < g_fbCache[victim].lastUse)) {
                victim = i;
            }
        }
        if (victim == g_fbCache.size()) {
            return; /* everything busy: this one stays uncached */
        }
        if (g_fbCache[victim].gem != mGemHandle) {
            ReleaseEntry(drmFd, g_fbCache[victim]);
        } else {
            drmModeRmFB(drmFd, g_fbCache[victim].fb);
        }
        g_fbCache.erase(g_fbCache.begin() + victim);
    }
    g_fbCache.push_back(FbCacheEntry { ino, (uint32_t)hdl.GetWight(), (uint32_t)hdl.GetHeight(),
        (uint32_t)hdl.GetStride(), mDrmFormat, mGemHandle, mFdId, 1, ++g_fbUse });
    mCached = true;
}

DrmGemBuffer::~DrmGemBuffer()
{
    DISPLAY_LOGD();
    if (mCached) {
        std::lock_guard<std::mutex> lock(g_fbCacheLock);
        for (auto &e : g_fbCache) {
            if (e.fb == mFdId) {
                e.refs--;
                break;
            }
        }
        return;
    }
    if (mFdId) {
        if (drmModeRmFB(mDrmFd, mFdId)) {
            DISPLAY_LOGE("can not free fdid %{public}d errno %{public}d", mFdId, errno);
        }
    }

    if (mGemHandle) {
        struct drm_gem_close gemClose = { 0 };
        gemClose.handle = mGemHandle;
        if (drmIoctl(mDrmFd, DRM_IOCTL_GEM_CLOSE, &gemClose)) {
            DISPLAY_LOGD("can not free gem handle %{public}d errno : %{public}d", mGemHandle, errno);
        }
    }
}

bool DrmGemBuffer::IsValid()
{
    DISPLAY_LOGD();
    return (mGemHandle != INVALID_DRM_ID) && (mFdId != INVALID_DRM_ID);
}

DrmGemBuffer *HdiDrmLayer::GetGemBuffer()
{
    DISPLAY_LOGD();
    std::unique_ptr<DrmGemBuffer> ptr = std::make_unique<DrmGemBuffer>(DrmDevice::GetDrmFd(), *GetCurrentBuffer());
    mLastBuffer = std::move(mCurrentBuffer);
    mCurrentBuffer = std::move(ptr);
    return mCurrentBuffer.get();
}
} // namespace OHOS
} // namespace HDI
} // namespace DISPLAY
