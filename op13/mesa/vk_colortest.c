// SPDX-License-Identifier: Apache-2.0
/*
 * op13-vk-colortest: check the Vulkan path render_service would use on the
 * OP13 port, through the HarmonyOS loader (libvulkan.so) and the ICD shim
 * (libvulkan_op13_ohos.so in front of Turnip).
 *
 *  1. VK_OHOS_native_buffer and VK_OHOS_external_memory are advertised.
 *  2. A device with VK_OHOS_native_buffer is created.
 *  3. Swapchain path: an OH_NativeBuffer (RGBA_8888) becomes a VkImage through
 *     VkNativeBufferOHOS; vkCmdClearColorImage writes red; the CPU reads the
 *     buffer's bytes back (expected ff0000ff).
 *  4. Import path: vkGetNativeBufferPropertiesOHOS + VkImportNativeBufferInfoOHOS,
 *     clear blue, read back (expected 0000ffff).
 *  5. vkAcquireImageOHOS / vkQueueSignalReleaseImageOHOS round trip.
 *
 * Run as root over ADB; it changes no state.
 */
#define VK_NO_PROTOTYPES
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <vulkan/vulkan_core.h>

#define SIZE 256
#define ST_NATIVE_BUFFER_OHOS ((VkStructureType)1000453001)
#define ST_NATIVE_BUFFER_PROPERTIES_OHOS ((VkStructureType)1000452001)
#define ST_NATIVE_BUFFER_FORMAT_PROPERTIES_OHOS ((VkStructureType)1000452002)
#define ST_IMPORT_NATIVE_BUFFER_INFO_OHOS ((VkStructureType)1000452003)
#define HANDLE_OHOS 0x00008000u

typedef struct { int32_t width, height, format, usage, stride; } NbConfig;
typedef struct { VkStructureType sType; const void *pNext; const void *handle; } NativeBufferOHOS;
typedef struct { VkStructureType sType; void *pNext; VkDeviceSize allocationSize; uint32_t memoryTypeBits; } NbProps;
typedef struct { VkStructureType sType; const void *pNext; void *buffer; } ImportNb;

static int failures;
static PFN_vkGetInstanceProcAddr gipa;
static PFN_vkGetDeviceProcAddr gdpa;
static VkInstance instance;
static VkDevice device;
#define I(name) PFN_##name name = (PFN_##name)gipa(instance, #name)
#define D(name) PFN_##name name = (PFN_##name)gdpa(device, #name)

static void *(*nb_alloc)(const NbConfig *);
static int (*nb_map)(void *, void **);
static int (*nb_unmap)(void *);
static void (*nb_config)(void *, NbConfig *);
static const void *(*nb_handle)(const void *);

static bool clear_and_check(VkQueue queue, uint32_t family, VkImage image, const float color[4],
                            void *buffer, const uint8_t want[4], const char *name)
{
    D(vkCreateCommandPool); D(vkAllocateCommandBuffers); D(vkBeginCommandBuffer); D(vkEndCommandBuffer);
    D(vkCmdPipelineBarrier); D(vkCmdClearColorImage); D(vkQueueSubmit); D(vkQueueWaitIdle); D(vkDestroyCommandPool);
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool pool;
    vkCreateCommandPool(device, &pci, NULL, &pool);
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cai, &cmd);
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &begin);
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier to_dst = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .image = image, .subresourceRange = range};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_dst);
    VkClearColorValue value;
    memcpy(value.float32, color, sizeof(value.float32));
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
    VkImageMemoryBarrier to_host = to_dst;
    to_host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_host.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
    to_host.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_host.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 0, NULL, 1, &to_host);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    VkResult r = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkDestroyCommandPool(device, pool, NULL);
    NbConfig cfg = {0};
    nb_config(buffer, &cfg);
    uint8_t *bytes = NULL;
    if (r != VK_SUCCESS || nb_map(buffer, (void **)&bytes) || !bytes) {
        printf("FAIL %-44s submit %d / map failed\n", name, r); ++failures; return false;
    }
    const uint8_t *px = bytes + (SIZE / 2) * cfg.stride + (SIZE / 2) * 4;
    bool ok = !memcmp(px, want, 4);
    printf("%s %-44s want %02x%02x%02x%02x got %02x%02x%02x%02x (stride %d)\n", ok ? "PASS" : "FAIL", name,
           want[0], want[1], want[2], want[3], px[0], px[1], px[2], px[3], cfg.stride);
    nb_unmap(buffer);
    if (!ok) ++failures;
    return ok;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    void *vk = dlopen("libvulkan.so", RTLD_NOW);
    void *nb = dlopen("/system/lib64/chipset-pub-sdk/libnative_buffer.so", RTLD_NOW);
    if (!vk || !nb) { printf("FAIL cannot load libvulkan/libnative_buffer: %s\n", dlerror()); return 2; }
    gipa = (PFN_vkGetInstanceProcAddr)dlsym(vk, "vkGetInstanceProcAddr");
    nb_alloc = dlsym(nb, "OH_NativeBuffer_Alloc");
    nb_map = dlsym(nb, "OH_NativeBuffer_Map");
    nb_unmap = dlsym(nb, "OH_NativeBuffer_Unmap");
    nb_config = dlsym(nb, "OH_NativeBuffer_GetConfig");
    nb_handle = dlsym(nb, "OH_NativeBuffer_GetBufferHandle");
    if (!gipa || !nb_alloc || !nb_map || !nb_unmap || !nb_config || !nb_handle) { printf("FAIL missing entry points\n"); return 2; }

    I(vkCreateInstance);
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkResult r = vkCreateInstance(&ici, NULL, &instance);
    if (r != VK_SUCCESS) { printf("FAIL vkCreateInstance %d\n", r); return 2; }
    I(vkEnumeratePhysicalDevices); I(vkGetPhysicalDeviceProperties); I(vkEnumerateDeviceExtensionProperties);
    I(vkGetPhysicalDeviceQueueFamilyProperties); I(vkCreateDevice); I(vkGetDeviceProcAddr);
    uint32_t n = 1;
    VkPhysicalDevice phys;
    if (vkEnumeratePhysicalDevices(instance, &n, &phys) < 0 || n == 0) { printf("FAIL no physical device\n"); return 2; }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys, &props);
    printf("Vulkan device: %s, API %u.%u.%u\n", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
           VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(phys, NULL, &count, NULL);
    static VkExtensionProperties exts[512];
    if (count > 512) count = 512;
    vkEnumerateDeviceExtensionProperties(phys, NULL, &count, exts);
    bool nbext = false, xmem = false;
    for (uint32_t i = 0; i < count; ++i) {
        nbext |= !strcmp(exts[i].extensionName, "VK_OHOS_native_buffer");
        xmem |= !strcmp(exts[i].extensionName, "VK_OHOS_external_memory");
    }
    printf("%s VK_OHOS_native_buffer advertised\n%s VK_OHOS_external_memory advertised\n", nbext ? "PASS" : "FAIL",
           xmem ? "PASS" : "FAIL");
    failures += !nbext + !xmem;

    uint32_t families = 8;
    VkQueueFamilyProperties qf[8];
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &families, qf);
    uint32_t family = 0;
    for (uint32_t i = 0; i < families; ++i) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = i; break; }
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
                                   .queueCount = 1, .pQueuePriorities = &priority};
    const char *enable[] = {"VK_OHOS_native_buffer", "VK_OHOS_external_memory"};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &qci, .enabledExtensionCount = nbext && xmem ? 2 : 0,
                              .ppEnabledExtensionNames = enable};
    r = vkCreateDevice(phys, &dci, NULL, &device);
    printf("%s vkCreateDevice with OHOS extensions (%d)\n", r == VK_SUCCESS ? "PASS" : "FAIL", r);
    if (r != VK_SUCCESS) return 1;
    gdpa = vkGetDeviceProcAddr;
    D(vkGetDeviceQueue); D(vkCreateImage); D(vkDestroyImage); D(vkAllocateMemory); D(vkBindImageMemory); D(vkFreeMemory);
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkResult (*acquire)(VkDevice, VkImage, int32_t, VkSemaphore, VkFence) = (void *)gdpa(device, "vkAcquireImageOHOS");
    VkResult (*release)(VkQueue, uint32_t, const VkSemaphore *, VkImage, int32_t *) =
        (void *)gdpa(device, "vkQueueSignalReleaseImageOHOS");
    VkResult (*props_fn)(VkDevice, const void *, NbProps *) = (void *)gdpa(device, "vkGetNativeBufferPropertiesOHOS");
    printf("%s OHOS device entry points resolved\n", acquire && release && props_fn ? "PASS" : "FAIL");
    if (!acquire || !release || !props_fn) return 1;

    const NbConfig want = {SIZE, SIZE, 12, (1 << 0) | (1 << 1) | (1 << 3) | (1 << 8) | (1 << 9), 0};
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {SIZE, SIZE, 1}, .mipLevels = 1, .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};

    /* swapchain path */
    void *buffer = nb_alloc(&want);
    NativeBufferOHOS nbinfo = {.sType = ST_NATIVE_BUFFER_OHOS, .handle = nb_handle(buffer)};
    VkImageCreateInfo swap = ii;
    swap.pNext = &nbinfo;
    VkImage image;
    r = vkCreateImage(device, &swap, NULL, &image);
    printf("%s vkCreateImage(VkNativeBufferOHOS) %d\n", r == VK_SUCCESS ? "PASS" : "FAIL", r);
    if (r == VK_SUCCESS) {
        static const float red[4] = {1, 0, 0, 1};
        static const uint8_t red_bytes[4] = {255, 0, 0, 255};
        r = acquire(device, image, -1, VK_NULL_HANDLE, VK_NULL_HANDLE);
        printf("%s vkAcquireImageOHOS(-1) %d\n", r == VK_SUCCESS ? "PASS" : "FAIL", r);
        clear_and_check(queue, family, image, red, buffer, red_bytes, "swapchain image clear red -> bytes");
        int32_t fd = -2;
        r = release(queue, 0, NULL, image, &fd);
        printf("%s vkQueueSignalReleaseImageOHOS %d fd %d\n", r == VK_SUCCESS ? "PASS" : "FAIL", r, fd);
        if (fd >= 0) close(fd);
        vkDestroyImage(device, image, NULL);
    } else {
        ++failures;
    }

    /* import path */
    void *buffer2 = nb_alloc(&want);
    NbProps np = {.sType = ST_NATIVE_BUFFER_PROPERTIES_OHOS};
    r = props_fn(device, buffer2, &np);
    printf("%s vkGetNativeBufferPropertiesOHOS %d size %llu types 0x%x\n", r == VK_SUCCESS ? "PASS" : "FAIL", r,
           (unsigned long long)np.allocationSize, np.memoryTypeBits);
    VkExternalMemoryImageCreateInfo ext = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, .handleTypes = HANDLE_OHOS};
    VkImageCreateInfo imp = ii;
    imp.pNext = &ext;
    r = vkCreateImage(device, &imp, NULL, &image);
    printf("%s vkCreateImage(external OHOS) %d\n", r == VK_SUCCESS ? "PASS" : "FAIL", r);
    if (r == VK_SUCCESS) {
        VkMemoryDedicatedAllocateInfo ded = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .image = image};
        ImportNb in = {.sType = ST_IMPORT_NATIVE_BUFFER_INFO_OHOS, .pNext = &ded, .buffer = buffer2};
        VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &in,
                                    .allocationSize = np.allocationSize,
                                    .memoryTypeIndex = np.memoryTypeBits ? (uint32_t)__builtin_ctz(np.memoryTypeBits) : 0};
        VkDeviceMemory memory;
        r = vkAllocateMemory(device, &mai, NULL, &memory);
        if (r == VK_SUCCESS) r = vkBindImageMemory(device, image, memory, 0);
        printf("%s import + bind native buffer memory %d\n", r == VK_SUCCESS ? "PASS" : "FAIL", r);
        if (r == VK_SUCCESS) {
            static const float blue[4] = {0, 0, 1, 1};
            static const uint8_t blue_bytes[4] = {0, 0, 255, 255};
            clear_and_check(queue, family, image, blue, buffer2, blue_bytes, "imported image clear blue -> bytes");
            vkDestroyImage(device, image, NULL);
            vkFreeMemory(device, memory, NULL);
        } else {
            ++failures;
        }
    } else {
        ++failures;
    }
    printf("RESULT %s (%d failing)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
