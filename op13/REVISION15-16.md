# Revision 15: real boot animation through the picture player, metadata HDI

Not phone-tested when written (2026-11-17).

## Revision 14 phone result (2026-11-17 capture)

The owner saw the OnePlus logo, then a dark screen.

- All 615 load-order rows completed: 57 required loaded, 483 optional loaded, 75 already present,
  **0 optional failures**, 3 boot arguments forwarded.
- **The revision 12 panel death is gone.** No CESTA `mode2 entry failed`, no DSI command timeout, no ESD failure and
  no `PANEL_DEAD` in the whole capture. The composer modesets the panel at 20.06 s.
- `bootanimation` set `bootevent.bootanimation.started` and `.finished` 0.3 ms apart (21.508 s), so no frame
  was drawn. The donor `sys_prod/etc/bootanimation` config selects `cust_bootvideo.mp4` (H.264 + AAC). The native
  player ends immediately, so the video branch's end callback fires at once.
- `render_service` retried `libmetadata_service_1.1.z.so` 477 times (namespace lookup, errno 2) starting at about 22 s.
  The donor file exists only in `vendor-lib64/chipsetsdk/`.
- At 50.13 s `render_service` exited with status 0. Init then restarted the dependent display/framework group
  (allocator_host, foundation, composer_host and others got SIGKILL), and the touch driver suspended because the panel was blanked.
  Its reason is not visible: render_service messages are dictionary-encoded ("OpenUuidFile fail") and
  `/data/log/dict` is not available. `ShouldUseDict` is a per-call compile-time bit, not a parameter.
- `foundation` also misses `libdevices_security_interface_service_1.0.z.so`. That service loads the Kirin
  `libsecurity_hal_shared.so`, so it is intentionally not bridged.

## Change

- `graphics_runtime.py` bridges the donor `chipsetsdk/libmetadata_service_1.1.z.so` like the mapper
  services. Its NEEDED libraries are all system chipset-pub-sdk libraries, and it loads our
  `libdisplay_buffer_vdi_impl.z.so`, whose SetMetadata/GetMetadata/ListMetadataKeys/EraseMetadataKey are implemented.
- `boot_animation.py` reads the reviewed donor video from `hmos5/sys_prod.img` (sha256 pinned) at packaging time.
  It converts it to 240 JPEG frames at 30 fps, scaled to 1440x3168 (the same aspect ratio as the 1256x2760 source, so no
  cropping), and writes a deterministic stored ZIP plus an old-format config with an empty video key. The bootstrap
  binds `/hmos/ui/bootanimation` over `/sys_prod/etc/bootanimation`. OpenHarmony bootanimation then
  takes `PlayPicture` (v5.0.3 `IsBootVideoEnabled`: video empty and pictures set), which draws through
  RenderService, the composer and the panel without media codecs. Sound is omitted.
- The frames are ROM-derived and only generated into the image; they are not committed.

## What to look for

- A visible HarmonyOS animation, or at least `bootevent.bootanimation.started` and `.finished` about 60 s apart.
- Whether the `libmetadata_service_1.1.z.so` errors are gone, and whether `render_service` still exits at around 50 s.

## Correction: superseded by revision 16 before any phone test

The OpenHarmony picture player stretches every frame into a centred square with side
min(width, height) (v5.0.3 `boot_picture_player.cpp` `InitPicCoordinates`). The tall 1440x3168
frames above would have been squashed into 1440x1440. Revision 15 was never flashed.

# Revision 16: original boot video stream without the audio track

- media_service and av_codec_service started in revision 14, but audio_server crash-looped
  ("Cannot find service audio_host"; there is no OP13 audio HDI). That plausibly made the player fail
  while preparing the AAC track, which matches started/finished being 0.3 ms apart.
- `boot_animation.py` now remuxes the pinned donor `cust_bootvideo.mp4` with `-c copy -an`. The H.264
  elementary stream is hash-checked as unchanged (same 1256x2760, 60 fps, full screen). Only the audio track
  and container metadata are dropped. The donor-format config points `cust.bootanimation.video` at it.
- Chosen by the owner over the original file (likely to fail until audio works) and a cropped square
  picture sequence. Restore the original file once audio_server works.
- The metadata HDI bridge from revision 15 is kept.
- If the animation still ends instantly, the next suspect is H.264 decoding (no Huawei hardware decoder;
  whether the av_codec software decoder is selected is unverified).
- Images are built incrementally from the previous revision's EROFS image, which cannot delete files.
  The unreferenced revision 15 `hmos_bootanim.zip` (11 MB) therefore remains in `/hmos/ui/bootanimation`.
  The config does not reference it, and the verifier allows exactly that one leftover file.
