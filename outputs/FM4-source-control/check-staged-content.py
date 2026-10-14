"""Check the entire Git index against a reviewed source-only inventory.

This guard catches accidental additions; human provenance review is still needed.
"""
import json
from pathlib import Path
import re
import subprocess
import sys

MANIFEST = 'outputs/FM4-source-control/tracked-files.json'

def git(*args):
    return subprocess.check_output(['git', *args])

# The runtime fork keeps a few large upstream sources (D3D12 command processor,
# render target cache); other files stay below 256 KiB.
RUNTIME = 'work/runtime/'

def check_blob(path, data, allowed):
    if path not in allowed:
        raise ValueError(f'Not in reviewed inventory: {path}')
    limit = 524288 if path.startswith(RUNTIME) else 262144
    if len(data) > limit or b'\0' in data:
        raise ValueError(f'Binary or oversized file: {path}')
    text = data.decode('utf-8-sig')
    # The recompiler's emitter writes these macro names as text; it is marked in the inventory.
    if path.endswith(('.cpp', '.h', '.patch')) and not allowed[path].get('emitter'):
        if re.search(r'PPC_FUNC_IMPL\s*\(|PPC_FUNC\s*\(|DEFINE_REX_FUNC\s*\(|__imp__sub_[0-9A-Fa-f]+\s*\([^;]*\)\s*\{', text):
            raise ValueError(f'Possible translated guest function: {path}')
        if 'GIT binary patch' in text:
            raise ValueError(f'Binary patch: {path}')
    return len(data)

def main():
    root = git('rev-parse', '--show-toplevel').decode().strip()
    import os
    os.chdir(root)
    manifest = json.loads(git('show', ':' + MANIFEST))
    allowed = {entry['path']: entry for entry in manifest['files']}
    total = 0
    count = 0
    for record in git('ls-files', '--stage', '-z').split(b'\0'):
        if not record:
            continue
        metadata, raw_path = record.split(b'\t', 1)
        mode, oid, stage = metadata.decode().split()
        path = raw_path.decode('utf-8')
        if mode not in ('100644', '100755') or stage != '0':
            raise ValueError(f'Nonregular or unresolved index entry: {path}')
        total += check_blob(path, git('cat-file', 'blob', oid), allowed)
        count += 1
    if not count:
        raise ValueError('Empty index')
    print(f'PASS: {count} reviewed text files, {total} bytes; no unlisted index entries.')

if __name__ == '__main__':
    try:
        main()
    except (ValueError, UnicodeError, subprocess.CalledProcessError) as exc:
        print(f'BLOCKED: {exc}', file=sys.stderr)
        sys.exit(1)
