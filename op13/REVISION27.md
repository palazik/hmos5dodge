# Revision 27: Adreno 830 GPU rendering with rebuilt Mesa, USB role for ADB

Not phone-tested when written (2026-11-19).

## Revision 26 phone result (2026-11-19)

The screen stayed black, with no boot animation visible and no ADB.

- **The firmware fix worked; the GPU started.** "firmware search path set to /vendor/firmware" and "GPU opened once" were logged,
  and render_service's Mesa then reported `ohos: using Adreno GPU (freedreno on kgsl)`, with EGL initialized (`ver=1.5`).
- **The old Mesa faulted the GPU** on its first submission:
  - `CP | Protected mode error | WRITE | addr=0x09313`, then `TIMEOUTFAULT`
  - `gpu fault threshold exceeded 3 faults in 2000 msecs`

  KGSL then bans render_service, so nothing is composed, although `lockscreen.ready` and `launcher.ready` are set.
- **0x9313** is `VPC_UNKNOWN_CNTL` on A8xx (`VPC_DS_CLIP_CULL_CNTL_V2` on A6xx/A7xx). It is **not** in the OnePlus 13 KGSL
  protect table (OnePlusOSS `gen8_0_0_protected_regs`). The shipped Mesa was a 26.1 snapshot built for the reference port's
  Adreno 618, older than the March 2026 upstream "a830 bringup fixes" (MR 40458, CRB vs NCRB packet handling). The fork's newer
  builds are reported running GL, GLES and Vulkan on A830 over KGSL.
- **Boot animation**: the HarmonyOS video played. `started` at 23.74 s and `finished` at 33.37 s cover the 8 s clip, decoded by
  software FCodec (the hardware codec HDI `libcodec_component_manager_service_3.0` is absent). Its frames were never composed because
  render_service was banned from the GPU. In revision 24 it was invisible because software composition was too slow. The boot
  animation and the lag share one root cause.
- **ADB**: `0 UDC(s) present`. On ColorOS (read-only check) `/sys/class/udc/a600000.dwc3` exists only while
  `a600000.ssusb-role-switch` is `device`.

## Change

- **Mesa rebuilt from source** (`op13/build_mesa.sh`, `op13/mesa/0001-op13-ohos-platform-mesa27.patch`):
  - Source: lfdevs/mesa-for-android-container `dev/adreno-main` 1e6ee4d (27.0.0-devel, 2026-11-18).
  - freedreno (GLES) and Turnip (Vulkan) on KGSL, plus Zink and softpipe. The HarmonyOS EGL platform is ported to the Mesa 27
    DRI loader API.
  - `EGL_PLATFORM_OHOS_KHR=0x34E0` and `EGL_NATIVE_BUFFER_OHOS=0x34E1`, verified in the donor libEGL, render_service,
    libnative_image and libegl_image.
  - No KMS display WSI, since the musl libc here has no `pthread_cancel`.
  - Every DT_NEEDED resolves to a donor library. Both drivers list "Adreno (TM) 830", and Turnip exports the three
    `vk_icd*` entry points.
- `mesa_runtime.py` stages the libraries in `/hmos/display/mesa`. libgallium is renamed to the vendor slot name
  `libgallium-26.1.0-devel.so` (SONAME and NEEDED rewritten). A Vulkan ICD is added at
  `/system/etc/vulkan/icd.d/freedreno_icd.aarch64.json`.
- The bootstrap binds the rebuilt EGL/GLES/gallium libraries over `/vendor/lib64/chipsetsdk/*` and logs how many it bound.
  Software rendering is still not forced; if the kgsl screen fails, the EGL platform falls back to kms_swrast by itself.
- The revision 26 firmware priming and the `GpuApitype=0` (GLES) setting are kept.
- `usb_manager.c`: when no UDC exists, the manager writes `device` to `/sys/class/usb_role/*/role` and logs each result.
