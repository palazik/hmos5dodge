# SPDX-License-Identifier: Apache-2.0
"""Expose the native graphics implementation closure before Android vendor DSOs."""
import shutil
import json

BRIDGES = ('liballocator_service_1.0.z.so', 'libdisplay_composer_service_1.2.z.so',
           'libcomposer_buffer_cache.z.so', 'libdisplay_buffer_stub_1.1.z.so',
           'libdisplay_buffer_stub_1.2.z.so')
CONFIG = 'system/etc/ld-musl-namespace-aarch64.ini'
DRM_TARGET = '/system/lib64/chipset-sdk/libdrm.so'
SANDBOX_CONFIGS = tuple('system/etc/sandbox/' + name + '-sandbox.json' for name in ('system', 'chipset'))
GRAPHICS_BIND = {'src-path': '/hmos/display', 'sandbox-path': '/hmos/display',
                 'sandbox-flags': ['bind', 'rec', 'private']}
FILES = (CONFIG, *SANDBOX_CONFIGS, *('hmos/display/' + name for name in BRIDGES))
SYMLINK = 'hmos/display/libdrm.so'


def native_config(source):
    text = source.decode()
    if '/hmos/display' in text:
        raise ValueError('expected unmodified donor namespace configuration')
    lines = text.splitlines(keepends=True)
    count = 0
    for i, line in enumerate(lines):
        if line.strip().startswith(('namespace.default.lib.paths = ', 'namespace.default.asan.lib.paths = ')):
            left, right = line.split(' = ', 1)
            if not right.startswith('/system/lib64:'):
                raise ValueError('unexpected donor default namespace paths')
            lines[i] = left + ' = /hmos/display:' + right
            count += 1
    if count != 4:
        raise ValueError('expected normal/ASAN paths for both native namespace sections')
    return ''.join(lines).encode()


def sandbox_config(source):
    data = json.loads(source)
    if data.get('sandbox-root') not in ('/mnt/sandbox/system', '/mnt/sandbox/chipset'):
        raise ValueError('unexpected native service sandbox root')
    paths = data['mount-bind-paths']
    if any(row.get('src-path', '').startswith('/hmos') or row.get('sandbox-path', '').startswith('/hmos') for row in paths):
        raise ValueError('expected donor sandbox without a port runtime entry')
    if not any(row.get('src-path') == row.get('sandbox-path') == '/system/lib64' for row in paths):
        raise ValueError('native sandbox does not expose its ordinary system libraries')
    paths.append(GRAPHICS_BIND)
    return (json.dumps(data, indent=2) + '\n').encode()


def install(workspace, stage):
    target = stage / CONFIG
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(native_config((workspace / 'work/inspect/system' / CONFIG).read_bytes()))
    for name in SANDBOX_CONFIGS:
        target = stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(sandbox_config((workspace / 'work/inspect/system' / name).read_bytes()))
    display = stage / 'hmos/display'
    display.mkdir(parents=True, exist_ok=True)
    for name in BRIDGES:
        shutil.copyfile(workspace / 'work/inspect/vendor-lib64' / name, display / name)
    link = display / 'libdrm.so'
    if link.is_symlink():
        link.unlink()
    elif link.exists():
        raise ValueError('refusing to replace a regular DRM library in the overlay')
    link.symlink_to(DRM_TARGET)


def verify(workspace, stage):
    if (stage / CONFIG).read_bytes() != native_config((workspace / 'work/inspect/system' / CONFIG).read_bytes()):
        raise ValueError('native graphics namespace differs from the reviewed path-only edit')
    for name in SANDBOX_CONFIGS:
        original = json.loads((workspace / 'work/inspect/system' / name).read_bytes())
        changed = json.loads((stage / name).read_bytes())
        if changed['mount-bind-paths'].pop() != GRAPHICS_BIND or changed != original:
            raise ValueError('native sandbox changed beyond the graphics bind: ' + name)
    link = stage / 'hmos/display/libdrm.so'
    if not link.is_symlink() or str(link.readlink()) != DRM_TARGET:
        raise ValueError('native DRM link does not select the original HMOS implementation')
    for name in BRIDGES:
        if (stage / 'hmos/display' / name).read_bytes() != (workspace / 'work/inspect/vendor-lib64' / name).read_bytes():
            raise ValueError('native graphics bridge changed: ' + name)


def stage_validated(workspace, stage):
    source = workspace / 'out/op13-graphics-runtime'
    verify(workspace, source)
    for name in FILES:
        target = stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, target)
    link = stage / SYMLINK
    if link.is_symlink():
        link.unlink()
    elif link.exists():
        raise ValueError('unexpected regular DRM file in system patch')
    link.symlink_to(DRM_TARGET)
