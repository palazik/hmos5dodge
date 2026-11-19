# Revision 30: audio HDI host so SceneBoard stops freezing; GPU color self-test

Not phone-tested when written (2026-11-19).

## Revision 29 phone result (2026-11-19)

No lag, but buttons, power key and touch seemed dead. The screen later woke on its own.

- **audio_server now runs** (0 crashes; the `HOME=/` fix worked).
- **Touch reaches multimodalinput**: `touch axes: touchpanel (/dev/input/event8) 6 degenerate axes widened, 0 failed`,
  and the MMI log has pointer down/move/up events (`P:D`, `P:M`, `P:U`). They are dropped with
  `UpdateTouchScreenTarget: The touchWindow is nullptr` and `Not found event down target window`.
- **Buttons reach multimodalinput** (512 key events; at 11:24:59 a `pmic_pwrkey` press was followed 0.2 s later by
  `SetDisplayPower state=2` and `SCREEN_ON`).
- **SceneBoard is still killed** with `THREAD_BLOCK_6S`, now inside binder calls to audio policy
  (`AudioPolicyProxy::GetNetworkIdByGroupId`, from `AudioGroupManager::Init`/`GetVolumeGroupManagerSync`).
  audio_server logs `[GetVolumeGroupInfos]load default device time out` and
  `stub:IAudioPolicy deal request code:57 cost time:5000ms`.

So windows, touch targets and visible key reactions all depend on SceneBoard surviving.

## Cause

audio_server's policy reads `/chip_prod/etc/audio/audio_policy_config.xml` or `/vendor/etc/audio/audio_policy_config.xml`.
Our `/vendor` is the OP13 Android vendor, so neither exists, and no `audio_host` HDF host was configured (the port kept
only the composer, allocator, auth and power hosts). The policy therefore never opens a default output device, and each
volume or group call waits 5 s for it.

## Change

- **Startup** (`prepare_startup.py`): add the donor `audio_host` HDF host (id 2, uid audio_host) with
  `audio_primary_device` (`libaudio_primary_driver.z.so` → `audio_manager_service`) and `effect_device`. The ADM, PnP
  and voice-trigger devices are dropped because their modules are not in the donor vendor.
- **Vendor additions** (`prepare_vendor.py`): the donor `/vendor/etc/audio/audio_policy_config.xml` and
  `audio_concurrency_config.xml`, plus the audio HDI chain that is loaded by path at run time:
  `libaudio_manager_service_4.0` → `libaudio_primary_impl_vendor` (VDI adapter) → `libaudio_primary_impl`.
  The last is the reference port's tinyalsa VDI (`hmos2sweet-reference/src/audio/sweet_audio_vdi.cpp`). It reports the
  `primary` adapter and, with no sound card (the ADSP is not booted), consumes frames in real time, so streams never stall.
- **GPU color self-test** (`op13/mesa/gpu_colortest.c`, `/system/bin/op13-gpu-colortest`): blue and pink are swapped
  on screen, in the UI and in the boot video, both of which render_service composites with GLES into one RGBA_8888 layer.
  The composer, gralloc and Mesa fourcc mappings are all consistent RGBA, so the test renders and samples known colors
  through RGBA and BGRA textures (Skia keeps images and layers in BGRA when `GL_EXT_texture_format_BGRA8888` exists) and
  reads them back. Run it over ADB: `op13-gpu-colortest`.
- The image is rebuilt from a full base (`build_multipart.py`, then `extend_with_dlkm.py`).

## Still open

- Colors: fix after the self-test result.
- Vulkan for render_service.
- Real sound needs the ADSP and OP13 mixer paths.
