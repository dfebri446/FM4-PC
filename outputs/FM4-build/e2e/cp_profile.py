"""Summarize a REX_CP_PROFILE stack file against the profiling build (Build.ps1 -Action profbuild).

  python cp_profile.py <stacks> <BuildRoot>\\game-build-prof [--top N]
      share of samples per leaf module, per first runtime frame (its own time plus the
      drivers and system calls it makes) and inclusive per runtime function
  python cp_profile.py <stacks> <BuildRoot>\\game-build-prof --function NAME [--top N]
      samples inside functions whose name contains NAME, per source line of that function
      and the function it calls there

Waits are samples too (for example ExecutePacketType3_WAIT_REG_MEM or the idle loop in
WorkerThreadMain); compare busy work as a share of the samples outside them. Needs
llvm-symbolizer (LLVM in PATH or C:\\Program Files\\LLVM\\bin). Standard library only.
"""
import argparse
import collections
import re
import shutil
import subprocess
from pathlib import Path

RUNTIME_MODULES = ('rexgpu-xenos.dll', 'rexruntime.dll')


def symbolizer():
    found = shutil.which('llvm-symbolizer')
    if found:
        return found
    default = Path(r'C:\Program Files\LLVM\bin\llvm-symbolizer.exe')
    if default.is_file():
        return str(default)
    raise SystemExit('llvm-symbolizer not found')


def load(stacks_path):
    rows = []
    for line in open(stacks_path):
        if line.startswith('#'):
            print(line.strip())
            continue
        parts = line.split()
        rows.append((int(parts[0]), parts[1:]))
    return rows


def symbolize(rows, dll_dir):
    """Frame (module+offset) -> [(function, file:line:column)], innermost inlined frame first."""
    offsets = collections.defaultdict(set)
    for _, frames in rows:
        for frame in frames:
            module, offset = frame.split('+')
            if module in RUNTIME_MODULES:
                offsets[module].add(offset)
    tool = symbolizer()
    symbols = {}
    for module, module_offsets in offsets.items():
        module_offsets = sorted(module_offsets)
        output = subprocess.run([tool, '--obj', str(dll_dir / module), '--relative-address',
                                 '--functions=linkage', '--demangle', '--inlines'],
                                input='\n'.join(module_offsets), capture_output=True,
                                text=True).stdout
        for offset, block in zip(module_offsets, output.strip().split('\n\n')):
            lines = block.split('\n')
            symbols[module + '+' + offset] = [(lines[i].split('(')[0][:110], lines[i + 1])
                                              for i in range(0, len(lines) - 1, 2)]
    return symbols


def summary(rows, symbols, top):
    total = sum(count for count, _ in rows)
    modules, own, inclusive = collections.Counter(), collections.Counter(), collections.Counter()
    for count, frames in rows:
        modules[frames[0].split('+')[0]] += count
        names = []
        for frame in frames:
            if frame in symbols:
                # The physical function: the outermost of the inlined frames.
                names.append(symbols[frame][-1][0])
        if names:
            own[names[0]] += count
        for name in set(names):
            inclusive[name] += count
    print(f'samples {total}')
    print('== leaf module')
    for name, count in modules.most_common(12):
        print(f'{100 * count / total:5.1f}% {name}')
    print('== first runtime frame (own time plus the calls into drivers and the system)')
    for name, count in own.most_common(top):
        print(f'{100 * count / total:5.1f}% {name}')
    print('== inclusive')
    for name, count in inclusive.most_common(top):
        print(f'{100 * count / total:5.1f}% {name}')


def lines_of(rows, symbols, needle, top):
    total = sum(count for count, _ in rows)
    hits = collections.Counter()
    matched = 0
    for count, frames in rows:
        found, callee = None, ''
        for frame in frames:
            pairs = symbols.get(frame)
            if not pairs:
                callee = frame.split('+')[0]
                continue
            for function, location in pairs:
                if needle in function:
                    found = (re.split(r'[\\/]', location)[-1], callee[:70])
                    break
                callee = function
            if found:
                break
        if found:
            hits[found] += count
            matched += count
    print(f'{needle}: {100 * matched / total:.1f}% of {total} samples')
    for (location, called), count in hits.most_common(top):
        print(f'{100 * count / total:5.2f}% {location:45s} {called}')


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('stacks')
    parser.add_argument('dll_dir', type=Path, help='BuildRoot\\game-build-prof')
    parser.add_argument('--function', help='per-line breakdown of functions containing this')
    parser.add_argument('--top', type=int, default=45)
    args = parser.parse_args()
    rows = load(args.stacks)
    symbols = symbolize(rows, args.dll_dir)
    if args.function:
        lines_of(rows, symbols, args.function, args.top)
    else:
        summary(rows, symbols, args.top)


if __name__ == '__main__':
    main()
