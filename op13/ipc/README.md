# HarmonyOS IPC module for the stock OP13 kernel

Revision 8 adds `op13_hmos_ipc.ko` inside vendor_dlkm.img. The OP13 kernel
Image and vendor_boot remain stock. The module provides a genuine per-task
access-token device and a separate HarmonyOS Binder transport. Existing
Qualcomm and GKI module bytes remain unchanged.

The module passed runtime contracts on the unchanged stock 6.6.118 kernel in
QEMU. Tests cover task/fork/thread inheritance, permission changes, rejected
unprivileged token spoofing, demand-paged ioctl buffers, sender/first-caller
identity, 64 nested calls and concurrent caller isolation. The stripped module
also passed with the production bootstrap's four device aliases and API checks.
These results establish IPC behavior on that kernel in a VM; the revision-8
phone boot and complete HarmonyOS framework remain untested.

## Sources and kernel interfaces

The 21 unmodified reference files in upstream are SHA-256 pinned in UPSTREAM.json,
from [matejkoo217/anyhmos_xiaomi_fuxi](https://github.com/matejkoo217/anyhmos_xiaomi_fuxi)
at `748c8fd3b2b61ad1083db032098f34770de723d2`, based on
[AnyHarmonyOSNext/anyhmos_xiaomi_sheng](https://github.com/AnyHarmonyOSNext/anyhmos_xiaomi_sheng).
GPL notices and the [full license](../../LICENSES/GPL-2.0.txt) are retained.
See ../FUXI-REVIEW.md for the commit review.

prepare.py adapts the supplied Linux 6.6.144 headers/APIs for the separate
module. It does not edit kernel_workspace/common or build a kernel Image.
The upstream typed-export-table kernel patch remains a reference; it is not
applied. The OP13 module resolves 20 real helper entry points with temporary
kprobes, unregistering each probe and checking its actual KCFI prefix against
a compiler-produced type hash before use. Missing symbols or mismatches fail
initialization. Compiler CFI and actual security/MM functions remain active.
Weak addressability references exist only to request compiler type constants;
they are discarded by the module linker script and supply no runtime fallback.

The live BTF capture matches the full stock Image exactly. inspect_stock.py
also validates every one of 110,092 core symbol names against the compressed
Image tables. It extracts the authentic stock export metadata, rather than
inventing CRCs: all 140 direct imports exist and are vendor-accessible, and all
20 helper KCFI hashes match. Normal Kbuild/modpost uses that stock Module.symvers.
The module retains its source-tree vermagic; no release string or version entry
is patched. The stock kernel accepted the unsigned module with load flags zero
in QEMU, with its normal MODVERSIONS and GKI signature protection enabled.

BTF comparison found 355 identical named kernel structure layouts. task_struct
has the same 4,800-byte extent; the newer source uses the stock eight-byte
android_kabi_reserved2 slot for a user_dumpable union. No task layout is extended
by this module. This inspection and the real-kernel runtime tests have a finite
scope; they are not a universal ABI guarantee across OP13 firmware versions.
A separate aggregated genksyms witness produced 85 equal and 55 different type
CRCs. Its header inclusion context differs from the kernel export translation
units; it was not used to overwrite any authentic stock CRC. The successful
normal stock-kernel load and runtime contracts are the runtime evidence.

## Reproduce the checks

Inputs stay local: the supplied kernel tree and Clang toolchain, captured stock
config/BTF/kallsyms, current stock boot.img and the pinned source references.
Use a new work directory for each tool's output.

```sh
python3 op13/ipc/prepare.py --output WORK/module
python3 -m unittest discover -s op13/ipc -p 'test_*.py' -v
```

Prepare an external-module Kbuild output using the supplied tree: gki_defconfig,
the captured config, then olddefconfig and modules_prepare. Use LLVM=1,
LLVM_IAS=1, ARCH=arm64, the supplied toolchain and separate O= and M= directories.
Build `op13_hmos_ipc.o` first; apply generated scripts/module.lds with ld.lld -r
for an object audit. audit_object.py checks AArch64, all 20 compiler constants,
three compiled ARM64 prefixes and the absence of live direct private imports.

```sh
python3 op13/ipc/inspect_stock.py --stock-boot STOCK_BOOT \
  --kallsyms CAPTURED_KALLSYMS --btf CAPTURED_BTF \
  --prototype-audit OBJECT_AUDIT_JSON --output WORK/stock-exports
```

Use that verified Module.symvers in the Kbuild output and build the external
`modules` target. Do not run modules_install. Strip debug sections into a
separate runtime .ko, then test those exact runtime bytes:

```sh
python3 op13/ipc/run_vm.py --qemu QEMU_SYSTEM_AARCH64 \
  --stock-boot STOCK_BOOT --module RUNTIME_KO \
  --expected-kernel 6.6.118-android15-8-g93e223c276e7-abogki500782043-4k \
  --output WORK/vm-result
```

The runner takes the exact boot-v4 kernel extent. An unpacker's kernel_dtb split
was inside this kernel's relocation data, so its truncated kernel output is
unsuitable for this VM. The runner uses RAM initramfs, no disks and no network;
vm_init.c is a VM fixture and is not installed on the phone or in the DSU images.
No phone module-loading commands are part of this workflow.

package_module.py requires a passing VM report matching both the exact module
bytes and production bootstrap source. It adds the module to a copy of stock
vendor_dlkm and checks all original contents and inode ownership/modes, the new
module hash/label, and e2fsck. The bootstrap explicitly loads it before dropping
module capability, creates the four separate misc aliases and requires real
native feature/token ioctls before handing control to HarmonyOS init.
