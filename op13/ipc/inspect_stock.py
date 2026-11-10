#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Recover stock exports without changing kernel bytes or fabricating CRCs.

Validates the full boot-v4 kernel against live BTF and every captured core
kallsyms name. Addresses may be zero in the read-only phone capture.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(boot, kallsyms, btf, expected):
    if boot[:8] != b'ANDROID!' or struct.unpack_from('<I', boot, 40)[0] != 4:
        raise ValueError('expected Android boot header v4')
    size = struct.unpack_from('<I', boot, 8)[0]
    data = boot[4096:4096 + size]
    if len(data) != size or data[56:60] != b'ARM\x64' or data.count(btf) != 1:
        raise ValueError('full kernel extent does not match live BTF')
    names = [line.split()[1:] for line in kallsyms.splitlines() if len(line.split()) == 3]
    count = len(names)
    needle = struct.pack('<I', count)
    if count < 1000 or data.count(needle) != 1:
        raise ValueError('ambiguous stock kallsyms count')
    align = lambda pos: (pos + 7) & ~7
    start = align(data.index(needle) + 4)
    pos = start
    encoded, markers = [], []
    for i in range(count):
        if i % 256 == 0:
            markers.append(pos - start)
        length = data[pos]
        pos += 1
        if length & 128:
            length = (length & 127) | (data[pos] << 7)
            pos += 1
        if not length:
            raise ValueError('empty compressed symbol')
        encoded.append(data[pos:pos + length])
        pos += length
    markpos = align(pos)
    if list(struct.unpack_from('<' + str(len(markers)) + 'I', data, markpos)) != markers:
        raise ValueError('stock symbol markers differ')
    pos = tablepos = align(markpos + 4 * len(markers))
    tokens, indices = [], []
    for _ in range(256):
        indices.append(pos - tablepos)
        end = data.index(0, pos)
        tokens.append(data[pos:end])
        pos = end + 1
    indexpos = align(pos)
    if list(struct.unpack_from('<256H', data, indexpos)) != indices:
        raise ValueError('stock token index differs')
    for row, encoded_name in zip(names, encoded):
        if b''.join(tokens[x] for x in encoded_name).decode() != ''.join(row):
            raise ValueError('compressed kernel symbol differs from capture')
    offsetpos = align(indexpos + 512)
    base = struct.unpack_from('<Q', data, align(offsetpos + count * 4))[0]
    offsets = struct.unpack_from('<' + str(count) + 'I', data, offsetpos)
    symbols = {}
    for row, offset in zip(names, offsets):
        symbols.setdefault(row[1], []).append(base + offset)

    def address(name):
        rows = symbols[name]
        if len(rows) != 1:
            raise ValueError('ambiguous stock symbol: ' + name)
        return rows[0]

    text = address('_text')

    def cstring(pointer):
        return data[pointer - text:data.index(0, pointer - text)].decode()

    exports = {}
    for suffix in ('', '_gpl'):
        begin, end = address('__start___ksymtab' + suffix), address('__stop___ksymtab' + suffix)
        crc = address('__start___kcrctab' + suffix)
        if (end - begin) % 12:
            raise ValueError('invalid PREL32 export table')
        for i, pointer in enumerate(range(begin, end, 12)):
            value, name, namespace = struct.unpack_from('<iii', data, pointer - text)
            exports[cstring(pointer + 4 + name)] = {
                'crc': f'{struct.unpack_from("<I", data, crc - text + 4 * i)[0]:08x}',
                'namespace': cstring(pointer + 8 + namespace), 'gpl_only': bool(suffix)}
    begin = address('gki_unprotected_symbols')
    end = min(x for values in symbols.values() for x in values if x > begin)
    array = data[begin - text:end - text]
    stride = next(i for i in range(array.index(0), len(array)) if array[i])
    length = len(array) // stride * stride
    if len(array) - length >= 8 or any(array[length:]):
        raise ValueError('invalid vendor symbol allowlist padding')
    allowed = [array[i:i + stride].split(b'\0', 1)[0].decode() for i in range(0, length, stride)]
    if allowed != sorted(allowed):
        raise ValueError('vendor symbol allowlist not sorted')
    for name, value in expected['compiler_kcfi_hashes'].items():
        actual = struct.unpack_from('<I', data, address(name) - text - 4)[0]
        if actual != int(value, 16):
            raise ValueError('stock helper KCFI mismatch: ' + name)
    imports = set(expected['active_export_imports']) - {'__this_module'}
    if imports - exports.keys() or imports - set(allowed):
        raise ValueError('module requires absent or protected exports')
    report = {'kernel_sha256': hashlib.sha256(data).hexdigest(),
              'live_btf_matches_stock_image': True, 'live_core_symbol_names_verified': count,
              'stock_export_count': len(exports), 'vendor_accessible_symbol_count': len(allowed),
              'helper_kcfi_hashes_matched': len(expected['compiler_kcfi_hashes']),
              'direct_imports_verified': len(imports), 'symbol_crcs_fabricated': False}
    symvers = ''.join('0x' + row['crc'] + '\t' + name + '\tvmlinux\t' +
        ('EXPORT_SYMBOL_GPL' if row['gpl_only'] else 'EXPORT_SYMBOL') + '\t' + row['namespace'] + '\n'
        for name, row in sorted(exports.items()))
    return report, symvers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stock-boot', 'kallsyms', 'btf', 'prototype-audit', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() and any(args.output.iterdir()):
        parser.error('use a new empty output directory')
    report, symvers = inspect(args.stock_boot.read_bytes(), args.kallsyms.read_text(),
                             args.btf.read_bytes(), json.loads(args.prototype_audit.read_text()))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'Module.symvers').write_text(symvers)
    (args.output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
