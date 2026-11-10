#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise native loader paths and HDI implementation loading in an isolated root."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from graphics_runtime import BRIDGES, CONFIG, DRM_TARGET, install, verify

REPO = Path(__file__).resolve().parents[1]
ROOT = REPO.parent


def library_skeleton(source, target):
    target.mkdir()
    for path in source.iterdir():
        to = target / path.name
        if path.is_symlink():
            to.symlink_to(os.readlink(path))
        elif path.is_dir():
            to.mkdir()
        elif path.is_file():
            os.link(path, to)  # immutable fixture; no duplicate library data


def main():
    runtime = ROOT / 'out/op13-graphics-runtime'
    runtime.mkdir(exist_ok=True)
    install(ROOT, runtime)
    for name in ('libdisplay_buffer_vdi_impl.z.so', 'libdisplay_composer_vdi_impl.z.so'):
        # Hardlinks are not used for the persistent stage: image builders set modes.
        shutil.copyfile(ROOT / 'out/op13-display' / name, runtime / 'hmos/display' / name)
    verify(ROOT, runtime)
    tests = []
    with tempfile.TemporaryDirectory(prefix='graphics-loader-', dir=ROOT / 'work') as temporary:
        fixture = Path(temporary)
        system = ROOT / 'work/inspect/system/system/lib64'
        vendor = ROOT / 'out/op13-vendor-additions/tree/lib64'
        library_skeleton(system, fixture / 'system-lib64')
        library_skeleton(vendor, fixture / 'vendor-lib64')
        # This exact stock collision must be present, otherwise a donor-only
        # fixture silently selects native libdrm and misses the phone failure.
        subprocess.run(['fsck.erofs', '--extract=' + str(fixture / 'vendor-lib64/libdrm.so'),
            '--path=/lib64/libdrm.so', '--no-preserve-owner', '--preserve-perms',
            str(ROOT / 'op13_stock/vendor.img')], check=True)
        before = fixture / 'before'
        before.mkdir()
        (before / 'libdrm.so').symlink_to(DRM_TARGET)
        for name in BRIDGES[:2]:
            assert not (fixture / 'vendor-lib64' / name).exists()
            assert not any(path.name == name for path in system.rglob('*'))
        for name in ('libdisplay_buffer_vdi_impl.z.so', 'libdisplay_composer_vdi_impl.z.so'):
            os.link(ROOT / 'out/op13-display' / name, before / name)
        common = ['bwrap', '--die-with-parent', '--unshare-all', '--tmpfs', '/',
            '--proc', '/proc', '--dev', '/dev', '--dir', '/system/lib', '--dir', '/vendor/bin', '--dir', '/system/bin',
            '--ro-bind', str(fixture / 'system-lib64'), '/system/lib64',
            '--ro-bind', str(fixture / 'vendor-lib64'), '/vendor/lib64',
            '--ro-bind', str(ROOT / 'work/inspect/system/system/etc'), '/system/etc',
            '--ro-bind', str(REPO / 'build/work/op13-sysroot/usr/lib/libc.so'), '/system/lib/ld-musl-aarch64.so.1',
            '--symlink', 'system/lib', '/lib', '--symlink', 'system/lib64', '/lib64', '--symlink', 'system/etc', '/etc']
        for source, target in ((system, '/system/lib64'), (vendor, '/vendor/lib64')):
            for path in source.iterdir():
                if path.is_dir() and not path.is_symlink():
                    common.extend(['--ro-bind', str(path), target + '/' + path.name])
        dependencies = ['libdisplay_buffer_vdi_impl.z.so', 'libdisplay_composer_vdi_impl.z.so',
            'libgralloc_priv.z.so', 'liballocator_driver_1.0.z.so', 'libdisplay_composer_driver_1.0.z.so',
            *BRIDGES]
        for directory in ('vendor', 'system'):
            executable = '/' + directory + '/bin/graphics-loader-test'
            for name, mode, overlay in [('stock-collision', 'expect-stock-collision', None),
                    ('missing-bridges', 'expect-missing-bridges', before),
                    ('native-runtime', 'expect-native', runtime / 'hmos/display')]:
                command = list(common)
                if overlay:
                    command += ['--ro-bind', str(runtime / CONFIG), '/' + CONFIG,
                        '--ro-bind', str(overlay), '/hmos/display']
                command += ['--ro-bind', str(ROOT / 'out/op13-display/graphics-loader-test'), executable,
                    '--ro-bind', str(ROOT / 'work/qemu-aarch64-static'), '/qemu', '--clearenv',
                    '--setenv', 'PATH', '/system/bin:/vendor/bin', '/qemu', executable, mode]
                if mode == 'expect-native':
                    command += dependencies
                elif mode == 'expect-missing-bridges':
                    command += list(BRIDGES[:2])
                result = subprocess.run(command, capture_output=True, text=True, timeout=15)
                label = directory + '-' + name
                (runtime / (label + '.log')).write_text(result.stdout + result.stderr)
                if result.returncode:
                    raise RuntimeError(label + ': ' + result.stdout[-3000:] + result.stderr[-1500:])
                tests.append({'name': label, 'passed': True})
                print('PASS native namespace/HDI loader', label, flush=True)
    (runtime / 'validation.json').write_text(json.dumps({'passed': True, 'tests': tests,
        'scope': 'Actual donor musl namespace config, stock Android DRM collision, native graphics relocation and HDI factories; no DRM display device, service registration or frame tested.'}, indent=2) + '\n')


if __name__ == '__main__':
    main()
