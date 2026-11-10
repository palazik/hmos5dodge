# Revision-11 result and revision-12 root visibility repair

The phone still shows the static OnePlus logo. Read-only capture is complete
and the device can be disconnected. No phone file, property, mount, module or
boot state was changed during collection. Raw logs remain local.

| Input | Lines | Bytes | SHA256 |
| --- | ---: | ---: | --- |
| bootstrap-v11.txt | 775 | 33,885 | 33a4a08821b1fa78bc3b4bfb5d9e88222cf8e6356ca1fc9a8eb778ad339ea33d |
| native-v11.txt | 31,970 | 2,520,290 | 43968cf2b2631ad77aceee32e2216d0080571ae68dac6c03f0e9682e671b3f6a |

Both logs were indexed across their full contents and reviewed by message
family and process lifecycle. The latest bootstrap starts at line 686, reaches
native init and has no STOP. Binder/token checks still pass; allocator_host
and composer_host start and survive the repeated snapshots. Their display
service remains unavailable and the screen does not establish a first frame.

The old Android libdrm __errno relocation failure still appears seven times,
starting at native line 7672. Most binary-format HDF errors remain undecoded.
Other device-security, account/analytics and parameter-DAC errors also remain.
There is no new evidence of a broken panel or a need to rebuild the kernel.

## Why revision 11's runtime files were not usable

The DSU image contains both a `system/` subtree and our `hmos/` additions at its
filesystem root. Before the native root switch, the bootstrap can read those
additions through Android's first-stage mount layout.

The guest root is a separate tmpfs. The bootstrap projects the native /system
subtree, vendor and other explicit mounts into it. Existing startup configs,
policies and the two custom VDIs are individually bound to their native target
paths. However, /hmos itself was never projected into that guest.

Revision 11 put the five service libraries and the native DRM symlink under
/hmos/display and added that path to the native loader configuration. Packed
readback proved they existed in the image. The previous loader fixture also
mounted that directory explicitly, so it passed. Neither check established
that the production handoff made the directory reachable after pivot_root.
The real guest therefore still lacked the prefixed path and fell back to the
Android vendor DRM library. This was an omission in the bootstrap and test
model, not a different ZIP or userdata problem.

## Revision 12

The production helper creates /hmos and /hmos/display in the guest's RAM root
and binds only the reviewed graphics directory there, read-only, before the
root switch. It exposes no other /hmos subtree. The existing explicit VDI binds
remain. The bootstrap logs:

```
native graphics runtime exposed read-only at /hmos/display before root switch
```

The directory is backed by the already mounted DSU system filesystem. No
physical partition write, device-node change, policy change, new kernel module
or userdata operation is introduced by this repair.

Native init also constructs separate system and chipset service roots. Their
donor sandbox templates bind selected paths, so exposing /hmos/display in the
first guest alone is insufficient. Revision 12 appends the same single graphics
directory entry to both templates, with the donor's ordinary bind/rec/private
flags. The source is already read-only; all original bindings, file mounts,
symbolic links and other template fields are preserved. It does not disable
service sandboxes or expose the rest of /hmos.

## Regression test through the actual root switch

The expanded fixture compiles the production bootstrap helpers into a static
host test. Inside a disposable bubblewrap namespace it creates the synthetic
guest, then invokes the same pivot_root and namespace-restoration helpers.
Afterward it execs the actual donor ARM64 loader and graphics test through QEMU.

- Omitting the production graphics bind reproduces revision 11: /hmos/display
  is absent after the root switch and the native program encounters the stock
  Android DRM __errno relocation failure despite the corrected namespace file.
- Calling the production helper retains /hmos/display after pivot_root and
  setns. statvfs reports read-only; attempted fixture-file creation fails with
  EROFS. The unrelated /hmos/modules path remains absent.
- From that actual guest root, the native DRM resolves correctly, the ten
  graphics DSOs relocate, and the donor LoadHdiImpl constructs both native
  allocator/composer implementations.

The earlier six namespace/implementation checks remain. This test still uses
no physical display node. Two additional cases construct a second service-root
fixture with the configured graphics bind flags: omission reproduces the DRM
failure, while inclusion retains a read-only runtime and both native factories
construct. Template validation proves that the added entry matches both real
donor service templates and that their other fields are unchanged. This fixture
models the relevant service-root projection; it does not execute the full
proprietary sandbox engine. It does not establish service registration on the
OP13, DMA allocation, modesetting or a real frame. Full phone boot remains
unproven. The next phone test must confirm the new bind message and then whether
the display service becomes available.

Only system.img changes from revision 11. The seven other partition images,
working IPC module, stock kernel Image, vendor_boot, native graphics DSOs,
linker namespace configuration, policy binaries and userdata logic remain
unchanged. The two native service sandbox templates gain only the scoped
graphics directory binding described above.
