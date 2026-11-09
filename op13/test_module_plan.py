# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import tempfile
import unittest
from module_plan import dependency_plan, ramdisk_modules


class ModulePlanTests(unittest.TestCase):
    def test_missing_dependency_and_cycle_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'missing hard'):
            dependency_plan({'display': {'name': 'display', 'depends': 'missing'}}, roots=['display'])
        with self.assertRaisesRegex(ValueError, 'cycle'):
            dependency_plan({'display': {'name': 'display', 'depends': 'bridge'},
                             'bridge': {'name': 'bridge', 'depends': 'display'}}, roots=['display'])

    def test_dependency_order_and_missing_optional_hint(self):
        rows = {'display': {'name': 'display', 'depends': 'bridge', 'softdep': 'pre: optional post: later'},
                'bridge': {'name': 'bridge', 'depends': ''}}
        plan, missing = dependency_plan(rows, roots=['display'])
        self.assertEqual([row['name'] for row in plan], ['bridge', 'display'])
        self.assertEqual(missing, ['optional'])

    def test_truncated_ramdisk_is_rejected_without_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'ramdisk.cpio'
            fields = [0, 0o100644, 0, 0, 1, 0, 1000, 0, 0, 0, 0, 20, 0]
            archive.write_bytes(b'070701' + ''.join(f'{v:08x}' for v in fields).encode() + b'lib/modules/foo.ko\0')
            with self.assertRaises(ValueError):
                ramdisk_modules(archive, root / 'modules')
            self.assertFalse((root / 'modules').exists())


if __name__ == '__main__':
    unittest.main()
