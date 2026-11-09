# DSU userdata startup

Revision 3 uses the DSU allocation's header to choose its startup path before
mounting. It does not format an existing filesystem after a mount failure.

| Header | Action |
| --- | --- |
| First 4096 bytes are zero | Treat as the Android DSU installer's wiped-allocation marker; create the initial ext4 filesystem, verify it, then mount it. |
| Existing ext4 or F2FS signature | Mount that filesystem. If mounting fails, stop and log the error without formatting. |
| Unknown, partial or unreadable header | Stop without formatting. |

The writable block device must be the exact `userdata_gsi` mapping. Its identity
is pinned and rechecked, and the existing size bounds are retained. First
initialization rechecks the wiped header, runs the native formatter, and checks
both the resulting ext4 header and unchanged allocation size. The owner's
size selected in the app is read from the block device and is not resized.
25 GiB was the tested size, not a fixed requirement; larger and smaller
allocations are supported within the 128 MiB to 512 GiB initialization bounds
and normal storage requirements. There is no physical Android userdata alternative.

This initialization creates filesystem metadata in a new DSU data allocation.
Afterward, normal boot mounts that filesystem and uses it as `/data`. Recreating
an installation through the DSU app can replace DSU data according to that app's
normal behavior; this port adds no data-reset or format-on-error option.

AOSP's `PartitionInstaller::Format()` writes a 4096-byte zero header for new
writable DSU images; it does not zero every allocated byte or create ext4:
[source](https://android.googlesource.com/platform/system/gsid/+/refs/heads/android16-release/partition_installer.cpp).
Android's fstab transformation marks DSU userdata as formattable, and its normal
mount path checks the wiped state before formatting:
[fstab](https://android.googlesource.com/platform/system/core/+/refs/heads/android16-release/fs_mgr/libfstab/fstab.cpp),
[fs_mgr](https://android.googlesource.com/platform/system/core/+/refs/heads/android16-release/fs_mgr/fs_mgr.cpp).
This bootstrap recognizes the DSU installer's zero-header convention. As with
that convention, a wiped header signals initialization; it cannot reconstruct
data whose filesystem header has already been erased.

The second phone test verified all eight DSU mounts and stopped at the previous
whole-allocation-zero check before HarmonyOS init. That check was too strict
for a normal fresh DSU allocation and is removed. There is no reset or
format-after-mount-error builder option.

The revision-3 device log confirmed first initialization, unchanged allocation
size and successful DSU data mounting. Revision 4 leaves this behavior unchanged.

Offline checks cover zero installer headers with nonzero tails, existing ext4
and F2FS signatures, unknown data and read failures. The real donor formatter
runs in an isolated ARM64 test using the native filesystem/library layout and
minimal environment. Its disposable ext4 image passes e2fsck and is recognized
as an existing filesystem by the same header classifier. No phone write was
performed by those checks. Successful native boot still needs a device test.
