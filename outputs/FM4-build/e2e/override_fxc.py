"""Compiles HLSL shader overrides with fxc.exe the way the runtime does (shader_override.cpp),
without starting the game: each override after the contract of its translation and the library
work/runtime/src/graphics/d3d12/shader_override.hlsli, FXC ps_5_1 with /Gis, /O3 and unbounded
descriptor tables. Contracts come from a run with --arg dump_shaders=<dir>
(shader_<hash>_<modification>.ps.contract.hlsli); overrides named shader_<hash>.ps.hlsl use
every contract of that hash.

  override_fxc.py <override file or directory> <contracts dir> [--asm <dir>]
      [--mods <shader_mod_path dir> --define NAME=VALUE ...]

With --mods, an override whose hash has shader_<hash>.mod.hlsli there is compiled with the mod
included after the library, as shader_mod_path does (with --define as shader_mod_defines).
Prints how many compiled and groups the failures by their first error."""
import argparse
import concurrent.futures
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
LIBRARY = HERE.parents[2] / 'work/runtime/src/graphics/d3d12/shader_override.hlsli'


def find_fxc():
    kits = pathlib.Path(r'C:\Program Files (x86)\Windows Kits\10\bin')
    candidates = sorted(kits.glob('10.*/x64/fxc.exe'))
    if not candidates:
        sys.exit('fxc.exe not found in the Windows 10/11 SDK')
    return candidates[-1]


def compile_one(fxc, override, contract, asm_dir, mods, defines):
    with tempfile.TemporaryDirectory() as work:
        work = pathlib.Path(work)
        (work / 'xenos_override.hlsli').write_bytes(LIBRARY.read_bytes())
        mod = ''
        if mods:
            for path in mods.glob('*.hlsli'):
                (work / path.name).write_bytes(path.read_bytes())
            name = f'{contract.name[:23]}.mod.hlsli'
            if (mods / name).is_file():
                mod = f'#include "{name}"\n'
        source = (contract.read_text() + '\n#include "xenos_override.hlsli"\n' + mod +
                  f'#line 1 "{override.as_posix()}"\n' + override.read_text() +
                  '\n#define XE_OVERRIDE_MAIN\n#include "xenos_override.hlsli"\n')
        (work / 'main.hlsl').write_text(source)
        command = [str(fxc), '/nologo', '/T', 'ps_5_1', '/E', 'main', '/Gis', '/O3',
                   '/enable_unbounded_descriptor_tables', '/Fo', str(work / 'main.cso')]
        command += [f'/D{define}' for define in defines]
        if asm_dir:
            command += ['/Fc', str(asm_dir / contract.name.replace('.contract.hlsli', '.asm'))]
        result = subprocess.run(command + [str(work / 'main.hlsl')], capture_output=True,
                                text=True)
        errors = [line for line in (result.stdout + result.stderr).splitlines() if 'error' in line]
        return contract.name, result.returncode, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('overrides', help='an override file or a directory of them')
    parser.add_argument('contracts', help='the dump_shaders directory of a run')
    parser.add_argument('--asm', help='write the FXC disassembly of each to this directory')
    parser.add_argument('--mods', help='a shader_mod_path directory')
    parser.add_argument('--define', action='append', default=[], help='NAME=VALUE for the mods')
    parser.add_argument('--workers', type=int, default=8)
    options = parser.parse_args()
    mods = pathlib.Path(options.mods) if options.mods else None
    overrides = pathlib.Path(options.overrides)
    contracts = pathlib.Path(options.contracts)
    asm_dir = pathlib.Path(options.asm) if options.asm else None
    if asm_dir:
        asm_dir.mkdir(parents=True, exist_ok=True)
    files = [overrides] if overrides.is_file() else sorted(overrides.glob('shader_*.ps.hlsl'))
    jobs = []
    for override in files:
        stem = override.name[:-len('.ps.hlsl')]
        # shader_<hash>_<modification> has one contract, shader_<hash> all of its hash.
        pattern = f'{stem}.ps.contract.hlsli' if stem.count('_') == 2 else f'{stem}_*.ps.contract.hlsli'
        jobs += [(override, contract) for contract in sorted(contracts.glob(pattern))]
    if not jobs:
        sys.exit('no override with a contract')
    fxc = find_fxc()
    failures = {}
    with concurrent.futures.ThreadPoolExecutor(options.workers) as pool:
        for name, code, errors in pool.map(
                lambda job: compile_one(fxc, *job, asm_dir, mods, options.define), jobs):
            if code:
                failures[name] = errors
    print(f'{len(jobs) - len(failures)}/{len(jobs)} compiled')
    by_error = {}
    for name, errors in failures.items():
        key = errors[0].split(': ', 1)[-1][:120] if errors else 'unknown'
        by_error.setdefault(key, []).append(name)
    for key, names in sorted(by_error.items(), key=lambda item: -len(item[1])):
        print(f'{len(names):4d}  {key}\n      for example {names[0]}')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
