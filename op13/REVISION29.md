# Revision 29: give HarmonyOS init the kernel's HOME so audio_server starts; make libinput accept the touchscreen

Not phone-tested when written (2026-11-19).

## Revision 28 phone result (2026-11-19)

The boot animation now plays, the GPU renders (freedreno on KGSL), and ADB works (`hmos5dodge`). Colors look wrong
(details pending). Touch and buttons appeared dead.

Live ADB session (read-only):

- The system is idle (about 790% of 800% CPU idle, GPU 0% busy), so the "lag" is not a rendering cost.
- **SceneBoard is killed every ~30 s** with `appfreeze THREAD_BLOCK_6S`. The main-thread stacks all block in
  `AudioPolicyManager::GetAudioPolicyManagerProxy()`, called from the volume panel, the status bar and VolumeController.
- **audio_server crash-loops**: init logs `exit with code : 255` 8 times, then leaves it stopped (`ctl.audio_server = 5`).
  The last line is `[paDaemonThread] Exiting ohos_pa_main`, 6 ms after start; audio_server `_exit(-1)`s when PulseAudio
  returns. SA 3009 (audio policy) never appears, and multimodalinput also logs `audio policy manager proxy is NULL`.
- Buttons reach multimodalinput (`event created by:pmic_pwrkey`), and power key presses toggle the panel in composer_host
  (`SetDisplayPowerStatus 0/3`). The UI looked dead because SceneBoard was frozen.
- Touch: the Synaptics IRQ count rises (885 → 1576), and `touchpanel` is a direct MT device, but multimodalinput never
  logged it, while it logged `gpio-keys`, `pmic_pwrkey` and `tof8801`. Debug live over ADB once SceneBoard no longer freezes.

## Cause

PulseAudio's own logs go to stderr (/dev/null), so the failure was reproduced off-device. The donor `libpulseaudio.z.so`
was run with `qemu-aarch64-static`, the donor musl loader and libraries, and the same `default.pa`, with a passwd entry like
HarmonyOS's `audio:x:1041:1041:::/bin/false` (empty home):

- without `HOME`: `Failed to get the home directory, not an absolute path:`, then
  `Daemon startup without any loaded modules, refusing to work.` (`ohos_pa_main` returns 1)
- with `HOME=/`: `Loaded "libmodule-suspend-on-idle.z.so"`, then `Daemon startup complete.`

The kernel starts PID 1 with `HOME=/ TERM=linux`, and HarmonyOS services inherit it. The bootstrap exec'd HarmonyOS init
with only `PATH`.

## Change

- **Bootstrap**: HarmonyOS init is exec'd with `HOME=/`, `TERM=linux` and `PATH=/system/bin:/vendor/bin`.
- **Touch** (`fix_touch_axes()` in the bootstrap): ColorOS `getevent -p` shows `touchpanel` advertising ABS_PRESSURE,
  0x21, 0x22, 0x23, ABS_MT_WIDTH_MAJOR and ABS_MT_PRESSURE with `min 0, max 0` (yet `dumpsys input` shows
  ABS_MT_PRESSURE=8 and WIDTH_MAJOR=7). HarmonyOS libinput (`evdev_reject_device` → `evdev_check_min_max`) only tolerates
  zero-range axes in ABS_MISC..ABS_MT_SLOT and otherwise rejects the device with "device has min == max", which is why
  multimodalinput adopted the keys but never the touchscreen. Before the guest starts, the bootstrap now widens every
  zero-range axis of direct multitouch devices with `EVIOCSABS` (pressure/size axes to 0..255, others to 0..1) and logs
  `touch axes: …`.

## Still open

- Colors: need a description or photo.
- Vulkan for render_service: Turnip needs the OHOS native-buffer and surface extensions before `GpuApitype=1`.
- Sound: still no `audio_host` (vendor audio HDI); PulseAudio and the policy now run without output devices.
