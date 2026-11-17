#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Stage the donor init plugins that the native framework needs.

The guest binds /hmos/init-modules over /system/lib64/init.  Revisions up to 18
staged only libselinuxadp and libinit_context, so init never ran
autorun/libhmos_cust_init.  That plugin sets const.cust.config_dir_layer; without
it libconfigpolicy_util falls back to /system:/chipset:/sys_prod:/chip_prod and
the phone variant layers (/system/variant/phone/{base,china}) disappear,
including the only system-app capability list (SceneBoard keepAlive and
privilege extension), pre-granted permissions and hw_defaults.para
(persist.sys.abilityms.*, const.window.*).  Revision 17 logged
"SystemReadParam failed! name is:const.cust.config_dir_layer" and "Failed to
install module udidmodule"; SceneBoard was never launched.

Only plugins audited as safe in this isolated guest are restored.  The oeminfo
access in libhmos_cust_init goes through /dev/oeminfo/* (Huawei kernel nodes
absent on OP13 and outside the device filter), so it fails and falls back to
the factory layer list, as on an unprovisioned donor.  Excluded: boot-fail
detector (recovery/reboot actions), eng partition mounts, zram block setup,
USB gadget configuration, hardware watchdog, TEE chmod, kdump, reboot modules,
and Huawei-kernel-only helpers.
"""
import hashlib
from pathlib import Path

SOURCE = 'work/inspect/system/system/lib64/init'
DONOR_FULL = 'work/donor-system-full/system/lib64/init'
TARGET = 'hmos/init-modules'
PLUGINS = ('libselinuxadp.z.so', 'libinit_context.z.so', 'libudidmodule.z.so', 'libeventmodule.z.so',
           'autorun/libhmos_cust_init.z.so', 'autorun/libinit_security.z.so')
EXCLUDED = ('libinit_bootDetector.z.so', 'libinit_eng.z.so', 'libinittrace.z.so', 'libbootchart.z.so',
            'autorun/libinithp.z.so', 'autorun/libinit_usbconfig.z.so', 'autorun/libkickdog.z.so',
            'autorun/libtee_chmod.z.so', 'autorun/libkdump_resize.z.so', 'autorun/libinit_dec.z.so',
            'autorun/libprocess_exit.z.so', 'autorun/libinit_manufacture.z.so', 'autorun/libinit_quickfix.z.so',
            'autorun/libfile_migrate_init.z.so', 'autorun/libprop_bridge.z.so', 'autorun/libprop_migrate.z.so',
            'reboot/librebootmodule.z.so', 'reboot/libhmos_update_reboot.z.so')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def donor_file(workspace, name):
    for base in (SOURCE, DONOR_FULL):
        path = workspace / base / name
        if path.is_file():
            return path
    raise RuntimeError('donor init plugin missing: ' + name + ' (extract hmos5/system.img to work/donor-system-full)')


def install(workspace, stage):
    target = stage / TARGET
    for directory in (target, target / 'autorun', target / 'reboot'):
        directory.mkdir(parents=True, exist_ok=True)
    record = {}
    for name in PLUGINS:
        source = donor_file(workspace, name)
        (target / name).write_bytes(source.read_bytes())
        record[name] = digest(source)
    return {'plugins': record, 'excluded': list(EXCLUDED), 'hardware_tested': False}


def verify(after, record):
    target = after / TARGET
    present = {str(path.relative_to(target)) for path in target.rglob('*') if path.is_file()}
    assert present == set(record['plugins']), present ^ set(record['plugins'])
    for name, expected in record['plugins'].items():
        assert digest(target / name) == expected, name
    assert not any(name in present for name in EXCLUDED)
