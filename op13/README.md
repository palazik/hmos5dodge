# HMOS5 port for OnePlus 13 — separate DSU partitions

This is an **OP13-specific port**, using DSU as its installation and boot
mechanism. It targets the full supplied HMOS5 framework, SceneBoard and
SuperLauncher. It is an experimental offline candidate: **no OP13 boot has
been verified**. The kernel is unchanged.

The first device attempt stopped at the OnePlus logo and left no pstore log.
The revised bootstrap adds explicit DSU mount fallback and earlier diagnostics;
its changes and the six reviewed fork commits are documented in
[FORK-REVIEW.md](FORK-REVIEW.md). A later successful boot has not been observed.

The local package is:
`/home/palaziks/hmosnext5_op13/out/hmos5-op13-dsu-current/hmos5-op13-dsu.zip`.
Earlier candidates have been superseded. Generated images and archives are
retained in this directory only; older directories retain small build records.

## Partition layout

The ZIP contains eight separate raw filesystem images (EROFS and ext4), with no nested ROM images:

| Image | Filesystem base and changes |
| --- | --- |
| `system.img` | Supplied HMOS system; native init handoff, isolated DSU storage, curated startup, storage shim and OP13 display/policy payloads. |
| `vendor.img` | **OP13 stock vendor**, fully unpacked/rebuilt with explicitly selected native HMOS runtime additions. |
| `vendor_dlkm.img` | Vendor modules from the restored OP13 payload, in their unchanged ext4 filesystem. |
| `system_dlkm.img` | GKI modules from that same OP13 payload, in their unchanged EROFS filesystem. |
| `sys_prod.img` | Supplied HMOS sys_prod filesystem, unchanged; initial UI overrides are bind-mounted during handoff. |
| `chip_prod.img` | Supplied HMOS chip_prod filesystem, unchanged; curated HDF configuration is bind-mounted during handoff. |
| `cust.img` | Supplied HMOS cust filesystem, unchanged. |
| `patch_hw.img` | Supplied HMOS patch filesystem, unchanged; named for its native mountpoint. |

The phone's existing `/odm` mount is reused read-only and must be EROFS.
`odm.img` is omitted from the ZIP. The handoff stops if that stock mount is
missing, writable, or just the system-image placeholder.

Every **output copy** receives a local development AVB footer for its DSU
partition name. Consequently, an unchanged filesystem does not mean its whole
output image is byte-identical to the input. Local signature/hashtree checks
do not prove the device trusts the key. Original input image bytes are not
modified.

The stock source was refreshed on 2026-11-09 from the owner's updated
`claudefix/stock/payload.bin`, identified by its metadata as
`PJZ110_11.C.93_1930_202608031342`. The extracted vendor, vendor_boot,
vendor_dlkm and system_dlkm hashes match the payload manifest. These four
original stock inputs now live together in `op13_stock/`; obsolete vendor
and vendor_boot files there were replaced by the authorized refresh.

No `boot`, `init_boot`, `vendor_boot`, `dtbo`, physical `vbmeta`, bootloader or
firmware-partition image is included or flashed. The phone was inspected with
read-only ADB commands; no mount, push, property change, reboot or flash was
performed on it. UEFI boot is not implemented here.

## OP13 vendor base

`prepare_vendor.py` selects 145 native runtime/configuration files, roughly
74 MiB, from the supplied donor and the rebuilt OP13 display layer. It records
each path, hash and inclusion reason. The selection comes from the active HDF
modules, startup executables and ELF dependency/library references in the
native framework. It does not install the whole donor vendor image.

No existing OP13 vendor file content is replaced. Stock file ownership,
permissions, capabilities and symlink targets are retained. Parent directory
labels needed by the native additions are adapted for HMOS SELinux. Native
policy/HDF files and interfaces are additions; Android HAL binaries do not
provide the native OHOS HDI ABI by themselves.

Donor firmware, kernel modules, secure-app payloads and the HiSilicon GPU
libraries are excluded. OP13 firmware files remain from the OP13 base. Initial
rendering uses `OHOS_MESA_SOFTWARE=true`; Adreno/OpenCL acceleration is not
claimed. Android and native libraries sharing a purpose are not assumed ABI
compatible.

The selected vendor ELF dependency-name check has no unresolved DT_NEEDED
names against the inspected system/vendor trees. This is not a symbol-version,
linker-namespace or runtime execution test. The donor input proxy uses
`LoadHdiImpl` for passthrough, and its
`libinput_interfaces_service_1.0.z.so` depends on the Linux userspace
`libhdi_input_udriver.z.so`. The old input HDF-host entry referenced an absent
`libinput_driver.z.so`; that stale host is omitted from the curated configuration.
The real ARM64 userspace backend initializes in an isolated namespace without
HDF/Binder devices and returns its expected errors for empty/invalid input
queries. This does not exercise physical touch, event delivery or device SELinux
permissions. The core MMI server also links the Linux libinput implementation.
See [INPUT-PATH.md](INPUT-PATH.md) for the inspected interfaces and test scope.

## Boot and data handoff

Android first-stage maps the separate DSU images and normally synthesizes
fstab entries and mounts them, including names absent from stock fstab. A static ARM64 init
entry accepts only PID 1 with the `selinux_setup` argument. It verifies read-only
partition mounts against their exact `*_gsi` mappings, allowing a single
verified-device layer above each mapping. These names are Android's internal
DSU convention, not a claim this port works on other devices. If an OEM
first-stage leaves an image unmounted, the bootstrap mounts that exact
`*_gsi` mapping read-only and rechecks its identity. It does not accept a
same-filesystem placeholder directory. Ext4 fallback disables journal replay.
The fallback does not create a dm-verity layer or authenticate image footers.

The bootstrap binds all eight DSU partition trees and the read-only host ODM
into a native root and starts
the saved donor init as `/system/bin/init --second-stage 0`. It no longer loop
mounts ROM images inside system.img and no longer overlays the whole donor
vendor over OP13 vendor.

Writes use the exact `userdata_gsi` mapping. A failed mount does not authorize
a format: the entire mapping must be zero, within size bounds, and match its
pinned device identity before the native ext4 formatter runs. Unmountable
nonblank data is preserved and boot stops. There is no physical-userdata
fallback. The guest receives private BinderFS nodes and a cgroup device filter
that denies all block-device access. Kernel attachment/namespace behavior
still needs a phone test; failures stop the handoff. No automatic reboot timer
is added.

The data policy is unchanged in this update, and the owner's 25 GiB allocation
was not modified. AOSP's installer clears only the first 4 KiB of writable DSU
images; the existing whole-mapping-zero check may reject a fresh allocation
whose later extents are nonzero. The revised logs expose that stop point. No
new data reset/format authorization or resize option is enabled here.

The current `vendor_dlkm` contains 458 modules, and `system_dlkm` contains
96 GKI modules, from the newly restored firmware. The October capture is
historical evidence and is not a source of modules for this candidate.
The dependency-ordered 119-module graphics plan reads 58 files directly from
`vendor_dlkm`; 61 early modules are copied from the new `vendor_boot` ramdisk.
All 712 checked imports provided by modules in this plan have matching CRCs.
Already loaded modules are skipped. The standard
`/system/lib/modules` link points at `/system_dlkm/lib/modules`. Normal kernel
ABI and signature checks remain enabled, with no force flags. Module-to-module
CRC checks and common payload provenance do not verify every live kernel export
or prove that the running kernel accepts each module.

Both DLKM images must correspond to the running OP13 kernel. In particular,
GKI modules in `system_dlkm` are built and signed together with their GKI;
they cannot be replaced with donor HMOS modules merely because userspace is
HMOS. See [AOSP GKI module partitions](https://source.android.com/docs/core/architecture/partitions/gki-partitions).

The startup overlay retains the main native framework with its original
users/domains, excludes donor physical-storage mounting and flashing/update
services, and limits the HDF host graph to display, allocator, authentication
and power/battery. The supplied donor already removed
its setup wizard; its setup-completion helper is retained. This does not prove
setup flags or app startup work.

A shim loaded only by storage_daemon supplies persistent local hmdfs views
and sharefs behavior using real private bind mounts. It does not fake successful
permission, access-token, encryption or integrity ioctls, or implement remote
HarmonyOS distributed storage.

## Stock kernel and ARB

The owner's earlier root probe confirmed Binder protocol 8 and DRM card0 dumb-buffer
capability. The queried OHOS feature ioctl returned EINVAL, and the access-token
node was absent. The actual HMOS5 IPC binary has a standard-Binder fallback;
native init treats its initial SetSelfTokenID failure as a warning. These
findings support a stock-kernel first test, not a claim all native permission
semantics work. Those observations have not been repeated on the restored
firmware. No broad OpenHarmony kernel patch set is applied.

OP13 ARB is a constraint. This package contains no boot-chain/firmware downgrade
or physical vbmeta change. The supplied image footers report rollback index
0, which does **not** establish the phone's stored rollback state or absence
of OEM anti-rollback enforcement. Development signing and changing DSU
compatibility metadata do not bypass hardware rollback protection.

Android compatibility metadata now uses the updated payload's actual system
properties: release 16, SDK 36, patch 2026-08-01. The earlier Android 17/SDK 37
capture values are no longer used. These fields do not assert Android security fixes in HMOS.
Device acceptance and the currently running kernel/module compatibility remain
unverified. Diagnose an acceptance failure from loader logs before considering
any change to the device's boot configuration.

References: [AOSP multipart DSU](https://source.android.com/docs/core/ota/dynamic-system-updates#multiple-partition-dsu-in-zip-file),
[AVB rollback protection](https://android.googlesource.com/platform/external/avb/+/master/README.md#rollback-protection).

## Manual first test

Select **hmos5-op13-dsu.zip**, rather than an individual system.img, in
[DSU Sideloader](https://github.com/VegaBobo/DSU-Sideloader#how-to-use). It documents
support for multipart DSU ZIPs. Keep automatic partition sizes and allocate
separate DSU userdata; the owner selected 25 GiB. This update does not change
that allocation. The phone must already have an unlocked
bootloader and enough free storage for all eight images, userdata and temporary
installation files. Keep the first attempt a one-shot boot.

Save loader diagnostics before reboot. Native ADB/HDC USB is unverified.
Bootstrap messages use `hmos-op13` in kernel logs. Before data mounts, they
also append to `/metadata/hmos5dodge/bootstrap.log` on the existing metadata
mount, if available. This root-only log is bounded to 1 MiB and is not exposed
in the guest. This diagnostic file is written by the next DSU boot, not by the
read-only ADB capture. Messages also append to the **DSU**
filesystem at `/data/hmos-port/bootstrap.log` once its data mount succeeds.
That is not Android's normal `/data`. After returning to rooted Android, any
retained pstore logs may help identify an early failure:

```sh
adb shell "su -c 'cat /metadata/hmos5dodge/bootstrap.log'" > op13-hmos-bootstrap.txt
adb shell su -c 'ls -l /sys/fs/pstore'
adb shell su -c 'cat /sys/fs/pstore/console-ramoops*' > op13-hmos-kernel.txt
```

A missing log does not establish the bootstrap ran. Actual device logs must
resolve policy loading/denials, module/firmware requests, DRM modesetting,
access-token identity, fscrypt, code signing, SceneBoard/app startup, setup
flags, touch and other peripherals.

## Local build and checks

Source base: `tsryzenstech/hmos2sweet` commit
`1be080c325fee50622d4fdbd996e1dc96717feba`, with new work in `op13/`.
The old `palazik/hmos2dodge` implementation is not used. The supplied donor is
already the iter43 Redmi port of ADY-LGRP3-CHN 205.1.0.128(SP5).

The local checkout has its OHOS sysroot/headers under `build/work/`. The display
interfaces use OpenHarmony 5.0.3 to match the actual donor vtable, rather than
5.1's different ABI. The policy tool uses local SELinux 3.8 libsepol. Private
ROMs, device captures, generated dependencies and keys are not for publication.

From `/home/palaziks/hmosnext5_op13/hmos5dodge`, using new empty output directories:

```sh
bash op13/build_display.sh
python3 op13/validate_components.py
python3 op13/prepare_startup.py \
  --audit /home/palaziks/hmosnext5_op13/out/stock-dsu-audit \
  --output /home/palaziks/hmosnext5_op13/out/op13-startup-new \
  --hdf-source /home/palaziks/hmosnext5_op13/work/hdf-default.d.hcs \
  --hc-gen build/work/openharmony/hdf_core/framework/tools/hc-gen/build/hc-gen
cp /home/palaziks/hmosnext5_op13/out/op13-startup/mask-init-dirs \
  /home/palaziks/hmosnext5_op13/out/op13-startup-new/mask-init-dirs
python3 op13/prepare_vendor.py \
  --workspace /home/palaziks/hmosnext5_op13 \
  --output /home/palaziks/hmosnext5_op13/out/op13-vendor-additions-new \
  --startup-dir /home/palaziks/hmosnext5_op13/out/op13-startup-new
python3 op13/prepare_dlkm.py \
  --workspace /home/palaziks/hmosnext5_op13 \
  --source-dir /home/palaziks/hmosnext5_op13/op13_stock \
  --vendor-boot /home/palaziks/hmosnext5_op13/op13_stock/vendor_boot.img \
  --payload-inventory /home/palaziks/hmosnext5_op13/work/cos16.0.10/manifest-inventory.json \
  --output /home/palaziks/hmosnext5_op13/out/module-audit-new
python3 op13/build_multipart.py \
  --workspace /home/palaziks/hmosnext5_op13 \
  --output /home/palaziks/hmosnext5_op13/out/hmos5-op13-dsu-multipart-new \
  --vendor-additions /home/palaziks/hmosnext5_op13/out/op13-vendor-additions-new \
  --dlkm-audit /home/palaziks/hmosnext5_op13/out/module-audit-new/report.json \
  --startup-dir /home/palaziks/hmosnext5_op13/out/op13-startup-new \
  --android-release 16 --android-sdk 36 --host-security-patch 2026-08-01
python3 op13/verify_multipart.py \
  --workspace /home/palaziks/hmosnext5_op13 \
  --output /home/palaziks/hmosnext5_op13/out/hmos5-op13-dsu-multipart-new
```

Fifteen component checks cover compilation, sanitizer-backed device-filter/
data-scan tests, rejected non-PID1 invocation, actual ARM64 storage mounts in an
isolated namespace, display ABI/mode behavior, actual input-backend
initialization without HDF, binary policy deltas, and the
ARM64 formatter on a disposable regular test image. They do not test a phone
boot, real userdata formatting, BPF attachment or display hardware.

The packer checks each output's EROFS consistency and AVB hashtree and tests ZIP
CRCs. Packed validation checks executable modes/labels, Android mountpoint
scaffolding, OP13 vendor file contents/symlinks/metadata/capabilities, original
filesystem block retention elsewhere, archive membership and all 14 current
input hashes. Full source-policy neverallow compilation is not claimed.
The mapper/scanner test also checks exact DM discovery, ambiguous mappings,
single-slave layers and rejection of mixed backing devices using temporary
filesystem fixtures. Real OEM first-stage mounting still needs the next test.

`manifest.json`, `packed-validation.json`, `SHA256SUMS`, and the component logs
record the actual scope. `development-key.pem` is a private local key, not an
OEM key. Module providers now come entirely from the updated payload. The older
`newfiles/`, July-payload extractions and October capture are not used for this
candidate's modules. Neither `preload` nor `anco_hmos` exists in the
supplied HMOS5 image set; no substitute image is invented for those HMOS7 names.

Module inventory and init-mask helpers are included as `inspect_modules.py`,
`check_module_crcs.py` and `list_init_masks.py`. They use the workspace containing
this checkout and the extracted private inputs described above.

To update the earlier validated seven-part candidate without repacking its
unchanged vendor/auxiliary images, use `extend_with_dlkm.py --workspace WORKSPACE
--base BASE_DIRECTORY --output NEW_EMPTY_DIRECTORY --dlkm-audit REPORT_JSON`,
then run `verify_multipart.py` on the new output. The helper reuses unchanged
images by hardlink and rebuilds only system plus the two new signed output copies.
The original input images and their module bytes remain untouched.

For a configuration-only update of an already validated eight-part candidate,
`extend_with_dlkm.py` also accepts an eight-part base and `--startup-dir NEW_TREE`.
It preserves the unchanged vendor/auxiliary/DLKM images and replaces only the
system image. `check_input_backend.sh` tests the real donor ARM64 input backend
in an isolated empty-device namespace. The missing legacy HDF-host entry is
removed rather than replaced with a fake driver or a successful-IOCTL stub.

When the stock firmware changes, use the full builder to rebuild vendor too.
The extender rejects an updated payload audit if its stock vendor differs
from the base candidate. `prepare_dlkm.py` checks the four stock partition
hashes against the verified payload inventory, rebuilds the dependency graph,
and checks module-to-module CRCs; it does not reuse the old capture's graph.

After a replacement candidate passes packed validation, run
`python3 op13/prune_outputs.py --workspace WORKSPACE --current CURRENT_DIRECTORY`.
It verifies the current image/archive hashes, removes generated images and
archives from older candidate directories, and discards full vendor build
trees. It retains the current eight raw images, one ZIP, source inputs and
small historical manifests. `cleanup.json` records the removed paths.
