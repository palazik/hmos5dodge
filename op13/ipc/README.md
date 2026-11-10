# Stock-kernel IPC investigation

Revision 7 reaches native init on OP13 but lacks native access-token setup and
has repeated samgr authorization failures. This directory imports the genuine
isolated Binder/access-token implementation from
[matejkoo217/anyhmos_xiaomi_fuxi](https://github.com/matejkoo217/anyhmos_xiaomi_fuxi),
pinned to `748c8fd3b2b61ad1083db032098f34770de723d2`. Its underlying project is
[AnyHarmonyOSNext/anyhmos_xiaomi_sheng](https://github.com/AnyHarmonyOSNext/anyhmos_xiaomi_sheng).
The 21 imported files are unmodified and SHA-256 pinned in UPSTREAM.json.
Their GPL notices remain intact; the [full license](../../LICENSES/GPL-2.0.txt)
is included in the repository.

The prototype retains the stock kernel Image and prepares an additional
`op13_hmos_ipc` module implementation. A loaded .ko executes in the kernel;
keeping the Image unchanged does not make a module independent of its ABI.
No .ko is built, loaded or included in the current DSU package.

## Linux 6.6 adaptation

The upstream module uses a typed kernel export table, supplied by its companion
kernel patch. Copying that .ko onto an unmodified OP13 kernel would not create
the missing export. Linux 6.6 uses KCFI, which allows a different prototype:

- Resolve actual helper entry points using temporary kprobes, unregistering
  each probe before using the result or registering the driver.
- Check each target's KCFI prefix against its compiler-produced type hash.
  Missing symbols or mismatched types abort initialization.
- Call the actual security and memory-management functions, keeping compiler
  CFI checks enabled. There are no replacement permission-success stubs.
- Retain upstream per-task token inheritance through Android task hooks and
  the separate HarmonyOS Binder transport; ordinary Android Binder is separate.
- Adapt fd closing, freezer waits, VMA flags, shrinker registration and page
  unmapping to the APIs in the supplied Linux 6.6 tree.

The compiler-generated type constants need declarations in a separate
translation unit from the three forwarding definitions. The object audit checks
all 20 constants, the three actual ARM64 function prefixes and the absence of
direct private-helper imports in allocated relocation sections after applying
the kernel module linker script.

## Current evidence and limits

The supplied `kernel_workspace/common` is Linux 6.6.144. The captured phone
kernel is 6.6.118-android15-8-g93e223c276e7-abogki500782043-4k. Header preparation
and the six-source AArch64 object build passed using the supplied Clang 19
toolchain, KCFI enabled and implicit-declaration/pointer-type errors fatal.
The stock configuration was captured from the phone, but olddefconfig against
a different source version does not reproduce the stock ABI. The captured
stock build uses Clang 18. No kernel Image, vmlinux or final .ko was built.

The source-preservation/refusal tests and ELF audit passed. Runtime availability
of exported dependencies, exact structure ABI and symbol CRCs, module signature
acceptance, task-hook behavior and native token/Binder contracts remain
unverified. The captured kernel trims unused exports and enables module
signature protection; the object audit cannot establish loadability.
Do not invent CRCs, force vermagic, bypass CFI or treat compilation as permission
to load this prototype. No device test is requested at this stage.

Prepare source in a new empty host work directory:

```sh
python3 op13/ipc/prepare.py --output ../work/stock-ipc-module
python3 -m unittest discover -s op13/ipc -p 'test_*.py' -v
```

Use the provided kernel tree and toolchain with a separate Kbuild `O=` directory.
After gki_defconfig, install the captured config in that output directory and
run olddefconfig and modules_prepare. With that prepared output, the tested
compile target is `M=<absolute prepared-module-directory> op13_hmos_ipc.o`,
using LLVM=1, LLVM_IAS=1, ARCH=arm64 and the supplied linker wrapper convention.
This target checks source compilation only. Apply generated scripts/module.lds
with ld.lld -r to create a separate `.link-audit.o`, then inspect it:

```sh
python3 op13/ipc/audit_object.py \
  --object ../work/stock-ipc-module/op13_hmos_ipc.link-audit.o \
  --preparation ../work/stock-ipc-module/PREPARATION.json \
  --report ../work/stock-ipc-object-audit.json
```

The imported token and nested-Binder contract tests are retained as source for
future verified module testing. They were not executed on the connected phone.
See ../FUXI-REVIEW.md for the commit-level applicability review and exclusions.
