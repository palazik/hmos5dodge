# Revision 25: GPU rendering, USB ADB, input diagnostics

Not phone-tested when written (2026-11-18).

## Revision 24 phone result (2026-11-18)

**The HarmonyOS UI booted**, after about 2 minutes of black screen. It was very laggy, touch and buttons appeared dead, and there
was no boot animation. The phone did not freeze or reset; returning to scanout-safe dumb buffers removed the revision 23 hard freeze.

- Buttons work: multimodalinput processed 188 key-down and 2 key-up events. The kernel logs power and volume presses
  (`oplus_bsp_dfr_combkey_monitor`). Software rendering is too slow to show any reaction.
- Touch: the Synaptics `touchpanel` (SPI, registers about 3.6 s on stock, before the guest device stage) resumes on panel
  unblank (`syna_dev_resume`). Yet no pointer or touch event marker appears in multimodalinput warnings, and
  `lastTouchEventOnBackGesture_` is null. MMI links Linux libinput (evdev). Device adoption is logged at info level only, which
  was not captured.

## Change

- **GPU**: the bootstrap no longer exports `OHOS_MESA_SOFTWARE=true`. The vendor `libEGL_impl.so` then tries
  "Adreno GPU (freedreno on kgsl)" through `/dev/kgsl-3d0` (0666). If kgsl screen creation fails, it logs
  `freedreno (kgsl) screen creation failed` and falls back to kms_swrast by itself. The libgallium build lists FD830.
- **ADB** (USB only, key authentication):
  - The bootstrap starts `/system/bin/hmos-usb-manager` (`op13/adb/usb_manager.c`) once after the pivot.
  - The manager creates its own configfs gadget `hmos5dodge` (18d1:4ee7, serial `hmos5dodge`) with a FunctionFS ADB function.
    It never edits stock g1, restores any prior UDC binding on failure, and never affects init.
  - `/system/bin/hmos-adbd` (pinned AOSP/Linux adbd for the native runtime) keeps only `CAP_DAC_READ_SEARCH` and `CAP_SYSLOG`.
  - The owner's public key is staged at build time as `/system/etc/hmos-adb/adb_keys`; it is not committed.
  - Log: `/data/hmos-port/adb.log`, also captured by the recorder.
- **Diagnostics**:
  - A multimodalinput HiLog snapshot at info level (domains 0xD002800-05) at 92 s and 130 s.
  - `/proc/bus/input/devices` and the `/dev/input` listing in the /data evidence.
  - `MESA` and `OpenGLWrapper` added to the graphics HiLog tags, to show the selected renderer.
