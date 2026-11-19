#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Userspace compatibility for kernel features the OP13 stock kernel lacks.

1. Code signing (see code_sign_compat.c).  Revision 19: every donor bundle with
   native libraries (77, SceneBoard included) failed to install, 84 of 89
   without native libraries installed, bootevent.bms.main.bundles.ready=false.
   libinstalls.z.so gets libop13_code_sign.so as its first DT_NEEDED entry.
2. io_uring.  libuv (platformsdk/libuv.so) enables io_uring on new kernels; the
   native seccomp policy kills it with SIGSYS (huawei_id_svc and hianalytics
   crash in uv__iou_init on the 6.6 kernel).  ArkTS apps such as SceneBoard
   run on libuv too.  libuv honours UV_USE_IO_URING=0; init exports it in
   pre-init, so every service and appspawn child inherits it.
"""
import hashlib
import json
import shutil
import subprocess

SHIM = 'libop13_code_sign.so'
SHIM_BUILD = 'out/op13-compat/' + SHIM
INSTALLER = 'system/lib64/libinstalls.z.so'
DONOR_INSTALLER = 'work/inspect/system/system/lib64/libinstalls.z.so'
ENV_CFG = 'hmos/startup/system-init/op13_compat_env.cfg'
# 3. Graphics API.  Revision 21: with CPU-mappable buffers RenderService got
#    to render and crashed (SIGSEGV, pc 0) in RSSurfaceOhosVulkan::RequestFrame
#    -> NativeBufferUtils::MakeFromNativeWindowBuffer: the phone variant layer
#    sets persist.sys.graphic.GpuApitype=1 (Vulkan), and there is no Vulkan
#    driver.  0 selects the GLES backend (software Mesa EGL), as the
#    hmos2sweet reference port forces with the same triggers.
GPU_API = 'persist.sys.graphic.GpuApitype'
HILOG_LEVEL = 'hilog.loggable.global'
ENV = {'jobs': [
    {'name': 'pre-init', 'cmds': ['export UV_USE_IO_URING 0']},
    {'name': 'post-fs-data', 'cmds': ['setparam ' + GPU_API + ' 0']},
    {'name': 'param:' + GPU_API + '=1', 'condition': GPU_API + '=1', 'cmds': ['setparam ' + GPU_API + ' 0']},
    {'name': 'param:' + GPU_API + '=2', 'condition': GPU_API + '=2', 'cmds': ['setparam ' + GPU_API + ' 0']},
    # 4. Diagnostics: revision 25 showed hilog.loggable.global=W set at 19.76 s,
    #    dropping all info logs (multimodalinput device adoption is info-level).
    {'name': 'param:' + HILOG_LEVEL + '=W', 'condition': HILOG_LEVEL + '=W', 'cmds': ['setparam ' + HILOG_LEVEL + ' I']},
]}
LABELS = [('/system/lib64/libop13_code_sign\\.so', 'system_lib_file')]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def needed(path):
    return subprocess.check_output(['patchelf', '--print-needed', str(path)], text=True).splitlines()


def env_content():
    return (json.dumps(ENV, indent=4) + '\n').encode()


def install(workspace, stage):
    shim = workspace / SHIM_BUILD
    if not shim.is_file():
        raise RuntimeError('build the code-sign shim first (validate_components.py)')
    target = stage / 'system/lib64'
    target.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(shim, target / SHIM)
    installer = stage / INSTALLER
    shutil.copyfile(workspace / DONOR_INSTALLER, installer)
    previous = needed(installer)
    subprocess.run(['patchelf', '--page-size', '4096', '--add-needed', SHIM, str(installer)], check=True)
    if needed(installer) != [SHIM] + previous:
        raise RuntimeError('code-sign shim is not the first installer dependency')
    (stage / ENV_CFG).parent.mkdir(parents=True, exist_ok=True)
    (stage / ENV_CFG).write_bytes(env_content())
    return {'code_sign_shim_sha256': digest(target / SHIM), 'installer_donor_sha256': digest(workspace / DONOR_INSTALLER),
            'installer_sha256': digest(installer), 'installer_needed': needed(installer),
            'env': ENV, 'hardware_tested': False}


def verify(after, record):
    assert digest(after / 'system/lib64' / SHIM) == record['code_sign_shim_sha256']
    assert digest(after / INSTALLER) == record['installer_sha256']
    assert needed(after / INSTALLER) == record['installer_needed'] and record['installer_needed'][0] == SHIM
    assert (after / ENV_CFG).read_bytes() == env_content()
