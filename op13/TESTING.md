# OP13 handoff revision 11

Revision 10 starts allocator_host and composer_host with the correct identities,
but the static logo remains. Their dynamically named service implementations
were missing, and Android's libdrm shadowed the native chipset-sdk library.

Revision 11 supplies the five native service/dependency DSOs and prefixes the
native graphics runtime in both linker namespaces. A symlink selects the
original HarmonyOS libdrm. The actual native HDI loader constructs both graphics
implementations in the isolated ARM64 test. See ATTEMPT10.md.
Only system.img changes. The working IPC module, kernel Image, vendor_boot
and userdata logic are retained. This revision has offline/VM validation,
not a successful phone boot result.

Install the complete hmos5-op13-dsu.zip through DSU Sideloader and keep permissive
selected for this diagnostic test. Choose your normal userdata size in the app;
it is not fixed to 25 GiB or resized. No boot/vendor_boot/firmware flashing is
part of this test.

Let it run for about three minutes before returning to Android, then collect:

```sh
adb shell "su -c 'cat /metadata/hmos5dodge/bootstrap.log'" > bootstrap-v11.txt
adb shell "su -c 'cat /metadata/hmos5dodge/native-startup.log'" > native-v11.txt
```

Find the newest revision-11 start in bootstrap.log. native-startup.log contains
only the latest attempt, with the existing 4 MiB / 180-second bound. New sections
start with `HiLog warning/error snapshot` and `native UI/HDF process inventory`.
Each query uses the donor tool's nonblocking tail mode, is limited to 256 KiB
and three seconds, and reads warnings/errors without clearing buffers or
changing log settings. The recorder only terminates its own query child.
Recording expiry does not terminate or reboot HarmonyOS.

Check whether init starts composer_host and allocator_host, whether they survive
in the process snapshots and whether their services register. A running process
alone does not prove a displayed frame.

Keep both complete logs and report whether the screen remains at the static
OnePlus logo, reaches an animation, or shows HarmonyOS UI. Read-only phone
capture is finished; the device can be disconnected during offline work.
