# SPDX-License-Identifier: Apache-2.0
"""Keep native init services and the reduced HDF host graph consistent."""
import copy
import json


def validate(stage, manifest):
    hosts = manifest['hdf_init_services']
    services = json.loads((stage / 'vendor-init/hdf_devhost.cfg').read_text())['services']
    if {row['name'] for row in hosts} != set(manifest['hdf_hosts']):
        raise ValueError('init hosts differ from the selected HDF graph')
    if [s['name'] for s in services] != [s['name'] for s in hosts]:
        raise ValueError('init host order differs from HCB order')
    for index, service in enumerate(services):
        name = service['name']
        if (hosts[index]['host_id'] != index or
                service['path'] != ['/vendor/bin/hdf_devhost', str(index), name, '0', '0'] or
                service['uid'] != name or service['gid'][0] != name or
                service['secon'] != 'u:r:' + name + ':s0'):
            raise ValueError('incomplete or mismatched HDF service: ' + name)
        definitions = [s for config in list((stage / 'system-init').glob('*.cfg')) +
                       list((stage / 'vendor-init').glob('*.cfg'))
                       for s in json.loads(config.read_text()).get('services', []) if s['name'] == name]
        if len(definitions) != 1:
            raise ValueError('duplicate or missing init host: ' + name)
    return hosts


def install(stage, donor, ordered_hosts, report):
    if len(ordered_hosts) != len(set(ordered_hosts)):
        raise ValueError('duplicate HDF host in device graph')
    by_name = {}
    for service in donor.get('services', []):
        name = service['name']
        if name in by_name:
            raise ValueError('duplicate donor HDF host: ' + name)
        by_name[name] = service
    services = []
    partial = {}
    # Some donor system configs extend a chip-prod service using a partial
    # definition. Merge their non-identity fields into the complete definition
    # and remove the duplicate, rather than registering a root/no-args process.
    for path in sorted((stage / 'system-init').glob('*.cfg')):
        data = json.loads(path.read_text())
        kept = []
        for service in data.get('services', []):
            if service['name'] in ordered_hosts:
                if service['name'] in partial:
                    raise ValueError('multiple partial HDF service definitions')
                partial[service['name']] = service
            else:
                kept.append(service)
        if 'services' in data and kept != data['services']:
            data['services'] = kept
            path.write_text(json.dumps(data, indent=2) + '\n')
    for host_id, name in enumerate(ordered_hosts):
        service = copy.deepcopy(by_name[name])
        path = service.get('path', [])
        if (len(path) != 5 or path[0] != '/vendor/bin/hdf_devhost' or path[2] != name or
                not service.get('uid') or not service.get('gid') or
                service.get('secon') != 'u:r:' + name + ':s0'):
            raise ValueError('incomplete donor HDF service: ' + name)
        for key, value in partial.get(name, {}).items():
            if key not in ('name', 'path', 'uid', 'gid', 'secon', 'caps'):
                if key in service and service[key] != value:
                    raise ValueError('conflicting HDF service field: ' + name + ':' + key)
                service[key] = value
        # HdfAttributeManagerGetHostList assigns IDs in HCB child order, before
        # priority sorting. Filtering the donor graph therefore changes the IDs.
        service['path'][1] = str(host_id)
        if service.get('critical'):
            service['critical'][0] = 0
        services.append(service)
    (stage / 'vendor-init/hdf_devhost.cfg').write_text(json.dumps({'services': services}, indent=2) + '\n')
    report['preserved_services'] = [row for row in report['preserved_services'] if row['service'] not in ordered_hosts]
    report['preserved_services'].extend({'source': 'chip_prod/etc/init/hdf_devhost.cfg', 'service': s['name'],
        'uid': s['uid'], 'secon': s['secon']} for s in services)
    report['hdf_init_services'] = [{'name': s['name'], 'host_id': int(s['path'][1]),
        'donor_host_id': int(by_name[s['name']]['path'][1])} for s in services]
