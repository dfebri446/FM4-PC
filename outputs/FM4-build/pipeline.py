"""Windows source-only bootstrap. Private outputs stay below build-root."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

from prepare import write_switches

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SDK_REV = 'f5337cdc947ff6d4c4196737e2c807a48f2a1fc2'
SDK_URL = 'https://github.com/rexglue/rexglue-sdk.git'
XEX_HASHES = {
    'default.xex': '763636fe8cc9d2ff822ade4c972efef907e193f31175def127863575bf3c4ad5',
    'SpeechFacade_default.xex': 'cf06cac7c34e38a9b45db5c7da4a690c7414c8b461537463d3162465e89431bd',
    'XMediaFacade_default.xex': 'fec814d3066c73b182543e80a50c7c7caf48749e4026ae933bf19654d4f118a7',
}

def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def copy_source(source, destination):
    """Keep timestamps stable when regenerating an unchanged private project."""
    source, destination = Path(source), Path(destination)
    if not destination.is_file() or source.read_bytes() != destination.read_bytes():
        shutil.copyfile(source, destination)

def run(args, cwd=None, log=None, env=None):
    args = [str(x) for x in args]
    print('Running:', subprocess.list2cmdline(args), flush=True)
    if log:
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open('w', encoding='utf-8') as output:
            result = subprocess.run(args, cwd=cwd, stdout=output, stderr=subprocess.STDOUT, env=env)
        if result.returncode:
            print(log.read_text(errors='replace')[-6000:])
            raise RuntimeError(f'Exit {result.returncode}; log: {log}')
    else:
        subprocess.run(args, cwd=cwd, env=env, check=True)

def doctor():
    if os.name != 'nt' or sys.maxsize <= 2**32 or sys.version_info < (3,11):
        raise RuntimeError('Requires Windows x64 and Python 3.11 or newer (64-bit).')
    for name in ('git', 'cmake', 'ninja', 'clang', 'clang++', 'link'):
        if not shutil.which(name):
            raise RuntimeError(f'Missing {name}. See README.txt and use Build.ps1 from a VS C++ environment.')
    version = subprocess.check_output(['clang++', '--version'], text=True)
    if not re.search(r'clang version 20\.', version):
        raise RuntimeError('Use LLVM 20.x (validated: 20.1.8); pass -LLVMRoot to Build.ps1.')
    cmake_version = subprocess.check_output(['cmake','--version'],text=True)
    match = re.search(r'version (\d+)\.(\d+)',cmake_version)
    if not match or tuple(map(int, match.groups())) < (3,25):
        raise RuntimeError('CMake 3.25+ is required.')
    sdk_dir = os.environ.get('WindowsSdkDir', '')
    sdk_version = os.environ.get('WindowsSDKVersion', '').strip('\\/')
    fxc = Path(sdk_dir)/'bin'/sdk_version/'x64/fxc.exe'
    if not sdk_dir or not sdk_version or not fxc.is_file():
        raise RuntimeError('Windows SDK x64 fxc.exe not found in the VS development environment.')
    print('Preflight passed. LLVM:', version.splitlines()[0], 'FXC:', fxc)
    return fxc

def validate_discs(disc1, disc2):
    if not disc1:
        raise ValueError('Provide -Disc1 with the extracted Play Disc directory, not an ISO.')
    disc1 = Path(disc1).resolve()
    for name, expected in XEX_HASHES.items():
        path = disc1/name
        if not path.is_file() or digest(path) != expected:
            raise ValueError(f'Unsupported or missing executable: {path}. This profile supports the tested Japan/Asia release only.')
    if not (disc1/'Media').is_dir() or not (disc1/'Database.xmplr').is_file():
        raise ValueError('Disc1 must contain the extracted game assets (Media and Database.xmplr), not just the three XEX files.')
    if disc2:
        disc2 = Path(disc2).resolve()
        for index in range(1,5):
            package = disc2/'content/0000000000000000/4d530910/00000002'/f'4d5309100000000{index}'
            if not package.is_file():
                raise ValueError(f'Missing Disc 2 content package: {package}')
    return disc1, disc2

def setup(root, fxc, jobs):
    source, build, install = root/'sdk-source', root/'sdk-build', root/'sdk-install'
    if not source.exists():
        run(['git','clone',SDK_URL,source],log=root/'logs/sdk-clone.log')
    head = subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
    marker = source/'.fm4-patch-sha256'
    if not marker.exists():
        if subprocess.check_output(['git','-C',str(source),'status','--porcelain']).strip():
            raise RuntimeError('SDK tree has unrecorded edits; use a new build-root.')
        run(['git','-C',source,'checkout','--detach',SDK_REV])
        run(['git','-C',source,'submodule','update','--init','--recursive'],log=root/'logs/sdk-submodules.log')
        run(['git','-C',source,'apply','--check',HERE/'sdk.patch'])
        run(['git','-C',source,'apply',HERE/'sdk.patch'])
        shutil.copytree(HERE/'sdk-extra',source,dirs_exist_ok=True)
        marker.write_text(digest(HERE/'sdk.patch'))
    elif head != SDK_REV or marker.read_text() != digest(HERE/'sdk.patch'):
        raise RuntimeError('SDK revision/patch changed. Use a new build-root to avoid mixing versions.')
    shader_source = source/'src/graphics/shaders/fm4-resolve'
    for bits in (8,16,32,64):
        for suffix in ('','_scaled'):
            name = f'resolve_full_{bits}bpp{suffix}'
            run([fxc,'/D','SHADING_LANGUAGE_HLSL_XE=1','/T','cs_5_1','/O3',
                 '/Qstrip_reflect','/Qstrip_debug','/Qstrip_priv','/Gfp','/nologo',
                 '/Fh',source/f'src/graphics/shaders/bytecode/d3d12_5_1/{name}_cs.h',
                 '/Vn',name+'_cs',shader_source/(name+'.cs.xesl')],log=root/f'logs/{name}.log')
    run(['cmake','-S',source,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++',
         '-DCMAKE_C_FLAGS=-march=x86-64-v2','-DCMAKE_CXX_FLAGS=-march=x86-64-v2',
         '-DREXGLUE_ENABLE_TRACY=OFF','-DREXGLUE_BUILD_TESTS=OFF','-DREXGLUE_USE_VULKAN=OFF',
         f'-DCMAKE_INSTALL_PREFIX={install}'],log=root/'logs/sdk-configure.log')
    run(['cmake','--build',build,'--parallel',jobs],log=root/'logs/sdk-build.log')
    run(['cmake','--install',build],log=root/'logs/sdk-install.log')
    (root/'setup-complete.json').write_text(json.dumps({'revision':SDK_REV,'patch':digest(HERE/'sdk.patch')}))

def generate(root, disc1, disc2, jobs):
    install, project = root/'sdk-install', root/'project'
    cli = install/'bin/rexglue.exe'
    if not cli.is_file() or not (root/'setup-complete.json').is_file():
        raise RuntimeError('Run setup first.')
    project.mkdir(exist_ok=True)
    # Only source files from the source-only checkout are copied. No developer cache.
    inventory=json.loads((REPO/'outputs/FM4-source-control/tracked-files.json').read_text())
    for entry in inventory['files']:
        name=entry['path']
        if name.startswith(('work/fm4-rex/src/','work/fm4-rex/tests/')):
            destination=project/Path(name).relative_to('work/fm4-rex')
            destination.parent.mkdir(parents=True,exist_ok=True)
            copy_source(REPO/name,destination)
    copy_source(REPO/'work/fm4-rex/CMakeLists.txt',project/'CMakeLists.txt')
    (project/'generated').mkdir(exist_ok=True)
    copy_source(HERE/'rexglue.cmake',project/'generated/rexglue.cmake')
    copy_source(HERE/'guard-generated.py',project/'guard-generated.py')
    for name in ('fm4_bounds.toml','fm4_initializers.toml','fm4_callbacks.toml','fm4_prior_cfg.toml','speech_bounds.toml','xmedia_bounds.toml'):
        copy_source(HERE/name,project/name)
    loader = root/'loader-build'
    run(['cmake','-S',HERE/'loader','-B',loader,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_CXX_COMPILER=clang++',f'-DCMAKE_PREFIX_PATH={install}'],log=root/'logs/loader-configure.log')
    run(['cmake','--build',loader,'--parallel',jobs],log=root/'logs/loader-build.log')
    env = dict(os.environ, PATH=str(install/'bin')+os.pathsep+os.environ['PATH'])
    run([loader/'fm4_dump.exe',disc1/'default.xex',root/'loaded-image.bin'],cwd=root,env=env,log=root/'logs/image-load.log')
    tables = write_switches(root/'loaded-image.bin',HERE/'switch-recipes.json',project/'fm4_switches.toml')
    print(f'Generated {tables} private switch tables from the supplied XEX.')
    quote = lambda path: json.dumps(str(path).replace('\\','/'))
    manifest = '[project]\nname = "fm4_rex"\nsdk_version = "0.10.0"\ngame_root = '+quote(disc1)+'\n'
    manifest += '\n[entrypoint]\nfile_path = '+quote(disc1/'default.xex')+'\nout_directory_path = "generated/default"\n'
    manifest += 'includes = ["fm4_bounds.toml", "fm4_switches.toml", "fm4_initializers.toml", "fm4_callbacks.toml", "fm4_prior_cfg.toml"]\n'
    for name,directory in [('XMediaFacade_default','xmedia'),('SpeechFacade_default','speech')]:
        manifest += '\n[[modules]]\nguest_path = '+json.dumps('game:\\'+name+'.xex')+'\nfile_path = '+quote(disc1/(name+'.xex'))+'\n'
        manifest += f'out_directory_path = "generated/{directory}"\nincludes = ["{directory}_bounds.toml"]\n'
    (project/'fm4_rex_manifest.toml').write_text(manifest,encoding='utf-8')
    run([cli,'codegen',project/'fm4_rex_manifest.toml'],cwd=project,log=root/'logs/codegen.log')
    for directory in ('default','xmedia','speech'):
        args = [sys.executable,project/'guard-generated.py',project/f'generated/{directory}',root/'loaded-image.bin']
        run(args + ([] if directory=='default' else ['--module']),log=root/f'logs/guard-{directory}.log')
    state = {'disc1':str(disc1),'disc2':str(disc2) if disc2 else None,'executables':XEX_HASHES}
    (root/'local.json').write_text(json.dumps(state,indent=2),encoding='utf-8')

FLAVORS = ('thinlto','v3','pgo-gen','pgo-use')

def game_build_dir(root,flavor=()):
    """game-build for the default build; game-build-<flavor> for opt-in compiler experiments."""
    return root/('game-build' if not flavor else 'game-build-'+'-'.join(sorted(flavor)))

def flavor_flags(root,flavor):
    # Opt-in only: the default flags stay -march=x86-64-v2 without LTO/PGO. The project keeps
    # its own floating-point flags (-ffp-model=strict); these only add to them.
    flags=['-march=x86-64-v3' if 'v3' in flavor else '-march=x86-64-v2']
    if 'thinlto' in flavor: flags.append('-flto=thin')
    profile_dir=root/'pgo'
    if 'pgo-gen' in flavor: flags.append(f'-fprofile-generate={profile_dir.as_posix()}')
    if 'pgo-use' in flavor:
        profile=profile_dir/'fm4.profdata'
        if not profile.is_file():
            raise RuntimeError(f'Missing {profile}; merge the pgo-gen *.profraw files with llvm-profdata first.')
        flags+=[f'-fprofile-use={profile.as_posix()}','-Wno-profile-instr-out-of-date','-Wno-profile-instr-unprofiled']
    return ' '.join(flags)

def build_game(root,jobs,flavor=()):
    project, install, build = root/'project',root/'sdk-install',game_build_dir(root,flavor)
    if not (root/'local.json').is_file():
        raise RuntimeError('Run generate first.')
    state=json.loads((root/'local.json').read_text())
    validate_discs(state['disc1'],state['disc2'])
    suffix=build.name[len('game-build'):]
    linker=['-DCMAKE_EXE_LINKER_FLAGS=/opt:lldltojobs='+str(jobs)] if 'thinlto' in flavor else []
    run(['cmake','-S',project,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_CXX_COMPILER=clang++',f'-DCMAKE_CXX_FLAGS={flavor_flags(root,flavor)}',*linker,
         f'-DCMAKE_PREFIX_PATH={install}',f'-DPython3_EXECUTABLE={sys.executable}',
         f'-DFM4_LOADED_IMAGE={root / "loaded-image.bin"}'],log=root/f'logs/game-configure{suffix}.log')
    started=time.time()
    run(['cmake','--build',build,'--parallel',jobs],log=root/f'logs/game-build{suffix}.log')
    print(f'Game build took {time.time()-started:.0f} s.')
    # Always stage the matching patched runtime/plugin, including incremental builds.
    for dll in (install/'bin').glob('*.dll'):
        shutil.copyfile(dll,build/dll.name)
    run([build/'fm4_instruction_tests.exe'],cwd=build,log=root/f'logs/instruction-tests{suffix}.log')
    artifacts = ['fm4_rex.exe','fm4_rex_XMediaFacade_default.dll','fm4_rex_SpeechFacade_default.dll','rexruntime.dll','rexgpu-xenos.dll']
    report = {'artifacts':{name:digest(build/name) for name in artifacts},'gameplay_verified':False}
    (root/f'build-result{suffix}.json').write_text(json.dumps(report,indent=2))
    print('Build and instruction tests passed. Run with Build.ps1 -Action run using the same -BuildRoot.')

def launch(root,single_tile=False):
    state = json.loads((root/'local.json').read_text())
    disc1,disc2 = validate_discs(state['disc1'],state['disc2'])
    report=json.loads((root/'build-result.json').read_text())
    for name,expected in report['artifacts'].items():
        if digest(root/'game-build'/name)!=expected:
            raise RuntimeError(f'Build artifact changed: {name}. Rebuild first.')
    env = {k:v for k,v in os.environ.items() if not k.startswith('FM4_')}
    env['FM4_SHADOW_WORKAROUND']='1'
    cvars=launch_cvars(root,disc1,root/'logs/runtime.log')
    if single_tile:
        # Races render 720p in one tile without MSAA (the title's UI tiling
        # scenario): no predicated tiling, FXAA on the swap instead.
        env['FM4_TILING_SCENARIO']='2'
        cvars['swap_post_effect']='fxaa'
    if disc2:
        env['FM4_DISC2_ROOT']=str(disc2)
    args=[root/'game-build/fm4_rex.exe']+[f'--{k}={v}' for k,v in cvars.items()]
    run(args,cwd=root,env=env,log=root/'logs/play.log')

def warm_cache(root):
    """Fills BuildRoot/cache with the shaders and pipelines of the menus and a Quick Race by
    driving the e2e fixed timeline (no network). Later runs then avoid compiling them."""
    out=root/'e2e/warmcache'
    run([sys.executable,HERE/'e2e/e2e.py','--build-root',root,'--out',out,'--scenario','quickrace',
         '--tail','30'],log=root/'logs/warmcache.log')
    print(f'Shader cache warmed; run details in {out}.')

def launch_cvars(root,disc1,log_file):
    """Launcher cvars, shared with e2e/e2e.py. New performance defaults go here once they pass e2e."""
    return {'game_data_root':disc1,'user_data_root':root/'user-data','cache_root':root/'cache',
            'allow_game_relative_writes':'false','headless':'false','fullscreen':'false',
            'render_target_path_d3d12':'rov','readback_resolve':'fast',
            'gpu_allow_invalid_fetch_constants':'true','async_shader_compilation':'false',
            'rov_occlusion_awaited_report_divisor':'2','rov_occlusion_async':'all',
            'input_backend':'sdl','mnk_mode':'true','mnk_mouse':'false',
            'keybind_right_trigger':'W','keybind_left_trigger':'S',
            'keybind_lstick_left':'A','keybind_lstick_right':'D',
            'keybind_a':'Space','keybind_b':'Backspace','keybind_start':'Return',
            'keybind_back':'Tab','keybind_dpad_up':'Up','keybind_dpad_down':'Down',
            'keybind_dpad_left':'Left','keybind_dpad_right':'Right',
            'log_file':log_file}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['doctor','setup','generate','build','all','run','warmcache'])
    parser.add_argument('--build-root',type=Path,default=REPO/'work/portable-fm4')
    parser.add_argument('--disc1',type=Path)
    parser.add_argument('--disc2',type=Path)
    parser.add_argument('--jobs',type=int,default=4)
    parser.add_argument('--single-tile',action='store_true',
                        help='run: races without MSAA and predicated tiling, with FXAA (faster)')
    parser.add_argument('--flavor',default='',help='opt-in compiler experiment for build: comma list of '+','.join(FLAVORS))
    args=parser.parse_args()
    root=args.build_root.resolve()
    # Never write private outputs into the tracked deliverables/source directories.
    if root in (REPO,*REPO.parents) or REPO/'outputs'==root or REPO/'outputs' in root.parents or REPO/'work/fm4-rex'==root or REPO/'work/fm4-rex' in root.parents:
        raise ValueError('BuildRoot must be a dedicated private directory, e.g. work/portable-fm4 or D:/FM4-build.')
    if args.jobs<1 or args.jobs>64:
        raise ValueError('jobs must be between 1 and 64')
    flavor=tuple(sorted({f for f in args.flavor.split(',') if f}))
    if set(flavor)-set(FLAVORS) or {'pgo-gen','pgo-use'}<=set(flavor):
        raise ValueError(f'flavor must be a comma list of {FLAVORS}, not both pgo-gen and pgo-use')
    fxc=doctor()
    if args.action=='doctor': return
    discs=None
    if args.action in ('generate','all'):
        discs=validate_discs(args.disc1,args.disc2)
    root.mkdir(parents=True,exist_ok=True)
    if args.action in ('setup','all'): setup(root,fxc,args.jobs)
    if args.action in ('generate','all'): generate(root,*discs,args.jobs)
    if args.action in ('build','all'): build_game(root,args.jobs,flavor)
    if args.action=='run': launch(root,args.single_tile)
    if args.action=='warmcache': warm_cache(root)

if __name__=='__main__':
    try: main()
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as error:
        sys.exit(f'ERROR: {error}')
