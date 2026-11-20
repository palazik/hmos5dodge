// SPDX-License-Identifier: Apache-2.0
/*
 * libvulkan_op13_ohos.so: Vulkan ICD shim that gives Turnip (Mesa's Adreno
 * Vulkan driver, KGSL backend) the two HarmonyOS driver extensions that
 * render_service and the HarmonyOS swapchain layer (libvulkan_swapchain.so,
 * implicit layer VK_LAYER_OHOS_surface) need:
 *
 *   VK_OHOS_native_buffer   vkCreateImage + VkNativeBufferOHOS (swapchain
 *                           images), vkGetSwapchainGrallocUsageOHOS,
 *                           vkAcquireImageOHOS, vkQueueSignalReleaseImageOHOS
 *   VK_OHOS_external_memory vkGetNativeBufferPropertiesOHOS,
 *                           VkImportNativeBufferInfoOHOS, VkExternalFormatOHOS
 *
 * Everything is translated to what Turnip already implements: dma-buf import
 * (VK_EXT_external_memory_dma_buf) of linear images with an explicit DRM
 * modifier and plane layout (VK_EXT_image_drm_format_modifier), and sync-fd
 * semaphores/fences (VK_KHR_external_{semaphore,fence}_fd).  All other calls go
 * straight to Turnip.  Turnip ignores the OHOS pNext structures it does not
 * know; the shim prepends the structures Turnip does know, which vk_find_struct
 * then finds first.
 *
 * Native buffers on this port come from the OP13 buffer VDI
 * (display/buffer/sweet_buffer_vdi.cpp): linear, pitch = align(width, 64) *
 * bytes per pixel, chroma of semi-planar YUV at pitch * height.
 */
#define VK_NO_PROTOTYPES
#include <dlfcn.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vulkan/vulkan_core.h>
#include <vulkan/vk_icd.h>

#define EXPORT __attribute__((visibility("default")))
#define TURNIP "/hmos/display/mesa/libvulkan_freedreno.so"

/* ---- HarmonyOS definitions (third_party_vulkan-headers vulkan_ohos.h) ---- */
typedef struct {
    int32_t fd, width, stride, height, size, format;
    uint64_t usage;
    void *virAddr;
    uint64_t phyAddr;
    uint32_t reserveFds, reserveInts;
    int32_t reserve[];
} OhosBufferHandle;

#define ST_NATIVE_BUFFER_OHOS ((VkStructureType)1000453001)
#define ST_SWAPCHAIN_IMAGE_CREATE_INFO_OHOS ((VkStructureType)1000453002)
#define ST_NATIVE_BUFFER_USAGE_OHOS ((VkStructureType)1000452000)
#define ST_NATIVE_BUFFER_PROPERTIES_OHOS ((VkStructureType)1000452001)
#define ST_NATIVE_BUFFER_FORMAT_PROPERTIES_OHOS ((VkStructureType)1000452002)
#define ST_IMPORT_NATIVE_BUFFER_INFO_OHOS ((VkStructureType)1000452003)
#define ST_MEMORY_GET_NATIVE_BUFFER_INFO_OHOS ((VkStructureType)1000452004)
#define ST_EXTERNAL_FORMAT_OHOS ((VkStructureType)1000452005)
#define HANDLE_OHOS_BITS (0x00002000u | 0x00008000u)

typedef struct { VkStructureType sType; const void *pNext; OhosBufferHandle *handle; } NativeBufferOHOS;
typedef struct { VkStructureType sType; void *pNext; uint64_t usage; } NativeBufferUsageOHOS;
typedef struct { VkStructureType sType; void *pNext; VkDeviceSize allocationSize; uint32_t memoryTypeBits; } NativeBufferPropertiesOHOS;
typedef struct {
    VkStructureType sType; void *pNext; VkFormat format; uint64_t externalFormat; VkFormatFeatureFlags formatFeatures;
    VkComponentMapping components; VkSamplerYcbcrModelConversion model; VkSamplerYcbcrRange range;
    VkChromaLocation xOffset, yOffset;
} NativeBufferFormatPropertiesOHOS;
typedef struct { VkStructureType sType; const void *pNext; void *buffer; } ImportNativeBufferInfoOHOS;
typedef struct { VkStructureType sType; void *pNext; uint64_t externalFormat; } ExternalFormatOHOS;

/* gralloc usage bits returned to the swapchain layer */
#define USAGE_MEM_DMA (1ull << 3)
#define USAGE_HW_RENDER (1ull << 8)
#define USAGE_HW_TEXTURE (1ull << 9)

/* ---- Turnip entry points ---- */
static void *turnip;
static PFN_vkGetInstanceProcAddr tu_gipa;
static PFN_vkVoidFunction (*tu_gpdpa)(VkInstance, const char *);
static VkResult (*tu_negotiate)(uint32_t *);
static VkInstance last_instance;
static pthread_once_t load_once = PTHREAD_ONCE_INIT;

static void load_turnip(void)
{
    turnip = dlopen(TURNIP, RTLD_NOW | RTLD_LOCAL);
    if (!turnip) { fprintf(stderr, "vk_ohos_shim: %s\n", dlerror()); return; }
    tu_gipa = (PFN_vkGetInstanceProcAddr)dlsym(turnip, "vk_icdGetInstanceProcAddr");
    tu_gpdpa = (PFN_vkVoidFunction (*)(VkInstance, const char *))dlsym(turnip, "vk_icdGetPhysicalDeviceProcAddr");
    tu_negotiate = (VkResult (*)(uint32_t *))dlsym(turnip, "vk_icdNegotiateLoaderICDInterfaceVersion");
}

static PFN_vkVoidFunction instance_fn(const char *name)
{
    return tu_gipa ? tu_gipa(last_instance, name) : NULL;
}

/* ---- per-device state ---- */
struct device {
    VkDevice handle;
    VkPhysicalDevice phys;
    VkQueue queue;
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkCreateImage CreateImage;
    PFN_vkDestroyImage DestroyImage;
    PFN_vkAllocateMemory AllocateMemory;
    PFN_vkFreeMemory FreeMemory;
    PFN_vkBindImageMemory BindImageMemory;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
    PFN_vkGetMemoryFdPropertiesKHR GetMemoryFdPropertiesKHR;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkQueueWaitIdle QueueWaitIdle;
    PFN_vkCreateSemaphore CreateSemaphore;
    PFN_vkDestroySemaphore DestroySemaphore;
    PFN_vkGetSemaphoreFdKHR GetSemaphoreFdKHR;
    PFN_vkImportSemaphoreFdKHR ImportSemaphoreFdKHR;
    PFN_vkImportFenceFdKHR ImportFenceFdKHR;
    PFN_vkCreateSamplerYcbcrConversion CreateSamplerYcbcrConversion;
    PFN_vkCreateImageView CreateImageView;
};
static struct device devices[4];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static struct device *find_device(VkDevice handle)
{
    for (unsigned i = 0; i < 4; ++i)
        if (devices[i].handle == handle) return &devices[i];
    return NULL;
}

static struct device *device_for_queue(VkQueue queue)
{
    for (unsigned i = 0; i < 4; ++i)
        if (devices[i].handle && devices[i].queue == queue) return &devices[i];
    for (unsigned i = 4; i-- > 0;)
        if (devices[i].handle) return &devices[i];
    return NULL;
}

/* images the shim owns memory for (swapchain images) or created with an
 * external format (their views/conversions need the real format) */
struct image { VkImage image; VkDeviceMemory memory; VkDevice device; VkFormat format; };
static struct image images[256];

static void remember_image(VkDevice device, VkImage image, VkDeviceMemory memory, VkFormat format)
{
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < 256; ++i)
        if (!images[i].image) { images[i] = (struct image){image, memory, device, format}; break; }
    pthread_mutex_unlock(&lock);
}

static struct image take_image(VkImage image, bool remove)
{
    struct image found = {0};
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < 256; ++i)
        if (images[i].image == image) { found = images[i]; if (remove) images[i] = (struct image){0}; break; }
    pthread_mutex_unlock(&lock);
    return found;
}

/* The external-memory import sequence is GetNativeBufferProperties ->
 * CreateImage -> AllocateMemory(import) on one thread; keep the handle so the
 * image gets the buffer's real pitch. */
static __thread const OhosBufferHandle *pending_handle;

static const void *find_struct(const void *chain, VkStructureType type)
{
    for (const VkBaseInStructure *s = chain; s; s = s->pNext)
        if (s->sType == type) return s;
    return NULL;
}

/* ---- formats ---- */
static VkFormat vk_format(int ohos, bool *ycbcr, unsigned *planes, unsigned *cpp)
{
    *ycbcr = false; *planes = 1; *cpp = 4;
    switch (ohos) {
    case 3: *cpp = 2; return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case 11: case 12: return VK_FORMAT_R8G8B8A8_UNORM;
    case 19: case 20: return VK_FORMAT_B8G8R8A8_UNORM;
    case 34: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case 39: *cpp = 8; return VK_FORMAT_R16G16B16A16_SFLOAT;
    case 24: case 25: *ycbcr = true; *planes = 2; *cpp = 1; return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    case 35: *ycbcr = true; *planes = 2; *cpp = 2; return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
    default: return VK_FORMAT_UNDEFINED;
    }
}

static unsigned format_cpp(VkFormat f)
{
    switch (f) {
    case VK_FORMAT_R5G6B5_UNORM_PACK16: return 2;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM: return 1;
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16: return 2;
    default: return 4;
    }
}

static bool format_two_plane(VkFormat f)
{
    return f == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM || f == VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
}

/* ---- image creation ---- */
static VkResult create_linear_image(struct device *d, const VkImageCreateInfo *info, int stride,
                                    const VkAllocationCallbacks *alloc, VkImage *image)
{
    VkImageCreateInfo copy = *info;
    unsigned cpp = format_cpp(copy.format);
    if (stride <= 0) stride = (int)(((copy.extent.width + 63u) & ~63u) * cpp);
    VkSubresourceLayout planes[2] = {
        {.offset = 0, .rowPitch = (VkDeviceSize)stride},
        {.offset = (VkDeviceSize)stride * copy.extent.height, .rowPitch = (VkDeviceSize)stride},
    };
    VkImageDrmFormatModifierExplicitCreateInfoEXT modifier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .pNext = copy.pNext,
        .drmFormatModifier = 0 /* DRM_FORMAT_MOD_LINEAR */,
        .drmFormatModifierPlaneCount = format_two_plane(copy.format) ? 2 : 1,
        .pPlaneLayouts = planes,
    };
    VkExternalMemoryImageCreateInfo external = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = &modifier,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    copy.pNext = &external;
    copy.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    return d->CreateImage(d->handle, &copy, alloc, image);
}

static VkResult import_fd(struct device *d, int fd, VkDeviceSize size, VkImage image, VkDeviceMemory *memory)
{
    VkMemoryFdPropertiesKHR fdp = {.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    if (d->GetMemoryFdPropertiesKHR)
        d->GetMemoryFdPropertiesKHR(d->handle, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdp);
    VkMemoryRequirements req = {0};
    d->GetImageMemoryRequirements(d->handle, image, &req);
    uint32_t bits = req.memoryTypeBits & (fdp.memoryTypeBits ? fdp.memoryTypeBits : ~0u);
    uint32_t type = bits ? (uint32_t)__builtin_ctz(bits) : 0;
    int dup_fd = dup(fd);
    if (dup_fd < 0) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    VkMemoryDedicatedAllocateInfo dedicated = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .image = image};
    VkImportMemoryFdInfoKHR import = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, .pNext = &dedicated,
                                      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, .fd = dup_fd};
    VkMemoryAllocateInfo info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &import,
                                 .allocationSize = size > req.size ? size : req.size, .memoryTypeIndex = type};
    VkResult r = d->AllocateMemory(d->handle, &info, NULL, memory);
    if (r != VK_SUCCESS) close(dup_fd);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL shim_CreateImage(VkDevice device, const VkImageCreateInfo *info,
                                                       const VkAllocationCallbacks *alloc, VkImage *image)
{
    struct device *d = find_device(device);
    if (!d) return VK_ERROR_DEVICE_LOST;
    const NativeBufferOHOS *native = find_struct(info->pNext, ST_NATIVE_BUFFER_OHOS);
    if (native && native->handle) {
        /* swapchain image backed by a native window buffer */
        const OhosBufferHandle *h = native->handle;
        VkResult r = create_linear_image(d, info, h->stride, alloc, image);
        if (r != VK_SUCCESS) return r;
        VkDeviceMemory memory;
        r = import_fd(d, h->fd, (VkDeviceSize)h->size, *image, &memory);
        if (r == VK_SUCCESS) r = d->BindImageMemory(device, *image, memory, 0);
        if (r != VK_SUCCESS) { d->DestroyImage(device, *image, alloc); return r; }
        remember_image(device, *image, memory, info->format);
        return VK_SUCCESS;
    }
    const VkExternalMemoryImageCreateInfo *ext = find_struct(info->pNext, VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    if (ext && (ext->handleTypes & HANDLE_OHOS_BITS)) {
        VkImageCreateInfo copy = *info;
        const ExternalFormatOHOS *xf = find_struct(info->pNext, ST_EXTERNAL_FORMAT_OHOS);
        bool ycbcr; unsigned planes, cpp;
        if (copy.format == VK_FORMAT_UNDEFINED && xf && xf->externalFormat)
            copy.format = vk_format((int)xf->externalFormat, &ycbcr, &planes, &cpp);
        const OhosBufferHandle *h = pending_handle;
        int stride = h && h->width == (int)copy.extent.width ? h->stride : 0;
        VkResult r = create_linear_image(d, &copy, stride, alloc, image);
        if (r == VK_SUCCESS && copy.format != info->format) remember_image(device, *image, VK_NULL_HANDLE, copy.format);
        return r;
    }
    return d->CreateImage(device, info, alloc, image);
}

static VKAPI_ATTR void VKAPI_CALL shim_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *alloc)
{
    struct device *d = find_device(device);
    if (!d) return;
    struct image owned = take_image(image, true);
    d->DestroyImage(device, image, alloc);
    if (owned.memory) d->FreeMemory(device, owned.memory, NULL);
}

static VKAPI_ATTR VkResult VKAPI_CALL shim_CreateImageView(VkDevice device, const VkImageViewCreateInfo *info,
                                                           const VkAllocationCallbacks *alloc, VkImageView *view)
{
    struct device *d = find_device(device);
    if (!d) return VK_ERROR_DEVICE_LOST;
    if (info->format == VK_FORMAT_UNDEFINED) {
        struct image known = take_image(info->image, false);
        if (known.format != VK_FORMAT_UNDEFINED) {
            VkImageViewCreateInfo copy = *info;
            copy.format = known.format;
            return d->CreateImageView(device, &copy, alloc, view);
        }
    }
    return d->CreateImageView(device, info, alloc, view);
}

static VKAPI_ATTR VkResult VKAPI_CALL shim_CreateSamplerYcbcrConversion(VkDevice device,
    const VkSamplerYcbcrConversionCreateInfo *info, const VkAllocationCallbacks *alloc, VkSamplerYcbcrConversion *out)
{
    struct device *d = find_device(device);
    if (!d) return VK_ERROR_DEVICE_LOST;
    const ExternalFormatOHOS *xf = find_struct(info->pNext, ST_EXTERNAL_FORMAT_OHOS);
    if (info->format == VK_FORMAT_UNDEFINED && xf && xf->externalFormat) {
        VkSamplerYcbcrConversionCreateInfo copy = *info;
        bool ycbcr; unsigned planes, cpp;
        copy.format = vk_format((int)xf->externalFormat, &ycbcr, &planes, &cpp);
        if (xf->externalFormat == 25) { /* NV21: chroma stored V,U */
            copy.components.r = VK_COMPONENT_SWIZZLE_B;
            copy.components.b = VK_COMPONENT_SWIZZLE_R;
        }
        return d->CreateSamplerYcbcrConversion(device, &copy, alloc, out);
    }
    return d->CreateSamplerYcbcrConversion(device, info, alloc, out);
}

/* ---- memory ---- */
static const OhosBufferHandle *(*nb_get_handle)(const void *);
static void load_native_buffer(void)
{
    void *lib = dlopen("/system/lib64/chipset-pub-sdk/libnative_buffer.so", RTLD_NOW);
    if (!lib) lib = dlopen("libnative_buffer.so", RTLD_NOW);
    if (lib) nb_get_handle = (const OhosBufferHandle *(*)(const void *))dlsym(lib, "OH_NativeBuffer_GetBufferHandle");
}
static pthread_once_t nb_once = PTHREAD_ONCE_INIT;

static const OhosBufferHandle *buffer_handle(const void *native_buffer)
{
    pthread_once(&nb_once, load_native_buffer);
    return nb_get_handle && native_buffer ? nb_get_handle(native_buffer) : NULL;
}

static VKAPI_ATTR VkResult VKAPI_CALL shim_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo *info,
                                                          const VkAllocationCallbacks *alloc, VkDeviceMemory *memory)
{
    struct device *d = find_device(device);
    if (!d) return VK_ERROR_DEVICE_LOST;
    const ImportNativeBufferInfoOHOS *import = find_struct(info->pNext, ST_IMPORT_NATIVE_BUFFER_INFO_OHOS);
    if (import) {
        const OhosBufferHandle *h = buffer_handle(import->buffer);
        if (!h || h->fd < 0) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
        int fd = dup(h->fd);
        if (fd < 0) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
        VkImportMemoryFdInfoKHR fdinfo = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, .pNext = info->pNext,
                                          .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, .fd = fd};
        VkMemoryAllocateInfo copy = *info;
        copy.pNext = &fdinfo;
        if (copy.allocationSize < (VkDeviceSize)h->size) copy.allocationSize = (VkDeviceSize)h->size;
        VkResult r = d->AllocateMemory(device, &copy, alloc, memory);
        if (r != VK_SUCCESS) close(fd);
        pending_handle = NULL;
        return r;
    }
    return d->AllocateMemory(device, info, alloc, memory);
}

static VKAPI_ATTR VkResult VKAPI_CALL shim_GetNativeBufferPropertiesOHOS(VkDevice device, const void *buffer,
                                                                         NativeBufferPropertiesOHOS *props)
{
    struct device *d = find_device(device);
    const OhosBufferHandle *h = buffer_handle(buffer);
    if (!d || !h || h->fd < 0) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    VkMemoryFdPropertiesKHR fdp = {.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    if (d->GetMemoryFdPropertiesKHR)
        d->GetMemoryFdPropertiesKHR(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, h->fd, &fdp);
    props->allocationSize = (VkDeviceSize)h->size;
    props->memoryTypeBits = fdp.memoryTypeBits ? fdp.memoryTypeBits : 1u;
    for (VkBaseOutStructure *s = props->pNext; s; s = s->pNext) {
        if (s->sType != ST_NATIVE_BUFFER_FORMAT_PROPERTIES_OHOS) continue;
        NativeBufferFormatPropertiesOHOS *f = (NativeBufferFormatPropertiesOHOS *)s;
        bool ycbcr; unsigned planes, cpp;
        f->format = vk_format(h->format, &ycbcr, &planes, &cpp);
        f->externalFormat = (uint64_t)h->format;
        f->formatFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if (ycbcr)
            f->formatFeatures |= VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT |
                                 VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT |
                                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;
        f->components = (VkComponentMapping){0};
        if (h->format == 25) { f->components.r = VK_COMPONENT_SWIZZLE_B; f->components.b = VK_COMPONENT_SWIZZLE_R; }
        f->model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
        f->range = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
        f->xOffset = f->yOffset = VK_CHROMA_LOCATION_MIDPOINT;
    }
    pending_handle = h;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL shim_GetMemoryNativeBufferOHOS(VkDevice device, const void *info, void **buffer)
{
    (void)device; (void)info; (void)buffer;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

/* ---- swapchain synchronization ---- */
static VKAPI_ATTR VkResult VKAPI_CALL shim_GetSwapchainGrallocUsageOHOS(VkDevice device, VkFormat format,
                                                                         VkImageUsageFlags usage, uint64_t *gralloc)
{
    (void)device; (void)format; (void)usage;
    *gralloc = USAGE_MEM_DMA | USAGE_HW_RENDER | USAGE_HW_TEXTURE;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL shim_AcquireImageOHOS(VkDevice device, VkImage image, int32_t fence_fd,
                                                            VkSemaphore semaphore, VkFence fence)
{
    (void)image;
    struct device *d = find_device(device);
    if (!d) return VK_ERROR_DEVICE_LOST;
    VkResult r = VK_SUCCESS;
    /* Importing a sync fd (-1 = already signalled) avoids a queue submission
     * from a thread that does not own the queue. */
    if (semaphore != VK_NULL_HANDLE) {
        int fd = fence_fd >= 0 ? dup(fence_fd) : -1;
        VkImportSemaphoreFdInfoKHR in = {.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR, .semaphore = semaphore,
                                         .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
                                         .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT, .fd = fd};
        r = d->ImportSemaphoreFdKHR ? d->ImportSemaphoreFdKHR(device, &in) : VK_ERROR_FEATURE_NOT_PRESENT;
        if (r != VK_SUCCESS && fd >= 0) close(fd);
    }
    if (r == VK_SUCCESS && fence != VK_NULL_HANDLE) {
        int fd = fence_fd >= 0 ? dup(fence_fd) : -1;
        VkImportFenceFdInfoKHR in = {.sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR, .fence = fence,
                                     .flags = VK_FENCE_IMPORT_TEMPORARY_BIT,
                                     .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT, .fd = fd};
        r = d->ImportFenceFdKHR ? d->ImportFenceFdKHR(device, &in) : VK_ERROR_FEATURE_NOT_PRESENT;
        if (r != VK_SUCCESS && fd >= 0) close(fd);
    }
    if (r != VK_SUCCESS && fence_fd >= 0) {
        /* fallback: wait on the CPU, then signal through an empty submission */
        struct pollfd p = {fence_fd, POLLIN, 0};
        poll(&p, 1, -1);
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .signalSemaphoreCount = semaphore ? 1u : 0u,
                           .pSignalSemaphores = &semaphore};
        r = d->QueueSubmit(d->queue, 1, &si, fence);
    }
    if (fence_fd >= 0) close(fence_fd);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL shim_QueueSignalReleaseImageOHOS(VkQueue queue, uint32_t count,
    const VkSemaphore *waits, VkImage image, int32_t *fence_fd)
{
    (void)image;
    struct device *d = device_for_queue(queue);
    if (!d) return VK_ERROR_DEVICE_LOST;
    if (fence_fd) *fence_fd = -1;
    if (count == 0) return VK_SUCCESS;
    VkPipelineStageFlags stages[16];
    for (unsigned i = 0; i < 16; ++i) stages[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if (count > 16) count = 16;
    VkExportSemaphoreCreateInfo export = {.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
                                          .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
    VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &export};
    VkSemaphore signal = VK_NULL_HANDLE;
    VkResult r = d->CreateSemaphore(d->handle, &sci, NULL, &signal);
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = count, .pWaitSemaphores = waits,
                       .pWaitDstStageMask = stages, .signalSemaphoreCount = r == VK_SUCCESS ? 1u : 0u,
                       .pSignalSemaphores = &signal};
    VkResult s = d->QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    if (s != VK_SUCCESS) { if (signal) d->DestroySemaphore(d->handle, signal, NULL); return s; }
    if (r == VK_SUCCESS && d->GetSemaphoreFdKHR && fence_fd) {
        VkSemaphoreGetFdInfoKHR get = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR, .semaphore = signal,
                                       .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
        if (d->GetSemaphoreFdKHR(d->handle, &get, fence_fd) != VK_SUCCESS) {
            *fence_fd = -1;
            d->QueueWaitIdle(queue);
        }
    } else {
        d->QueueWaitIdle(queue);
    }
    if (signal) d->DestroySemaphore(d->handle, signal, NULL);
    return VK_SUCCESS;
}

/* ---- physical device queries ---- */
static const VkExtensionProperties ohos_extensions[] = {
    {"VK_OHOS_native_buffer", 1},
    {"VK_OHOS_external_memory", 1},
};

static VKAPI_ATTR VkResult VKAPI_CALL shim_EnumerateDeviceExtensionProperties(VkPhysicalDevice phys, const char *layer,
    uint32_t *count, VkExtensionProperties *props)
{
    PFN_vkEnumerateDeviceExtensionProperties real =
        (PFN_vkEnumerateDeviceExtensionProperties)instance_fn("vkEnumerateDeviceExtensionProperties");
    if (!real) return VK_ERROR_INITIALIZATION_FAILED;
    if (layer) return real(phys, layer, count, props);
    uint32_t n = 0;
    VkResult r = real(phys, NULL, &n, NULL);
    if (r != VK_SUCCESS) return r;
    uint32_t total = n + 2;
    if (!props) { *count = total; return VK_SUCCESS; }
    VkExtensionProperties *all = calloc(total, sizeof(*all));
    if (!all) return VK_ERROR_OUT_OF_HOST_MEMORY;
    r = real(phys, NULL, &n, all);
    memcpy(all + n, ohos_extensions, sizeof(ohos_extensions));
    total = n + 2;
    uint32_t give = *count < total ? *count : total;
    memcpy(props, all, give * sizeof(*all));
    free(all);
    *count = give;
    return give < total ? VK_INCOMPLETE : r;
}

static VKAPI_ATTR VkResult VKAPI_CALL shim_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice phys,
    const VkPhysicalDeviceImageFormatInfo2 *info, VkImageFormatProperties2 *props)
{
    PFN_vkGetPhysicalDeviceImageFormatProperties2 real =
        (PFN_vkGetPhysicalDeviceImageFormatProperties2)instance_fn("vkGetPhysicalDeviceImageFormatProperties2");
    if (!real) return VK_ERROR_INITIALIZATION_FAILED;
    const VkPhysicalDeviceExternalImageFormatInfo *ext =
        find_struct(info->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);
    if (!ext || !(ext->handleType & HANDLE_OHOS_BITS)) return real(phys, info, props);
    /* answer for a plain image; the native buffer import is linear dma-buf */
    VkPhysicalDeviceExternalImageFormatInfo none = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
                                                    .pNext = info->pNext, .handleType = 0};
    VkPhysicalDeviceImageFormatInfo2 copy = *info;
    copy.pNext = &none;
    VkResult r = real(phys, &copy, props);
    for (VkBaseOutStructure *s = props->pNext; s; s = s->pNext) {
        if (s->sType == VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES) {
            VkExternalImageFormatProperties *e = (VkExternalImageFormatProperties *)s;
            e->externalMemoryProperties.externalMemoryFeatures =
                VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
            e->externalMemoryProperties.exportFromImportedHandleTypes = ext->handleType;
            e->externalMemoryProperties.compatibleHandleTypes = ext->handleType;
        } else if (s->sType == ST_NATIVE_BUFFER_USAGE_OHOS) {
            ((NativeBufferUsageOHOS *)s)->usage = USAGE_MEM_DMA | USAGE_HW_RENDER | USAGE_HW_TEXTURE;
        }
    }
    return r;
}

/* ---- device creation ---- */
static bool has_extension(VkPhysicalDevice phys, const char *name)
{
    PFN_vkEnumerateDeviceExtensionProperties real =
        (PFN_vkEnumerateDeviceExtensionProperties)instance_fn("vkEnumerateDeviceExtensionProperties");
    uint32_t n = 0;
    if (!real || real(phys, NULL, &n, NULL) != VK_SUCCESS) return false;
    VkExtensionProperties *all = calloc(n, sizeof(*all));
    bool found = false;
    if (all && real(phys, NULL, &n, all) == VK_SUCCESS)
        for (uint32_t i = 0; i < n && !found; ++i) found = !strcmp(all[i].extensionName, name);
    free(all);
    return found;
}

static PFN_vkVoidFunction device_proc(struct device *d, const char *name)
{
    return d->gdpa(d->handle, name);
}

static VKAPI_ATTR VkResult VKAPI_CALL shim_CreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo *info,
                                                        const VkAllocationCallbacks *alloc, VkDevice *device)
{
    PFN_vkCreateDevice real = (PFN_vkCreateDevice)instance_fn("vkCreateDevice");
    if (!real) return VK_ERROR_INITIALIZATION_FAILED;
    static const char *needed[] = {
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,
    };
    uint32_t max = info->enabledExtensionCount + 5;
    const char **names = calloc(max, sizeof(*names));
    if (!names) return VK_ERROR_OUT_OF_HOST_MEMORY;
    uint32_t n = 0;
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
        const char *e = info->ppEnabledExtensionNames[i];
        if (!strcmp(e, "VK_OHOS_native_buffer") || !strcmp(e, "VK_OHOS_external_memory")) continue;
        names[n++] = e;
    }
    for (unsigned k = 0; k < 5; ++k) {
        bool present = false;
        for (uint32_t i = 0; i < n; ++i) present |= !strcmp(names[i], needed[k]);
        if (!present && has_extension(phys, needed[k])) names[n++] = needed[k];
    }
    VkDeviceCreateInfo copy = *info;
    copy.enabledExtensionCount = n;
    copy.ppEnabledExtensionNames = names;
    VkResult r = real(phys, &copy, alloc, device);
    free(names);
    if (r != VK_SUCCESS) return r;
    pthread_mutex_lock(&lock);
    struct device *d = NULL;
    for (unsigned i = 0; i < 4 && !d; ++i) if (!devices[i].handle) d = &devices[i];
    if (!d) d = &devices[3];
    memset(d, 0, sizeof(*d));
    d->handle = *device;
    d->phys = phys;
    d->gdpa = (PFN_vkGetDeviceProcAddr)instance_fn("vkGetDeviceProcAddr");
#define LOAD(f) d->f = (PFN_vk##f)device_proc(d, "vk" #f)
    LOAD(CreateImage); LOAD(DestroyImage); LOAD(AllocateMemory); LOAD(FreeMemory); LOAD(BindImageMemory);
    LOAD(GetImageMemoryRequirements); LOAD(GetMemoryFdPropertiesKHR); LOAD(QueueSubmit); LOAD(QueueWaitIdle);
    LOAD(CreateSemaphore); LOAD(DestroySemaphore); LOAD(GetSemaphoreFdKHR); LOAD(ImportSemaphoreFdKHR);
    LOAD(ImportFenceFdKHR); LOAD(CreateSamplerYcbcrConversion); LOAD(CreateImageView);
#undef LOAD
    PFN_vkGetDeviceQueue get_queue = (PFN_vkGetDeviceQueue)device_proc(d, "vkGetDeviceQueue");
    if (get_queue && info->queueCreateInfoCount)
        get_queue(*device, info->pQueueCreateInfos[0].queueFamilyIndex, 0, &d->queue);
    pthread_mutex_unlock(&lock);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL shim_DestroyDevice(VkDevice device, const VkAllocationCallbacks *alloc)
{
    struct device *d = find_device(device);
    PFN_vkDestroyDevice real = d ? (PFN_vkDestroyDevice)device_proc(d, "vkDestroyDevice") : NULL;
    if (real) real(device, alloc);
    if (d) { pthread_mutex_lock(&lock); memset(d, 0, sizeof(*d)); pthread_mutex_unlock(&lock); }
}

/* ---- dispatch ---- */
static PFN_vkVoidFunction device_override(const char *name)
{
#define OVR(n) if (!strcmp(name, "vk" #n)) return (PFN_vkVoidFunction)shim_##n
    OVR(CreateImage); OVR(DestroyImage); OVR(CreateImageView); OVR(AllocateMemory); OVR(DestroyDevice);
    OVR(CreateSamplerYcbcrConversion);
    OVR(GetNativeBufferPropertiesOHOS); OVR(GetMemoryNativeBufferOHOS); OVR(GetSwapchainGrallocUsageOHOS);
    OVR(AcquireImageOHOS); OVR(QueueSignalReleaseImageOHOS);
    if (!strcmp(name, "vkCreateSamplerYcbcrConversionKHR")) return (PFN_vkVoidFunction)shim_CreateSamplerYcbcrConversion;
#undef OVR
    return NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL shim_GetDeviceProcAddr(VkDevice device, const char *name);

static PFN_vkVoidFunction instance_override(const char *name)
{
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)shim_GetDeviceProcAddr;
    if (!strcmp(name, "vkCreateDevice")) return (PFN_vkVoidFunction)shim_CreateDevice;
    if (!strcmp(name, "vkEnumerateDeviceExtensionProperties"))
        return (PFN_vkVoidFunction)shim_EnumerateDeviceExtensionProperties;
    if (!strcmp(name, "vkGetPhysicalDeviceImageFormatProperties2") ||
        !strcmp(name, "vkGetPhysicalDeviceImageFormatProperties2KHR"))
        return (PFN_vkVoidFunction)shim_GetPhysicalDeviceImageFormatProperties2;
    return device_override(name);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL shim_GetDeviceProcAddr(VkDevice device, const char *name)
{
    PFN_vkVoidFunction f = device_override(name);
    if (f) return f;
    struct device *d = find_device(device);
    PFN_vkGetDeviceProcAddr real = d ? d->gdpa : (PFN_vkGetDeviceProcAddr)instance_fn("vkGetDeviceProcAddr");
    return real ? real(device, name) : NULL;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *version)
{
    pthread_once(&load_once, load_turnip);
    if (!tu_negotiate) return VK_ERROR_INCOMPATIBLE_DRIVER;
    return tu_negotiate(version);
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name)
{
    pthread_once(&load_once, load_turnip);
    if (!tu_gipa) return NULL;
    if (instance) last_instance = instance;
    PFN_vkVoidFunction f = instance_override(name);
    if (f) {
        /* only expose an override when Turnip has the underlying function */
        return tu_gipa(instance, !strncmp(name, "vk", 2) && strstr(name, "OHOS") ? "vkCreateImage" : name) ? f : NULL;
    }
    return tu_gipa(instance, name);
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *name)
{
    pthread_once(&load_once, load_turnip);
    if (instance) last_instance = instance;
    if (!strcmp(name, "vkEnumerateDeviceExtensionProperties"))
        return (PFN_vkVoidFunction)shim_EnumerateDeviceExtensionProperties;
    if (!strcmp(name, "vkGetPhysicalDeviceImageFormatProperties2") ||
        !strcmp(name, "vkGetPhysicalDeviceImageFormatProperties2KHR"))
        return (PFN_vkVoidFunction)shim_GetPhysicalDeviceImageFormatProperties2;
    return tu_gpdpa ? tu_gpdpa(instance, name) : NULL;
}
