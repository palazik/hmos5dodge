#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build and exercise the offline DSU components without connecting a phone."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import subprocess
from selinux_config import permissive_config
from hdf_hosts import validate as validate_hdf_hosts

REPO = Path(__file__).resolve().parents[1]
WORKSPACE = REPO.parent
OUTPUT = WORKSPACE / 'out/component-validation'
SYSROOT = REPO / 'build/work/op13-sysroot'


def run(name, command, env=None, expected=0):
    result = subprocess.run([str(item) for item in command], cwd=REPO, env=env,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    (OUTPUT / (name + '.log')).write_text(result.stdout)
    if result.returncode != expected:
        raise RuntimeError(name + ': ' + result.stdout[-4000:])
    print('PASS', name, flush=True)
    return {'name': name, 'exit_status': result.returncode, 'passed': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup-dir', type=Path, default=WORKSPACE / 'out/op13-startup')
    args = parser.parse_args()
    startup = args.startup_dir.resolve()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    bootstrap = WORKSPACE / 'out/op13-bootstrap'
    storage = WORKSPACE / 'out/op13-storage'
    display = WORKSPACE / 'out/op13-display'
    qemu = WORKSPACE / 'work/qemu-aarch64-static'
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', '/opt/android-ndk'))
    cc = ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang'
    tests = []
    tests.append(run('bootstrap-build', [cc, '-static', '-O2', '-Wall', '-Wextra', '-Werror',
        'op13/dsu_bootstrap.c', '-o', bootstrap / 'init']))
    tests.append(run('guard-test-build', ['clang', '-O1', '-g', '-fsanitize=address,undefined',
        '-Wall', '-Wextra', '-Werror', 'op13/bootstrap_test.c', '-o', bootstrap / 'bootstrap-test']))
    tests.append(run('device-filter-and-data-scan', [bootstrap / 'bootstrap-test']))
    tests.append(run('bootstrap-refuses-non-handoff', [qemu, bootstrap / 'init', '--not-pid1'], expected=2))
    tests.append(run('namespace-test-build', ['clang', '-O2', '-Wall', '-Wextra', '-Werror',
        'op13/namespace_test.c', '-o', bootstrap / 'namespace-test']))
    (bootstrap / 'namespace-marker').touch()
    tests.append(run('native-mount-root-round-trip', ['bwrap', '--die-with-parent', '--unshare-all',
        '--uid', '0', '--gid', '0', '--cap-add', 'CAP_SYS_ADMIN', '--cap-add', 'CAP_SYS_CHROOT',
        '--tmpfs', '/', '--proc', '/proc',
        '--dev', '/dev', '--dir', '/test',
        '--ro-bind', bootstrap / 'namespace-marker', '/hmos-namespace-test',
        '--ro-bind', bootstrap / 'namespace-test', '/test/namespace-test',
        '--ro-bind', '/usr/lib', '/usr/lib', '--symlink', 'usr/lib', '/lib',
        '--symlink', 'usr/lib', '/lib64', '/test/namespace-test']))
    # Run the actual donor init with its native loader/search paths. Bwrap's
    # reaper remains PID 1, so donor init reaches its non-PID1 refusal instead
    # of starting services. No LD_LIBRARY_PATH or substituted libc is used.
    tests.append(run('arm64-donor-init-loader', ['bwrap', '--die-with-parent', '--unshare-all',
        '--uid', '0', '--gid', '0', '--tmpfs', '/', '--proc', '/proc', '--dev', '/dev',
        '--dir', '/system/lib',
        '--ro-bind', WORKSPACE / 'work/inspect/system/system/bin', '/system/bin',
        '--ro-bind', WORKSPACE / 'work/inspect/system/system/lib64', '/system/lib64',
        '--ro-bind', WORKSPACE / 'work/inspect/system/system/etc', '/system/etc',
        '--ro-bind', SYSROOT / 'usr/lib/libc.so', '/system/lib/ld-musl-aarch64.so.1',
        '--symlink', 'system/lib', '/lib', '--symlink', 'system/lib64', '/lib64',
        '--symlink', 'system/etc', '/etc', '--clearenv', '--setenv', 'PATH', '/system/bin:/vendor/bin',
        '--ro-bind', qemu, '/qemu', '/qemu', '/system/bin/init', '--second-stage', '0']))
    environment = dict(os.environ, SYSROOT=str(SYSROOT))
    donor_etc = WORKSPACE / 'work/inspect/system/system/etc'
    (bootstrap / 'config.permissive').write_bytes(permissive_config((donor_etc / 'selinux/config').read_bytes()))
    tests.append(run('selinux-loader-test-build', ['bash', 'build/ohos-clang', '-O2', '-Wall', '-Wextra', '-Werror',
        'op13/selinux_loader_test.c', '-Wl,--export-dynamic', '-ldl', '-lc', '-o', bootstrap / 'selinux-loader-test'],
        env=environment))
    for suffix, config, cmdline, expected in [
            ('original-enforcing', donor_etc / 'selinux/config', 'console=fixture androidboot.selinux=permissive', 1),
            ('preserved-permissive', bootstrap / 'config.permissive', 'console=fixture androidboot.selinux=permissive', 0),
            ('explicit-enforcing', bootstrap / 'config.permissive', 'console=fixture enforcing=1', 1),
            ('explicit-permissive', donor_etc / 'selinux/config', 'console=fixture enforcing=0', 0)]:
        (bootstrap / 'test-cmdline').write_text(cmdline + '\n')
        tests.append(run('arm64-selinux-' + suffix, ['bwrap', '--die-with-parent', '--unshare-all',
            '--uid', '0', '--gid', '0', '--tmpfs', '/', '--proc', '/proc', '--dev', '/dev',
            '--dir', '/system/lib', '--dir', '/system/bin',
            '--ro-bind', donor_etc, '/system/etc', '--tmpfs', '/system/etc/selinux',
            '--dir', '/system/etc/selinux/targeted/policy',
            '--ro-bind', WORKSPACE / 'work/inspect/system/system/lib64', '/system/lib64',
            '--ro-bind', SYSROOT / 'usr/lib/libc.so', '/system/lib/ld-musl-aarch64.so.1',
            '--ro-bind', config, '/system/etc/selinux/config',
            '--ro-bind', WORKSPACE / 'out/op13-policy/policy.31', '/system/etc/selinux/targeted/policy/policy.31',
            '--ro-bind', bootstrap / 'test-cmdline', '/proc/cmdline',
            '--symlink', 'system/lib', '/lib', '--symlink', 'system/lib64', '/lib64',
            '--symlink', 'system/etc', '/etc', '--ro-bind', bootstrap, '/test',
            '--ro-bind', bootstrap / 'selinux-loader-test', '/system/bin/selinux-loader-test',
            '--ro-bind', qemu, '/qemu', '--clearenv', '--setenv', 'PATH', '/system/bin',
            '/qemu', '/system/bin/selinux-loader-test', str(expected)]))
    tests.append(run('storage-library-build', ['bash', 'build/ohos-clang', '-O2', '-fPIC', '-Wall', '-Wextra', '-Werror',
        '-shared', 'op13/local_storage.c', '-Wl,-soname,libop13_local_storage.so', '-Wl,--no-undefined',
        '-ldl', '-lc', '-o', storage / 'libop13_local_storage.so'], env=environment))
    tests.append(run('storage-test-build', ['bash', 'build/ohos-clang', '-O2', '-Wall', '-Wextra', '-Werror',
        'op13/local_storage_test.c', '-L' + str(storage), '-lop13_local_storage', '-lc',
        '-o', storage / 'storage-test'], env=environment))
    (storage / 'namespace-marker').touch()
    sandbox = ['bwrap', '--die-with-parent', '--unshare-all', '--uid', '0', '--gid', '0',
               '--cap-add', 'CAP_SYS_ADMIN', '--tmpfs', '/', '--proc', '/proc', '--dev', '/dev',
               '--tmpfs', '/tmp', '--dir', '/etc', '--dir', '/mnt', '--dir', '/data',
               '--ro-bind', storage / 'namespace-marker', '/data/hmos-storage-test-namespace',
               '--ro-bind', storage, '/test', '--ro-bind', SYSROOT, '/runtime',
               '--ro-bind', qemu, '/qemu', '/qemu', '-L', '/runtime', '-E',
               'LD_LIBRARY_PATH=/test:/runtime/usr/lib', '/test/storage-test']
    tests.append(run('arm64-local-storage-mounts', sandbox))
    tests.append(run('display-vtable-abi', ['python3', 'op13/check_display_abi.py',
        WORKSPACE / 'work/inspect/vendor-lib64/libdisplay_composer_vdi_impl.z.so',
        display / 'libdisplay_composer_vdi_impl.z.so', '--output', display / 'abi.json']))
    tests.append(run('arm64-display-mode-behavior', ['bwrap', '--die-with-parent', '--unshare-all',
        '--ro-bind', '/', '/', '--dev', '/dev', '--proc', '/proc', '--tmpfs', '/tmp', qemu,
        '-L', SYSROOT, '-E', 'LD_LIBRARY_PATH=' + str(display) + ':' + str(SYSROOT / 'usr/lib'),
        display / 'display-mode-test']))
    tests.append(run('arm64-donor-gralloc-relocation-and-rejection', ['bwrap', '--die-with-parent', '--unshare-all',
        '--ro-bind', '/', '/', '--dev', '/dev', '--proc', '/proc', '--tmpfs', '/tmp', qemu,
        '-L', SYSROOT, '-E', 'LD_LIBRARY_PATH=' + str(display) + ':' +
        str(WORKSPACE / 'work/inspect/vendor-lib64') + ':' + str(SYSROOT / 'usr/lib'),
        display / 'display-gralloc-test']))
    tests.append(run('hdf-native-parser-test-build', ['bash', 'build/ohos-clang', '-std=c11', '-O2',
        '-Ibuild/work/openharmony/hdf_core/interfaces/inner_api/utils',
        '-Ibuild/work/openharmony/hdf_core/interfaces/inner_api/osal/uhdf',
        'op13/hdf_graph_test.c', '-lhdf_utils.z', '-lhilog', '-lsec_shared.z', '-lc',
        '-o', display / 'hdf-graph-test'], env=environment))
    hosts = validate_hdf_hosts(startup, json.loads((startup / 'manifest.json').read_text()))
    tests.append(run('arm64-donor-hdf-graph-host-ids', ['bwrap', '--die-with-parent', '--unshare-all',
        '--ro-bind', '/', '/', '--dev', '/dev', '--proc', '/proc', '--tmpfs', '/tmp', qemu,
        '-L', SYSROOT, '-E', 'LD_LIBRARY_PATH=' + str(SYSROOT / 'usr/lib'),
        display / 'hdf-graph-test', startup / 'hdf_default.hcb', *[s['name'] for s in hosts]]))
    tests.append(run('arm64-input-backend-without-hdf', ['bash', 'op13/check_input_backend.sh']))
    for prefix in ('', 'developer-'):
        policy = WORKSPACE / 'out/op13-policy'
        tests.append(run(prefix + 'policy-delta', ['python3', 'op13/validate_policy_delta.py',
            policy / (prefix + 'before.cil'), policy / (prefix + 'after.cil'),
            '--output', policy / (prefix + 'validation.json')]))
    image = bootstrap / 'test-data.img'
    with image.open('wb') as file:
        file.truncate(64 * 1024 * 1024)
    tests.append(run('arm64-ext4-initialization', ['bwrap', '--die-with-parent', '--unshare-all',
        '--uid', '0', '--gid', '0', '--tmpfs', '/', '--proc', '/proc', '--dev', '/dev',
        '--tmpfs', '/tmp', '--dir', '/system/bin', '--dir', '/system/lib',
        '--ro-bind', WORKSPACE / 'work/inspect/system/system/bin/mke2fs', '/system/bin/mke2fs',
        '--ro-bind', SYSROOT / 'usr/lib/libc.so', '/system/lib/ld-musl-aarch64.so.1',
        '--ro-bind', WORKSPACE / 'work/inspect/system/system/lib64', '/system/lib64',
        '--ro-bind', WORKSPACE / 'work/inspect/system/system/etc', '/system/etc',
        '--symlink', 'system/lib', '/lib', '--symlink', 'system/lib64', '/lib64',
        '--symlink', 'system/etc', '/etc', '--clearenv', '--setenv', 'PATH', '/system/bin:/vendor/bin',
        '--bind', bootstrap, '/test', '--ro-bind', qemu, '/qemu',
        '/qemu', '/system/bin/mke2fs',
        '-t', 'ext4', '-b', '4096', '-I', '256', '-O', 'encrypt,verity', '-F', '-m', '0',
        '-L', 'hmos-dsu', '/test/test-data.img']))
    tests.append(run('formatted-ext4-consistency', ['e2fsck', '-fn', image]))
    tests.append(run('formatted-ext4-header-recognition', [bootstrap / 'bootstrap-test', '--formatted-image', image]))
    tests.append(run('startup-audit-regressions', ['python3', '-m', 'unittest', 'discover', '-s', 'op13', '-p', 'test_*.py']))
    image.unlink()
    artifacts = [bootstrap / 'init', storage / 'libop13_local_storage.so',
        display / 'libdisplay_buffer_vdi_impl.z.so', display / 'libdisplay_composer_vdi_impl.z.so',
        WORKSPACE / 'out/op13-policy/policy.31', WORKSPACE / 'out/op13-policy/developer_policy']
    artifacts.extend(p for p in startup.rglob('*') if p.is_file() and not p.is_symlink())
    record = {'passed': True, 'tests': tests,
              'artifact_sha256': {str(p.relative_to(WORKSPACE)): hashlib.sha256(p.read_bytes()).hexdigest() for p in artifacts},
              'scope': 'Offline userspace/logic/format tests. No OP13 kernel boot, BPF attachment, native framework boot or hardware rendering tested.'}
    (OUTPUT / 'validation.json').write_text(json.dumps(record, indent=2) + '\n')
    print('All offline component checks passed; hardware/DSU boot remains untested.')


if __name__ == '__main__':
    main()
