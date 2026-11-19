#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Stage the rebuilt Mesa (op13/build_mesa.sh) into the system image.

The vendor image keeps the old 26.1 Mesa under /vendor/lib64/chipsetsdk.  The
bootstrap binds each rebuilt GL library from /hmos/display/mesa over the same
vendor path (like the display VDI libraries), so every HarmonyOS lookup path
stays unchanged.  The new libgallium is renamed to the vendor slot name
libgallium-26.1.0-devel.so (SONAME and the EGL/GLES DT_NEEDED entries are
rewritten to match).  Turnip is added as a Vulkan ICD through
/system/etc/vulkan/icd.d (the HarmonyOS loader searches /system/etc and
/vendor/etc), pointing at /hmos/display/mesa/libvulkan_freedreno.so.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

BUILD = 'out/op13-mesa/lib64/chipsetsdk'
TARGET = 'hmos/display/mesa'
SLOT_GALLIUM = 'libgallium-26.1.0-devel.so'
GL = ('libEGL_impl.so', 'libGLESv1_impl.so', 'libGLESv2_impl.so', 'libGLESv3_impl.so')
VULKAN = 'libvulkan_freedreno.so'
ICD = 'system/etc/vulkan/icd.d/freedreno_icd.aarch64.json'
ICD_CONTENT = {'file_format_version': '1.0.1',
               'ICD': {'library_path': '/' + TARGET + '/' + VULKAN, 'api_version': '1.4.0'}}
COLORTEST = ('out/op13-mesa/bin/op13-gpu-colortest', 'system/bin/op13-gpu-colortest')
LABELS = [('/system/etc/vulkan(/.*)?', 'system_etc_file'), ('/system/bin/op13-gpu-colortest', 'system_bin_file')]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def needed(path):
    return subprocess.check_output(['patchelf', '--print-needed', str(path)], text=True).split()


def icd_content():
    return (json.dumps(ICD_CONTENT, indent=2) + '\n').encode()


def install(workspace, stage):
    source = workspace / BUILD
    built = sorted(source.glob('libgallium-*.so'))
    if len(built) != 1:
        raise RuntimeError('expected exactly one rebuilt libgallium in ' + str(source))
    target = stage / TARGET
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    gallium = target / SLOT_GALLIUM
    shutil.copyfile(built[0], gallium)
    subprocess.run(['patchelf', '--set-soname', SLOT_GALLIUM, str(gallium)], check=True)
    for name in GL + (VULKAN,):
        shutil.copyfile(source / name, target / name)
        if built[0].name in needed(target / name):
            subprocess.run(['patchelf', '--replace-needed', built[0].name, SLOT_GALLIUM, str(target / name)], check=True)
    for path in target.iterdir():
        path.chmod(0o644)
        if built[0].name in needed(path):
            raise RuntimeError('stale libgallium reference in ' + path.name)
    icd = stage / ICD
    icd.parent.mkdir(parents=True, exist_ok=True)
    icd.write_bytes(icd_content())
    colortest = stage / COLORTEST[1]
    colortest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(workspace / COLORTEST[0], colortest)
    colortest.chmod(0o755)
    return {'mesa_source': 'lfdevs/mesa-for-android-container dev/adreno-main 1e6ee4d + OHOS EGL patch',
            'renamed_from': built[0].name,
            'files': {str(p.relative_to(stage)): digest(p) for p in sorted(target.iterdir())},
            'icd': digest(icd), 'colortest': digest(colortest), 'hardware_tested': False}


def verify(after, record):
    target = after / TARGET
    present = {str(p.relative_to(after)) for p in target.iterdir()}
    assert present == set(record['files']), present ^ set(record['files'])
    for path, expected in record['files'].items():
        assert digest(after / path) == expected, path
    for name in GL:
        assert record['renamed_from'] not in needed(target / name), name
    assert (after / ICD).read_bytes() == icd_content()
    if 'colortest' in record:
        assert digest(after / COLORTEST[1]) == record['colortest']
