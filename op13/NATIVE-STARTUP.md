# Native init diagnostics

The complete revision-4 phone log was 167 lines / 7,034 bytes. It included
earlier attempts; the latest attempt began at line 88. All eight DSU partition
mounts and the read-only stock ODM bind passed. Fresh DSU data initialization
and mounting passed at the size selected in the installer. The display selector
was forwarded, required DRM/GPU/DMA-heap devices were found, and the device filter
attached. The final line was the message immediately before the native init
handoff. There was no bootstrap STOP in that attempt.

That last message does not prove `execve` succeeded, that native init reached
its main function, or that SceneBoard ran. The returned Android boot had empty
pstore and no remaining DSU mappings. Its current kernel messages cannot
recover the previous boot's native init errors.

The earlier diagnostic redirect was insufficient across reboot. OpenHarmony's
[`SystemInit`](https://github.com/openharmony/startup_init/blob/OpenHarmony-5.0.3-Release/services/init/standard/init.c)
calls `CloseStdio`, which points standard descriptors at `/dev/null`.
[`LogToDmesg`](https://github.com/openharmony/startup_init/blob/OpenHarmony-5.0.3-Release/services/log/init_log.c)
writes init messages to `/dev/kmsg`. Redirecting stderr alone therefore cannot
retain the complete native startup log. The proprietary donor may differ;
the next phone capture must establish its actual behavior.

Revision 5 keeps the revision-4 hardware, data and startup configuration and
adds a recorder after entering the guest chroot and attaching the device filter:

- It opens the real kernel log read-only and seeks to the current end without
  clearing the ring or changing kernel logging settings.
- One child retains only that read descriptor, a write-only regular metadata
  log and a read-only exec-status pipe. It closes every other descriptor,
  drops its capability sets and enables `no_new_privs`. It executes no commands.
- The parent keeps only the close-on-exec error-pipe writer until `execve`.
  Successful exec closes it; failure sends the actual errno. Neither a metadata
  descriptor nor an old-root directory/block descriptor reaches native init.
- The child records subsequent kernel messages and periodically reads PID 1's
  status, wait location, command line and executable link. Some proc entries may
  be unavailable after dropping capabilities; their read errors are recorded.
- `/metadata/hmos5dodge/native-startup.log` is root-owned mode 0600, limited to
  4 MiB and at most 180 seconds. It is replaced only when the next handoff
  starts. Regular-file, owner, mode and single-link checks precede truncation.
  Writes are synchronized during recording. The metadata mount is not exposed
  in the guest. Failure to open a diagnostic stream does not reset userdata.
- Recorder expiry or reaching its size limit only ends recording. It does not
  stop native init, reboot, format, resize or switch to another data device.

The existing bounded bootstrap log remains append-only and may contain older
attempts. After a manual test and return to rooted Android, collect both files:

```sh
adb shell "su -c 'cat /metadata/hmos5dodge/bootstrap.log'" > op13-hmos-bootstrap.txt
adb shell "su -c 'cat /metadata/hmos5dodge/native-startup.log'" > op13-hmos-native-startup.txt
```

Keep raw logs local: kernel records can contain device identifiers. A closed
exec-status pipe without an error is distinct from successful library loading
or framework startup; use the subsequent messages and PID 1 state as evidence.

Offline tests exercise streamed messages, successful exec with close-on-exec,
actual exec failure with partial pipe delivery, and the output size limit.
An isolated ARM64 check loads the real donor init with its native library paths,
then reaches its non-PID1 refusal. That check does not launch the framework or
verify the phone's kernel interfaces. The recorder still needs a device test.
