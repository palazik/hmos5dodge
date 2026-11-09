# OP13 handoff revision 6

Revision 5 ran native init and started services. Its logs show failure to load
the native SELinux policy, followed by loss of the guest data/device view during
native namespace switches. The phone stayed at the OnePlus logo.

Revision 6 mounts and checks the SELinux filesystem API and replaces the chroot
handoff with a real pivot_root in a private mount namespace. A local regression
reproduces the old namespace failure and verifies guest data/devices survive
repeated namespace switches after the correction. Full phone boot remains
unverified. Kernel/module binaries, the hardware startup plan, seven other
images and userdata handling are unchanged. Only system.img changes.
See NATIVE-STARTUP.md for evidence, recorder scope and remaining uncertainty.

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

Find the latest `starting OP13 DSU handoff revision 6` in the bootstrap log;
earlier attempts remain above it. The native log contains only the latest
handoff, bounded to 4 MiB. It should show exec status, subsequent kernel records
and PID 1 state, now including bounded mountinfo and SELinux-context reads.
The bootstrap should report the real SELinux API and detached old mount root.
Native logs must establish successful policy loading, working sockets, samgr
and subsequent framework progress. Keep raw captures local. If either file is missing or empty,
report that too; do not infer a successful native boot from the handoff message.

Nineteen offline component checks and full packed readback are required for
delivery. They do not prove native framework boot, rendering, touch or other
hardware functionality. No connected-phone push, mount, property change,
module load, reboot, flash or userdata modification is part of preparation.
