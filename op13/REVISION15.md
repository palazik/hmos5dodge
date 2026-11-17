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
