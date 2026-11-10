# SPDX-License-Identifier: Apache-2.0
import unittest
from graphics_runtime import native_config


class GraphicsNamespaceTests(unittest.TestCase):
    def donor(self):
        section = ('namespace.default.lib.paths = /system/lib64:/vendor/lib64:/system/lib64/chipset-sdk\n'
                   'namespace.default.asan.lib.paths = /system/lib64:/vendor/lib64:/system/lib64/chipset-sdk\n'
                   'namespace.ndk.lib.paths = /system/lib64/ndk\n'
                   'namespace.default.inherit.ndk.shared.libs = allow_all_shared_libs\n')
        return ('[acquiescence]\n' + section + '[systemscence]\n' + section).encode()

    def test_only_prepend_graphics_path_in_both_sections(self):
        source = self.donor()
        result = native_config(source)
        self.assertEqual(result.count(b' = /hmos/display:/system/lib64:'), 4)
        self.assertEqual(result.replace(b' = /hmos/display:/system/lib64:', b' = /system/lib64:'), source)

    def test_reject_unreviewed_or_already_modified_config(self):
        for source in (b'[empty]\n', self.donor().replace(b'/system/lib64:', b'/vendor/lib64:', 1),
                       native_config(self.donor())):
            with self.assertRaises(ValueError):
                native_config(source)
