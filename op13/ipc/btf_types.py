#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read standalone Linux BTF types for offline ABI comparisons."""
from pathlib import Path
import struct


class BTF:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        magic, version, flags, header, type_off, type_len, str_off, str_len = struct.unpack_from(
            '<HBBIIIII', self.data)
        if magic != 0xeb9f or version != 1 or flags or header < 24:
            raise ValueError('unsupported BTF header')
        start, end = header + type_off, header + type_off + type_len
        if end > len(self.data) or header + str_off + str_len != len(self.data):
            raise ValueError('invalid BTF extent')
        self.strings = self.data[header + str_off:header + str_off + str_len]
        self.types = [None]
        self.named = {}
        pos = start
        while pos < end:
            name, info, value = struct.unpack_from('<III', self.data, pos)
            pos += 12
            kind, count, flag = (info >> 24) & 31, info & 0xffff, info >> 31
            extra = {1: 4, 2: 0, 3: 12, 4: count * 12, 5: count * 12,
                     6: count * 8, 7: 0, 8: 0, 9: 0, 10: 0, 11: 0,
                     12: 0, 13: count * 8, 14: 4, 15: count * 12,
                     16: 0, 17: 4, 18: 0, 19: count * 12}.get(kind)
            if extra is None or pos + extra > end:
                raise ValueError('unsupported or truncated BTF type')
            row = {'name': self.string(name), 'kind': kind, 'value': value,
                   'count': count, 'flag': flag, 'extra': self.data[pos:pos + extra]}
            self.types.append(row)
            if row['name']:
                self.named.setdefault((kind, row['name']), []).append(len(self.types) - 1)
            pos += extra
        if pos != end:
            raise ValueError('BTF type section is misaligned')

    def string(self, offset):
        return self.strings[offset:self.strings.index(b'\0', offset)].decode()

    def shape(self, index, seen=()):
        if index == 0:
            return ['void']
        row = self.types[index]
        kind, name, value = row['kind'], row['name'], row['value']
        if kind in (8, 9, 10, 11, 18):
            # Typedef names and qualifiers do not change physical layout.
            return self.shape(value, seen)
        if kind in (4, 5, 7):
            if name:
                return [kind if kind != 7 else (5 if row['flag'] else 4), name]
            if index in seen:
                return ['recursive-anonymous']
            return [kind, value, self.members(index, seen + (index,))]
        if kind == 2:
            return ['pointer', self.shape(value, seen)]
        if kind == 3:
            element, _, count = struct.unpack('<III', row['extra'])
            return ['array', count, self.shape(element, seen)]
        if kind in (1, 6, 16, 19):
            return [kind, name, value, row['flag'], row['extra'].hex() if kind == 1 else '']
        if kind == 13:
            args = [self.shape(struct.unpack_from('<II', row['extra'], p)[1], seen)
                    for p in range(0, len(row['extra']), 8)]
            return ['function', self.shape(value, seen), args]
        return [kind, name]

    def members(self, index, seen=()):
        row = self.types[index]
        members = []
        for pos in range(0, len(row['extra']), 12):
            name, typ, bits = struct.unpack_from('<III', row['extra'], pos)
            width, offset = (bits >> 24, bits & 0xffffff) if row['flag'] else (0, bits)
            members.append([self.string(name), offset, width, self.shape(typ, seen)])
        return members

    def layout(self, index):
        row = self.types[index]
        return {'kind': row['kind'], 'bytes': row['value'], 'members': self.members(index)}
