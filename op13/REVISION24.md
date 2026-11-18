# Revision 24: scanout-safe dumb buffers with DRM CPU mapping

Not phone-tested when written (2026-11-18).

## Revision 23 phone result (2026-11-18 capture)

The screen stayed black, without the revision 22 logo hang. About 30 s in, the phone reset to ColorOS.

- The DRM permission fix worked: no `Permission denied`, and no render_service crash loop before the freeze.
- The composer modeset the panel at 23.60 s (`dsi_display_set_mode`, panel init DCS, backlight 1433). The recorder's last
  kernel record is at 23.74 s, and nothing was written after it, although the recorder syncs to `/metadata` continuously. The
  whole phone hard-froze, and a hardware watchdog reset it.
- No HiLog covers the window: the snapshots run at 10-16 s and 25 s.
- The Android-side checks (read-only) found nothing. `/sys/fs/pstore` is empty. The OnePlus `criticallog` has only charger
  events. `ro.boot.bootreason=reboot,shell` belongs to a later reboot (uptime 2 min at 21:13).

## Analysis

Two things were new in the path that reached the display:

1. **Scanout of system-heap dma-bufs** (introduced in revision 21). Revision 23 is the first build in which render_service got
   far enough to submit one.
2. **Mesa software EGL opening `/dev/dri/card0`** alongside the composer, possible only since the revision 23 permission fix.

Dumb buffers are proven safe for scanout: in revision 12 the composer committed real AR24 frames from dumb buffers and the SDE
event log showed `frame_done`, with no hang. Their only defect was dma-buf `mmap` (EINVAL).

## Change

`sweet_buffer_vdi.cpp`:

- Allocation returns to DRM dumb buffers. The heap path is kept but disabled (`USE_SYSTEM_HEAP = false`).
- `Mmap` first tries dma-buf `mmap`. If that fails, it imports the PRIME fd into the process's own `/dev/dri/card0` file, calls
  `DRM_IOCTL_MODE_MAP_DUMB`, maps the fake offset, and closes the GEM handle; the mapping keeps the object alive.

If the phone still freezes, suspect 2 remains.
