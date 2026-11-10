# OP13 handoff revision 9

Revision 8 confirmed that the IPC module loads on OP13 and clears the old token
setup/service-registration denials. The phone still showed the static OnePlus
logo; seeing render_service ready did not establish a displayed HarmonyOS frame.

Revision 9 adds the missing guest /mnt/data parent, makes the donor HDF
ready-event permission file visible to parameter loading, and adds bounded
HiLog warnings/errors plus UI/HDF process snapshots. Only system.img changes.
The working IPC module, kernel Image, vendor_boot and userdata logic are retained.
This revision has offline/VM validation, not a successful phone boot result.

Install the complete hmos5-op13-dsu.zip through DSU Sideloader and keep permissive
selected for this diagnostic test. Choose your normal userdata size in the app;
it is not fixed to 25 GiB or resized. No boot/vendor_boot/firmware flashing is
part of this test.

Let it run for about three minutes before returning to Android, then collect:

```sh
adb shell "su -c 'cat /metadata/hmos5dodge/bootstrap.log'" > bootstrap-v9.txt
adb shell "su -c 'cat /metadata/hmos5dodge/native-startup.log'" > native-v9.txt
```

Find the newest revision-9 start in bootstrap.log. native-startup.log contains
only the latest attempt, with the existing 4 MiB / 180-second bound. New sections
start with `HiLog warning/error snapshot` and `native UI/HDF process inventory`.
Each query uses the donor tool's nonblocking tail mode, is limited to 256 KiB
and three seconds, and reads warnings/errors without clearing buffers or
changing log settings. The recorder only terminates its own query child.
Recording expiry does not terminate or reboot HarmonyOS.

Keep both complete logs and report whether the screen remains at the static
OnePlus logo, reaches an animation, or shows HarmonyOS UI. Read-only phone
capture is finished; the device can be disconnected during offline work.
