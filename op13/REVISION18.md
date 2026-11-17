# Revision 18: framework evidence from /data

A diagnostic build. It does not change boot behaviour. Not phone-tested when written (2026-11-17).

## Revision 17 phone result (2026-11-17 capture)

The screen stayed black after the OnePlus logo.

**The composer trace shows RenderService handling the screen correctly:**

- 53.798: `RegHotPlugCallback` called; the hot-plug event is delivered for display 0, which is connected.
- 53.945: capability, mode and supported-modes queries; `SetDisplayPowerStatus 0` (panel on); `RegDisplayVBlankCallback`;
  `SetDisplayVsyncEnabled 1`.
- 53.989: `SetDisplayVsyncEnabled 0`, called from another thread 44 ms later. That matches RenderService
  sampling hardware vsync and then switching to its software model, so vsync delivery looks functional.
- **No `CreateLayer`, `SetLayerBuffer`, `SetDisplayClientBuffer`, `PrepareDisplayLayers` or `Commit` ever happens.**
  RenderService is ready, but no client gives it anything to compose.

**No application process is spawned.** Every one of the 12 SIGKILLed "Unknown" processes is SAMGR unloading an idle
on-demand SA. SceneBoard (launcher, lock screen and window shell) is never started, and
`bootevent.launcher.ready` / `lockscreen.ready` are never set. The donor ships `/system/app/SceneBoard`.

Other findings:

- `av_codec_service` dies with SIGSEGV, which explains the boot video failing after about 6 s.
- `huawei_id_svc` and `hianalytics` die with SIGSYS (seccomp).
- User 100 is created and unlocked: `GenerateUserKeys` at 34.2 s, `ActiveUserKey` succeeds at 37.1 s,
  `bootevent.account.ready` at 38.4 s. The earlier `user id [100] is not exist` FATAL lines come from before creation.
- The cloud hmdfs mount fails (the OP13 kernel has no hmdfs or sharefs; `local_storage.c` only emulates /data-sourced
  views). Upstream `MountManager::HmdfsMount` only logs a cloud failure and still returns success, so this is not the blocker.
- The revision 17 hidumper queries all returned `request error`. The recorder has no access token and runs in SELinux
  context `kernel`.

## Change

The hidumper queries are replaced with sources that need no IPC permission:

- `param get`, a full system-parameter dump, at 120 s (boot events and service/account/ability state).
- At 60 s and 125 s, the recorder reads from the guest /data:
  - installed bundle directories (`/data/app/el1/bundle/public`), to see whether SceneBoard is installed;
  - account service JSON state (`/data/service/el1/public/account`), to see whether user 100 is created and activated;
  - faultloggerd plain-text crash reports (`/data/log/faultlog/{faultlogger,temp}`), for the av_codec_service stack and any
    framework or SceneBoard crashes.

  Each file head is capped at 6-12 KiB, file counts are bounded, and symlinks are listed but never followed. All of this stays within the
  recorder's 4 MiB total.
- The recorder keeps exactly one capability, `CAP_DAC_READ_SEARCH`, because those files are owned by other services. It
  still has no write, mount, module or reboot capability, and NNP stays set.
