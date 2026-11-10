# SPDX-License-Identifier: Apache-2.0
import copy
import json
from pathlib import Path
import tempfile
import unittest
from hdf_hosts import install


class HdfHostsTest(unittest.TestCase):
    def test_restore_complete_services_and_remap_filtered_ids(self):
        donor = {'services': [dict(name=name, path=['/vendor/bin/hdf_devhost', str(original), name, '0', '0'],
            uid=name, gid=[name, 'graphics'], secon='u:r:' + name + ':s0')
            for original, name in [(7, 'composer_host'), (8, 'allocator_host'), (24, 'pin_auth_host')]]}
        before = copy.deepcopy(donor)
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            (stage / 'system-init').mkdir()
            (stage / 'vendor-init').mkdir()
            partial = {'services': [{'name': 'pin_auth_host', 'path': ['/vendor/bin/hdf_devhost'],
                                     'writepid': ['/dev/cpuset/foreground/cgroup.procs']},
                                    {'name': 'render_service', 'path': ['/system/bin/render_service']}]}
            config = stage / 'system-init/resource.cfg'
            config.write_text(json.dumps(partial))
            report = {'preserved_services': [{'service': 'pin_auth_host'}, {'service': 'render_service'}]}
            order = ['composer_host', 'allocator_host', 'pin_auth_host']
            install(stage, donor, order, report)
            hosts = json.loads((stage / 'vendor-init/hdf_devhost.cfg').read_text())['services']
            self.assertEqual([s['path'][1] for s in hosts], ['0', '1', '2'])
            for service, original in zip(hosts, donor['services']):
                for key in ('uid', 'gid', 'secon'):
                    self.assertEqual(service[key], original[key])
            self.assertEqual(hosts[-1]['writepid'], partial['services'][0]['writepid'])
            self.assertEqual(json.loads(config.read_text())['services'], [partial['services'][1]])
            self.assertEqual(sum(r['service'] == 'pin_auth_host' for r in report['preserved_services']), 1)
            self.assertEqual(donor, before)

    def test_reject_missing_identity_and_duplicate_graph(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            (stage / 'system-init').mkdir()
            (stage / 'vendor-init').mkdir()
            donor = {'services': [{'name': 'composer_host', 'path': ['/vendor/bin/hdf_devhost']}]}
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                install(stage, donor, ['composer_host'], {'preserved_services': []})
            with self.assertRaisesRegex(ValueError, 'duplicate HDF'):
                install(stage, donor, ['composer_host', 'composer_host'], {'preserved_services': []})
