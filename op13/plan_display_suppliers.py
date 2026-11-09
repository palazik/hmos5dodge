#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Extend a verified stock module plan with the working phone's DRM suppliers."""
import argparse
from collections import Counter
import copy
import json
from pathlib import Path
from image_utils import digest
from module_plan import crc_checks, dependency_plan, display_supplier_roots
from prepare_dlkm import load_report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, output = args.workspace.resolve(), args.output.resolve()
    if not output.is_relative_to(root / 'out') or output.exists():
        parser.error('use a new report path in workspace/out')
    original = load_report(root, args.audit)
    if 'vendor_boot' not in original:
        parser.error('use a verified single-payload module audit with vendor_boot')
    roots, observed = display_supplier_roots(args.capture)
    modules = {row['name']: row for row in original['modules']}
    plan, missing_soft = dependency_plan(modules, roots)
    old_names = {row['name'] for row in original['load_plan']}
    new_names = {row['name'] for row in plan}
    if not old_names <= new_names:
        raise ValueError('supplier plan lost original graphics dependencies')
    for row in plan:
        if digest(root / row['source']) != row['sha256']:
            raise ValueError('stock module source changed: ' + row['name'])
    report = copy.deepcopy(original)
    report.update({'load_plan': plan, 'load_plan_sources': dict(Counter(row['partition'] for row in plan)),
        'unresolved_soft_pre_hints': missing_soft, 'crc_checks': crc_checks(root, plan),
        'display_suppliers': {**observed, 'capture_sha256': digest(args.capture),
            'additional_modules': sorted(new_names - old_names), 'requested_roots': roots},
        'scope': 'Verified single-payload modules plus observed DRM device-link providers; symbol CRC checks; new DSU binding and rendering untested'})
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'modules': len(plan), 'display_suppliers': report['display_suppliers'],
                      'load_plan_sources': report['load_plan_sources'], 'crc_checks': report['crc_checks']}, indent=2))


if __name__ == '__main__':
    main()
