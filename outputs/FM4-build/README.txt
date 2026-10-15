FM4: setup, generate, build and run on Windows x64
================================================

This workflow rebuilds the current experimental FM4 integration from source.
It does not turn FM4 into a complete or bug-free native port. Existing rendering,
mode-coverage and performance limitations still apply.

See STATUS.txt for what has been verified, historical gameplay results, known
issues and remaining validation. verification.json records the latest clean
build/startup test; it does not certify full gameplay compatibility.

Supported input
--------------
Use your own extracted Japan/Asia Play Disc (media 633772A0). The three executable
SHA-256 hashes in pipeline.py identify the tested revision. Other releases,
modified executables and title updates are rejected before generation. Do not
rename another release to bypass the check. Disc 2 is optional for base-game
launch, but required to import its additional cars. Supply its extracted root,
containing content/0000000000000000/4d530910/00000002 and four content packages.
ISO extraction is outside this workflow. No game downloads are performed.

Prerequisites
-------------
- Windows x64, an x86-64-v2 compatible CPU and a Direct3D 12 capable GPU.
- Git for Windows with HTTPS access to GitHub (SDK and pinned submodules).
- Python 3.11+ x64, with a real python executable rather than the Store alias.
- Visual Studio 2022 / Build Tools: Desktop development with C++, MSVC x64 tools,
  Windows SDK (validated: 10.0.26100.0), CMake 3.25+ and Ninja.
- LLVM 20.x x64 (validated: 20.1.8); pass its install directory as -LLVMRoot.
- Allow substantial disk space and time for SDK dependencies, generated source,
  compilation and Disc 2 import. A separate drive can be used for -BuildRoot.

The script checks prerequisites and reports missing tools. It does not silently
install system software or change security settings. SDK source, dependencies
and build products are downloaded/generated under BuildRoot.

Commands (PowerShell, from the cloned repository root)
-----------------------------------------------------
Replace example directories below with your actual locations. All commands use
the same BuildRoot. Quotes support directories containing spaces.

  .\outputs\FM4-build\Build.ps1 -Action doctor -LLVMRoot 'C:\Program Files\LLVM' -Python 'C:\Python311\python.exe'

  .\outputs\FM4-build\Build.ps1 -Action all -Disc1 'D:\Games\FM4\Disc 1' -Disc2 'D:\Games\FM4\Disc 2' -BuildRoot 'D:\FM4-build' -LLVMRoot 'C:\Program Files\LLVM' -Python 'C:\Python311\python.exe' -Jobs 4

  .\outputs\FM4-build\Build.ps1 -Action run -BuildRoot 'D:\FM4-build' -LLVMRoot 'C:\Program Files\LLVM' -Python 'C:\Python311\python.exe'

If PowerShell blocks a script you have reviewed, you can launch it with a
process-only policy, for example:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\outputs\FM4-build\Build.ps1 -Action doctor -LLVMRoot 'C:\Program Files\LLVM' -Python 'C:\Python311\python.exe'

Stages can be run separately: setup, generate, build, run. generate requires
Disc1/Disc2 parameters; build and run read saved local paths from BuildRoot.
The all action performs setup + generate + build, but does not launch gameplay.
After source edits, repeat generate + build to refresh the private project.
If the pinned SDK revision/patch changes, choose a new BuildRoot.

Optional actions for performance work:
- run -FixedVblank returns to the fixed 60 Hz guest vblank grid. By default
  (vblank_vrr=true) a frame that misses a vblank gets its vblank as soon as it
  is ready instead of waiting for the next one. Quick Race on ROV, three runs
  each, interleaved: same average fps (52.5 vs 52.7) but frame p95 23 ms
  instead of 33 and p99 26 instead of 35; game speed unchanged.
- run -SingleTile renders races as one 720p tile without MSAA (the title's own
  UI tiling scenario, through the render scenario override) and applies FXAA
  on the swap. Quick Race on ROV: 58.6 fps instead of 54.3, frame p95 20 ms
  instead of 33, GPU 13 ms instead of 17.5. Edges are visibly more jagged
  than with 2x MSAA (thin cables break up); photo mode and split screen keep
  their own settings.
- run -FrameRate 120 lifts the title's 30/60 fps limits: the refresh rate it
  sees and its render scenario FrameRate become 120, and the title scales its
  time step (race clock against wall time 1.001). Quick Race: frame time
  median 8.4 ms (about 119 fps), p95 11.6 ms, p99 16.1 ms, every frame fully
  rendered; GPU about 5 ms and the command processor set the ceiling. 30-240
  accepted.
- run -Rov uses the earlier pixel shader interlock render target path
  (render_target_path_d3d12=rov, rov_occlusion_async=all) for comparisons:
  Quick Race 54.3 fps, frame p99 35 ms, GPU 17.5 ms a frame, too slow for
  -FrameRate above 60.
- warmcache drives the e2e Quick Race timeline once (no network) so the shader
  and pipeline cache under BuildRoot\cache is filled before playing.
- build -Flavor thinlto,v3,pgo-gen,pgo-use builds a compiler experiment into
  BuildRoot\game-build-<flavor> without touching the default game-build:
  ThinLTO, -march=x86-64-v3 (newer CPUs only), PGO instrumentation or PGO use
  of BuildRoot\pgo\fm4.profdata. Profiles stay local. Run one with
  e2e.py --game-build game-build-<flavor>. The default build keeps
  -march=x86-64-v2 without LTO/PGO.

What happens
------------
1. setup builds the project's runtime, a D3D12-only fork of the ReXGlue SDK kept
   in work/runtime. It mirrors work/runtime to BuildRootuntime-source, takes
   the third-party libraries from rexglue/rexglue-sdk at
   f5337cdc947ff6d4c4196737e2c807a48f2a1fc2 (its vendored directories and the
   13 submodules the D3D12 build needs; Vulkan, SPIR-V, Tracy and the tests are
   not fetched), compiles the licensed Xenia/ReXGlue resolve shaders with
   Windows SDK FXC, then builds and installs the Release runtime locally. The
   old individual SDK patches in FM4-analysis are historical.
2. generate validates the input executables. A small SDK loader reads the user's
   executable without running guest code. It creates a private image; bounded
   switch dispatches are evaluated from that image to regenerate switch labels.
   ReXGlue generates the main executable and both auxiliary module sources.
   Compiler-template transforms add instruction helpers and reject unresolved
   direct branches/labels. No existing generated tree is required.
3. build first mirrors work/runtime again and rebuilds the runtime
   incrementally, so runtime edits only need build. It then compiles the
   application and both modules, stages the matching runtime
   and GPU DLLs, runs arithmetic instruction tests and writes build-result.json.
4. run verifies the build artifacts and game revision, mounts Disc 1, imports
   Disc 2 packages if provided and starts the interactive game. Shader compilation
   is synchronous; the existing shadow workaround is enabled. Rendering uses the
   host render target path (render_target_path_d3d12=rtv with
   edram_single_rt_no_wrap=true): GPU about 10 ms a race frame instead of 17.5
   on the pixel shader interlock path, a stable 60 fps in Quick Race. Occlusion
   reports are published at once from the previous frame's samples
   (rov_occlusion_async=previous) and resolve readbacks do not wait for the GPU
   (readback_resolve=fast). A frame that misses a guest vblank gets it as soon
   as it is ready (vblank_vrr=true). The road sections this path used to leave
   undrawn came from the title's track streaming and are fixed for every path
   (STATUS.txt). Sun glare has not been calibrated on this path, and career,
   photo mode and interactive Autovista have not been tested on it; if
   rendering misbehaves, compare with run -Rov (render_target_path_d3d12=rov,
   rov_occlusion_async=all, sun query scaled by
   rov_occlusion_awaited_report_divisor=2). Controller input uses SDL. Keyboard defaults include W/S for pedals and A/D for steering.

All private output is under BuildRoot: project/generated, loaded-image.bin,
game-build, local.json, user-data, cache and logs. Input discs are read in place;
game-relative writes are disabled. A separate new profile is used. Nothing is
uploaded by setup/generate/build/run. BuildRoot defaults to work/portable-fm4,
ignored by this repository. Never commit or publish BuildRoot or a package of
its contents.

End-to-end tests
----------------
e2e/e2e.py launches a built BuildRoot, drives a diagnostic controller and
records captures, a per-second GPU stall trace and a summary. Its optional goal
mode navigates menus with the Jev model and sends OCR'd screen text to
TypeSafe; see e2e/README.txt before using it.

Tracked content / provenance
----------------------------
No original FM4 instruction bodies, shader binaries, assets or generated game
sources are stored here. TOML function addresses and switch recipes are factual
integration metadata (locations, lengths, registers and counts); switch labels
and instruction contents are read from the user's executable during generation.
guard-generated.py transforms generic compiler output templates locally; it
contains no stored original function bodies. rexglue.cmake is SDK-generated build
boilerplate, covered by the SDK notice. work/runtime contains SDK code (BSD-3,
with Xenia-derived portions), not game code. Shader sources have Xenia BSD notices and PROVENANCE.txt; see also
../FM4-source-control/third-party-notices/ReXGlue-LICENSE.txt.

The root allowlist excludes newly created files by default. Run the tracked
check-staged-content.py before committing future additions. The existing local
pre-commit hook is not installed automatically when cloning; content review is
still required. No additional project-wide license is assigned here.
