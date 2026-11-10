# Revision-8 phone result and revision-9 changes

The owner sees the static OnePlus boot logo, with no observed HarmonyOS boot
animation. The read-only capture is complete and the phone can be disconnected.
Both provided logs were indexed in full and reviewed by message family and
service lifecycle: bootstrap-v8.txt has 505 lines / 21,646 bytes; native-v8.txt
has 13,530 lines / 849,805 bytes. Raw logs and phone captures remain local.

## What improved

The newest bootstrap attempt starts at line 416. All eight guest partition
mounts and read-only stock ODM pass. The new IPC module loads normally on OP13,
its four device identities are exposed, and actual feature/token ioctl checks
pass. Native SELinux policy loads and AVC records remain permissive. The
previous `failed to set access token` and samgr registration permission-denial
messages both disappear. There are 158 SA insertion messages, including
repeated registrations, rather than a count of unique booted services.

Render-service readiness appears at native line 5791, BMS readiness at 7515,
app-framework readiness at 7859 and account readiness at 8587. Bootanimation's
process is started and exec is attempted, but the capture does not establish
a boot-animation frame, first display commit or SceneBoard startup. The
retained static logo cannot distinguish the exact graphics/startup stall.

## Storage startup failure

At lines 8497–8499 the daemon reports successful user-key activation for user
100. At 8513 it reports `create hmdfs virtual dir error`; at 8515 StartUser fails.
An earlier init command cannot create /mnt/data/external because its parent is
missing. The guest bootstrap creates /mnt, while the removed donor tmpfs mount
had implicitly supplied /mnt/data.

[OpenHarmony MountManager](https://github.com/openharmony/filemanagement_storage_service/blob/7df976109b344754ffed21aa805926e3fa69ba06/services/storage_daemon/user/src/mount_manager.cpp)
calls CreateVirtualDirs before attempting hmdfs/local mounts. Its per-user
directory sequence includes /mnt/data/<user>. The proprietary donor contains
the same error and path strings; its exact implementation can differ.

Revision 9 creates only the missing /mnt/data parent in the guest's existing
tmpfs root. It leaves native storage responsible for its per-user children.
The test reproduces ENOENT before the production helper and successful child
creation afterward in a disposable host directory. No block mapping, userdata
allocation, filesystem formatter, key operation or failed-mount behavior changes.
The next phone test must establish whether this resolves StartUser.

## HDF parameter permissions

Native lines 4759–4767 show hdf_devmgr (UID/GID 3044) attempting its ready-event
write. The DAC lookup falls back to a root-owned rule and rejects the write.
The supplied donor has the intended exact rule in
vendor/etc/param/hdf_devmgr.para.dac, but the logged parameter-loading sequence
does not load that vendor directory.

Revision 9 copies this existing Apache-licensed donor file into the system
parameter directory that init does load. Its only rule retains
`hdf_devmgr:hdf_devmgr:0775` for `bootevent.hdf_devmgr.ready`. It grants no wider
parameter prefix and does not fabricate the event value. The generator rejects
broader permissions or additional rules. Packed readback checks bytes, root
ownership, mode 0644 and the native system_etc_file label.

## Missing HiLog diagnostics

The adapted display HAL calls HiLogPrint. Once hilogd is available, these
messages are not necessarily copied to the kernel stream captured by revision
8. The initial failed socket messages and later framework-ready events do not
provide the display/HDF error details.

Revision 9 queries the actual donor hilog binary with `-L W,E,F -z 2000`.
[TailHandler](https://github.com/openharmony/hiviewdfx_hilog/blob/OpenHarmony-5.0.3-Release/services/hilogtool/main.cpp)
sets nonblocking output itself. The actual donor rejects combining that query
with -x; the accepted argument combination was checked against the donor binary
in the isolated ARM64 loader environment. That environment has no running
hilogd, so it validates CLI/loader behavior, not real server output.

Queries run at 10, 15, 25, 45, 90, 150 and 175 seconds. Each has a 256 KiB and
three-second bound inside the existing 4 MiB / 180-second recorder. They read
logs without clearing buffers or changing logging configuration. The query
child inherits no metadata descriptor, old-root descriptor, kernel-log stream
or exec-status pipe; only pipe stdout/stderr survive exec. Existing capability
drops and no_new_privs remain. Timeout kills only the recorder's own query
child, with nonblocking reaping. Tests cover ordinary output, exec failure,
output flooding, descriptor isolation and a child stalled after closing output.

The recorder also reads a bounded process inventory for core UI/HDF names,
including status, wait location and command line when proc permissions allow.
It adds no privileges to overcome denied diagnostic reads.

Other errors remain: unsupported Huawei device nodes, cgroup mismatches,
auth/audio service exits and SIGSYS in some account/analytics services. These
are retained for further diagnosis. The two targeted fixes and better logging
do not establish a complete boot. The IPC module and seven other images are
reused unchanged; no kernel Image or vendor_boot modification is introduced.
