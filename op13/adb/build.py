#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build an authenticated, USB-first native OHOS adbd from pinned AOSP/Linux sources."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

REVISION = '07f091cd595d61555bb857096532c8a9d7d81291'


def replace(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise RuntimeError(f'patch mismatch: {path}: {old[:80]!r}')
    path.write_text(text.replace(old, new))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    workspace = repo.parent
    source = args.source.resolve()
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != REVISION or subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain']):
        raise RuntimeError('expected pristine pinned arduino/adbd source')
    work = workspace / 'work/op13-adb/patched-source'
    if work.exists():
        shutil.rmtree(work)
    shutil.copytree(source, work, symlinks=True, ignore=shutil.ignore_patterns('.git'))
    replace(work / 'adb/adb_auth_client.cpp', '"/adb_keys",\n    "/data/misc/adb/adb_keys",',
            '"/system/etc/hmos-adb/adb_keys",')
    replace(work / 'adb/adb_auth_client.cpp', '#include <resolv.h>',
            '#include <resolv.h>\nextern "C" int __b64_pton(const char*, unsigned char*, size_t);')
    replace(work / 'adb/transport_local.cpp',
            'serverfd = network_inaddr_any_server(port, SOCK_STREAM, &error);',
            'serverfd = network_loopback_server(port, SOCK_STREAM, &error);')
    replace(work / 'adb/adb_trace.cpp', 'const char* adb_device_banner = "host";',
            'const char* adb_device_banner = "device";')
    replace(work / 'adb/daemon/main.cpp',
            '        // Listen on default port.\n        local_init(DEFAULT_ADB_LOCAL_TRANSPORT_PORT);',
            '        fprintf(stderr, "hmos-adbd: no USB endpoints; TCP fallback disabled\\n");\n        return 1;')
    replace(work / 'base/logging.cpp',
            '  return program_invocation_short_name;\n}\n#endif',
            '  return program_invocation_short_name;\n}\n#else\nconst char* getprogname() { return "hmos-adbd"; }\n#endif')
    output = workspace / 'out/op13-adb'
    objects = output / 'obj'
    objects.mkdir(parents=True, exist_ok=True)
    # Public OpenSSL declarations; all linked implementations are donor-native.
    includes = workspace / 'work/op13-adb/headers'
    shutil.copytree('/usr/include/openssl', includes / 'openssl', dirs_exist_ok=True)
    env = dict(os.environ, SYSROOT=str(repo / 'build/work/op13-sysroot'))
    flags = ['-O2', '-g0', '-DADB_HOST=0', '-DADB_NON_ANDROID=1', '-DHAVE_PTHREADS=1',
             '-D_GNU_SOURCE', '-D_XOPEN_SOURCE=700', '-DPROP_NAME_MAX=32', '-DPROP_VALUE_MAX=92',
             '-DALLOW_ADBD_NO_AUTH=0', '-DADB_REVISION="hmos5dodge-auth-usb"',
             '-Wno-deprecated-declarations', '-Wno-macro-redefined', '-ffunction-sections', '-fdata-sections',
             '-include', str(repo / 'op13/adb/compat.h')]
    for directory in [work / 'include', work / 'base/include', work / 'libcrypto_utils/include',
                      work / 'adb', includes]:
        flags += ['-I', str(directory)]
    cpp = ['adb/' + name + '.cpp' for name in
           'adb adb_auth adb_utils adb_trace adb_io adb_listeners diagnose_usb shell_service '
           'shell_service_protocol sockets transport transport_local transport_usb log-non-android '
           'fdevent get_my_path_linux adb_auth_client services file_sync_service framebuffer_service '
           'remount_service set_verity_enable_state_service usb_linux_client daemon/main'.split()]
    cpp += ['base/' + name + '.cpp' for name in
            'errors_unix file logging parsenetaddress stringprintf strings'.split()]
    cpp += ['libcutils/sockets.cpp', 'libcutils/sockets_unix.cpp']
    cpp.remove('adb/diagnose_usb.cpp')  # Host-side glibc USB permission diagnostics.
    c = ['libcutils/' + name + '.c' for name in
          'socket_inaddr_any_server_unix socket_local_client_unix socket_local_server_unix '
          'socket_loopback_client_unix socket_loopback_server_unix socket_network_client_unix '
          'load_file threads sched_policy'.split()]
    files = [work / name for name in cpp + c] + [repo / 'op13/adb/compat.c']

    def compile_one(path):
        obj = objects / (hashlib.sha256(str(path).encode()).hexdigest()[:12] + '.o')
        compiler = repo / ('build/ohos-clang++' if path.suffix == '.cpp' else 'build/ohos-clang')
        command = [str(compiler), *flags]
        command += ['-std=c++14'] if path.suffix == '.cpp' else ['-std=gnu11']
        command += ['-c', str(path), '-o', str(obj)]
        result = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f'{path.name}:\n{result.stdout}')
        return obj

    with ThreadPoolExecutor(max_workers=6) as executor:
        built = list(executor.map(compile_one, files))
    native_libs = workspace / 'work/inspect/system/system/lib64/chipset-pub-sdk'
    subprocess.run([str(repo / 'build/ohos-clang++'), *map(str, built), '-Wl,--gc-sections',
                    '-L' + str(native_libs), '-lcrypto_openssl.z', '-lc++', '-lc', '-o', str(output / 'hmos-adbd')],
                   env=env, check=True)
    report = {'source': 'https://github.com/arduino/adbd', 'revision': REVISION,
              'authentication_required': True, 'default_transport': 'USB FunctionFS',
              'test_transport': 'explicit loopback only',
              'binary_sha256': hashlib.sha256((output / 'hmos-adbd').read_bytes()).hexdigest()}
    (output / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
