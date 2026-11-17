# Revision 19: restore the cust-layer init plugin (SceneBoard prerequisites)

The first fix aimed directly at the missing SceneBoard launch. Not phone-tested when written (2026-11-17).
It also keeps the revision 18 /data evidence collection.

## Cause found offline

The guest binds `/hmos/init-modules` over `/system/lib64/init`. Every revision up to 18 staged only
`libselinuxadp` and `libinit_context`. The donor has 22 init plugins (8 top-level, 14 `autorun/`).

`autorun/libhmos_cust_init` sets `const.cust.config_dir_layer`, `const.cust.custPath`, `devmodel`, `vendor` and
`region` from `/system/etc/cust/baselayer.cfg`. Without it, `libconfigpolicy_util` falls back to
`/system:/chipset:/sys_prod:/chip_prod`, and the layers named by `sys_prod:/variant/hw_oem/factory/etc/cust/system_layer.cfg`
(`/system/variant/phone/base`, `/system/variant/phone/china`) are invisible. That removes:

- `base/etc/app/install_list_capability.json`, the only system-app capability list. Its `com.ohos.sceneboard` entry
  sets `keepAlive`, `allowAppUsePrivilegeExtension`, `allowAbilityExcludeFromMissions` and others. The systemui, settingsdata
  and launcher entries are in it too.
- `base/etc/app/install_list_permissions.json`: SceneBoard's pre-granted permissions.
- `base/etc/param/hw_defaults.para`: 13 `persist.sys.abilityms.*` keys, `const.abilityms.*`, `const.window.*` and
  `const.product.os.dist.*`.

Revision 17 logged `SystemReadParam failed! name is:const.cust.config_dir_layer` and
`Failed to install module udidmodule`.

A separate check, from OpenHarmony 5.0.3 `SceneBoardJudgement`: the `/etc/sceneboard.config` gate does not exist anywhere in the donor's
system, sys_prod, chip_prod or cust images, so this Huawei build does not use it.

## Change

`init_plugins.py` stages the hash-recorded donor plugins `libselinuxadp`, `libinit_context`, `libudidmodule`, `libeventmodule`,
`autorun/libhmos_cust_init` and `autorun/libinit_security`. The verifier requires exactly that set.

`libhmos_cust_init` dlopens `liboeminfo_shared.so`, which only opens `/dev/oeminfo/oeminfo_nvm` and `/dev/oeminfo/oinv`.
Those are Huawei kernel nodes that do not exist on OP13 and are outside the guest device filter, so oeminfo read, write and erase
all fail. The plugin then uses the `factory` model layer list, the same as an unprovisioned donor.

Excluded after audit:

| Plugin | Reason |
|---|---|
| `libinit_bootDetector` | Boot-fail ioctls and recovery/reboot actions |
| `libinit_eng` | Mounts `eng_*` block partitions |
| `libinithp` | zram block setup |
| `libinit_usbconfig` | Would reconfigure the USB gadget |
| `libkickdog` | Hardware watchdog |
| `libtee_chmod` | TEE device permissions |
| `libkdump_resize` | kdump |
| `libinit_dec`, `libprocess_exit`, `libinit_manufacture` | Need Huawei kernel nodes or device-tree properties |
| tracing, bootchart, quickfix, file/prop migration | Not needed to boot |
| `reboot/*` | Reboot handling |

## Not addressed yet

- `av_codec_service` SIGSEGV (boot video). The revision 19 fault-log capture should show its stack.
- `/module_update` is not mounted (the ArkWebCore module image), so web components will be missing. This does not affect SceneBoard.
- cgroup v1 hierarchies fail to mount (`/dev/pids`, `/dev/cpuset`, ...). Upstream appspawn only logs this.
