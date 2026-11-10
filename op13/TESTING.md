# OP13 handoff revision 12

Revision 11 still shows the static logo. Its native graphics libraries were
packed but the /hmos/display directory was absent from the root used after the
handoff, so native loading still fell back to Android libdrm.

Revision 12 binds only that graphics directory read-only into the guest before
the root switch and includes it in both native service-root templates. The
new tests reproduce the omissions, then verify that both
native graphics implementations construct after the production bind, pivot_root
and namespace restoration. See ATTEMPT11.md.
Only system.img changes. The working IPC module, kernel Image, vendor_boot
and userdata logic are retained. This revision has offline/VM validation,
not a successful phone boot result.

Install the complete hmos5-op13-dsu.zip through DSU Sideloader and keep permissive
selected for this diagnostic test. Choose your normal userdata size in the app;
it is not fixed to 25 GiB or resized. No boot/vendor_boot/firmware flashing is
part of this test.

Let it run for about three minutes before returning to Android, then collect:

```sh
adb shell "su -c 'cat /metadata/hmos5dodge/bootstrap.log'" > bootstrap-v12.txt
adb shell "su -c 'cat /metadata/hmos5dodge/native-startup.log'" > native-v12.txt
```

Find the newest revision-12 start in bootstrap.log and the new
`native graphics runtime exposed read-only at /hmos/display before root switch`
message. native-startup.log contains
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
