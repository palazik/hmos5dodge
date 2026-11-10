# SPDX-License-Identifier: Apache-2.0
import unittest
from param_config import hdf_dac


class HdfDacTest(unittest.TestCase):
    def test_preserves_donor_rule_and_notice(self):
        source = b'# donor notice\nbootevent.hdf_devmgr.ready = hdf_devmgr:hdf_devmgr:0775'
        self.assertEqual(hdf_dac(source), source + b'\n')

    def test_rejects_broader_permissions_or_extra_rules(self):
        for source in (b'', b'bootevent. = root:root:0777\n',
                       b'bootevent.hdf_devmgr.ready = hdf_devmgr:hdf_devmgr:0777\n',
                       b'bootevent.hdf_devmgr.ready = hdf_devmgr:hdf_devmgr:0775\nother = root:root:0777\n'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                hdf_dac(source)
