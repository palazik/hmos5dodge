# OP13 handoff revision 5

Revision 4 passed the partition/data mounts, required DRM/GPU/DMA-heap device
checks and device filter, then reached the native init handoff message. The
phone still stayed at the OnePlus logo. There are no surviving native errors
in that attempt's log, so the cause remains unresolved.

Revision 5 adds a persistent native-startup recorder. This is a diagnostic
candidate, not a confirmed boot fix. It preserves the same stock kernel/module
binaries, hardware startup plan, vendor/auxiliary images and userdata handling.
Only system.img changes. See NATIVE-STARTUP.md for recorder scope and limits.

Install this directory's complete **hmos5-op13-dsu.zip** using DSU Sideloader.
Choose your desired userdata size in the app; it is not fixed to 25 GiB or
resized by the port. Existing filesystem mount failures never trigger a reset
or formatting. Keep the first test a one-shot boot.

Allow about three minutes at the logo before returning to Android, so the
recorder can capture native startup and PID 1 state. Its three-minute recording
limit does not reboot the phone or limit how long HarmonyOS can run.

After returning to rooted Android, collect **both** logs:

```sh
adb shell "su -c 'cat /metadata/hmos5dodge/bootstrap.log'" > op13-hmos-bootstrap.txt
adb shell "su -c 'cat /metadata/hmos5dodge/native-startup.log'" > op13-hmos-native-startup.txt
```

Find the latest `starting OP13 DSU handoff revision 5` in the bootstrap log;
earlier attempts remain above it. The native log contains only the latest
handoff, bounded to 4 MiB. It should show exec status, subsequent kernel records
and PID 1 state. Keep raw captures local. If either file is missing or empty,
report that too; do not infer a successful native boot from the handoff message.

Seventeen offline component checks and full packed readback are required for
delivery. They do not prove native framework boot, rendering, touch or other
hardware functionality. No connected-phone push, mount, property change,
module load, reboot, flash or userdata modification is part of preparation.
