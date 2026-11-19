# Revision 26: load Adreno firmware, fix ADB controller selection

Not phone-tested when written (2026-11-19).

## Revision 25 phone result (2026-11-19)

The UI booted but was as slow as revision 24. There was no boot animation, and the host saw no ADB device.

- GPU: Mesa did try "freedreno on kgsl". The first `/dev/kgsl-3d0` open came from render_service and logged
  `kgsl-3d0: Direct firmware load for gen80000_sqe.fw failed with error -2`. Mesa then reported
  `ohos: cannot open /dev/kgsl-3d0` and fell back to `kms_swrast`. With no ueventd, the kernel only loads firmware directly,
  resolved in the opener's root, and render_service's sandbox has no vendor firmware. The stock vendor ships
  `firmware/gen80000_{sqe.fw,gmu.bin,zap.mbn,aqe.fw}`.
- ADB: `hmos-usb-manager` ran but logged `expected exactly one USB device controller`, without saying what it found.
- Touch: `/proc/bus/input/devices` shows `touchpanel` = `event8`, `PROP=2` (INPUT_PROP_DIRECT) with MT axes. The guest has
  `/dev/input/event0..8`, so the kernel side is fine. multimodalinput still shows no touch event. Its info logs were absent:
  `hilog.loggable.global = W` is set at 19.76 s.

## Change

- **GPU firmware**: after the device namespace is built and while the bootstrap still sees the Android root, it writes
  `/vendor/firmware` to `/sys/module/firmware_class/parameters/path` and opens the guest `/dev/kgsl-3d0` once. KGSL loads
  SQE, GMU and zap on that first open and keeps them for later sandboxed openers. Each step is logged.
- **ADB**: `usb_manager.c` logs every `/sys/class/udc` entry, prefers a `dwc3` controller (Android uses `a600000.dwc3`), and
  waits up to 30 s for it to appear.
- **Logs**: a `param:` trigger resets `hilog.loggable.global` from W to I, so multimodalinput's info-level device
  adoption is recorded.
