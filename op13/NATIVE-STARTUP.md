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

## Revision-5 phone result and revision-6 corrections

The complete bootstrap log contained 248 lines. The native capture reached its
4 MiB bound: 96,356 split lines including blanks and a truncated final record.
Every line was parsed locally; repeated kernel headers, numeric variants and
message families were indexed before reviewing the errors and their order.
The recorder successfully retained evidence from native init and its services.

The log confirms native init execution, configuration parsing and service
starts. Its first policy-load attempt reports `Security getenforce failed`,
followed by `load_policy failed`, invalid file contexts, and later
`Unknown class parameter_service`. The bootstrap created a fresh read-only
sysfs view, but never mounted selinuxfs at `/sys/fs/selinux`. An empty directory
does not provide the policy loader's API. The read-only Android capture confirms
that this stock kernel supports selinuxfs and policy version 33; our prepared
native policies use version 31.

Revision 6 mounts the actual selinuxfs inside the guest before native library
initialization. It checks the filesystem identity, supported policy version and
readable enforcement state. The bootstrap neither changes enforcement nor writes
policy. Native init remains responsible for loading the prepared native policy
during the owner's manual DSU boot. Loading that policy on the phone still needs
verification; permissive Android policy alone does not supply native contexts.

A second defect appears after `mksandbox`: writes start returning EROFS for
`/log` and `/data`, service sockets disappear, and samgr fails context setup.
The guest data filesystem need not have become read-only. Namespace restoration
can change which mount tree resolves those paths.

OpenHarmony's
[`DoMkSandbox`](https://github.com/openharmony/startup_init/blob/OpenHarmony-5.0.3-Release/services/init/standard/init_cmds.c)
creates a mount namespace then returns with `setns`. The supplied kernel's
`fs/namespace.c:mntns_install` resets the process root and working directory to
the destination namespace's root. Our previous `chroot` changed the process
root without changing that mount namespace root, which still belonged to the
Android first-stage filesystem. Thus the native namespace round trip loses the
guest's data/devices and overlays. This also explains the transition from early
successful socket creation to later ENOENT failures.

Revision 6 first unshares a mount namespace and makes its mounts private. At
handoff it uses `pivot_root` to make the assembled guest the actual mount root,
then lazily detaches the old root. Metadata logging retains only its fixed regular
file descriptors in the recorder; no old-root directory/block descriptor reaches
the native framework. There is no chroot fallback if the root switch fails.

`namespace_test.c` runs inside an empty disposable user/mount namespace with
tmpfs data/device fixtures. It reproduces losing those fixtures after a
chroot/setns round trip, then exercises the production pivot function and checks
that repeated namespace switches retain writable data and the guest device tree,
with the original root absent. It does not mount a phone image or run a formatter.
The recorder now also captures bounded PID 1 mountinfo and SELinux context reads.

The same kernel, modules, seven other image payloads and userdata code are retained.
These corrections address demonstrated bootstrap defects; samgr/Binder behavior,
native policy loading and full framework boot must still be checked on the phone.
