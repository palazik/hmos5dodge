# Revision 20: install apps with native code, avoid io_uring seccomp kills

Not phone-tested when written (2026-11-18).

## Revision 19 phone result (2026-11-18 capture)

The screen stayed black. The new /data evidence and parameter dump show:

- The cust-layer fix worked: `const.cust.config_dir_layer` lists `/system/variant/phone/{base,china}`, and the
  `persist.sys.abilityms.*` / `const.abilityms.*` parameters are present.
- User 100 is fully up (`account_info.json`: `isActived`, `isCreateCompleted`, `isForeground`,
  `isLoggedIn` and `isVerified` are all true).
- `bootevent.bms.main.bundles.ready = false`. 92 bundle directories exist, and `com.ohos.sceneboard` is not among them.
- Of the donor bundles, 166 were parsed from `/system/app` and `/sys_prod/app`:

  | | Installed | Missing |
  |---|---|---|
  | With native `libs/*.so` | **0** | 77 |
  | Without native libraries | 84 | 5 |

  SceneBoard (170 MiB, with native libraries) and SettingsData are among the missing.
- AbilityManager logged `ImplicitQueryInfoByPriority: can't find target AbilityInfo` 103 times: its boot loop is waiting for
  SceneBoard.
- Cause: `libinstalls.z.so` enforces code signatures through `libcode_sign_utils.z.so`, which enables fs-verity with
  a signature and uses XPM (`/proc/sys/kernel/xpm/xpm_mode`). The OP13 kernel has `CONFIG_FS_VERITY=y` (and the bootstrap's
  ext4 has `-O verity`) but no `FS_VERITY_BUILTIN_SIGNATURES` and no XPM. The encryption-metadata check is userspace ELF
  parsing only.
- `huawei_id_svc` and `hianalytics` crash with SIGSYS in libuv `uv__iou_init`. libuv enables io_uring on this newer kernel,
  and the native seccomp policy kills the syscall.
- `av_codec_service` SIGSEGV: `libfcodec.z.so FCodec::FillFrameBuffer+476` dereferences NULL at 0x30. OpenHarmony 5.0.3
  `UpdateSurfaceMemory` / `RequestBufferFromConsumer` store `CreateAVBuffer` results unchecked when no surface buffer is
  obtained. `allocator_host` logged no allocation at all, so the request never reached the allocator. The boot video
  therefore still fails; the exact cause is not identified.

## Change

- `code_sign_compat.c` builds `libop13_code_sign.so`, which defines the four `CodeSignUtils::EnforceCodeSign*` entry points
  imported by `libinstalls.z.so`. All four mangled names are verified against the donor import and export tables. Each call is
  logged (`OP13CodeSign`) and returns success. HAP signature verification (hapverify) is untouched. The stock kernel cannot
  enforce code signatures at exec time, so no protection available on this device is lost.
- `native_compat.py` places the shim first in the patched `libinstalls.z.so` `DT_NEEDED` list, labels it `system_lib_file`,
  and adds `op13_compat_env.cfg` with `export UV_USE_IO_URING 0` in `pre-init`. libuv honours this variable, and every service
  and appspawn child inherits it.
- The verifier checks the shim hash, the installer hash and dependency order, and the environment config.
