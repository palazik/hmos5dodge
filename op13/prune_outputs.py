#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Retain one validated DSU image set; remove superseded generated artifacts."""
import argparse
import json
import os
from pathlib import Path
import shutil
from image_utils import digest


def remove_tree(path):
    if path.is_symlink():
        raise ValueError('refusing to traverse symlink: ' + str(path))
    for current, directories, files in os.walk(path, followlinks=False):
        os.chmod(current, Path(current).stat().st_mode | 0o700)
    shutil.rmtree(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--current', type=Path, required=True)
    args = parser.parse_args()
    root = args.workspace.resolve() / 'out'
    current = args.current.resolve()
    if current.parent != root or not current.name.startswith('hmos5-op13-dsu'):
        parser.error('current must be a direct workspace/out/hmos5-op13-dsu* directory')
    report = json.loads((current / 'manifest.json').read_text())
    validation = json.loads((current / 'packed-validation.json').read_text())
    if report['status'] != 'experimental-op13-only-multipart-dsu' or not validation['passed']:
        raise RuntimeError('current candidate must pass packed validation first')
    for row in [*report['partitions'].values(), report['zip']]:
        path = current / row['file']
        if path.parent != current or path.is_symlink() or digest(path) != row['sha256']:
            raise RuntimeError('current artifact changed: ' + str(path))
    before = shutil.disk_usage(root).free
    removed = []
    for directory in sorted(root.glob('hmos5-op13-dsu*')):
        if directory.is_symlink() or directory.resolve().parent != root or not directory.is_dir():
            continue
        if directory != current:
            # Only generated image/archive files at the root of older candidates.
            for pattern in ('*.img', '*.zip', '*.zip.partial', 'development-key.pem'):
                for path in directory.glob(pattern):
                    if path.is_symlink() or not path.is_file():
                        raise RuntimeError('unexpected artifact type: ' + str(path))
                    path.unlink()
                    removed.append(str(path.relative_to(root)))
            (directory / 'SUPERSEDED.txt').write_text('Use ../' + current.name +
                '/. Generated image/archive copies removed; manifests retained.\n')
        # Final images have already passed readback; full vendor extraction is disposable.
        trees = ['vendor-tree', 'vendor-patch']
        if directory != current:
            trees += ['system-patch', 'staging']
        for name in trees:
            path = directory / name
            if path.exists():
                remove_tree(path)
                removed.append(str(path.relative_to(root)))
    result = {'current': str(current.relative_to(root)), 'removed_generated_artifacts': removed,
              'free_bytes_before': before, 'free_bytes_after': shutil.disk_usage(root).free,
              'original_inputs_touched': False}
    (current / 'cleanup.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
