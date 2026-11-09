#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preserve the donor config except its single explicit SELINUX mode value."""
import re
from pathlib import Path


def permissive_config(original):
    settings = [line.strip() for line in original.splitlines()
                if line.strip() and not line.lstrip().startswith(b'#')]
    modes = [line for line in settings if line.startswith(b'SELINUX=')]
    if modes != [b'SELINUX=enforcing']:
        raise ValueError('expected exactly one explicit donor enforcing mode')
    result, count = re.subn(rb'(?m)^SELINUX=enforcing(?=\r?$)', b'SELINUX=permissive', original)
    if count != 1:
        raise ValueError('ambiguous donor SELinux config')
    return result


def write_config(workspace, stage):
    original = (Path(workspace) / 'work/inspect/system/system/etc/selinux/config').read_bytes()
    path = Path(stage) / 'hmos/policy/config.permissive'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(permissive_config(original))
