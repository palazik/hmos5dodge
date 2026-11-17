# Revision-12 result and revision-13 mapper/runtime diagnostics

The owner observes the OnePlus logo followed by a dark screen, while the phone
remains running. It was subsequently connected in Android. Read-only capture
is complete and the phone can be disconnected. No phone setting, file, module,
mount or boot state was changed during collection. Captures remain local.

| Capture | Lines | Bytes | SHA256 |
| --- | ---: | ---: | --- |
| work/phone-attempt12/bootstrap.log | 866 | 38,043 | 1474df273f3d04e2ec78eb6426810d275d03eb21075f0752751150a8f07b5a26 |
| work/phone-attempt12/native-startup.log | 28,977 | 2,255,227 | 8482d85fe94b9288052546677c710093e08f97f904ac614bbeb4bacd37ee9274 |

Both logs were indexed across their full contents and reviewed by message
family, process lifecycle and the display-driver timeline. Native Binder/token
checks continue to pass. The old Android libdrm __errno failure is absent. Our
composer now reaches DRM-master checks and display discovery. The physical DSI
connector is 68; connected virtual connectors are filtered out. A warning about
encoder/CRTC ID zero is followed by the existing possible-encoder/CRTC fallback;
that warning alone does not establish a failed selection.

This is evidence that the native display path is executing. A dark panel is
not proof that a valid HarmonyOS frame or boot animation has been displayed.

## Buffer mapper omitted from the runtime closure

The capture contains 481 missing-library records for libmapper_service_1.0.z.so,
starting at native line 7314, plus the associated loader messages. Counts
include repeated snapshots, not necessarily unique attempts. The real donor
contains mapper implementations 1.0 and 1.2 under vendor/lib64/chipsetsdk; they
were not in the selected vendor additions or the exposed graphics runtime.
These are pass-through native implementations whose names are constructed by
the HDI loader, as with the earlier allocator/composer omissions.

Revision 13 exposes both unmodified mapper DSOs under the already projected
/hmos/display directory. Version 1.2 depends on the 1.0 implementation. Their
dependencies resolve through the same native namespace and service roots.
No mapper HDF host, Kirin GPU binary or new kernel module is enabled.

The actual donor LoadHdiImpl now constructs allocator, composer and mapper
interfaces 1.0/1.2 in the isolated root-switch fixture. A new native C++ test
then calls the real mapper methods through the generated HDI ABI:

- A disposable memfd-backed BufferHandle maps through the actual donor mapper
  into the OP13 VDI. Written bytes are read back from the fd, then unmapped.
- FreeMem rejects a borrowed handle without closing its fd. After ownership
  transfer, FreeMem succeeds, clears NativeBuffer and closes the fd. This also
  checks that the real donor's release behavior matches our VDI's ownership.
- Both mapper descriptors pass. The old descriptor's implementation reports
  version 1.2, consistent with this donor's backwards-compatible service.

The fixture has no physical DRM device. CPU mapping of a memfd does not verify
DMA-BUF allocation/import, cache maintenance, GPU rendering or panel scanout.

## DSI failures remain unresolved

The kernel reports DSI command-transfer failures, missing TE/completion events,
panel-status reads returning no data and PANEL_DEAD for connector 68/encoder 67.
It also reports requested backlight levels not becoming effective on the panel.
The first composer is killed with SIGKILL around native line 21136; a later
composer process re-enters display discovery.

These records do not establish whether the missing mapper, initial modeset,
panel power sequencing or another display requirement causes the timeouts.
Revision 13 does not change panel timings, backlight policy, ESD checks or the
existing KMS commit strategy to guess around this failure. The absent
libdisplay_gfx.z.so is an optional 2D plugin: this composer already falls back
to client composition when it is unavailable.

Mesa EGL/GLES implementation libraries are present in the selected vendor
payload. The log also contains probes for unavailable Huawei Mali/hvgr drivers;
they do not prove that Mesa initialized successfully. An exploratory fixture
probe obtained a public EGL display but eglInitialize returned EGL_NOT_INITIALIZED
without any DRM device. That is recorded as an unverified boundary, not counted
as a passing software-rendering test or proof of a phone EGL failure.

## Focused graphics information capture

The existing recorder collected only warnings/errors. Important custom HAL
messages about allocation, brightness, power and modeset progress are information
messages, while its legacy /dev/kmsg writes need privileges the service lacks.

Revision 13 adds seven bounded queries for information/warning/error records
tagged DISP, OP13Gralloc, SweetHAL or MAPPER_SRV, interleaved with the seven
existing warning/error queries. Each selects the last 200 matching records.
The existing per-query 256 KiB / three-second bounds and overall 4 MiB /
180-second limit remain. Only one query child runs at a time. Queries neither
change log levels nor clear buffers. The donor CLI accepts the exact arguments;
the isolated CLI check has no live hilogd and therefore cannot validate server
output. Descriptor, flood, timeout and reaping tests cover both argument modes.

Only system.img changes. The seven other images, stock kernel Image,
vendor_boot, IPC module, policy binaries and userdata logic remain unchanged.
A first valid HarmonyOS frame and a successful boot remain unproven.

## Live debugging

ADB would help inspect a failed native boot without returning to Android. The
donor already contains /system/bin/hdcd and its native init configuration.
[OpenHarmony HDC](https://github.com/openharmony/developtools_hdc) is another
possible transport. Working OP13 USB gadget setup and daemon integration still
need to be established; neither a new ADB service nor a debug-transport bypass
is part of this revision.
