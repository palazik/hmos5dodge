# OP13 handoff revision 7

Revision 6 installed the guest mount root and the kernel accepted native SELinux
policy. Its donor config forced the permissive test into enforcing, after
which diagnostic access was denied. The later framework state remains unknown.

Revision 7 preserves the initially selected permissive state through the native
config, while retaining native enforcing requests and real policy loading.
A recognized native `enforcing=1` boot argument takes precedence. Both config
modes and both native argument overrides passed ARM64 tests using the actual
donor loader/parser; only the test's kernel calls are counters. These test
implementations are not included in the ROM.

Full phone boot remains unverified. Kernel/module and policy binaries, the
hardware startup plan, seven other images and userdata handling are unchanged.
Only system.img changes. See NATIVE-STARTUP.md for evidence and remaining uncertainty.

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

Find the latest `starting OP13 DSU handoff revision 7` in the bootstrap log;
earlier attempts remain above it. The native log contains only the latest
handoff, bounded to 4 MiB. It should show exec status, subsequent kernel records
and PID 1 state, now including bounded mountinfo and SELinux-context reads.
The bootstrap should report the real SELinux API, which native mode config is
selected, and the detached old mount root. For the next permissive test, keep
the DSU app/device permissive setting selected as before.
Native logs must establish successful policy loading, working sockets, samgr
and subsequent framework progress. Keep raw captures local. If either file is missing or empty,
report that too; do not infer a successful native boot from the handoff message.

Twenty-four offline component checks and full packed readback are required for
delivery. They do not prove native framework boot, rendering, touch or other
hardware functionality. No connected-phone push, mount, property change,
module load, reboot, flash or userdata modification is part of preparation.
