# Revision 23: let render_service open the DRM device

Not phone-tested when written (2026-11-18).

## Revision 22 phone result (2026-11-18 capture)

The phone stayed on the OnePlus logo, a regression from revision 21's black screen: the HarmonyOS composer never took over
the panel.

- The bootstrap and module stages completed normally (615 rows, 0 failures).
- `render_service` crashed with SIGSEGV every 5 s from 24.8 s (24.8, 29.8, 34.7, 39.6 and 44.5 s), and init stopped restarting it
  after the fifth crash. Each crash also restarted composer, allocator and foundation.
- Crash: `RenderContext::SetUpGpuContext` -> `strlen(0x35)`, during GL context setup in `RSUniRenderThread::InitGrContext`.
- Preceding HiLog lines:
  - `MESA: EGL: warning: failed to open /dev/dri/card0: Permission denied`
  - `OpenGLWrapper: eglInitialize Error`
  - `Call To OpenGL ES API With No Current Context`
- Cause: the bootstrap created `/dev/dri/card0` and `renderD128` as `root:1003` with mode 0660. render_service (uid 1003,
  a different gid) could not open them. Revision 21 crashed in the Vulkan path before EGL needed the device, which is why this
  only appeared once GLES was forced.

## Change

`dsu_bootstrap.c` uses mode 0666 for DRM nodes, matching the donor `/system/etc/ueventd.config`
(`/dev/dri/card0 0666 0 1003`, `/dev/dri/renderD128 0666 0 1003`). The nodes exist only in the guest's private /dev and stay
inside the cgroup device filter. Rendering stays software Mesa for this test; the owner chose one change per test.
