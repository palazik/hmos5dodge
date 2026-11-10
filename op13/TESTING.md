# OP13 handoff revision 8

Revision 8 adds the VM-tested op13_hmos_ipc module inside vendor_dlkm.img and
connects the native Binder/access-token aliases. The unchanged stock 6.6.118
kernel passed token, nested-callback, concurrent-caller and production-alias
contracts in QEMU. All 24 component checks and full packed readback passed.
Full framework boot on OP13 is still untested.

Install this directory's complete **hmos5-op13-dsu.zip** using DSU Sideloader.
Keep permissive selected for this diagnostic test, as before. The kernel Image
and vendor_boot stay stock. Choose your desired userdata size in the app; it is
not fixed to 25 GiB or resized by the port. Userdata handling is unchanged.

Allow about three minutes at the logo before returning to Android, so the
recorder can capture startup. Recording expiry does not stop or reboot HarmonyOS.
After returning to rooted Android, collect both logs:

```sh
adb shell "su -c 'cat /metadata/hmos5dodge/bootstrap.log'" > bootstrap-v8.txt
adb shell "su -c 'cat /metadata/hmos5dodge/native-startup.log'" > native-v8.txt
```

Find `starting OP13 DSU handoff revision 8` in the bootstrap log; older history
remains above it. Look for `loaded planned module op13_hmos_ipc` and
`native Harmony Binder and task-token APIs verified`. Native logs then establish
whether access-token setup and service registration improve. Report the visible
screen and keep both full logs. A new failure after IPC needs its own diagnosis;
the VM test does not establish working graphics, hardware or a booted desktop.

No firmware, bootloader, physical vbmeta, boot or vendor_boot partition image
is included in the package. Source changes and logs do not require the phone
connected; current read-only capture has completed.
