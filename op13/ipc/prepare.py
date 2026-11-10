#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare a Linux 6.6 stock-kernel IPC module prototype from pinned sources.

This prepares source only. It does not build a kernel Image, load a module,
assign symbol CRCs, patch a running kernel or add anything to the DSU ZIP.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def replace(text, old, new, count=1):
    if text.count(old) != count:
        raise ValueError('unexpected upstream source: ' + old)
    return text.replace(old, new)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    record = json.loads((here / 'UPSTREAM.json').read_text())
    for row in record['files']:
        if hashlib.sha256((here / 'upstream' / row['path']).read_bytes()).hexdigest() != row['sha256']:
            raise ValueError('upstream reference changed: ' + row['path'])
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('output must be empty')
    shutil.copytree(here / 'upstream/compat/ipc', output, dirs_exist_ok=True)
    # Linux 6.6 replaced zap_page_range with the four-argument single-VMA API.
    for name in ('private_api.h', 'sheng_hmos_api_v1.h'):
        path = output / name
        path.write_text(path.read_text().replace('zap_page_range', 'zap_page_range_single'))
    path = output / 'binder_alloc.c'
    text = replace(path.read_text(), 'zap_page_range(vma, page_addr, PAGE_SIZE);',
                   'zap_page_range_single(vma, page_addr, PAGE_SIZE, NULL);')
    text = replace(text, 'ret = register_shrinker(&binder_shrinker);',
                   'ret = register_shrinker(&binder_shrinker, "op13-hmos-binder");')
    path.write_text(text)
    # Match the real Linux 6.6 Binder implementations for changed core APIs.
    path = output / 'binder.c'
    text = replace(path.read_text(), 'close_fd_get_file(fd, &twcb->file);',
                   'twcb->file = close_fd_get_file(fd);')
    text = replace(text, '\tfreezer_do_not_count();\n', '')
    text = replace(text, '\tfreezer_count();\n', '')
    text = replace(text, 'prepare_to_wait(&thread->wait, &wait, TASK_INTERRUPTIBLE);',
                   'prepare_to_wait(&thread->wait, &wait, TASK_INTERRUPTIBLE | TASK_FREEZABLE);')
    text = replace(text,
                   'vma->vm_flags = (vma->vm_flags | VM_DONTCOPY | VM_MIXEDMAP) & ~VM_MAYWRITE;',
                   'vm_flags_mod(vma, VM_DONTCOPY | VM_MIXEDMAP, VM_MAYWRITE);')
    path.write_text(text)
    # The old typed export table remains an upstream reference. This prototype
    # instead resolves real core helpers with temporary probes, then checks the
    # kernel's KCFI prefix against compiler-produced prototype hashes. Normal
    # compiler CFI instrumentation and the actual LSM/MM functions remain used.
    header = (output / 'private_api.h').read_text()
    names = re.findall(r'extern typeof\((\w+)\) \*ace5_api_\w+;', header)
    if len(names) != 20:
        raise ValueError('unexpected private helper set')
    lines = ['// SPDX-License-Identifier: GPL-2.0',
        '/* OP13 Linux 6.6 prototype. No CFI bypass or replacement security checks. */',
        '#define ACE5_PRIVATE_IMPLEMENTATION', '#include "private_api.h"',
        '#include <linux/module.h>', '#include <linux/kprobes.h>',
        '#include <linux/cfi.h>', '#include <linux/cfi_types.h>',
        '#ifndef CONFIG_CFI_CLANG', '#error "This prototype requires Linux KCFI"', '#endif']
    for name in names:
        # DEFINE_CFI_TYPE takes a declaration's address solely to request the
        # compiler hash. Its anchor lives in .discard.addressable; mark that
        # unused reference weak so modpost does not demand a private export.
        # Actual calls still require successful probe lookup and a hash match.
        lines += [f'extern typeof({name}) {name} __weak;',
                  f'typeof({name}) *ace5_api_{name};',
                  f'DEFINE_CFI_TYPE(op13_cfi_{name}, {name});']
    lines += ['static int resolve_helper(const char *name, u32 expected, void **result)', '{',
        '    struct kprobe probe = { .symbol_name = name };',
        '    int status = register_kprobe(&probe);',
        '    if (status) { pr_err("OP13_IPC helper unavailable: %s err=%d\\n", name, status); return status; }',
        '    void *address = (void *)probe.addr;', '    unregister_kprobe(&probe);',
        '    u32 observed = cfi_get_func_hash(address);',
        '    if (!expected || observed != expected) {',
        '        pr_err("OP13_IPC KCFI mismatch: %s expected=%08x observed=%08x\\n", name, expected, observed);',
        '        return -ELIBBAD;', '    }', '    *result = address;',
        '    pr_info("OP13_IPC real helper type checked: %s\\n", name);', '    return 0;', '}',
        'int ace5_private_api_init(void)', '{', '    int status;']
    for name in names:
        lines += [f'    status = resolve_helper("{name}", op13_cfi_{name}, (void **)&ace5_api_{name});',
                  '    if (status) return status;']
    lines += ['    return 0;', '}']
    (output / 'private_api.c').write_text('\n'.join(lines) + '\n')
    # Keep these definitions in a separate translation unit: DEFINE_CFI_TYPE
    # needs address-taken declarations to emit all 20 __kcfi_typeid constants.
    wrappers = ['// SPDX-License-Identifier: GPL-2.0',
        '#define ACE5_PRIVATE_IMPLEMENTATION', '#include "private_api.h"',
        'void __wake_up_pollfree(struct wait_queue_head *head) { ace5_api___wake_up_pollfree(head); }',
        'unsigned long list_lru_count_node(struct list_lru *lru, int nid)',
        '{ return ace5_api_list_lru_count_node(lru, nid); }',
        'unsigned long list_lru_walk_node(struct list_lru *lru, int nid, list_lru_walk_cb isolate, void *arg, unsigned long *nr)',
        '{ return ace5_api_list_lru_walk_node(lru, nid, isolate, arg, nr); }']
    (output / 'private_api_wrappers.c').write_text('\n'.join(wrappers) + '\n')
    path = output / 'Makefile'
    text = path.read_text().replace('sheng_hmos_ipc', 'op13_hmos_ipc')
    path.write_text(replace(text, 'private_api.o\n', 'private_api.o private_api_wrappers.o\n'))
    (output / 'PREPARATION.json').write_text(json.dumps({
        'upstream_commit': record['commit'], 'helpers': names,
        'status': 'prototype-source-only', 'stock_kernel_runtime_tested': False,
        'kernel_or_module_built_by_this_tool': False, 'cfi_bypass_used': False,
        'module_crc_values_generated_or_replaced': False,
        'dsu_package_modified': False}, indent=2) + '\n')
    print('Prepared pinned-source IPC prototype:', output)


if __name__ == '__main__':
    main()
