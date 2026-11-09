#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inspect current OP13 DLKM images and select unchanged module providers."""
import argparse
from collections import Counter
import json
from pathlib import Path
import shutil
import subprocess
from image_utils import check_filesystem, digest


def module_info(path):
    result = {}
    for line in subprocess.check_output(['modinfo', str(path)], text=True).splitlines():
        if ':' in line:
            key, value = line.split(':', 1)
            result.setdefault(key, value.strip())
    return result


def load_report(workspace, path):
    report = json.loads(path.read_text())
    if not report.get('passed'):
        raise RuntimeError('DLKM source audit did not pass')
    for row in report['images'].values():
        if digest(Path(row['source'])) != row['sha256']:
            raise RuntimeError('DLKM source image changed: ' + row['source'])
    for row in report['load_plan']:
        if digest(workspace / row['source']) != row['sha256']:
            raise RuntimeError('planned module changed: ' + row['name'])
    if 'vendor_boot' in report:
        row = report['vendor_boot']
        if digest(Path(row['source'])) != row['sha256']:
            raise RuntimeError('vendor boot source changed')
    return report


def write_load_plan(workspace, stage, report):
    directory = stage / 'hmos/modules'
    directory.mkdir(parents=True, exist_ok=True)
    for old in directory.glob('*.ko'):
        old.unlink()
    lines = []
    for row in report['load_plan']:
        if row['runtime_path'].startswith('/hmos/modules/'):
            shutil.copyfile(workspace / row['source'], directory / (row['name'] + '.ko'))
        lines.append(row['name'] + ' ' + row['runtime_path'])
    (directory / 'load-order').write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--vendor-boot', type=Path, help='rebuild plan from this firmware instead of the old capture')
    parser.add_argument('--payload-inventory', type=Path, help='verified partition hashes from this payload')
    args = parser.parse_args()
    root, output = args.workspace.resolve(), args.output.resolve()
    if bool(args.vendor_boot) != bool(args.payload_inventory):
        parser.error('--vendor-boot and --payload-inventory must be supplied together')
    if not output.is_relative_to(root / 'out') or (output.exists() and any(output.iterdir())):
        parser.error('use a new empty workspace/out directory')
    output.mkdir(parents=True)
    inventory = None
    if args.payload_inventory:
        inventory = json.loads(args.payload_inventory.read_text())
        if not inventory['metadata_hash_verified_against_supplied_properties']:
            raise RuntimeError('payload metadata not verified')
        for name, path in [('vendor_boot', args.vendor_boot), ('vendor', root / 'op13_stock/vendor.img'),
                           ('vendor_dlkm', args.source_dir / 'vendor_dlkm.img'),
                           ('system_dlkm', args.source_dir / 'system_dlkm.img')]:
            if digest(path) != inventory['partitions'][name]['sha256']:
                raise RuntimeError('partition does not match payload: ' + name)
    captured = {}
    for path in (root / 'work/stock-modules').glob('*.ko'):
        captured[module_info(path)['name'].replace('-', '_')] = digest(path)
    images, modules = {}, {}
    for name in ('vendor_dlkm', 'system_dlkm'):
        source = (args.source_dir / (name + '.img')).resolve()
        info = check_filesystem(source)
        tree = output / (name + '-tree')
        if info['type'] == 'erofs':
            subprocess.run(['fsck.erofs', '--extract=' + str(tree), '--no-preserve-owner',
                            '--preserve-perms', str(source)], check=True)
        else:
            tree.mkdir()
            # Quoted debugfs path is needed for a workspace containing spaces.
            target = str(tree).replace('\\', '\\\\').replace('"', '\\"')
            subprocess.run(['debugfs', '-R', 'rdump /lib "' + target + '"', str(source)], check=True)
        count = 0
        for path in tree.rglob('*.ko'):
            data = module_info(path)
            module = data['name'].replace('-', '_')
            relative = path.relative_to(tree)
            # debugfs rdump /lib includes the source directory itself.
            if relative.parts[0] != 'lib':
                relative = Path('lib') / relative
            sha = digest(path)
            if module in modules and modules[module]['sha256'] != sha:
                raise RuntimeError('different module providers for ' + module)
            modules[module] = {'name': module, 'source': str(path.relative_to(root)),
                'sha256': sha, 'partition': name, 'runtime_path': '/' + name + '/' + str(relative),
                'vermagic': data.get('vermagic'), 'matches_capture': captured.get(module) == sha,
                'depends': data.get('depends', ''), 'softdep': data.get('softdep', '')}
            count += 1
        if not count:
            raise RuntimeError('no kernel modules extracted from ' + name)
        images[name] = {'source': str(source), 'sha256': digest(source), 'filesystem': info,
                        'module_count': count}
    vendor_rows = [row for row in modules.values() if row['partition'] == 'vendor_dlkm']
    if args.vendor_boot:
        from module_plan import ramdisk_modules, dependency_plan, crc_checks
        unpacked = output / 'vendor-boot'
        subprocess.run(['unpack_bootimg', '--boot_img', str(args.vendor_boot), '--out', str(unpacked)], check=True)
        early = output / 'early-modules'
        fragments = sorted(unpacked.glob('vendor_ramdisk[0-9]*'))
        if not fragments:
            fragments = [unpacked / 'vendor_ramdisk']
        for index, fragment in enumerate(fragments):
            raw = unpacked / ('fragment-' + str(index) + '.cpio')
            if fragment.read_bytes()[:6] in (b'070701', b'070702'):
                shutil.copyfile(fragment, raw)
            else:
                subprocess.run(['magiskboot', 'decompress', str(fragment), str(raw)], check=True)
            target = output / ('fragment-' + str(index) + '-modules')
            ramdisk_modules(raw, target)
            early.mkdir(exist_ok=True)
            for path in target.glob('*.ko'):
                destination = early / path.name
                if destination.exists() and digest(destination) != digest(path):
                    raise RuntimeError('conflicting early module: ' + path.name)
                path.replace(destination)
            target.rmdir()
        for path in early.glob('*.ko'):
            data = module_info(path)
            name, sha = data['name'].replace('-', '_'), digest(path)
            if name in modules:
                if modules[name]['sha256'] != sha:
                    raise RuntimeError('different ramdisk/DLKM providers: ' + name)
                continue
            modules[name] = {'name': name, 'source': str(path.relative_to(root)), 'sha256': sha,
                'partition': 'vendor_boot-copy', 'runtime_path': '/hmos/modules/' + name + '.ko',
                'vermagic': data.get('vermagic'), 'depends': data.get('depends', ''),
                'softdep': data.get('softdep', '')}
        plan, missing_soft = dependency_plan(modules)
        crcs = crc_checks(root, plan)
        report = {'passed': True, 'images': images, 'modules': list(modules.values()), 'load_plan': plan,
            'vendor_boot': {'source': str(args.vendor_boot.resolve()), 'sha256': digest(args.vendor_boot)},
            'payload_inventory': str(args.payload_inventory.resolve()),
            'payload_properties': inventory['payload_properties'],
            'vendor_base_sha256': inventory['partitions']['vendor']['sha256'],
            'captured_vendor_modules_byte_identical': sum(row['matches_capture'] for row in vendor_rows),
            'capture_scope': 'Historical October capture comparison only; source is the user-restored payload',
            'load_plan_sources': dict(Counter(row['partition'] for row in plan)),
            'unresolved_soft_pre_hints': missing_soft, 'crc_checks': crcs,
            'early_module_count': len(list(early.glob('*.ko'))),
            'scope': 'All providers from one verified payload; offline module CRC checks; live kernel signatures, exports and loading untested'}
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        # Keep module providers needed by the plan, not another ramdisk backup.
        shutil.rmtree(unpacked)
        print(json.dumps({key: report[key] for key in ('passed', 'early_module_count', 'load_plan_sources', 'crc_checks')}, indent=2))
        return
    if {row['name'] for row in vendor_rows} != set(captured) or not all(row['matches_capture'] for row in vendor_rows):
        raise RuntimeError('vendor DLKM differs from the supplied current-device capture; rebuild the module plan first')
    graphics = json.loads((root / 'out/stock-dsu-audit/graphics-modules.json').read_text())
    if graphics['conflicting_module_bytes'] or graphics['unresolved_hard_dependencies']:
        raise RuntimeError('original graphics dependency plan is incomplete')
    plan = []
    for original in graphics['closure']:
        row = dict(modules.get(original['name'], original))
        if row['sha256'] != original['sha256']:
            raise RuntimeError('selected module ABI/source changed: ' + original['name'])
        if 'source' not in row:
            row['source'] = original['path']
            row['runtime_path'] = '/hmos/modules/' + original['name'] + '.ko'
            row['partition'] = 'vendor_boot-copy'
        plan.append(row)
    report = {'passed': True, 'images': images, 'modules': list(modules.values()), 'load_plan': plan,
        'captured_vendor_modules_byte_identical': len(vendor_rows),
        'load_plan_sources': dict(Counter(row['partition'] for row in plan)),
        'scope': 'Offline filesystem checks, source hashes and unchanged module plan/capture; current kernel loading, signatures and missing kernel exports remain untested'}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('passed', 'captured_vendor_modules_byte_identical', 'load_plan_sources')}, indent=2))


if __name__ == '__main__':
    main()
