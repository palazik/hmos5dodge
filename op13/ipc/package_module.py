#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Add the VM-tested compatibility module to a copy of stock vendor_dlkm."""
import hashlib
import json
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile


RUNTIME_PATH = '/vendor_dlkm/lib/modules/op13_hmos_ipc.ko'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def attach(workspace, report, module, vm_report):
    vm = json.loads(vm_report.read_text())
    if not vm['passed'] or not vm['production_ipc_aliases_exercised'] or vm['module_load_flags'] != 0:
        raise ValueError('normal-load VM contract validation required')
    if digest(module) != vm['module_sha256']:
        raise ValueError('module differs from VM-tested bytes')
    if digest(Path(__file__).resolve().parent.parent / 'dsu_bootstrap.c') != vm['bootstrap_source_sha256']:
        raise ValueError('bootstrap IPC aliases changed after VM test')
    if digest(workspace / 'op13_stock/boot.img') != vm['stock_boot_sha256']:
        raise ValueError('stock boot differs from VM input')
    report['ipc_module'] = {'source': str(module.resolve()), 'sha256': digest(module),
                            'runtime_path': RUNTIME_PATH, 'stock_kernel_vm': vm}
    report['load_plan'].append({'name': 'op13_hmos_ipc',
        'source': str(module.resolve().relative_to(workspace)), 'sha256': digest(module),
        'partition': 'vendor_dlkm-compat', 'runtime_path': RUNTIME_PATH,
        'depends': '', 'softdep': '', 'stock_module_modified': False})


def debug(image, command, write=False):
    result = subprocess.run(['debugfs', *(['-w'] if write else []), '-R', command, str(image)],
                            capture_output=True, text=True, check=True)
    # debugfs can report command failure with exit status zero. Every mutation
    # is subsequently checked by filesystem readback and e2fsck.
    return result.stdout


def quote(path):
    return '"' + str(path).replace('\\', '\\\\').replace('"', '\\"') + '"'


def inventory(image):
    result = {}

    def visit(directory):
        for line in debug(image, 'ls -l -p ' + directory).splitlines():
            if not line.startswith('/'):
                continue
            _, inode, mode, uid, gid, name, length, _ = line.split('/')
            if name in ('.', '..'):
                continue
            path = directory.rstrip('/') + '/' + name
            result[path] = {'inode': int(inode), 'mode': int(mode, 8),
                            'uid': int(uid), 'gid': int(gid), 'bytes': length}
            if stat.S_ISDIR(int(mode, 8)):
                visit(path)
    visit('/')
    return result


def contents(image):
    with tempfile.TemporaryDirectory(prefix='op13-dlkm-readback-', dir=image.parent) as directory:
        debug(image, 'rdump / ' + quote(directory))
        result = {}
        for path in sorted(Path(directory).rglob('*')):
            name = '/' + str(path.relative_to(directory))
            if path.is_symlink():
                result[name] = {'symlink': str(path.readlink())}
            elif path.is_file():
                result[name] = {'sha256': digest(path)}
        return result


def verify(source, target, module):
    before, after = inventory(source), inventory(target)
    new = '/lib/modules/op13_hmos_ipc.ko'
    if set(after) != set(before) | {new}:
        raise ValueError('unexpected vendor_dlkm directory changes')
    for name, row in before.items():
        check = dict(after[name])
        if stat.S_ISDIR(row['mode']):
            check['bytes'] = row['bytes']  # parent directory may grow
        if row != check:
            raise ValueError('stock inode metadata changed: ' + name)
    original, updated = contents(source), contents(target)
    for name, row in original.items():
        if updated.get(name) != row:
            raise ValueError('stock DLKM content changed: ' + name)
    if updated.get(new) != {'sha256': digest(module)}:
        raise ValueError('packed compatibility module differs')
    row = after[new]
    if (row['mode'], row['uid'], row['gid']) != (0o100644, 0, 0):
        raise ValueError('incorrect compatibility module metadata')
    attrs = debug(target, 'ea_list ' + new)
    if 'u:object_r:vendor_file:s0' not in attrs:
        raise ValueError('missing module SELinux xattr')
    result = subprocess.run(['e2fsck', '-fn', str(target)], capture_output=True, text=True)
    if result.returncode:
        raise ValueError('modified vendor_dlkm filesystem check failed: ' + result.stdout + result.stderr)
    return {'passed': True, 'stock_inode_metadata_entries_preserved': len(before),
            'stock_file_contents_or_symlinks_preserved': len(original),
            'added_path': new, 'module_sha256': digest(module),
            'original_stock_modules_modified': False}


def patch(source, target, ipc):
    shutil.copyfile(source, target)
    if subprocess.run(['avbtool', 'info_image', '--image', str(target)], capture_output=True).returncode == 0:
        subprocess.run(['avbtool', 'erase_footer', '--image', str(target)], check=True)
    module = Path(ipc['source'])
    debug(target, 'write ' + quote(module) + ' /lib/modules/op13_hmos_ipc.ko', write=True)
    for field, value in [('mode', '0100644'), ('uid', '0'), ('gid', '0')]:
        debug(target, 'set_inode_field /lib/modules/op13_hmos_ipc.ko ' + field + ' ' + value, write=True)
    with tempfile.TemporaryDirectory(prefix='op13-module-label-', dir=target.parent) as directory:
        value = Path(directory) / 'label'
        value.write_bytes(b'u:object_r:vendor_file:s0\0')
        debug(target, 'ea_set -f ' + quote(value) + ' /lib/modules/op13_hmos_ipc.ko security.selinux', write=True)
    return verify(source, target, module)
