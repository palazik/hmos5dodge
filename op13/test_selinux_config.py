# SPDX-License-Identifier: Apache-2.0
import unittest
from selinux_config import permissive_config


class SelinuxConfigTest(unittest.TestCase):
    def test_preserves_other_settings_and_comments(self):
        source = b'# SELINUX=enforcing example\nSELINUX=enforcing\nSELINUXTYPE=targeted\n'
        self.assertEqual(permissive_config(source),
                         b'# SELINUX=enforcing example\nSELINUX=permissive\nSELINUXTYPE=targeted\n')

    def test_rejects_ambiguous_or_disabled_input(self):
        for source in (b'', b'SELINUX=disabled\n', b'SELINUX=enforcing\nSELINUX=permissive\n',
                       b'SELINUX=enforcing\nSELINUX=enforcing\n'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                permissive_config(source)
