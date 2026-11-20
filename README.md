# hmos5dodge

Experimental **HarmonyOS NEXT 5 port for OnePlus 13**, using the stock OP13
kernel and separate partitions installed through Android DSU.

The port targets the native HarmonyOS framework, SceneBoard and SuperLauncher.
It uses an OP13 stock vendor base with selected native HarmonyOS runtime
additions, matching OP13 DLKM images and the phone's existing stock ODM filesystem.

## Status

**Revision 30 boots to a usable HarmonyOS UI: GPU rendering, the original boot animation, touch, buttons, audio
services and ADB. Revision 32 targets colors and 120 Hz smoothness.** Revision 32 is not yet phone-tested.

Working on the phone (revision 30):

- Boot through DSU to SceneBoard, with the stock kernel and full stock module set.
- The original HarmonyOS boot video (audio track removed), unscaled.
- GPU rendering: rebuilt Mesa 27 (freedreno/KGSL) on the Adreno 830. `op13-gpu-colortest` passes RGBA/BGRA, tiled,
  NV12/NV21 and native-buffer cases.
- Touch and buttons, with SceneBoard stable (audio HDI host, `HOME=/`, touchscreen axis fix).
- ADB over USB (`hmos5dodge`).

Changes in revisions 31-32 (see [REVISION31.md](op13/REVISION31.md), [REVISION32.md](op13/REVISION32.md)):

- Colors: render_service's output and the scanned-out buffer are correct, but the panel path rotates the channels
  (ABGR shown as B,R,G; ARGB as R,B,G). Revision 32 programs the DSPP PCC with the inverse rotation
  (live override via `/dev/shm/op13-pcc`).
- Smoothness: WALT with ~44% CPU floors and a 525 MHz GPU floor (revision 31's bare WALT dropped below 120 Hz).
- Heat: the stock kernel's `performance` CPU governor is replaced by WALT/schedutil.
- Stutter: no per-frame chipset-vsync IPC or composer error logs.
- CPU: apps now find the mapper bridges in `/vendor/lib64/chipsetsdk`, with a HarmonyOS libdrm Android cannot shadow.
- Vulkan: an ICD shim adds `VK_OHOS_native_buffer`/`VK_OHOS_external_memory` to Turnip; `op13-vk-colortest` passes on
  the phone. render_service is still on GLES.
- hdc over USB alongside ADB.

Still open:

- Real sound, charging, sensors and USB-C detection need the ADSP/CDSP booted. The phone does not charge under DSU
  HarmonyOS.

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
