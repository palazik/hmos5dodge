# OP13 handoff revision 13

Revision 12 reaches composer DRM initialization and display discovery. The
owner observes a dark screen after the logo; no HarmonyOS frame is established.
The capture shows missing native mapper libraries and DSI/TE timeouts.

Revision 13 supplies the donor mapper implementations 1.0/1.2. Actual native
buffer map/readback/unmap and ownership tests pass in the guest/service-root
fixture. It also captures focused graphics information messages alongside the
existing warning/error queries. See ATTEMPT12.md. DSI recovery and a valid frame
still require phone testing.
Only system.img changes. The working IPC module, kernel Image, vendor_boot
and userdata logic are retained. This revision has offline/VM validation,
not a successful phone boot result.

Install the complete hmos5-op13-dsu.zip through DSU Sideloader and keep permissive
selected for this diagnostic test. Choose your normal userdata size in the app;
it is not fixed to 25 GiB or resized. No boot/vendor_boot/firmware flashing is
part of this test.

Let it run for about three minutes before returning to Android, then collect:

```sh
adb shell "su -c 'cat /metadata/hmos5dodge/bootstrap.log'" > bootstrap-v13.txt
adb shell "su -c 'cat /metadata/hmos5dodge/native-startup.log'" > native-v13.txt
```

Find the newest revision-13 start in bootstrap.log and the
`native graphics runtime exposed read-only at /hmos/display before root switch`
message. native-startup.log contains
only the latest attempt, with the existing 4 MiB / 180-second bound. New sections
start with `HiLog warning/error snapshot`, `HiLog graphics info/warning/error snapshot`
and `native UI/HDF process inventory`.
Each query uses the donor tool's nonblocking tail mode, is limited to 256 KiB
and three seconds, and reads warnings/errors without clearing buffers or
changing log settings. Seven additional queries include information messages
for DISP, OP13Gralloc, SweetHAL and MAPPER_SRV, selecting the last 200 matching
records under the same bounds. The recorder only terminates its own query child.
Recording expiry does not terminate or reboot HarmonyOS.

Check whether init starts composer_host and allocator_host, whether they survive
in the process snapshots and whether their services register. A running process
alone does not prove a displayed frame.

Keep both complete logs and report whether the screen remains at the static
OnePlus logo, reaches an animation, or shows HarmonyOS UI. Read-only phone
capture is finished; the device can be disconnected during offline work.
