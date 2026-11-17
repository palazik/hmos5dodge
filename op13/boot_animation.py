#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Play the real HarmonyOS NEXT boot video, minus its audio track.

The donor sys_prod config selects /sys_prod/etc/bootanimation/cust_bootvideo.mp4
(H.264 Main 1256x2760 60 fps + AAC LC, 8 s).  In revision 14 bootanimation set
started/finished 0.3 ms apart: media_service and av_codec_service were running,
but audio_server crash-looped ("Cannot find service audio_host"; no OP13 audio
HDI yet), and the player's audio track could not be prepared.

The picture player is not an alternative: it stretches every frame into a
centred min(width, height) square (OpenHarmony-v5.0.3 boot_picture_player.cpp
InitPicCoordinates), which would distort this tall animation.

So the original video stream is copied bit-for-bit (no re-encode, same
resolution, frame rate and smoothness) and only the audio track is dropped.
Restore the original file once audio works.  The output is ROM-derived: it is
built into the image at packaging time and never committed.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

DIRECTORY = 'hmos/ui/bootanimation'
CONFIG_NAME = 'bootanimation_custom_config.json'
VIDEO_NAME = 'hmos_bootvideo_noaudio.mp4'
RUNTIME_VIDEO = '/sys_prod/etc/bootanimation/' + VIDEO_NAME
DONOR_IMAGE = 'hmos5/sys_prod.img'
DONOR_VIDEO = '/etc/bootanimation/cust_bootvideo.mp4'
DONOR_VIDEO_SHA256 = '66f2b50aad967e080175366ccf83114fc5d86dcac36c223682f42e7e52b7fba7'
STALE_FROM_BASE = {'hmos_bootanim.zip'}  # unreferenced revision 15 leftover
# Same keys as the donor config; only the video path differs (sound stays off).
CONFIG = {'cust.bootanimation.pics': '', 'cust.bootanimation.sounds': '',
          'cust.bootanimation.video': RUNTIME_VIDEO}


def config_content():
    return (json.dumps(CONFIG, indent='\t') + '\n').encode()


def donor_video(workspace):
    data = subprocess.run(['dump.erofs', '--cat', '--path=' + DONOR_VIDEO, str(workspace / DONOR_IMAGE)],
                          check=True, capture_output=True).stdout
    if hashlib.sha256(data).hexdigest() != DONOR_VIDEO_SHA256:
        raise RuntimeError('donor boot video differs from the reviewed file')
    return data


def video_packets(path):
    """Hash of the video elementary stream, to prove it was copied unchanged."""
    data = subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-i', str(path), '-map', '0:v:0', '-c', 'copy',
                           '-f', 'h264', '-'], check=True, capture_output=True).stdout
    return hashlib.sha256(data).hexdigest()


def streams(path):
    result = subprocess.run(['ffprobe', '-v', 'error', '-show_entries',
                             'stream=codec_type,codec_name,width,height,r_frame_rate', '-of', 'json', str(path)],
                            check=True, capture_output=True, text=True).stdout
    return json.loads(result)['streams']


def build_video(workspace, target):
    with tempfile.TemporaryDirectory() as temporary:
        source = Path(temporary) / 'cust_bootvideo.mp4'
        source.write_bytes(donor_video(workspace))
        subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-i', str(source), '-map', '0:v:0', '-c', 'copy',
                        '-an', '-map_metadata', '-1', '-fflags', '+bitexact', '-flags:v', '+bitexact',
                        '-movflags', '+faststart', str(target)], check=True)
        original = video_packets(source)
    if video_packets(target) != original:
        raise RuntimeError('video stream changed during remux')
    found = streams(target)
    if [row['codec_type'] for row in found] != ['video'] or found[0]['codec_name'] != 'h264':
        raise RuntimeError('unexpected remuxed streams')
    return original, found[0]


def install(workspace, stage):
    directory = stage / DIRECTORY
    if directory.exists():
        shutil.rmtree(directory)  # drop files carried over from an older base stage
    directory.mkdir(parents=True)
    stream_sha256, stream = build_video(workspace, directory / VIDEO_NAME)
    (directory / CONFIG_NAME).write_bytes(config_content())
    return {'source': DONOR_IMAGE + ':' + DONOR_VIDEO, 'source_sha256': DONOR_VIDEO_SHA256,
            'video_stream_sha256': stream_sha256, 'video_stream_copied_unchanged': True,
            'width': stream['width'], 'height': stream['height'], 'frame_rate': stream['r_frame_rate'],
            'file_sha256': hashlib.sha256((directory / VIDEO_NAME).read_bytes()).hexdigest(),
            'runtime_video': RUNTIME_VIDEO, 'audio': False,
            'audio_reason': 'audio_server has no OP13 audio_host; restore the original file once audio works',
            'hardware_tested': False}


def verify(after, record):
    directory = after / DIRECTORY
    # Incremental EROFS builds add/replace but cannot delete, so revision 15's
    # unreferenced picture archive remains in images derived from it.
    present = {path.name for path in directory.iterdir()}
    assert {CONFIG_NAME, VIDEO_NAME} <= present and present - {CONFIG_NAME, VIDEO_NAME} <= STALE_FROM_BASE, present
    assert (directory / CONFIG_NAME).read_bytes() == config_content()
    video = directory / VIDEO_NAME
    assert hashlib.sha256(video.read_bytes()).hexdigest() == record['file_sha256']
    assert video_packets(video) == record['video_stream_sha256']
    assert [row['codec_type'] for row in streams(video)] == ['video']
