# Revision 28: let Mesa pick the KGSL GPU, force USB device role

Not phone-tested when written (2026-11-19).

## Revision 27 phone result (2026-11-19)

The UI booted but was still slow. Touch and buttons could not be judged, and ADB did not appear.

- All 5 rebuilt Mesa libraries were bound over the vendor copies, but render_service logged:
  - `MESA-EGL: egl: failed to create dri2 screen`
  - `ohos: freedreno (kgsl) screen creation failed`
  - `ohos: using software rendering (kms_swrast)`
- Cause: `loader_get_driver_for_fd()` returns `kgsl` for a KGSL fd only when `MESA_LOADER_DRIVER_OVERRIDE` or the
  Termux-specific `TERMUX_VA_GPU_BACKEND=kgsl` is set. Otherwise it falls back to DRM version/PCI queries, which fail on a
  non-DRM fd, so the pipe loader gets no driver name. (The fork also defaults to Zink unless one of those is set.)
- ADB: `role switch a600000.ssusb-role-switch was none -> device: Read-only file system`. The guest `/sys` is read-only.
  The role is `none` because nothing detects the USB-C partner.
- Read-only check on ColorOS:
  - The ADSP, modem, CDSP and SoCCP remoteprocs are running. Firmware comes from the stock `modem` partition (vfat, ro, at
    `/vendor/firmware_mnt`) and `dsp` (ext4, ro, at `/vendor/dsp`), mounted by `fstab.qcom`.
  - The typec data role is `[device]`, and `/sys/class/usb_role/a600000.ssusb-role-switch/role` is `rw-r--r--`
    (userspace-controllable).
  - Under DSU HarmonyOS no remoteproc is booted (no firmware partitions mounted, no ueventd), so USB-C detection,
    charging and audio services on the ADSP are absent.

## Change

- **Mesa** (`op13/mesa/0001-…patch`): in OHOS builds `loader_get_driver_for_fd()` returns `kgsl` when the fd is the same
  character device as `/dev/kgsl-3d0`. This is the fork's own TERMUX check, without the Termux-only switch, so render_service's
  EGL creates the freedreno KGSL screen.
- **Bootstrap**: `force_usb_device_role()` writes `device` to `a600000.ssusb-role-switch/role` from the Android root
  (writable `/sys`), right after the device namespace is built and before the device filter. The before/after state is logged,
  so the dwc3 UDC exists when `hmos-usb-manager` looks for it.

## Deferred

Booting the ADSP/CDSP (charging, audio, sensors, USB-C detection) needs the stock `modem` and `dsp` partitions mounted
read-only plus the pd-mapper/QRTR userspace that Android provides. That is a separate step.
