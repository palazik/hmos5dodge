#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Expose the existing donor HDF ready-event DAC in the loaded system path."""
from pathlib import Path


def hdf_dac(source):
    rules = [line.strip() for line in source.decode().splitlines()
             if line.strip() and not line.lstrip().startswith('#')]
    if rules != ['bootevent.hdf_devmgr.ready = hdf_devmgr:hdf_devmgr:0775']:
        raise ValueError('unexpected donor HDF parameter permission rules')
    return source + (b'' if source.endswith(b'\n') else b'\n')


def write_config(workspace, stage):
    source = workspace / 'work/inspect/vendor/etc/param/hdf_devmgr.para.dac'
    target = stage / 'system/etc/param/hdf_devmgr.para.dac'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(hdf_dac(source.read_bytes()))
