#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise IPC on the unmodified stock kernel in a diskless, networkless VM."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import stat
import struct
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', type=Path, required=True)
    parser.add_argument('--stock-boot', type=Path, required=True)
    parser.add_argument('--module', type=Path, required=True)
    parser.add_argument('--expected-kernel', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ndk', type=Path, default=Path('/opt/android-ndk'))
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('use a new empty output directory')
    out.mkdir(parents=True, exist_ok=True)
    boot = args.stock_boot.read_bytes()
    if boot[:8] != b'ANDROID!' or struct.unpack_from('<I', boot, 40)[0] != 4:
        raise ValueError('expected stock Android boot header v4')
    length = struct.unpack_from('<I', boot, 8)[0]
    kernel = boot[4096:4096 + length]
    if len(kernel) != length or kernel[56:60] != b'ARM\x64':
        raise ValueError('invalid full stock kernel extent')
    (out / 'stock-kernel').write_bytes(kernel)
    here = Path(__file__).resolve().parent
    cc = args.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang'
    binaries = {'init': here / 'vm_init.c',
                'token_contract': here / 'upstream/tests/ipc/token_contract.c',
                'binder_nested': here / 'upstream/tests/ipc/binder_nested.c'}
    for name, source in binaries.items():
        subprocess.run([str(cc), '-static', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-I', str(here / 'upstream/compat/ipc'),
                        str(source), '-o', str(out / name)], check=True)
    archive = bytearray()
    inode = 0

    def entry(name, mode, data=b'', major=0, minor=0):
        nonlocal inode
        inode += 1
        fields = [inode, mode, 0, 0, 1, 0, len(data), 0, 0, major, minor, len(name) + 1, 0]
        archive.extend(b'070701' + b''.join(f'{x:08x}'.encode() for x in fields))
        archive.extend(name.encode() + b'\0')
        archive.extend(b'\0' * (-len(archive) % 4))
        archive.extend(data)
        archive.extend(b'\0' * (-len(archive) % 4))

    for name in ('dev', 'proc', 'sys'):
        entry(name, stat.S_IFDIR | 0o755)
    entry('dev/console', stat.S_IFCHR | 0o600, major=5, minor=1)
    for name in binaries:
        entry(name, stat.S_IFREG | 0o755, (out / name).read_bytes())
    entry('op13_hmos_ipc.ko', stat.S_IFREG | 0o600, args.module.read_bytes())
    entry('TRAILER!!!', 0)
    (out / 'initramfs.cpio.gz').write_bytes(gzip.compress(archive, mtime=0))
    command = [str(args.qemu.resolve()), '-machine', 'virt,gic-version=3', '-cpu', 'max',
               '-smp', '2', '-m', '1024', '-nodefaults', '-display', 'none', '-monitor', 'none',
               '-serial', 'stdio', '-nic', 'none', '-no-reboot', '-kernel', str(out / 'stock-kernel'),
               '-initrd', str(out / 'initramfs.cpio.gz'), '-append',
               'console=ttyAMA0 earlycon=pl011,mmio32,0x9000000 rdinit=/init nokaslr enforcing=0 loglevel=7 panic=-1']
    with (out / 'console.log').open('wb') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=90)
    text = (out / 'console.log').read_text()
    passed = result.returncode == 0 and all(x in text for x in (
        'OP13_IPC_VM kernel=' + args.expected_kernel + '\n',
        'VM TEST /token_contract status=0', 'VM TEST /binder_nested status=0',
        'native Harmony Binder and task-token APIs verified', 'OP13_IPC_VM_RESULT=PASS'))
    report = {'passed': passed, 'stock_boot_sha256': digest(args.stock_boot),
              'stock_kernel_sha256': hashlib.sha256(kernel).hexdigest(),
              'kernel_release': args.expected_kernel, 'module_sha256': digest(args.module),
              'bootstrap_source_sha256': digest(here.parent / 'dsu_bootstrap.c'),
              'module_load_flags': 0, 'production_ipc_aliases_exercised': True,
              'kernel_image_modified': False, 'physical_phone_tested': False,
              'scope': 'Real stock kernel and module in diskless/networkless QEMU; token, nested Binder and production alias tests'}
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if not passed:
        raise RuntimeError('stock-kernel VM IPC contract failed; inspect console.log')


if __name__ == '__main__':
    main()
