#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build a graphics module plan from one firmware's DLKM and vendor ramdisk."""
import re
import subprocess


def display_supplier_roots(capture):
    """Read bound module providers observed in the phone's display device links."""
    providers, nodes, edges = set(), 0, 0
    display_bound = False
    for line in capture.read_text().splitlines():
        row = line.split('|')
        if row[0] == 'EDGE' and len(row) == 3:
            edges += 1
            continue
        if row[0] != 'NODE' or len(row) != 4 or not row[1].startswith('/sys/devices/'):
            raise ValueError('invalid display supplier capture')
        nodes += 1
        if not row[3]:
            continue  # built-in providers have no module link
        match = re.fullmatch(r'/sys/module/([A-Za-z0-9_]+)', row[3])
        if not match or not row[2].startswith('/sys/bus/'):
            raise ValueError('invalid bound module provider')
        providers.add(match[1])
        if row[2].endswith('/msm_drm') and match[1] == 'msm_drm':
            display_bound = True
    if not display_bound or not nodes or not edges:
        raise ValueError('capture does not show a bound msm_drm and its suppliers')
    # Register supplier modules before requesting the display/GPU consumers.
    roots = sorted(providers - {'msm_drm', 'msm_kgsl'}) + ['msm_drm', 'msm_kgsl']
    return roots, {'bound_provider_modules': sorted(providers), 'node_count': nodes, 'edge_count': edges}


def ramdisk_modules(archive, output):
    """Read regular module files only; never extract archive paths or symlinks."""
    data = archive.read_bytes()
    position = 0
    found = {}
    while position < len(data):
        if data[position] == 0:
            position += 1
            continue
        if data[position:position + 6] not in (b'070701', b'070702'):
            raise ValueError('invalid newc ramdisk header')
        fields = [int(data[position + 6 + i * 8:position + 14 + i * 8], 16) for i in range(13)]
        mode, size, length = fields[1], fields[6], fields[11]
        if not 0 < length < 4096 or position + 110 + length > len(data):
            raise ValueError('invalid ramdisk name')
        raw = data[position + 110:position + 110 + length]
        if raw[-1:] != b'\0':
            raise ValueError('unterminated ramdisk name')
        name = raw[:-1].decode()
        start = (position + 110 + length + 3) & ~3
        if start + size > len(data):
            raise ValueError('truncated ramdisk file')
        if mode & 0o170000 == 0o100000 and name.startswith('lib/modules/') and name.endswith('.ko'):
            basename = name.rsplit('/', 1)[-1]
            if not re.fullmatch(r'[A-Za-z0-9_+.-]+\.ko', basename):
                raise ValueError('invalid module filename')
            payload = data[start:start + size]
            if basename in found and found[basename] != payload:
                raise ValueError('conflicting ramdisk modules: ' + basename)
            found[basename] = payload
        position = (start + size + 3) & ~3
    output.mkdir()
    for name, payload in found.items():
        (output / name).write_bytes(payload)
    if not found:
        raise ValueError('no vendor ramdisk modules')


def dependency_plan(modules, roots=('msm_drm', 'msm_kgsl')):
    ordered, done, active, missing_soft = [], set(), set(), set()

    def visit(name):
        if name in done:
            return
        if name not in modules:
            raise ValueError('missing hard module dependency: ' + name)
        if name in active:
            raise ValueError('module dependency cycle: ' + name)
        active.add(name)
        entry = modules[name]
        dependencies = [x.replace('-', '_') for x in entry.get('depends', '').split(',') if x]
        for hint in entry.get('softdep', '').partition('pre:')[2].partition('post:')[0].split():
            hint = hint.replace('-', '_')
            if hint in modules:
                dependencies.append(hint)
            else:
                missing_soft.add(hint)
        for dependency in dependencies:
            visit(dependency)
        active.remove(name)
        done.add(name)
        ordered.append(entry)

    for name in roots:
        visit(name)
    return ordered, sorted(missing_soft)


def crc_checks(workspace, plan):
    def query(flag, path):
        result = subprocess.run(['modprobe', flag, str(path)], text=True, capture_output=True)
        if result.returncode:
            section = '__ksymtab' if flag == '--show-exports' else '__versions'
            listing = subprocess.check_output(['readelf', '-SW', str(path)], text=True)
            if section in listing:
                raise RuntimeError(result.stderr)
            return []
        return [line.split() for line in result.stdout.splitlines()]

    exports = {}
    for row in plan:
        for crc, symbol in query('--show-exports', workspace / row['source']):
            if symbol in exports and exports[symbol] != crc:
                raise ValueError('conflicting module export CRC: ' + symbol)
            exports[symbol] = crc
    matched, unprovided = 0, set()
    for row in plan:
        for crc, symbol in query('--show-modversions', workspace / row['source']):
            if symbol in exports:
                if exports[symbol] != crc:
                    raise ValueError('module import CRC mismatch: ' + row['name'] + ':' + symbol)
                matched += 1
            else:
                unprovided.add(symbol)
    return {'matching_imports_from_closure_modules': matched, 'mismatches': [],
            'conflicting_exports': [], 'imports_without_module_provider': len(unprovided),
            'kernel_exports_verified': False,
            'scope': 'Only module exports compared; running kernel exports, signatures and loading untested'}
