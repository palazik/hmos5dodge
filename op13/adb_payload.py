#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Stage authenticated USB ADB for live diagnostics in the guest.

- /system/bin/hmos-adbd: native OHOS adbd built by op13/adb/build.py from pinned
  AOSP/Linux sources; key-based authentication only, no TCP listener.
- /system/bin/hmos-usb-manager: op13/adb/usb_manager.c, started once by the
  bootstrap after the root pivot.  It creates its own configfs gadget with a
  FunctionFS ADB function (never edits stock g1) and gives up quietly.
- /system/etc/hmos-adb/adb_keys: the owner's adb PUBLIC key, copied at build
  time from ~/.android/adbkey.pub (override with HMOS_ADB_PUBKEY).  It is
  never committed; private keys are rejected.
"""
import base64
import hashlib
import os
from pathlib import Path
import shutil

DAEMON = ('out/op13-adb/hmos-adbd', 'system/bin/hmos-adbd')
MANAGER = ('out/op13-adb/hmos-usb-manager', 'system/bin/hmos-usb-manager')
KEYS = 'system/etc/hmos-adb/adb_keys'
EXECUTABLES = (DAEMON[1], MANAGER[1])
LABELS = [('/system/bin/hmos-(adbd|usb-manager)', 'system_bin_file'),
          ('/system/etc/hmos-adb(/.*)?', 'system_etc_file')]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def public_key():
    path = Path(os.environ.get('HMOS_ADB_PUBKEY', str(Path.home() / '.android/adbkey.pub')))
    text = path.read_text().strip()
    if 'PRIVATE KEY' in text or '\n' in text:
        raise RuntimeError('expected a single-line adb public key, not a private key: ' + str(path))
    blob = text.split()[0]
    if len(base64.b64decode(blob)) < 256:
        raise RuntimeError('adb public key looks truncated: ' + str(path))
    return (text + '\n').encode()


def install(workspace, stage):
    record = {}
    for source, target in (DAEMON, MANAGER):
        src = workspace / source
        if not src.is_file():
            raise RuntimeError('missing ADB component (run validate_components.py / adb build): ' + source)
        dst = stage / target
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
        dst.chmod(0o755)
        record[target] = digest(dst)
    keys = stage / KEYS
    keys.parent.mkdir(parents=True, exist_ok=True)
    keys.write_bytes(public_key())
    record[KEYS] = digest(keys)
    return {'files': record, 'transport': 'USB FunctionFS only, key authentication', 'hardware_tested': False}


def verify(after, record):
    for path, expected in record['files'].items():
        assert digest(after / path) == expected, path
