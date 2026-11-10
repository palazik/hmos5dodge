# Revision-9 result and revision-10 repairs

The phone still shows the static OnePlus logo. Read-only capture is complete;
the device can be disconnected. No phone setting, file, mount, module or boot
state was changed during collection. Raw captures remain local.

Both files were indexed across their full contents and reviewed by message
family, timestamp, error origin and process lifecycle:

| Input | Lines | Bytes | SHA256 |
| --- | ---: | ---: | --- |
| bootstrap-v9.txt | 595 | 25,725 | e3bbd7a0060fe60a31bb0e0b302f448f57da4c2df396a174cfdd6f5bf02415dc |
| native-v9.txt | 29,688 | 2,437,501 | a24da6007b80e1a427ab4a4afa535e134f6856a7371b60dc52bb0682924ad09a |

The latest bootstrap starts at line 506. Partition setup, the IPC module's
Binder/token checks, device isolation and the native handoff pass. The old
token-setting/service-registration errors are absent. The previous StartUser
failure is absent, although that alone does not prove complete storage startup.
HDF's ready-event write reaches init without the old parameter-DAC rejection.
All seven HiLog queries exit zero. Most native binary-format messages still lack
their dictionary; `OpenUuidFile fail` is the decoder's message, not proof that
each named service failed to open its own runtime configuration file.

## Missing HDF init services

Render service repeatedly cannot obtain its display composer service. HDF's
service-manager stub also repeatedly reports an error. Their exact binary log
text is unavailable, but the bounded process snapshots show no composer or
allocator host. Source and supplied-image inspection establish a concrete
configuration defect independently of those undecoded messages:

- The selected HDF device graph includes five hosts, but the startup generator
  imported system init definitions and vendor hdf_devmgr.cfg only.
- Complete host service definitions reside in the supplied donor's
  `chip_prod/etc/init/hdf_devhost.cfg`. That directory's original import was
  removed, leaving composer_host and allocator_host undefined.
- A partial pin_auth_host entry from resource_schedule_service.cfg survived
  with no UID, security context or host arguments. Native lines 2525 and
  4775–5953 show this malformed service starting as UID 0 and exiting repeatedly.

The standard UHDF installer does not spawn static hosts itself; init owns their
service processes. See the supplied OpenHarmony `driver_installer_full.c` and
`hdf_attribute.c`. The latter assigns host IDs in HCB child order before sorting
by priority. Simply importing the original donor's numeric IDs would also fail
because our reduced graph has different indices.

Revision 10 selects the five complete donor definitions, preserves their UIDs,
groups, capabilities and security contexts, and remaps their arguments:

| Host | Original ID | Selected graph ID |
| --- | ---: | ---: |
| power_host | 1 | 0 |
| composer_host | 7 | 1 |
| allocator_host | 8 | 2 |
| pin_auth_host | 24 | 3 |
| user_auth_host | 25 | 4 |

The partial pin-auth definition is merged into its complete definition and
removed as a duplicate. Its writepid fields are retained. No other chip-prod
services or hardware hosts are enabled. The donor HCS parser, executed under
ARM64 emulation, confirms all five compiled HCB IDs. Packaging and readback
reject missing, duplicate or mismatched host definitions.

## Gralloc relocation failure

Native line 7608 and later snapshots show a real unresolved
`gralloc::GrallocAttrBufferMgr::AllocAttrBuffer` import in libgralloc_priv.z.so.
It appears under processdump, so this message alone does not establish the
cause of the display stall. Four related metadata imports are missing too.

Re-extracting the buffer library directly from hmos5/vendor.img confirms that
the supplied donor already contains the upstream sweet replacement; it is
byte-identical to the upstream patch binary. That replacement also lacks these
five imports' providers. There is no pristine Huawei metadata implementation
in that file to preserve or copy into our replacement.

Revision 10 exports the five verified static-method ABIs from the OP13 buffer
provider. They explicitly return DISPLAY_NOT_SUPPORT (-5) for Huawei's private
buffer layout. They neither allocate fake metadata nor return successful writes.
The standard OHOS buffer interface remains separate. The donor's own wrapper
validates its private layout and handles this result; when its required-attribute
bit is set, it propagates allocation failure. This does not implement Huawei
private metadata, hardware compression, GPU acceleration or shared metadata.

An ARM64 regression test loads the actual unmodified donor libgralloc_priv with
RTLD_NOW. The previous provider reproduces the phone's exact missing-symbol
failure. The repaired provider loads, and calls through all five donor public
wrappers confirm explicit unsupported/failure responses and unchanged outputs.
An ordinary OHOS handle is rejected by the private API. This test exercises
relocation and wrapper behavior, not a graphics frame. A separate exploratory
driver-DSO load hits a native constructor/signal failure in the incomplete
userspace-emulation environment; HDF driver initialization remains unverified
until the phone test.

## Candidate scope

The incremental builder now refreshes the validated display overlays instead
of silently retaining the previous stage's HAL binaries. Revision 10 changes
system.img only. It retains the working IPC module, seven other images, stock
kernel Image and vendor_boot. Userdata selection, initialization and mount
failure handling are unchanged. Full boot and a first HarmonyOS frame remain
unproven; the next test must establish that the restored host services start
and register the display service.
