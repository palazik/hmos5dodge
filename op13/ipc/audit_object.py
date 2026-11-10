#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check a prototype's linked ELF object without loading or packaging it."""
import argparse
import json
from pathlib import Path
import struct


def inspect(path, helpers):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('expected little-endian ELF64')
    if struct.unpack_from('<HH', data, 16) != (1, 183):
        raise ValueError('expected relocatable AArch64 object')
    offset = struct.unpack_from('<Q', data, 40)[0]
    size, count, _ = struct.unpack_from('<HHH', data, 58)
    if size != 64:
        raise ValueError('unexpected ELF section layout')
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * size)
                for i in range(count)]
    tables = {}
    for i, section in enumerate(sections):
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        rows = []
        for pos in range(section[4], section[4] + section[5], section[9]):
            name, info, other, index, value, length = struct.unpack_from('<IBBHQQ', data, pos)
            text = names[name:names.index(b'\0', name)].decode()
            rows.append((text, index, value))
        tables[i] = rows
    active_imports = set()
    for section in sections:
        if section[1] != 4 or not sections[section[7]][2] & 2:  # RELA into SHF_ALLOC
            continue
        for pos in range(section[4], section[4] + section[5], section[9]):
            _, info, _ = struct.unpack_from('<QQq', data, pos)
            name, index, _ = tables[section[6]][info >> 32]
            if index == 0:
                active_imports.add(name)
    symbols = {name: (index, value) for rows in tables.values()
               for name, index, value in rows}
    hashes = {}
    for name in helpers:
        index, value = symbols['__kcfi_typeid_' + name]
        if index != 0xfff1 or not 0 < value <= 0xffffffff:
            raise ValueError('missing compiler-produced KCFI constant: ' + name)
        hashes[name] = f'{value:08x}'
    forbidden = active_imports.intersection(helpers)
    forbidden.update(name for name in active_imports if name.startswith('__kcfi_typeid_'))
    if forbidden:
        raise ValueError('unresolved private call/hash relocation: ' + ', '.join(sorted(forbidden)))
    # ARM64 Clang emits the hash directly before the function entry, without
    # requiring a separate __cfi_* symbol (unlike some other targets).
    for name in ('__wake_up_pollfree', 'list_lru_count_node', 'list_lru_walk_node'):
        index, value = symbols[name]
        section = sections[index]
        if value < 4:
            raise ValueError('missing KCFI prefix: ' + name)
        observed = struct.unpack_from('<I', data, section[4] + value - 4)[0]
        if observed != int(hashes[name], 16):
            raise ValueError('compiled wrapper KCFI type mismatch: ' + name)
    return {'status': 'offline-prototype-object-only', 'architecture': 'AArch64',
            'compiler_kcfi_hashes': hashes, 'active_export_imports': sorted(active_imports),
            'private_helpers_have_direct_imports': False,
            'stock_export_crcs_verified': False, 'stock_structure_abi_verified': False,
            'module_signature_accepted': False, 'phone_runtime_tested': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--object', type=Path, required=True)
    parser.add_argument('--preparation', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    helpers = json.loads(args.preparation.read_text())['helpers']
    report = inspect(args.object, helpers)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Checked AArch64 prototype: {len(helpers)} KCFI constants; no direct private-helper relocations')


if __name__ == '__main__':
    main()
