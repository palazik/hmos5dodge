# Revision 22: force the GLES render backend

Not phone-tested when written (2026-11-18).

## Revision 21 phone result (2026-11-18 capture)

The screen stayed black, but the buffer fix worked.

- No `mmap ... failed` errors remain: system-heap buffers map.
- `bootanimation` started at 20.36 s and finished at 80.30 s. That is the configured 60 s; the video decoder no longer crashes instantly.
- `render_service` crashed with SIGSEGV (pc 0) four times (21.6, 54.5, 56.3 and 58.1 s) in
  `RSSurfaceOhosVulkan::RequestFrame` -> `NativeBufferUtils::MakeFromNativeWindowBuffer`. It was a NULL Vulkan entry point:
  `persist.sys.graphic.GpuApitype = 1` (Vulkan) and there is no Vulkan driver. Each crash restarted allocator, composer,
  foundation and SceneBoard, so `launcher.ready` was never reached and no frame was displayed.

## Change

`native_compat.py` adds to `op13_compat_env.cfg`: `setparam persist.sys.graphic.GpuApitype 0` in `post-fs-data` (before
render_service starts at about 19.8 s), plus `param:` triggers that reset the value to 0 if it becomes 1 or 2. This mirrors
`hmos2sweet.cfg`. Rendering stays software Mesa (`OHOS_MESA_SOFTWARE=true`).

## GPU plan (after a visible UI)

The vendor `libgallium-26.1.0-devel.so` contains freedreno with the KGSL backend (`kgsl_pipe_new`, `kgsl_bo_new_dmabuf`) and
lists FD830, the OP13 Adreno 830. GLES acceleration should therefore need mainly reverting `OHOS_MESA_SOFTWARE` and giving
render_service access to `/dev/kgsl-3d0`. Vulkan needs a Turnip (KGSL) build for the native musl runtime; the only ICD present
is Huawei's `vulkan.hvgr_v200.so`.
