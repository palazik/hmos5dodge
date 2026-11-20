# Revision 31: correct scanout colors, remove per-frame stalls, app mapper access, Vulkan OHOS shim, hdc

Not phone-tested when written (2026-11-20).

## Revision 30 phone result (2026-11-20)

Touch and buttons work; SceneBoard stays up. Colors are still wrong (blue/pink); the UI has short stutters; the phone
gets hot. It later switched itself off; there is no pstore record, and the 180 s recorder does not cover that time.

- **Colors.** `op13-gpu-colortest` passes every case on the Adreno 830: RGBA/BGRA, linear and UBWC-tiled, NV12/NV21,
  and OH_NativeBuffer render/sample through `EGL_NATIVE_BUFFER_OHOS`. The SDE format table in the stock `msm_drm.ko`
  is standard (`AB24` → R,G,B,A; `AR24` → B,G,R,A). The owner's HarmonyOS screenshot (render_service's own output) has
  **correct** colors: blue wallpaper, orange Video icon, red AppGallery. The panel shows red and blue exchanged (light
  blue as peach/pink). So the bytes are right and the exchange happens at scanout.
- **Frame pacing.** render_service's per-frame vsync-lookup errors show 120 Hz with most frames at 8–12 ms, some at 16–20 ms,
  and 11 stalls over 25 ms in 97 s (worst 713 ms). Per frame, its main thread fails
  `get hdi service chipset_vsync_service` (a hdf_devmgr IPC plus two error logs), and composer_host logs an error and two
  warnings because five per-frame VDI hints returned NOT_SUPPORT.
- **CPU/heat.** `formrenderservice` and `commonUI` retry `libmapper_service_1.0.z.so` every ~5 ms
  (`No such file or directory`): app processes do not see `/hmos/display`, where the mapper bridges were installed.
  CPU clocks are at `performance` maximum.

## Change

- **Scanout** (`hdi_drm_layer.cpp`): RGBA/RGBX buffers are scanned out as ARGB/XRGB8888, which cancels the exchange.
  `/dev/shm/op13-scanout` (`native`, `swap` (default) or a fourcc) overrides it live for checks.
- **Frame dump** (`hdi_drm_composition.cpp`): about 45, 90 and 180 s after the first frame, a 1/8-scale PPM of the
  scanned-out buffer goes to `/dev/shm/op13-frame-N.ppm`, with the DRM fourcc and sample pixels logged.
- **Per-frame stalls**: `libchipset_vsync.z.so` is replaced by a no-op stub (`chipset_vsync_stub.c`; render_service is
  its only user and imports three functions). `SetDisplayClientCrop/Damage`, `SetLayerVisibleRegion`, `SetLayerMaskInfo`
  and `SetLayerColor` succeed as no-ops.
- **App mapper access**: the three mapper/metadata bridges are also shipped in `/vendor/lib64/chipsetsdk`, where app
  processes look. `/vendor/lib64/libdrm.so` is Android's (bionic) and comes first in the search order, so the HarmonyOS
  libdrm ships as `/vendor/lib64/chipsetsdk/libdrm_ohos.z.so`. The display VDIs and all Mesa libraries link that name.
- **Vulkan** (`mesa/vk_ohos_shim.c`, `libvulkan_op13_ohos.so`, now the registered ICD in front of Turnip):
  `VK_OHOS_native_buffer` and `VK_OHOS_external_memory`, implemented as linear DRM-modifier dma-buf images with the
  buffer's real pitch and sync-fd semaphore/fence import/export. `/system/bin/op13-vk-colortest` checks the swapchain and
  import paths over ADB. render_service stays on GLES until that test passes on the phone.
- **hdc**: the ADB gadget also carries an `ffs.hdc` FunctionFS for hdcd (`/dev/usb-ffs/hdc`) if hdcd is ready within
  20 s; init enables `persist.hdc.mode.usb` and sets `const.hdc.secure 0`.
- **CPU governor** (`set_cpu_governors()` in the bootstrap): the stock kernel boots every cpufreq policy with
  `performance`, and ColorOS's vendor init normally switches them to WALT. Under DSU HarmonyOS nothing did (and the
  guest's `/sys` is read-only), so revision 30 ran all cores at 3.5/4.3 GHz permanently. The bootstrap now selects
  `walt`, or `schedutil` if WALT is absent, for each policy and logs the result.
