# hmos5dodge

Experimental **HarmonyOS NEXT 5 port for OnePlus 13**, using the stock OP13
kernel and separate partitions installed through Android DSU.

The port targets the native HarmonyOS framework, SceneBoard and SuperLauncher.
It uses an OP13 stock vendor base with selected native HarmonyOS runtime
additions, matching OP13 DLKM images and the phone's existing stock ODM filesystem.

## Status

**Revision 29 boots to the HarmonyOS UI with GPU rendering, the original boot animation, ADB, and touch and keys reaching
multimodalinput. Revision 30 targets the remaining SceneBoard freezes.** Revision 30 is not yet phone-tested.

Working on the phone (revision 29):

- Boot through DSU to SceneBoard, with the stock kernel and full stock module set.
- The original HarmonyOS boot video (audio track removed), unscaled.
- GPU rendering: rebuilt Mesa 27 (freedreno/KGSL) on the Adreno 830, with GPU firmware loaded at boot.
- ADB over USB (`hmos5dodge`), with the USB-C port forced into device role.
- audio_server starts (HarmonyOS init gets `HOME=/`).
- Touch and buttons reach multimodalinput (the touchscreen's zero-range axes are widened with `EVIOCSABS`), and the
  power key wakes the screen.

Fixed in revision 30 (see [REVISION30.md](op13/REVISION30.md)):

- SceneBoard was still killed by its 6 s watchdog because every audio policy call waited 5 s for a default output
  device. The port now ships the `audio_host` HDF host, the audio HDI chain with the reference port's tinyalsa VDI (which
  plays to nowhere without a sound card), and the HarmonyOS audio policy config. Touch targets and visible key reactions
  depend on SceneBoard staying up.

Still open:

- Blue and pink are swapped on screen. `/system/bin/op13-gpu-colortest` checks GPU RGBA/BGRA handling over ADB.
- Vulkan for render_service: Turnip is built and registered, but render_service stays on GLES until the OHOS
  native-buffer/surface extensions exist.
- Real sound, charging, sensors and USB-C detection need the ADSP/CDSP booted.
- hdc is disabled; ADB is used instead.

Per-revision diagnoses are in `op13/REVISION*.md`; see also the [test instructions](op13/TESTING.md) and the
[userdata allocation logic](op13/DSU-DATA-INITIALIZATION.md).

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
