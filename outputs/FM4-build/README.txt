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
After edits in work/fm4-rex, repeat generate + build to refresh the private
project; edits in work/runtime only need build. A BuildRoot from before the
runtime fork (with sdk-source) is rejected; choose a new BuildRoot.

Optional actions for performance work:
- run -FixedVblank returns to the fixed 60 Hz guest vblank grid. By default
  (vblank_vrr=true) a frame that misses a vblank gets its vblank as soon as it
  is ready instead of waiting for the next one. Quick Race on ROV, three runs
  each, interleaved: same average fps (52.5 vs 52.7) but frame p95 23 ms
  instead of 33 and p99 26 instead of 35; game speed unchanged. Scenarios
  that render at 30 fps (countdown, post-race, replays, photo mode) keep the
  fixed grid either way: their render thread waits for the vblanks itself,
  and with vblank_vrr one long frame left every later vblank forced or late
  (the career replay at -ResolutionScale 2 stayed at 23 fps instead of 30).
- run -SingleTile renders races as one 720p tile without MSAA (the title's own
  UI tiling scenario, through the render scenario override) and applies FXAA
  on the swap. Quick Race on ROV: 58.6 fps instead of 54.3, frame p95 20 ms
  instead of 33, GPU 13 ms instead of 17.5. Edges are visibly more jagged
  than with 2x MSAA (thin cables break up); photo mode and split screen keep
  their own settings.
- run -FrameRate 120 lifts the 60 fps limit of races: the title sees a 120 Hz
  display and the FrameRate of its race render scenarios becomes 120, and the
  title scales its time step (race clock against wall time 1.001). The guest
  vblank stays at 60 Hz in menus, countdown, post-race, replays and photo mode
  (30 fps as on the Xbox 360) and runs at 120 Hz only while a race scenario
  sets the frame rate (fm4-vblank-rate.cpp). Quick Race, three runs: 118-120
  fps, frame p99 8.8-9.8 ms; every frame is fully rendered. At 240 races run
  at 220-235 fps (p99 5.3-6.2 ms), set by the command processor (about 4.3 ms
  a race frame; GPU about 2.2 ms). 30-240 accepted.
- run -ResolutionScale 2 renders at 2560x1440 and -ResolutionScale 3 at
  3840x2160 instead of 1280x720 (draw_resolution_scale_x/y; CPU readbacks of
  resolves are downscaled). Quick Race: 59.7 fps with GPU about 3.7 ms a
  frame at 2, 59.9 fps with GPU about 6.8 ms at 3, sharp HUD and scene.
  The smoke gate at 2 (240 Hz) passes: races 202-206 fps, photo mode 30 fps
  (GPU 7.5 ms), saving and loading a photo, and a career race with its
  replay at 30 fps (GPU 8.2 ms). The replay used to drop to 23.5 fps after a
  long frame: see -FixedVblank. 3 has not been through the gates.
- run -FullRateMirror renders the rear-view mirror every frame. By default
  every race renders it every other frame, as the title itself does for 12 and
  16 car races (HalfRateMirror in the race render scenarios; 1 and 8 car races
  render it every frame). Quick Race (12 cars) with the mirror every frame,
  measured before the upload fix below: 55.6 fps instead of 59.7, GPU 9.5 ms
  instead of 8.1 a frame; at -FrameRate 120 75 fps instead of 84.
- run -ShadowWorkaround adds SkipShadowMapUnlessCockpit to the race render
  scenarios (fm4-render-overrides.cpp, as the title's split screen does): the
  shadow map (a depth pass of 70-160 draws resolved to 512x512 and a 16_16
  4x MSAA pass) is rendered with the cockpit camera only. Without it the
  hood, bumper and chase cameras get the shadow map too (sharp car shadows
  with the wheels apart instead of soft ones) for about 0.2-0.4 ms of GPU a
  frame. Xenia needed it against square shadows; captures of every race
  camera on this runtime show none.
- run -Quality xbox|high|ultra sets the render quality of races through
  their render scenarios (FM4_RACE_SETTINGS, fm4-render-overrides.cpp; the
  N-Car, Autocross and Bowling scenarios, not split screen). xbox keeps the
  title's settings. high, the default, gives races what the title gives its
  replays and photo mode: 4x MSAA in three predicated tiles (TilingScenario
  0, the title's 4xMSAA scenario, instead of 1, 2xMSAA in two), soft
  particles, shadows of collidable objects, full detail wheels and car LOD
  transitions twice as far (30/50/100 m). It also undoes the 16 car race
  cuts: the cheaper car shader, no damage, an environment map never
  updated, half rate bloom and no collidables in the mirror. ultra also
  keeps every car at its top LOD (CarKeepLOD0, as 1 car races do) and
  renders the mirror every frame with up to 11 cars. At 240 Hz (12 car race
  phase of the photo scenario) the command processor sets the frame rate:
  xbox 219-224 fps in three runs (GPU 2.3 ms a frame); high 178-225 fps in
  four, 201 on average (GPU 2.8 ms), as the three tiles execute 4,500-8,300
  draws a frame depending on where the cars are (xbox about 5,200); ultra
  144.8 fps (median of three, GPU 3.1 ms, command processor busy 974 ms a
  second). All three hold 60 fps with room to spare; high, at 178 fps at
  worst, leaves room for 120 (not measured at 120 Hz), and ultra is meant
  for 60-120 Hz.
- run -Look <presets and knobs> sets the look mods (work/fm4-rex/look,
  original code hooked into the HLSL that the runtime generates from the
  game's post-processing and car body paint shaders; e2e/README.txt, "Shader
  overrides"). The default is the pc preset: tonemap=aces, saturation=1.1,
  bloom=0.7, sharpen=0.4, clearcoat=1. aces, gt and neutral set the tone
  curve alone with the clear coat, original runs without mods (the game's
  own look), and knobs after a preset change it, for example -Look
  "pc,sharpen=0.6" or "aces,clearcoat=0".
  Knobs: tonemap (filmic, gt, aces, neutral), sharpen, bloom, exposure,
  contrast, saturation, vignette, motion_blur; car paint (fm4-paint.hlsli):
  clearcoat, clearcoat_roughness (0.3), clearcoat_glint (4), flakes,
  flake_scale (1200). The six shaders compile with FXC when first drawn
  (80-940 ms each, the paint 2.4 s).
- run -Rov uses the earlier pixel shader interlock render target path
  (render_target_path_d3d12=rov, rov_occlusion_async=all) for comparisons:
  Quick Race 60.0 fps, GPU about 7.1 ms a frame (2.5 on the default path).
  Only this path halves the sun occlusion query
  (rov_occlusion_awaited_report_divisor=2).
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
   in work/runtime. It mirrors work/runtime to BuildRoot\runtime-source, takes
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
   is synchronous, every race renders the rear-view mirror every other frame
   (see -FullRateMirror), and every mipmapped linear texture is filtered with
   16x anisotropic filtering (anisotropic_override=5; the title asks for none
   on most and 2x-16x on a few, the runtime default is 4x). Races render the
   title's shadow map with every camera; -ShadowWorkaround skips it outside
   the cockpit (SkipShadowMapUnlessCockpit, as the title's split screen does),
   the xenia-canary/game-patches#398 workaround this runtime no longer needs.
   Races render at the high quality (see -Quality) and the post-processing
   uses the pc look (see -Look).
   Rendering uses the host render target path (render_target_path_d3d12=rtv with
   edram_single_rt_no_wrap=true): GPU about 2.5 ms a race frame instead of
   about 7 on the pixel shader interlock path, a stable 60 fps in Quick Race. Guest
   memory the CPU wrote is uploaded to the GPU once, not again every frame
   (clear_memory_page_state=false), command lists are recorded and executed
   on a thread of their own (d3d12_submit_thread=true), and so are the copies
   of resolve readbacks into guest memory (d3d12_readback_publish_thread=true).
   Occlusion
   reports are published at once from the previous frame's samples
   (rov_occlusion_async=previous) and resolve readbacks do not wait for the GPU
   (readback_resolve=fast). A frame that misses a guest vblank gets it as soon
   as it is ready (vblank_vrr=true). The road sections this path used to leave
   undrawn came from the title's track streaming and are fixed for every path
   (STATUS.txt). On this path the e2e scenarios pass: Quick Race, saving a
   photo and loading it in a new process and a career race with its replay at
   60, 120 and 240 Hz, and a career race to its rewards with the progress
   checked after a restart at 240 Hz. Sun glare
   has not been calibrated on it (the sun query divisor only applies to
   -Rov), and interactive Autovista has not been tested on it; if rendering
   misbehaves, compare with run -Rov (render_target_path_d3d12=rov,
   rov_occlusion_async=all, sun query scaled by
   rov_occlusion_awaited_report_divisor=2). Controller input uses SDL.
   Keyboard defaults include W/S for pedals and A/D for steering.

All private output is under BuildRoot: project/generated, loaded-image.bin,
game-build, local.json, user-data, cache and logs. Input discs are read in place;
game-relative writes are disabled. A separate new profile is used. Nothing is
uploaded by setup/generate/build/run. BuildRoot defaults to work/portable-fm4,
ignored by this repository. Never commit or publish BuildRoot or a package of
its contents.

End-to-end tests
----------------
e2e/e2e.py launches a built BuildRoot, drives a diagnostic controller and
records captures, a per-second GPU stall trace and a summary. Its scenarios
(Quick Race, photo save and load, career race, career race to its rewards and
the progress after a restart) run on fixed timelines with OCR screen checks and
no network. e2e/matrix.py runs them as gates (smoke, perf, full, career tiers)
on a snapshot of game-build and compares them with an older gate. In the gates
the title's own AI drives the player's car: FM4's developer option
engageaionstart, rebuilt in work/fm4-rex (FM4_GAME_CMDLINE feeds developer
options to the title). The optional goal mode navigates menus with the Jev
model and sends OCR'd screen text to TypeSafe; see e2e/README.txt before using
it.

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
