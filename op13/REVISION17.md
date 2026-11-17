# Revision 17: diagnostics for the missing first frame

A diagnostic build. It does not change boot behaviour. Not phone-tested when written (2026-11-17).

## Revision 16 phone result (2026-11-17 capture)

The owner saw the OnePlus logo, then a black screen.

- Modules: the same 615 rows, 0 failures. The panel stays healthy: no CESTA, DSI, ESD or PANEL_DEAD errors.
- The metadata HDI errors are gone (0, versus 477 in revision 14). `render_service` no longer exits around 50 s,
  so there is no cascade restart.
- `bootanimation`: ready at 20.27 s; started and finished together at 26.27 s. Instead of failing instantly, the
  video player now fails after about 6 s. H.264 decoding or player preparation is still unverified.
- **The composer never commits a frame.** Its DISP tag is captured at info level, yet neither
  `commit ok with strategy` (logged on every success) nor `commit keeps failing` appears. After the initial
  modeset and `SetDisplayPowerStatus 0 (panel on)` at 20 s, nothing reaches the composer, and no backlight
  request arrives either. The black screen is therefore not caused by the boot video: RenderService never
  submits any frame, including the boot animation's.

## Change

- Composer: info-level trace of the first three calls (then every 600th, with a count) for hot-plug registration
  and callback delivery, capability/mode queries, power/backlight, vsync, layer creation, layer and client
  buffers, PrepareDisplayLayers and Commit (with result and fence). This shows where RenderService stops.
- Recorder: bounded `hidumper` snapshots at 35-38 s and 100-102 s: the registered system-ability list,
  RenderService (SA 10) `screen` and `surface`, and DisplayManager (SA 3203) `-a`. The IDs come from the
  revision 14 SAMGR DeadSA records. The graphics HiLog snapshot depth is raised from 200 to 1000 lines.

## Reading the next capture

| Last trace seen | Meaning |
|---|---|
| No `hotplug callback registered` | RenderService never connected to the composer HDI. |
| Hot-plug delivered, but no capability/mode queries | RenderService ignored or rejected the screen. |
| Modes queried, but no layers/buffers | Screen created, but nothing composed (check the hidumper `screen` power and mode output). |
| Buffers set, but no Commit | Composition stalls between prepare and present. |
