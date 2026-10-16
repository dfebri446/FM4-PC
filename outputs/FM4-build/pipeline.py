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
# The runtime (ReXGlue SDK fork, D3D12 only) lives in work/runtime. Its
# third-party libraries still come from the upstream SDK at this revision:
# the directories it vendors and the submodules the D3D12 build needs.
RUNTIME = REPO/'work/runtime'
SDK_REV = 'f5337cdc947ff6d4c4196737e2c807a48f2a1fc2'
SDK_URL = 'https://github.com/rexglue/rexglue-sdk.git'
THIRDPARTY_VENDORED = ('aes_128','crypto','disasm','disruptorplus','dxbc','dxc','ffmpeg-overlay','pe',
                       'renderdoc','stb','tiny-aes-c')
THIRDPARTY_SUBMODULES = ('cli11','libmspack','FFmpeg','tomlplusplus','simde','xxHash','spdlog','fmt',
                         'utfcpp','imgui','sdl3','inja','o1heap')
RESOLVE_SHADERS = [f'resolve_full_{bits}bpp{suffix}' for bits in (8,16,32,64) for suffix in ('','_scaled')]
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

def check_layout(root):
    if (root/'sdk-source').exists() and not (root/'runtime-source').exists():
        raise RuntimeError('This BuildRoot uses the old sdk-source/sdk.patch layout. Use a new BuildRoot.')

def sync_runtime(root):
    """Mirror work/runtime into <BuildRoot>/runtime-source. Unchanged files keep their
    timestamps, so incremental runtime builds only recompile what was edited."""
    source = root/'runtime-source'
    listing = source/'.fm4-runtime-files.json'
    previous = set(json.loads(listing.read_text())) if listing.is_file() else set()
    current = set()
    for path in RUNTIME.rglob('*'):
        if path.is_file():
            relative = path.relative_to(RUNTIME).as_posix()
            current.add(relative)
            destination = source/relative
            destination.parent.mkdir(parents=True,exist_ok=True)
            copy_source(path,destination)
    for relative in previous-current:
        (source/relative).unlink(missing_ok=True)
    listing.write_text(json.dumps(sorted(current)))

def fetch_thirdparty(root):
    """Third-party libraries from the upstream SDK revision, outside the repository."""
    upstream, destination = root/'runtime-upstream', root/'runtime-source/thirdparty'
    marker = destination/'.fm4-thirdparty.json'
    wanted = {'revision':SDK_REV,'vendored':THIRDPARTY_VENDORED,'submodules':THIRDPARTY_SUBMODULES}
    if marker.is_file() and json.loads(marker.read_text()) == json.loads(json.dumps(wanted)):
        return
    if not upstream.exists():
        run(['git','clone','--no-checkout',SDK_URL,upstream],log=root/'logs/upstream-clone.log')
    run(['git','-C',upstream,'checkout','--detach',SDK_REV],log=root/'logs/upstream-checkout.log')
    run(['git','-C',upstream,'submodule','update','--init','--recursive','--',
         *[f'thirdparty/{name}' for name in THIRDPARTY_SUBMODULES]],log=root/'logs/upstream-submodules.log')
    for name in THIRDPARTY_VENDORED+THIRDPARTY_SUBMODULES:
        shutil.rmtree(destination/name,ignore_errors=True)
        shutil.copytree(upstream/'thirdparty'/name,destination/name,symlinks=True)
    marker.write_text(json.dumps(wanted))

def compile_resolve_shaders(root, fxc):
    """FM4 resolve shaders and the scaled readback downscale shader -> DXBC headers; only
    when their sources changed."""
    source = root/'runtime-source'
    shaders = source/'src/graphics/shaders'
    shader_source = shaders/'fm4-resolve'
    downscale = shaders/'resolve_downscale.cs.hlsl'
    inputs = hashlib.sha256(b''.join(path.read_bytes() for path in
                                     sorted(shader_source.glob('*.xesl*'))+[downscale]))
    marker = source/'.fm4-resolve-shaders'
    bytecode = shaders/'bytecode/d3d12_5_1'
    outputs = [bytecode/f'{name}_cs.h' for name in RESOLVE_SHADERS]+[bytecode/'resolve_downscale_cs.h']
    if marker.is_file() and marker.read_text() == inputs.hexdigest() and all(path.is_file() for path in outputs):
        return
    flags = ['/T','cs_5_1','/O3','/Qstrip_reflect','/Qstrip_debug','/Qstrip_priv','/Gfp','/nologo']
    for name,output in zip(RESOLVE_SHADERS,outputs):
        run([fxc,'/D','SHADING_LANGUAGE_HLSL_XE=1',*flags,
             '/Fh',output,'/Vn',name+'_cs',shader_source/(name+'.cs.xesl')],log=root/f'logs/{name}.log')
    run([fxc,*flags,'/Fh',outputs[-1],'/Vn','resolve_downscale_cs',downscale],
        log=root/'logs/resolve_downscale.log')
    marker.write_text(inputs.hexdigest())

def build_runtime(root, fxc, jobs):
    source, build, install = root/'runtime-source', root/'runtime-build', root/'runtime-install'
    sync_runtime(root)
    fetch_thirdparty(root)
    compile_resolve_shaders(root, fxc)
    if not (build/'build.ninja').is_file():
        run(['cmake','-S',source,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++',
             '-DCMAKE_C_FLAGS=-march=x86-64-v2','-DCMAKE_CXX_FLAGS=-march=x86-64-v2',
             '-DREXGLUE_ENABLE_TRACY=OFF','-DREXGLUE_BUILD_TESTS=OFF','-DREXGLUE_USE_VULKAN=OFF',
             f'-DCMAKE_INSTALL_PREFIX={install}'],log=root/'logs/runtime-configure.log')
    run(['cmake','--build',build,'--parallel',jobs],log=root/'logs/runtime-build.log')
    run(['cmake','--install',build],log=root/'logs/runtime-install.log')

def setup(root, fxc, jobs):
    build_runtime(root, fxc, jobs)
    (root/'setup-complete.json').write_text(json.dumps({'thirdparty_revision':SDK_REV,'runtime':'work/runtime'}))

def generate(root, disc1, disc2, jobs):
    install, project = root/'runtime-install', root/'project'
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

def build_game(root,fxc,jobs,flavor=()):
    project, install, build = root/'project',root/'runtime-install',game_build_dir(root,flavor)
    if not (root/'local.json').is_file():
        raise RuntimeError('Run generate first.')
    state=json.loads((root/'local.json').read_text())
    validate_discs(state['disc1'],state['disc2'])
    # Edits in work/runtime are picked up here: sync, incremental build, install.
    build_runtime(root,fxc,jobs)
    suffix=build.name[len('game-build'):]
    # clang++ drives lld-link, so linker options go through -Xlinker.
    lto_jobs=f'-Xlinker /opt:lldltojobs={jobs}'
    linker=[f'-DCMAKE_EXE_LINKER_FLAGS={lto_jobs}',f'-DCMAKE_SHARED_LINKER_FLAGS={lto_jobs}'] \
        if 'thinlto' in flavor else []
    run(['cmake','-S',project,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_CXX_COMPILER=clang++',f'-DCMAKE_CXX_FLAGS={flavor_flags(root,flavor)}',*linker,
         f'-DCMAKE_PREFIX_PATH={install}',f'-DPython3_EXECUTABLE={sys.executable}',
         f'-DFM4_LOADED_IMAGE={root / "loaded-image.bin"}'],log=root/f'logs/game-configure{suffix}.log')
    started=time.time()
    run(['cmake','--build',build,'--parallel',jobs],log=root/f'logs/game-build{suffix}.log')
    print(f'Game build took {time.time()-started:.0f} s.')
    # Always stage the matching runtime/plugin, including incremental builds.
    for dll in (install/'bin').glob('*.dll'):
        shutil.copyfile(dll,build/dll.name)
    run([build/'fm4_instruction_tests.exe'],cwd=build,log=root/f'logs/instruction-tests{suffix}.log')
    artifacts = ['fm4_rex.exe','fm4_rex_XMediaFacade_default.dll','fm4_rex_SpeechFacade_default.dll','rexruntime.dll','rexgpu-xenos.dll']
    report = {'artifacts':{name:digest(build/name) for name in artifacts},'gameplay_verified':False}
    (root/f'build-result{suffix}.json').write_text(json.dumps(report,indent=2))
    print('Build and instruction tests passed. Run with Build.ps1 -Action run using the same -BuildRoot.')

def build_profiling(root,fxc,jobs):
    """game-build-prof: game-build with a runtime that has line tables and PDBs, for the
    command processor sampling profiler (REX_CP_PROFILE, e2e/README.txt). The runtime writes its
    outputs into <source>/out, so this compiles a mirror of runtime-source (runtime-source-prof)
    and leaves the default build alone."""
    game=root/'game-build'
    if not (game/'fm4_rex.exe').is_file():
        raise RuntimeError('Run build first.')
    sync_runtime(root)
    fetch_thirdparty(root)
    compile_resolve_shaders(root,fxc)
    source,mirror,build=root/'runtime-source',root/'runtime-source-prof',root/'runtime-build-prof'
    current=set()
    for path in source.rglob('*'):
        relative=path.relative_to(source)
        if relative.parts[0]=='out' or not path.is_file():
            continue
        current.add(relative)
        (mirror/relative).parent.mkdir(parents=True,exist_ok=True)
        copy_source(path,mirror/relative)
    for path in list(mirror.rglob('*')):
        relative=path.relative_to(mirror)
        if relative.parts[0]!='out' and path.is_file() and relative not in current:
            path.unlink()
    flags='-march=x86-64-v2 -gline-tables-only -gcodeview'
    if not (build/'build.ninja').is_file():
        run(['cmake','-S',mirror,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++',
             f'-DCMAKE_C_FLAGS={flags}',f'-DCMAKE_CXX_FLAGS={flags}',
             '-DCMAKE_SHARED_LINKER_FLAGS=-Xlinker /DEBUG','-DCMAKE_EXE_LINKER_FLAGS=-Xlinker /DEBUG',
             '-DREXGLUE_ENABLE_TRACY=OFF','-DREXGLUE_BUILD_TESTS=OFF','-DREXGLUE_USE_VULKAN=OFF'],
            log=root/'logs/runtime-configure-prof.log')
    run(['cmake','--build',build,'--parallel',jobs],log=root/'logs/runtime-build-prof.log')
    profiling=root/'game-build-prof'
    profiling.mkdir(exist_ok=True)
    for path in game.iterdir():
        if path.suffix in ('.exe','.dll'):
            shutil.copyfile(path,profiling/path.name)
    for name in ('rexgpu-xenos.dll','rexgpu-xenos.pdb','rexruntime.dll','rexruntime.pdb'):
        shutil.copyfile(mirror/'out/win-amd64'/name,profiling/name)
    print(f'Profiling build in {profiling}: e2e/e2e.py --game-build game-build-prof, then e2e/cp_profile.py.')

LOOK_KNOBS={'sharpen':'FM4_LOOK_SHARPEN','bloom':'FM4_LOOK_BLOOM','exposure':'FM4_LOOK_EXPOSURE',
            'contrast':'FM4_LOOK_CONTRAST','saturation':'FM4_LOOK_SATURATION',
            'vignette':'FM4_LOOK_VIGNETTE','motion_blur':'FM4_LOOK_MOTION_BLUR',
            'tonemap':'FM4_LOOK_TONEMAP'}
# Knobs that take a name instead of a number.
LOOK_NAMES={'tonemap':{'filmic':0,'gt':1,'aces':2,'neutral':3}}
# Named looks; a -Look value may mix them with knobs ('pc,sharpen=0.6': later items win).
# original runs without any shader mod.
LOOK_PRESETS={'pc':'tonemap=aces,saturation=1.1,bloom=0.7,sharpen=0.4',
              'aces':'tonemap=aces','gt':'tonemap=gt','neutral':'tonemap=neutral','original':''}
DEFAULT_LOOK='pc'

def look_cvars(root,look):
    """Cvars of the look mods (work/fm4-rex/look/fm4-look.hlsli): copies them to BuildRoot/look
    and passes the knobs of 'pc', 'sharpen=0.5,bloom=0.8,tonemap=aces' or both (knobs left out
    keep the original look). None of them ('original'): no mods at all."""
    knobs={}
    for item in filter(None,(part.strip() for part in look.split(','))):
        if '=' not in item:
            if item not in LOOK_PRESETS:
                raise RuntimeError(f'Unknown look {item!r}; use {", ".join(LOOK_PRESETS)} or knobs.')
            if item=='original':
                knobs.clear()
            knobs.update(part.split('=',1) for part in filter(None,LOOK_PRESETS[item].split(',')))
            continue
        name,_,value=item.partition('=')
        knobs[name]=value
    if not knobs:
        return {}
    defines=[]
    for name,value in knobs.items():
        if name not in LOOK_KNOBS:
            raise RuntimeError(f'Unknown look knob {name!r}; use {", ".join(LOOK_KNOBS)}.')
        names=LOOK_NAMES.get(name,{})
        if value in names:
            value=names[value]
        elif names:
            raise RuntimeError(f'Unknown {name} {value!r}; use {", ".join(names)}.')
        defines.append(f'{LOOK_KNOBS[name]}={float(value)!r}')
    directory=root/'look'
    if directory.is_dir():
        shutil.rmtree(directory)
    directory.mkdir()
    for path in (REPO/'work/fm4-rex/look').glob('*.hlsli'):
        shutil.copyfile(path,directory/path.name)
    return {'shader_mod_path':directory,'shader_mod_defines':';'.join(defines)}

def launch(root,single_tile=False,fixed_vblank=False,frame_rate=None,rov=False,resolution_scale=1,
           full_rate_mirror=False,look=None,shadow_workaround=False):
    state = json.loads((root/'local.json').read_text())
    disc1,disc2 = validate_discs(state['disc1'],state['disc2'])
    report=json.loads((root/'build-result.json').read_text())
    for name,expected in report['artifacts'].items():
        if digest(root/'game-build'/name)!=expected:
            raise RuntimeError(f'Build artifact changed: {name}. Rebuild first.')
    env = {k:v for k,v in os.environ.items() if not k.startswith('FM4_')}
    env.update(launch_env())
    if full_rate_mirror:
        env['FM4_HALF_RATE_MIRROR']='0'
    if shadow_workaround:
        # Skips the shadow map pass outside the cockpit in races, as split screen does: the
        # xenia-canary/game-patches#398 workaround, no longer needed on this runtime.
        env['FM4_SHADOW_WORKAROUND']='1'
    cvars=launch_cvars(root,disc1,root/'logs/runtime.log')
    if single_tile:
        # Races render 720p in one tile without MSAA (the title's UI tiling
        # scenario): no predicated tiling, FXAA on the swap instead.
        env['FM4_TILING_SCENARIO']='2'
        cvars['swap_post_effect']='fxaa'
    if fixed_vblank:
        cvars['vblank_vrr']='false'
    if rov:
        # The earlier pixel shader interlock path: GPU about 7 ms a race frame
        # instead of 2.5, for comparisons when the host render target path misbehaves.
        cvars.pop('edram_single_rt_no_wrap')
        cvars.update({'render_target_path_d3d12':'rov','rov_occlusion_async':'all'})
    if resolution_scale>1:
        # Render targets and resolved textures at N times 1280x720 on both axes;
        # CPU readbacks of resolves are downscaled.
        cvars['draw_resolution_scale_x']=cvars['draw_resolution_scale_y']=str(resolution_scale)
    if frame_rate:
        # The display refresh the title sees and the FrameRate of its race scenarios
        # (60). The guest vblank runs at 60 Hz, and at this rate only while a race
        # scenario sets the frame rate (fm4-vblank-rate.cpp): menus, countdown,
        # post-race, replays and photo mode keep their 60 Hz timing.
        # The title scales its time step, so the race clock stays real time.
        cvars['video_mode_refresh_rate']=str(frame_rate)
        env['FM4_FRAME_RATE']=str(frame_rate)
    # Shader mods on the game's post-processing (tone curve, sharpening, bloom, exposure,
    # contrast, saturation, vignette, motion blur): the pc look unless -Look says otherwise.
    cvars.update(look_cvars(root,DEFAULT_LOOK if look is None else look))
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

def launch_env():
    """Launcher environment, shared with e2e/e2e.py: the rear-view mirror rendered every other
    frame in every race (the title does that for 12 and 16 cars)."""
    return {'FM4_HALF_RATE_MIRROR':'1'}

def launch_cvars(root,disc1,log_file):
    """Launcher cvars, shared with e2e/e2e.py. New performance defaults go here once they pass e2e."""
    return {'game_data_root':disc1,'user_data_root':root/'user-data','cache_root':root/'cache',
            'allow_game_relative_writes':'false','headless':'false','fullscreen':'false',
            'render_target_path_d3d12':'rtv','edram_single_rt_no_wrap':'true','readback_resolve':'fast',
            'gpu_allow_invalid_fetch_constants':'true','async_shader_compilation':'false',
            'rov_occlusion_awaited_report_divisor':'2','rov_occlusion_async':'previous','vblank_vrr':'true',
            # 16x anisotropic filtering for every mipmapped linear texture: the title asks for
            # none on most and 2x-16x on a few, and the runtime default forces 4x.
            'anisotropic_override':'5',
            # FM4 writes guest memory only through watched pages (REX_STALE_PAGE_TRACE finds no
            # stale data), so CPU-uploaded pages need not be re-uploaded every frame; command lists
            # are recorded and executed, and resolve readbacks copied into guest memory, off the
            # command processor thread.
            'clear_memory_page_state':'false','d3d12_submit_thread':'true',
            'd3d12_readback_publish_thread':'true',
            'input_backend':'sdl','mnk_mode':'true','mnk_mouse':'false',
            'keybind_right_trigger':'W','keybind_left_trigger':'S',
            'keybind_lstick_left':'A','keybind_lstick_right':'D',
            'keybind_a':'Space','keybind_b':'Backspace','keybind_start':'Return',
            'keybind_back':'Tab','keybind_dpad_up':'Up','keybind_dpad_down':'Down',
            'keybind_dpad_left':'Left','keybind_dpad_right':'Right',
            'log_file':log_file}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['doctor','setup','generate','build','all','run','warmcache',
                                          'profbuild'])
    parser.add_argument('--build-root',type=Path,default=REPO/'work/portable-fm4')
    parser.add_argument('--disc1',type=Path)
    parser.add_argument('--disc2',type=Path)
    parser.add_argument('--jobs',type=int,default=4)
    parser.add_argument('--single-tile',action='store_true',
                        help='run: races without MSAA and predicated tiling, with FXAA (faster)')
    parser.add_argument('--fixed-vblank',action='store_true',
                        help='run: hold every swap to the fixed 60 Hz vblank grid (vblank_vrr=false)')
    parser.add_argument('--frame-rate',type=int,choices=range(30,241),metavar='30-240',
                        help='run: race frame rate above 60 (for example 120 or 240); menus, '
                             'replays and photo mode keep their own')
    parser.add_argument('--resolution-scale',type=int,choices=range(1,4),default=1,metavar='1-3',
                        help='run: render at 2x (2560x1440) or 3x (3840x2160) of 1280x720')
    parser.add_argument('--full-rate-mirror',action='store_true',
                        help='run: render the rear-view mirror every frame in every race')
    parser.add_argument('--shadow-workaround',action='store_true',
                        help='run: no shadow map outside the cockpit in races (the old Xenia workaround)')
    parser.add_argument('--rov',action='store_true',
                        help='run: the slower pixel shader interlock render target path instead of RTV')
    parser.add_argument('--look',
                        help='run: post-processing look, a preset ('+', '.join(LOOK_PRESETS)+
                             '; default '+DEFAULT_LOOK+') and/or knobs, for example '
                             '"pc,sharpen=0.6" (knobs: '+', '.join(LOOK_KNOBS)+
                             '; work/fm4-rex/look/fm4-look.hlsli)')
    parser.add_argument('--flavor',default='',help='opt-in compiler experiment for build: comma list of '+','.join(FLAVORS))
    args=parser.parse_args()
    root=args.build_root.resolve()
    # Never write private outputs into the tracked deliverables/source directories.
    if root in (REPO,*REPO.parents) or any(root==REPO/d or REPO/d in root.parents for d in ('outputs','work/fm4-rex','work/runtime')):
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
    check_layout(root)
    if args.action in ('setup','all'): setup(root,fxc,args.jobs)
    if args.action in ('generate','all'): generate(root,*discs,args.jobs)
    if args.action in ('build','all'): build_game(root,fxc,args.jobs,flavor)
    if args.action=='run': launch(root,args.single_tile,args.fixed_vblank,args.frame_rate,args.rov,
                                     args.resolution_scale,args.full_rate_mirror,args.look,
                                     args.shadow_workaround)
    if args.action=='warmcache': warm_cache(root)
    if args.action=='profbuild': build_profiling(root,fxc,args.jobs)

if __name__=='__main__':
    try: main()
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as error:
        sys.exit(f'ERROR: {error}')
