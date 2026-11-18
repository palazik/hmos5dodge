# Revision 21: CPU-mappable graphics buffers

Not phone-tested when written (2026-11-18).

## Revision 20 phone result (2026-11-18 capture)

The screen stayed black, but HarmonyOS booted.

- 175 bundles were installed (92 before), including `com.ohos.sceneboard`.
- These boot events were `true`: `bms.main.bundles.ready`, `launcher.ready`, `lockscreen.ready`, `wms.ready`,
  `wms.fullscreen.ready` and `boot.completed`.
- RenderService logged `Mmap: mmap fd 41 size 18653184 failed: 22` 16 times. 18653184 = 1472 x 3168 x 4, a full-screen RGBA
  buffer with a 64-pixel-aligned pitch. msm_drm DRM dumb buffers exported through PRIME cannot be mmap'ed (EINVAL). Rendering
  is CPU-based (software Mesa), so no frame was produced and the composer never received a layer or commit.
- SceneBoard: `appfreeze THREAD_BLOCK_6S`. The main thread was stuck in `ArkUIRunPageUrl` from 07:32:38, during the first page load.
- Boot video: `av_codec_service` crashed again in `FCodec::FillFrameBuffer`. The software decoder writes frames through a CPU
  mapping, and a failed mapping leaves a null base that `CreateAVBuffer` stores unchecked. This is very likely the same root cause.
  Correction: the earlier note that the "allocator was never reached" was wrong. allocator_host info logs are simply not
  captured, even when buffers are allocated.

## Change

- `sweet_buffer_vdi.cpp` allocates from `/dev/dma_heap/system` (or `qcom,system`) with `DMA_HEAP_IOCTL_ALLOC`. Allocation keeps
  the same 64-pixel pitch alignment and plane height, and the size is page-aligned. These dma-bufs support mmap, and msm_drm
  imports them for scanout, as Qualcomm gralloc does. The dumb-buffer path stays only as a fallback when no heap is available.
- `hdi_drm_layer.cpp`: before each frame's framebuffer import or reuse, the composer issues `DMA_BUF_IOCTL_SYNC END|RW`, which
  cleans the CPU cache for the cached system heap.
