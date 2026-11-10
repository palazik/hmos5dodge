# OP13 handoff revision 10

Revision 9 still showed the static OnePlus logo. Binder/token checks pass and
the previous storage/HDF parameter errors are absent. The display composer
service remains unavailable; complete HDF host init definitions were omitted.

Revision 10 restores the five selected donor host services, matches their host
IDs to the reduced HCB graph, and merges the malformed duplicate pin-auth
definition into its complete definition. It also repairs five unresolved
private-gralloc imports with explicit unsupported responses. See ATTEMPT9.md.
Only system.img changes. The working IPC module, kernel Image, vendor_boot
and userdata logic are retained. This revision has offline/VM validation,
not a successful phone boot result.

Install the complete hmos5-op13-dsu.zip through DSU Sideloader and keep permissive
selected for this diagnostic test. Choose your normal userdata size in the app;
it is not fixed to 25 GiB or resized. No boot/vendor_boot/firmware flashing is
part of this test.

Let it run for about three minutes before returning to Android, then collect:

```sh
adb shell "su -c 'cat /metadata/hmos5dodge/bootstrap.log'" > bootstrap-v10.txt
adb shell "su -c 'cat /metadata/hmos5dodge/native-startup.log'" > native-v10.txt
```

Find the newest revision-10 start in bootstrap.log. native-startup.log contains
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
