# Revision-10 result and revision-11 graphics runtime fixes

The owner still observes the static OnePlus logo. The connected-phone capture
is complete and the device can be disconnected. Collection used read-only
commands; no phone file, property, mount, module or boot state was changed.
Raw logs and captures remain local.

| Input | Lines | Bytes | SHA256 |
| --- | ---: | ---: | --- |
| bootstrap-v10.txt | 685 | 29,805 | 21610705cefada9e3070f5e6d78fdf4d658db16af1365d2776d6c5f8d10c163f |
| native-v10.txt | 32,008 | 2,527,353 | c4111bc177c5b2a8904bfa9400b0e7348bc629833874c2e1fdc90d4aa27f9dd6 |

Both logs were indexed across their entire contents and reviewed by message
family and process lifecycle. The latest bootstrap starts at line 596 and has
no STOP. Older failures at lines 13 and 87 belong to earlier attempts. Native
Binder/token checks continue to pass; the old storage StartUser failure and HDF
ready-parameter denial are absent. SELinux remains permissive for this test.

## Host startup is repaired, implementation loading is incomplete

Native lines 4731–4751 show allocator_host (UID 3041) and composer_host (UID 3036)
starting. Both survive in repeated process snapshots with the correct arguments
and groups. This confirms revision 10 repaired the omitted init definitions.
It does not establish a registered display service or a first frame.

HDF's service-manager errors and render-service composer lookup errors continue.
Most binary-format HiLog messages still lack their dictionary. Their printed
`OpenUuidFile fail` text is a decoding failure, not the original error text.

Inspection of the selected vendor payload and the actual donor HDI loader
reveals another omission: the generated driver stubs dynamically construct the
names of their implementation libraries. Scanning DT_NEEDED and literal full
library names did not include these implementations:

- liballocator_service_1.0.z.so
- libdisplay_composer_service_1.2.z.so

The composer implementation additionally requires libcomposer_buffer_cache.z.so
and display-buffer stubs 1.1/1.2. Revision 11 supplies all five unmodified native
donor DSOs under /hmos/display. The donor's actual LoadHdiImpl function can then
find and construct both implementations in the offline ARM64 test.

## Android libdrm wins the native search order

Native line 7702 and six later messages show a real relocation failure:
`/vendor/lib64/libdrm.so` requires `__errno`, a Bionic symbol. The native library
is already present at /system/lib64/chipset-sdk/libdrm.so, but the donor's search
order visits /vendor/lib64 before that directory. OP13's Android library thus
shadows the HarmonyOS implementation. The sysroot-only tests selected native
DRM directly and failed to model this collision.

Revision 11 prefixes /hmos/display to the normal and ASAN default namespace
paths in both native configuration sections. All other configuration bytes,
NDK inheritance and existing paths are retained. A symlink at
/hmos/display/libdrm.so selects the original native chipset-sdk file; neither
libdrm implementation is modified. The Android vendor library stays untouched.
The original native DRM bytes are included in the validation hash checks.

The prefix also exposes the five native service dependencies above. Existing
OP13 buffer/composer VDIs remain the implementations used by that native chain.
The namespace configuration applies inside the HarmonyOS guest; stock Android
outside the DSU continues to use its own filesystem and Bionic linker.

## Expanded validation

The new fixture uses the actual donor musl loader and namespace configuration,
native system libraries, the selected vendor libraries and the exact stock
Android libdrm extracted from the supplied vendor image. LD_LIBRARY_PATH is
unset. Fixtures hardlink immutable files and are deleted afterward.

For executables under both /system/bin and /vendor/bin, the test demonstrates:

1. The original search order reproduces the __errno relocation failure.
2. With native DRM exposed but the service DSOs absent, both implementations
   still fail to load by their dynamically constructed names.
3. With the complete runtime, all ten requested graphics DSOs relocate with
   RTLD_NOW. Native drmGetVersion performs a real ioctl on disposable /dev/null
   and returns ENOTTY. The actual donor LoadHdiImpl returns real allocator and
   composer implementations for their versioned interface descriptors.

This improves on the earlier constructor test that failed under incomplete
sysroot path emulation. It does not test HDF registration against the phone's
manager, DMA allocation, modesetting, touch or a displayed frame. The fixture
models the known stock DRM collision, not every possible Android vendor DSO.

Other errors remain, including missing unrelated device-security services,
Huawei account/analytics failures and parameter-DAC warnings. Foundation exits
with status zero around native line 19039 and restarts; this capture does not
establish why. No arbitrary framework permissions or credential bypasses are
added to address those secondary errors.

Revision 11 changes system.img only. The seven other images, working IPC module,
stock kernel Image, vendor_boot, policy binaries and userdata logic are retained.
A successful boot or HarmonyOS animation remains unproven. The next test must
establish display-service registration and then a real frame on the OP13 panel.
