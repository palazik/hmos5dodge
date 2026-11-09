# Review of the giuliac fork

Reviewed the actual code of all six commits after the common base `1e2e6d2`,
through [`e3c24bd`](https://github.com/KSN2redawew/hmos5giuluac/commit/e3c24bd05b672a40407b42c7f0977deb8eb97c5d).
The fork targets OnePlus Ace 5 / SM8650; this port targets OP13 / SM8750.
Its descriptions do not establish that its images boot on either device.

| Commit | Code examined and OP13 decision |
| --- | --- |
| [`248ce59`](https://github.com/KSN2redawew/hmos5giuluac/commit/248ce59739d91cc2d778c437f0a030ccae997246) | Read its new builder, bootstrap, storage shim, UI XML and README. Keep OP13 geometry, the OHOS toolchain for native shared libraries, the complete native vendor selection and curated startup. The fork builds its storage shim with a GNU/Linux compiler and omits several existing checks and integrations. |
| [`a772807`](https://github.com/KSN2redawew/hmos5giuluac/commit/a772807d726fe65f90b7c0d572fe83794ffcc5c8) | Adapt the explicit mounting of mapped DSU images when first-stage mounts are absent. Retain exact mapper identity and the single-slave rule; require all eight partitions. The fork's ZIP changes also remove CRC testing, atomic publication and AVB readback; retain those checks. |
| [`546b256`](https://github.com/KSN2redawew/hmos5giuluac/commit/546b2568f44678980ee70f8025a43059d130ce3b) | Adapt early persistent metadata logging, using a bounded root-only log and no host metadata mount inside the guest. Reject its generic `userdata` substring fallback and binding any non-tmpfs directory: an unmounted placeholder on system EROFS would satisfy that test. |
| [`a5c69aa`](https://github.com/KSN2redawew/hmos5giuluac/commit/a5c69aaaac08deb2e3e78118f483fa98a7c3a5cc) | Adopt real basic character nodes before running mke2fs and clearer formatter diagnostics. Retain mapping revalidation, size bounds and filesystem features. Revision 3 recognizes the installer's wiped header before mounting; existing filesystems are never reformatted because a mount failed. |
| [`27f8cb4`](https://github.com/KSN2redawew/hmos5giuluac/commit/27f8cb46d1028434fa3f71a8740ca9f56ffe1cb3) | Keep the firmware-specific dependency/CRC-checked module plan instead of loading every modules.load list and ignoring failures. Existing character nodes are accepted only after checking their actual type/device identity. Module opens now use the guest DLKM mounts, including explicit-mount fallbacks. |
| [`e3c24bd`](https://github.com/KSN2redawew/hmos5giuluac/commit/e3c24bd05b672a40407b42c7f0977deb8eb97c5d) | Adapt visible native-init stdout/stderr. Keep the block-device filter and capability drops. A broad watchdog substring blacklist is not added without evidence for a particular OP13 module; it cannot remove drivers already loaded by Android first-stage. |

## Fstab and mount handling

Android 16's `TransformFstabForDsu` synthesizes read-only ext4 and EROFS entries
for installed `*_gsi` images even when the stock fstab has no corresponding
partition. Source:
[AOSP libfstab](https://android.googlesource.com/platform/system/core/+/refs/heads/android16-release/fs_mgr/libfstab/fstab.cpp).
`UseDsuIfPresent` maps backing images and calls that transformation before
mounting: [first-stage mount](https://android.googlesource.com/platform/system/core/+/refs/heads/android16-release/init/first_stage_mount.cpp).

Thus chip_prod, sys_prod, cust and patch_hw do not automatically require a
stock vendor-fstab patch. The revised bootstrap also handles an OEM first-stage
that maps but leaves a partition unmounted. It mounts the exact DSU mapping
read-only, with ext4 journal replay disabled, and verifies the resulting mount.
This fallback does not add a dm-verity layer or independently authenticate AVB.
It never selects a physical stock partition as an alternative.

## What the first phone test establishes

The user reported a OnePlus logo stall and empty pstore. A read-only Android
capture confirmed all eight DSU images were installed and the three stock
vendor_boot/vendor_dlkm/system_dlkm hashes matched the refreshed payload.
It did not recover the failed boot's stop point or establish a SELinux denial.
Raw backing-file hashes are not interchangeable with mapped-image hashes on
metadata-encrypted userdata; they are not used as evidence of image corruption.

The second phone test identified the whole-mapping-zero check as the actual
stop, after all eight DSU filesystems mounted. AOSP's
[PartitionInstaller::Format](https://android.googlesource.com/platform/system/gsid/+/refs/heads/android16-release/partition_installer.cpp)
clears only the first 4 KiB of writable images. That does not establish an
entirely zero allocation. Revision 3 uses this normal wiped-header convention
for initial filesystem creation before mounting. Existing ext4/F2FS signatures
select a mount-only path; a mount error or unknown header cannot request a
format. The 25 GiB size is unchanged, and no connected-device write was made.
See [DSU-DATA-INITIALIZATION.md](DSU-DATA-INITIALIZATION.md).

The package keeps SELinux behavior unchanged and records bootstrap stages
in `/metadata/hmos5dodge/bootstrap.log` while the host root is active. The log
is root-only, bounded to 1 MiB, and never passed to native applications. It
also retains the existing DSU-data log and directs native-init output to kmsg
(or a DSU-data file if kmsg cannot be opened). Metadata logging is best effort;
it cannot capture failures before the bootstrap is executed.

Changing Android's live enforcement setting is not persistent across a reboot.
A diagnostic permissive boot would need to affect the policy actually loaded
by the DSU/native init; it would not solve missing mounts or failed module loads.
No SELinux mode change is bundled with these mount/diagnostic corrections.
