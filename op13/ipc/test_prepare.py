# SPDX-License-Identifier: Apache-2.0
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


HERE = Path(__file__).resolve().parent


class PreparationTest(unittest.TestCase):
    def invoke(self, script, output):
        return subprocess.run([sys.executable, str(script), '--output', str(output)],
                              capture_output=True, text=True)

    def test_preparation_preserves_pinned_inputs(self):
        record = json.loads((HERE / 'UPSTREAM.json').read_text())
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'module'
            result = self.invoke(HERE / 'prepare.py', output)
            self.assertEqual(result.returncode, 0, result.stderr)
            prepared = json.loads((output / 'PREPARATION.json').read_text())
            self.assertEqual(len(prepared['helpers']), 20)
            self.assertFalse(prepared['dsu_package_modified'])
            self.assertFalse(prepared['stock_kernel_runtime_tested'])
            self.assertFalse(list(output.glob('*.ko')))
        for row in record['files']:
            self.assertEqual(hashlib.sha256((HERE / 'upstream' / row['path']).read_bytes()).hexdigest(),
                             row['sha256'])

    def test_nonempty_output_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            sentinel = output / 'existing'
            sentinel.write_text('keep')
            self.assertNotEqual(self.invoke(HERE / 'prepare.py', output).returncode, 0)
            self.assertEqual(sentinel.read_text(), 'keep')
            self.assertEqual(list(output.iterdir()), [sentinel])

    def test_changed_upstream_is_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            shutil.copytree(HERE, source, ignore=shutil.ignore_patterns('__pycache__'))
            (source / 'upstream/compat/ipc/binder.c').write_text('unexpected source')
            result = self.invoke(source / 'prepare.py', root / 'output')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('upstream reference changed', result.stderr)
            self.assertFalse((root / 'output').exists())


if __name__ == '__main__':
    unittest.main()
