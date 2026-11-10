#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Update a validated OP13 candidate, using DLKM and read-only stock ODM."""
import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
from build_multipart import command, digest, metadata, patch_image, sign_image, write_package
from prepare_dlkm import load_report, write_load_plan
from selinux_config import write_config
from ipc.package_module import attach as attach_ipc, patch as patch_ipc
from param_config import write_config as write_param_config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--dlkm-audit', type=Path, required=True)
    parser.add_argument('--ipc-module', type=Path, required=True)
    parser.add_argument('--ipc-vm-report', type=Path, required=True)
    parser.add_argument('--replace-base-package', action='store_true',
                        help='remove the verified generated base ZIP after replacement image checks to save space')
    parser.add_argument('--startup-dir', type=Path, help='optional regenerated native startup tree')
    args = parser.parse_args()
    root, base, output = args.workspace.resolve(), args.base.resolve(), args.output.resolve()
    if not output.is_relative_to(root / 'out') or (output.exists() and any(output.iterdir())):
        parser.error('use a new empty workspace/out directory')
    report = json.loads((base / 'manifest.json').read_text())
    base_validation = json.loads((base / 'packed-validation.json').read_text())
    if not base_validation['passed']:
        raise RuntimeError('expected the validated experimental base candidate')
    old_parts = {'system', 'vendor', 'odm', 'sys_prod', 'chip_prod', 'cust', 'patch_hw'}
    current_parts = (old_parts - {'odm'}) | {'vendor_dlkm', 'system_dlkm'}
    if set(report['partitions']) not in (old_parts, current_parts):
        raise RuntimeError('expected a validated seven/eight-part OP13 baseline')
    for name, row in report['partitions'].items():
        print('Verifying base', name, flush=True)
        if digest(base / row['file']) != row['sha256']:
            raise RuntimeError('base image changed: ' + name)
    components = json.loads((root / 'out/component-validation/validation.json').read_text())
    if not components['passed']:
        raise RuntimeError('component validation required')
    for source, expected in components['artifact_sha256'].items():
        if digest(root / source) != expected:
            raise RuntimeError('component changed after validation: ' + source)
    dlkm = load_report(root, args.dlkm_audit)
    attach_ipc(root, dlkm, args.ipc_module, args.ipc_vm_report)
    if 'vendor_base_sha256' in dlkm:
        base_vendor = report.get('input_images', {}).get('op13_stock/vendor.img', {}).get('sha256')
        if base_vendor != dlkm['vendor_base_sha256']:
            raise RuntimeError('stock vendor firmware changed; use build_multipart.py to rebuild vendor too')
    output.mkdir(parents=True)
    stage = output / 'system-patch'
    shutil.copytree(base / 'system-patch', stage, symlinks=True)
    authorization = stage / 'hmos/allow-fresh-userdata'
    authorization.unlink(missing_ok=True)
    startup = None
    if args.startup_dir:
        startup = json.loads((args.startup_dir / 'manifest.json').read_text())
        if not startup['full_framework_preserved']:
            raise RuntimeError('regenerated startup lost required framework services')
        shutil.rmtree(stage / 'hmos/startup')
        shutil.copytree(args.startup_dir, stage / 'hmos/startup', symlinks=True)
        shutil.copyfile(args.startup_dir / 'mask-init-dirs', stage / 'hmos/mask-init-dirs')
    shutil.copyfile(root / 'out/op13-bootstrap/init', stage / 'system/bin/init')
    write_load_plan(root, stage, dlkm)
    (stage / 'system/lib').mkdir(exist_ok=True)
    link = stage / 'system/lib/modules'
    if link.is_symlink():
        link.unlink()
    link.symlink_to('/system_dlkm/lib/modules')
    write_config(root, stage)
    write_param_config(root, stage)
    owners, labels = metadata(root, 'system', 'system')
    image = patch_image(root, output, 'system', base / 'system.img', stage, owners, labels, [
        ('/hmos/startup(/.*)?', 'system_etc_file'), ('/hmos/startup/vendor-init(/.*)?', 'vendor_etc_file'),
        ('/hmos/startup/hdf_default\\.hcb', 'chip_prod_file'), ('/hmos/display(/.*)?', 'vendor_lib_file'),
        ('/hmos/ui(/.*)?', 'sys_prod_file'), ('/hmos/policy(/.*)?', 'vendor_etc_file'),
        ('/hmos/init-modules(/.*)?', 'system_lib_file'), ('/system/bin/(init|hmos-native-init)', 'init_exec'),
        ('/system/bin/hmos-oobedone', 'system_bin_file'), ('/system/lib/modules', 'system_lib_file'),
        ('/system/etc/param/hdf_devmgr\\.para\\.dac', 'system_etc_file'),
        ('/system/lib64/libop13_local_storage\\.so', 'system_lib_file')])
    key = output / 'development-key.pem'
    fd = os.open(key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as file:
        file.write((base / 'development-key.pem').read_bytes())
    compatibility = report['android_compatibility_metadata']
    images = {'system': image}
    records = {'system': sign_image(image, 'system', key, compatibility)}
    for name in ('vendor', 'sys_prod', 'chip_prod', 'cust', 'patch_hw'):
        target = output / (name + '.img')
        os.link(base / (name + '.img'), target)
        images[name] = target
        records[name] = report['partitions'][name]
    for name, row in dlkm['images'].items():
        target = output / (name + '.img')
        if name == 'vendor_dlkm':
            existing = report.get('native_ipc', {})
            if (existing.get('sha256') == dlkm['ipc_module']['sha256'] and
                    report.get('dlkm_inputs', {}).get(name, {}).get('sha256') == row['sha256'] and
                    existing.get('packed_validation', {}).get('passed')):
                os.link(base / (name + '.img'), target)
                dlkm['ipc_module']['packed_validation'] = existing['packed_validation']
                images[name] = target
                records[name] = report['partitions'][name]
                continue
            dlkm['ipc_module']['packed_validation'] = patch_ipc(Path(row['source']), target, dlkm['ipc_module'])
            images[name] = target
            records[name] = sign_image(target, name, key, compatibility)
            continue
        if name in report['partitions'] and report.get('dlkm_inputs', {}).get(name, {}).get('sha256') == row['sha256']:
            os.link(base / (name + '.img'), target)
            images[name] = target
            records[name] = report['partitions'][name]
            continue
        shutil.copyfile(row['source'], target)
        if subprocess.run(['avbtool', 'info_image', '--image', str(target)], capture_output=True).returncode == 0:
            command('avbtool', 'erase_footer', '--image', target)
        images[name] = target
        records[name] = sign_image(target, name, key, compatibility)
    shutil.copyfile(base / 'vendor-base-content.json', output / 'vendor-base-content.json')
    if args.replace_base_package:
        previous = base / report['zip']['file']
        if (base.parent != root / 'out' or not base.name.startswith('hmos5-op13-dsu') or
                previous.parent != base or previous.is_symlink()):
            raise RuntimeError('refusing removal of an unverified base package')
        if previous.exists():
            if digest(previous) != report['zip']['sha256']:
                raise RuntimeError('refusing removal of a changed base package')
            previous.unlink()
            print('Removed superseded generated ZIP after replacement filesystem/AVB checks', flush=True)
        else:
            print('Base ZIP already absent; rebuilding from verified raw base images', flush=True)
    print('Writing updated eight-part DSU ZIP', flush=True)
    package = write_package(output, images)
    updated = copy.deepcopy(report)
    updated.pop('odm_filesystem_base', None)
    updated.pop('packed_validation_file', None)
    updated.pop('last_phone_test', None)
    updated.pop('userdata_policy_changed', None)
    updated.pop('fresh_dsu_data_initialization_authorized', None)
    updated.update({'partitions': records, 'zip': package, 'component_validation': components, 'phone_boot_tested': False,
        'userdata_initialization': 'Android DSU installer-wiped header only, before mount; no formatting on mount failure',
        'userdata_allocation_resized': False,
        'handoff_revision': 9, 'selinux_mode_changed': False,
        'native_ipc': dlkm['ipc_module'], 'kernel_image_modified': False,
        'additional_kernel_module': True,
        'bootstrap_metadata_log': '/metadata/hmos5dodge/bootstrap.log',
        'native_mount_root': 'pivot_root in a private mount namespace; old root detached',
        'native_selinux_config': 'Preserve boot-selected permissive mode through the native config; explicit native enforcing=1 takes precedence',
        'native_selinux_api': 'selinuxfs mounted and API checked before donor init; enforcement unchanged by bootstrap',
        'native_startup_metadata_log': '/metadata/hmos5dodge/native-startup.log',
        'native_startup_recorder': {'maximum_bytes': 4194304, 'duration_seconds': 180,
                                    'scope': 'Kernel records, PID-1/UI process state and bounded read-only HiLog queries; latest handoff only',
                                    'hilog_query_limit_bytes': 262144, 'hilog_query_timeout_ms': 3000},
        'base_candidate': {'system_sha256': report['partitions']['system']['sha256'], 'packed_validation': base_validation,
                           'last_phone_test': report.get('last_phone_test')},
        'odm_source': 'Existing stock ODM EROFS mounted by Android first-stage; reused read-only',
        'dlkm_inputs': dlkm['images'], 'module_load_plan': dlkm['load_plan'],
        'dlkm_audit_scope': dlkm['scope'], 'captured_vendor_modules_byte_identical': dlkm['captured_vendor_modules_byte_identical']})
    updated['module_audit'] = {key: dlkm[key] for key in ('scope', 'vendor_boot', 'payload_properties',
        'crc_checks', 'load_plan_sources', 'vendor_base_sha256', 'display_suppliers') if key in dlkm}
    if startup:
        updated['startup_configuration'] = startup
        updated['known_unverified'] = [item for item in updated['known_unverified'] if 'missing donor libinput_driver' not in item]
        updated['known_unverified'].append('Actual OP13 touch events and input permissions/hotplug remain untested')
    updated['known_unverified'][0] = 'Android first-stage mounting all eight DSU partitions and supplying stock ODM'
    for row in dlkm['images'].values():
        if digest(Path(row['source'])) != row['sha256']:
            raise RuntimeError('DLKM source changed during packing')
    (output / 'manifest.json').write_text(json.dumps(updated, indent=2) + '\n')
    (output / 'SHA256SUMS').write_text(''.join(row['sha256'] + '  ' + row['file'] + '\n'
        for row in list(records.values()) + [package]))
    print(json.dumps({'package': str(output / package['file']), 'images': list(images), 'phone_boot_tested': False}, indent=2))


if __name__ == '__main__':
    main()
