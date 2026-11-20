# Revision 32: undo the panel colour rotation with the DSPP PCC, clock floors for 120 Hz, late hdc

Not phone-tested when written (2026-11-20).

## Revision 31 phone result (2026-11-20)

The phone no longer overheats. Blue now shows as green; the UI feels below 120 Hz and stutters "more weirdly"; the owner
prefers revision 30.

- `cpufreq policy0/policy6: performance -> walt (ok)`: cores idled at 384 MHz / 1.02 GHz, GPU at 342 MHz, 48% busy.
- `op13-vk-colortest` **passes everything**: both OHOS extensions are advertised, device creation works, the swapchain
  image path (`VkNativeBufferOHOS`, clear red → bytes `ff0000ff`), acquire/release, and the import path
  (`vkGetNativeBufferPropertiesOHOS` + `VkImportNativeBufferInfoOHOS`, clear blue → `0000ffff`).
- The composer's frame dump of the **scanned-out buffer has correct colors**. With the observations of revisions 30 and 31,
  the panel path applies a fixed channel **rotation** after the SDE fetch:
  - ABGR8888 fetch (R,G,B) is shown as (B,R,G): blue → pink (revision 30);
  - ARGB8888 fetch (B,G,R) is shown as (R,B,G): blue → green (revision 31).

  A brute-force check of every GPU byte order × DRM fourcc finds no combination that cancels a rotation.
- hdc: hdcd was started by init only at 262 s and exited with code 255 within 10 ms; the 20 s gadget window had passed.

## Change

- **Colours** (`hdi_drm_composition.cpp`): scanout is back to native ABGR8888. Each commit sets the CRTC
  `SDE_DSPP_PCC*` property to the inverse rotation (out R = in G, out G = in B, out B = in R; coefficient 1.0 = 32768).
  The CRTC's property names are logged once. `/dev/shm/op13-pcc` (`off`, `rotate2`, `scale=N`) and
  `/dev/shm/op13-scanout` allow live checks.
- **120 Hz** (`set_cpu_governors()`): WALT stays, but each policy gets a floor of ~44% of its maximum (≈1.56 / 1.96 GHz),
  and the GPU devfreq minimum is 525 MHz, because Android's touch/top-app boosts that normally lift WALT are absent.
- **hdc** (`usb_manager.c`): the manager keeps polling and adds the hdc function (unbind, link, rebind) whenever hdcd's
  endpoints appear.
