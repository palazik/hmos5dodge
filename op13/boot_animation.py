#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Play the real HarmonyOS NEXT boot animation through the picture player.

The donor sys_prod config selects /sys_prod/etc/bootanimation/cust_bootvideo.mp4
(H.264 1256x2760 60 fps + AAC, 8 s).  On OP13 the native media player ends at
once (no Huawei codec HAL), so revision 14 logged bootanimation.started and
.finished 0.3 ms apart and never drew a frame.

This builds a picture-sequence archive from that exact donor video (same
aspect ratio as the 1440x3168 panel, scaled without cropping) and points the
old-format config at it with no video key, so IsBootVideoEnabled() is false and
OpenHarmony bootanimation uses PlayPicture (frameworks/bootanimation,
OpenHarmony-v5.0.3 util.cpp ParseOldConfigFile, boot_animation_operation.cpp).
That is also an end-to-end RenderService -> composer -> panel test that needs
no media codecs.  Sound is omitted (audio_server is not working yet).

The generated archive is ROM-derived content: it is built into the image at
packaging time and never committed.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

DIRECTORY = 'hmos/ui/bootanimation'
CONFIG_NAME = 'bootanimation_custom_config.json'
ARCHIVE_NAME = 'hmos_bootanim.zip'
RUNTIME_ARCHIVE = '/sys_prod/etc/bootanimation/' + ARCHIVE_NAME
DONOR_IMAGE = 'hmos5/sys_prod.img'
DONOR_VIDEO = '/etc/bootanimation/cust_bootvideo.mp4'
DONOR_VIDEO_SHA256 = '66f2b50aad967e080175366ccf83114fc5d86dcac36c223682f42e7e52b7fba7'
FRAME_RATE = 30
WIDTH, HEIGHT = 1440, 3168
FRAME_DIR = 'hmos_boot/'
ZIP_TIME = (2024, 6, 6, 0, 0, 0)
# Picture path only: an empty or absent video key keeps IsBootVideoEnabled() false.
CONFIG = {'cust.bootanimation.pics': RUNTIME_ARCHIVE, 'cust.bootanimation.sounds': '',
          'cust.bootanimation.video': '', 'cust.bootanimation.duration': '60'}
PLAYER_CONFIG = {'Remark': 'Real HarmonyOS NEXT boot animation from donor cust_bootvideo.mp4',
                 'FrameRate': FRAME_RATE}


def config_content():
    return (json.dumps(CONFIG, indent=2) + '\n').encode()


def donor_video(workspace):
    data = subprocess.run(['dump.erofs', '--cat', '--path=' + DONOR_VIDEO, str(workspace / DONOR_IMAGE)],
                          check=True, capture_output=True).stdout
    if hashlib.sha256(data).hexdigest() != DONOR_VIDEO_SHA256:
        raise RuntimeError('donor boot video differs from the reviewed file')
    return data


def build_archive(workspace, target):
    with tempfile.TemporaryDirectory() as temporary:
        temporary = Path(temporary)
        (temporary / 'video.mp4').write_bytes(donor_video(workspace))
        frames = temporary / 'frames'
        frames.mkdir()
        subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-i', str(temporary / 'video.mp4'), '-an',
                        '-vf', f'fps={FRAME_RATE},scale={WIDTH}:{HEIGHT}:flags=lanczos', '-q:v', '3',
                        '-threads', '1', str(frames / 'hmos_boot_%04d.jpg')], check=True)
        names = sorted(path.name for path in frames.glob('*.jpg'))
        if not 200 <= len(names) <= 260:
            raise RuntimeError('unexpected boot animation frame count: %d' % len(names))
        # Fixed timestamps and ordering; frames are already JPEG, so store them.
        with zipfile.ZipFile(target, 'w', zipfile.ZIP_STORED) as archive:
            def add(name, data):
                info = zipfile.ZipInfo(name, ZIP_TIME)
                info.external_attr = 0o644 << 16
                archive.writestr(info, data)
            add('config.json', json.dumps(PLAYER_CONFIG, indent=1).encode() + b'\n')
            for name in names:
                add(FRAME_DIR + name, (frames / name).read_bytes())
    return len(names)


def install(workspace, stage):
    directory = stage / DIRECTORY
    directory.mkdir(parents=True, exist_ok=True)
    count = build_archive(workspace, directory / ARCHIVE_NAME)
    (directory / CONFIG_NAME).write_bytes(config_content())
    return {'source': DONOR_IMAGE + ':' + DONOR_VIDEO, 'source_sha256': DONOR_VIDEO_SHA256,
            'frames': count, 'frame_rate': FRAME_RATE, 'size': [WIDTH, HEIGHT],
            'archive_sha256': hashlib.sha256((directory / ARCHIVE_NAME).read_bytes()).hexdigest(),
            'runtime_archive': RUNTIME_ARCHIVE, 'sound': False, 'hardware_tested': False}


def verify(after, record):
    directory = after / DIRECTORY
    assert (directory / CONFIG_NAME).read_bytes() == config_content()
    archive_path = directory / ARCHIVE_NAME
    assert hashlib.sha256(archive_path.read_bytes()).hexdigest() == record['archive_sha256']
    with zipfile.ZipFile(archive_path) as archive:
        names = archive.namelist()
        assert names[0] == 'config.json'
        assert json.loads(archive.read('config.json'))['FrameRate'] == FRAME_RATE
        assert len([n for n in names if n.startswith(FRAME_DIR) and n.endswith('.jpg')]) == record['frames']
