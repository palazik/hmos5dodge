# hmos5dodge

Experimental **HarmonyOS NEXT 5 port for OnePlus 13**, using the stock OP13
kernel and separate partitions installed through Android DSU.

The port targets the native HarmonyOS framework, SceneBoard and SuperLauncher.
It uses an OP13 stock vendor base with selected native HarmonyOS runtime
additions, matching OP13 DLKM images and the phone's existing stock ODM filesystem.

## Status

**Revision 9 still shows the static OnePlus logo. Revision 10 repairs missing display-host startup definitions.**

Native Binder/token checks pass; the previous storage and HDF parameter errors
are absent. The selected device graph lacked complete init definitions for
composer/allocator hosts. Revision 10 restores the five selected donor host
services with matching HCB IDs, removes a malformed duplicate pin-auth entry,
and fixes a donor gralloc relocation failure with explicit unsupported responses
for Huawei private metadata. See the [attempt-9 diagnosis](op13/ATTEMPT9.md) and
[test instructions](op13/TESTING.md). A first HarmonyOS frame remains unproven.
Only system.img changes from revision 9. The stock kernel Image, vendor_boot,
working IPC module, all seven other images, native policy binaries and
[userdata allocation logic](op13/DSU-DATA-INITIALIZATION.md) remain unchanged.
Full framework boot on the phone remains unresolved.

- Separate partition images and the DSU ZIP have been built and checked.
- OP13 vendor contents, permissions, capabilities and symlinks are preserved.
- The kernel Image stays stock; initial rendering uses software Mesa.
- Display hardware, touch, GPU acceleration, connectivity and full framework
  startup still need device testing.
- The stale input HDF-host entry is removed; the existing Linux userspace
  input backend has passed an isolated ARM64 initialization test. Phone touch
  events and permissions remain unverified.

## Package layout

| Image | Base |
| --- | --- |
| `system.img` | HMOS5 with the OP13 boot handoff and runtime adaptations |
| `vendor.img` | OP13 stock vendor with selected native HMOS additions |
| `vendor_dlkm.img` | OP13 stock modules preserved, plus op13_hmos_ipc.ko |
| `system_dlkm.img` | Current OP13 GKI modules, unchanged |
| `sys_prod.img` | HMOS5 sys_prod |
| `chip_prod.img` | HMOS5 chip_prod |
| `cust.img` | HMOS5 cust |
| `patch_hw.img` | HMOS5 patch |

The eight images are packaged together as `hmos5-op13-dsu.zip`. The phone's
mounted stock ODM EROFS is reused read-only; `odm.img` is omitted. Output copies receive
local development AVB signatures; revision 7 reached native init on the phone.
Bootloader, physical vbmeta, boot, vendor_boot, dtbo and firmware-partition
images are outside this DSU package. OP13 anti-rollback constraints still apply.

## Build and test

See **[op13/README.md](op13/README.md)** for the build workflow, required local
inputs, validation scope, DSU installation steps and boot-log collection.

The OP13 implementation is in [`op13/`](op13/). Its main tools are:

- [`prepare_vendor.py`](op13/prepare_vendor.py): select native HMOS runtime additions.
- [`prepare_dlkm.py`](op13/prepare_dlkm.py): inspect matching stock module images.
- [`plan_display_suppliers.py`](op13/plan_display_suppliers.py): add the working phone's display supplier drivers to the audited module plan.
- [`prune_outputs.py`](op13/prune_outputs.py): keep one validated output set and remove superseded generated images.
- [`extend_with_dlkm.py`](op13/extend_with_dlkm.py): update an existing candidate with DLKM and omit ODM.
- [`build_multipart.py`](op13/build_multipart.py): build separate partition images and a DSU ZIP.
- [`verify_multipart.py`](op13/verify_multipart.py): check packed contents and input preservation.
- [`validate_components.py`](op13/validate_components.py): run offline component checks.

ROM images, private device captures and development signing keys stay local.
Building requires the supplied firmware inputs and a prepared native build
environment, as described in the port documentation.

## Credits and licensing

Based on [tsryzenstech/hmos2sweet](https://github.com/tsryzenstech/hmos2sweet),
with OP13-specific adaptations. Thanks to
[matejkoo217 (~jkoo)](https://github.com/matejkoo217) for
[anyhmos_xiaomi_fuxi](https://github.com/matejkoo217/anyhmos_xiaomi_fuxi) and its
boot/display fixes, and to [AnyHarmonyOSNext](https://github.com/AnyHarmonyOSNext)
for the underlying Binder/access-token compatibility sources. Imported code
retains its upstream licensing and provenance. Component origins are listed in
[NOTICE.md](NOTICE.md); see also [LICENSE](LICENSE).
