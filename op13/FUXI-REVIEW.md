# Xiaomi fuxi source review

Reviewed the code changes in
[`748c8fd`](https://github.com/matejkoo217/anyhmos_xiaomi_fuxi/commit/748c8fd3b2b61ad1083db032098f34770de723d2),
the fork's single commit after its inherited base. This review includes actual
patches, rather than relying on the commit message. It changes 14 files,
including manifests and a prebuilt native_screenlock_request executable.

| Change | OP13 decision |
| --- | --- |
| init_entry and probe persistent-log selection | Keep our DSU mount bootstrap and bounded private metadata log. The fork's hardcoded physical sda30 cust mount and writable physical-partition marker do not apply to OP13. |
| Optional missing aliases/assets and conditional msm_drm loading | Keep our required native-file inventory and firmware-specific module dependency plan. The v7 phone log already passes those stages. |
| Composer removes 3048×2032/dual-plane assumptions and accepts a single plane | Useful display reference. Our backend already selects OP13 modes independently; Xiaomi plane/connector assumptions are not transplanted. |
| Composer fence handling, initial/power-on modesets, pacing, black-frame handling, brightness and fallback vsync | Retain as reviewed follow-up work for graphics after IPC. Applying this different composer implementation is not a demonstrated fix for current token/service-registration errors. |
| Allocator accepts bit 25 ALLOC_NO_IPC as a routing hint | Relevant if the HMOS5 allocator exhibits that usage bit. No such failure has been established in v7; allocation layout must remain unchanged. |
| Turnip recomputes imported image layout after clearing old layout/size | Relevant to a future Turnip backend. The current OP13 package uses software Mesa and does not execute this Turnip import path. |
| native_screenlock_disable.cpp and prebuilt request client | Source inspected; not imported as a boot workaround. It acts on later framework/user lock services, while our registration is failing earlier. The binary is not copied. |
| apply-compat-paths.sh / apply-runtime-v116.sh binary patch offsets | Reject donor-specific HMOS6/7 offsets for our HMOS5 libraries. Any needed adaptation requires exact donor identity and instruction-level validation. |
| build_current.py | Its 4 KiB link-page settings and extra screenlock target were reviewed; our existing native toolchain and runtime validation remain the build path. |
| .gitignore, SHA256SUMS and SOURCE-MANIFEST.json | Repository metadata, not evidence of OP13 boot compatibility. No foreign release manifest or binary hashes are adopted as ours. |

## IPC inherited by the fork

The most relevant implementation is inherited code, not a new addition in that
single commit: `compat/ipc` provides real task tokens and an isolated HarmonyOS
Binder driver. The base README describes a 5.15.178 kernel with the required IPC
interfaces. Its private_api.c resolves `sheng_hmos_ipc_api_v1`; the companion
kernel patch exports a typed table of real core helpers. Thus the repo does not
establish that an arbitrary stock OP13 kernel can load the unchanged module.

Imported 21 pinned files covering the IPC implementation, two native contract
tests and the companion typed API references under ipc/upstream. The OP13
Linux 6.6 preparation and object audit are described in ipc/README.md. They are
host-only prototypes and are absent from the DSU ZIP. No custom kernel Image
is being prepared, and the supplied kernel tree is the only tree used for
compilation.

Credits name matejkoo217 (~jkoo) and AnyHarmonyOSNext in the root README and
NOTICE. GPL attribution, full license text and per-file provenance are retained.
