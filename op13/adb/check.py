#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run real ARM64 adbd authentication, shell, PTY and sync protocols offline."""
import hashlib
import json
import os
from pathlib import Path
import socket
import shutil
import subprocess
import tempfile
import time


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def main():
    repo = Path(__file__).resolve().parents[2]
    root = repo.parent
    output = root / 'out/op13-adb'
    build = json.loads((output / 'build.json').read_text())
    assert hashlib.sha256((output / 'hmos-adbd').read_bytes()).hexdigest() == build['binary_sha256']
    subprocess.run(['clang', '-static', '-O2', str(repo / 'op13/adb/qemu_shell.c'),
                    '-o', str(output / 'qemu-shell')], check=True)
    cases = []
    with tempfile.TemporaryDirectory(prefix='adb-check-', dir=root / 'work') as temporary:
        fixture = Path(temporary)
        keys = fixture / 'keys'
        keys.mkdir()
        for name in ('allowed', 'unknown'):
            subprocess.run(['adb', 'keygen', str(keys / name)], check=True, capture_output=True)
        # Strip the machine/account comment. No test private key enters the image.
        public = fixture / 'adb_keys'
        public.write_text((keys / 'allowed.pub').read_text().split()[0] + ' hmos-test\n')
        (fixture / 'data').mkdir()
        port, server_port = free_port(), free_port()
        system = root / 'work/inspect/system/system'
        shutil.copytree(system / 'etc', fixture / 'etc', symlinks=True)
        (fixture / 'etc/hmos-adb').mkdir()
        (fixture / 'etc/hmos-adb/adb_keys').touch()
        common = ['bwrap', '--die-with-parent', '--unshare-all', '--share-net',
            '--tmpfs', '/', '--proc', '/proc', '--dev', '/dev',
            '--ro-bind', str(system / 'lib64'), '/system/lib64',
            '--ro-bind', str(fixture / 'etc'), '/system/etc',
            '--ro-bind', str(repo / 'build/work/op13-sysroot/usr/lib/libc.so'), '/system/lib/ld-musl-aarch64.so.1',
            '--ro-bind', str(output / 'hmos-adbd'), '/system/bin/hmos-adbd',
            '--ro-bind', str(output / 'qemu-shell'), '/system/bin/sh',
            '--ro-bind', str(system / 'bin/sh'), '/system/bin/native-sh',
            '--ro-bind', str(public), '/system/etc/hmos-adb/adb_keys',
            '--bind', str(fixture / 'data'), '/data',
            '--ro-bind', str(root / 'work/qemu-aarch64-static'), '/qemu',
            '--symlink', 'system/bin', '/bin', '--symlink', 'system/lib', '/lib',
            '--symlink', 'system/lib64', '/lib64', '--symlink', 'system/etc', '/etc',
            '--clearenv', '--setenv', 'PATH', '/system/bin', '--setenv', 'HOME', '/data',
            '--setenv', 'ADB_TRACE', 'auth transport',
            '--setenv', 'HMOS_ADB_TEST_PORT', str(port), '/qemu', '/system/bin/hmos-adbd']
        with (output / 'protocol-daemon.log').open('w') as log:
            daemon = subprocess.Popen(common, stdout=log, stderr=subprocess.STDOUT)
            try:
                for _ in range(60):
                    if daemon.poll() is not None:
                        raise RuntimeError((output / 'protocol-daemon.log').read_text()[-4000:])
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=.1):
                            break
                    except OSError:
                        time.sleep(.1)
                else:
                    raise RuntimeError('native daemon did not start')
                serial = f'127.0.0.1:{port}'

                def host(key, *args, timeout=12):
                    home = fixture / ('host-' + key)
                    (home / '.android').mkdir(parents=True, exist_ok=True)
                    # The host client uses these private keys normally; tools never display them.
                    env = dict(os.environ, HOME=str(home), ANDROID_USER_HOME=str(home / '.android'),
                               ADB_VENDOR_KEYS=str(keys / key))
                    return subprocess.run(['adb', '-P', str(server_port), *args], env=env,
                                          capture_output=True, text=True, timeout=timeout)

                def stop(key):
                    host(key, 'kill-server')

                # Unknown key must never acquire shell/file privileges.
                host('unknown', 'connect', serial)
                time.sleep(.5)
                refused = host('unknown', '-s', serial, 'shell', '-T', 'printf forbidden')
                assert refused.returncode and 'unauthorized' in (refused.stdout + refused.stderr), refused
                cases.append({'name': 'unknown-key-rejected', 'passed': True})
                stop('unknown')
                host('allowed', 'connect', serial)
                for _ in range(30):
                    state = host('allowed', '-s', serial, 'get-state')
                    if state.returncode == 0 and state.stdout.strip() == 'device':
                        break
                    time.sleep(.1)
                else:
                    raise RuntimeError('authorized host rejected: ' + state.stdout + state.stderr)
                cases.append({'name': 'authorized-rsa-host', 'passed': True})
                result = host('allowed', '-s', serial, 'shell', '-T',
                              "printf 'native-shell-ok\\n'; printf 'stderr-ok\\n' >&2; exit 7")
                assert result.returncode == 7 and result.stdout == 'native-shell-ok\n' and result.stderr == 'stderr-ok\n', result
                cases.append({'name': 'native-shell-v2-output-and-status', 'passed': True})
                result = host('allowed', '-s', serial, 'shell', '-tt', "printf 'pty-ok\\n'")
                assert result.returncode == 0 and 'pty-ok' in result.stdout, result
                cases.append({'name': 'native-interactive-pty', 'passed': True})
                original = fixture / 'source.txt'
                original.write_bytes(b'HMOS native ADB sync\x00\xff\n')
                result = host('allowed', '-s', serial, 'push', str(original), '/data/sync.txt')
                assert result.returncode == 0, result
                result = host('allowed', '-s', serial, 'pull', '/data/sync.txt', str(fixture / 'pulled.txt'))
                assert result.returncode == 0, result
                assert (fixture / 'pulled.txt').read_bytes() == original.read_bytes()
                cases.append({'name': 'sync-push-pull-roundtrip', 'passed': True})
                stop('allowed')
            finally:
                try:
                    subprocess.run(['adb', '-P', str(server_port), 'kill-server'], capture_output=True, timeout=5)
                finally:
                    daemon.terminate()
                    daemon.wait(timeout=5)
        report = {**build, 'passed': True, 'cases': cases,
                  'scope': 'real ARM64 native daemon and OHOS shell via QEMU user mode; '
                           'test-only static shell exec adapter; actual host ADB; loopback; '
                           'USB controller/enumeration and phone SELinux require device testing'}
        (output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
