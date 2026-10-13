FM4: setup, generate, build and run on Windows x64
================================================

This workflow rebuilds the current experimental FM4 integration from source.
It does not turn FM4 into a complete or bug-free native port. Existing rendering,
mode-coverage and performance limitations still apply.

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

What happens
------------
1. setup clones rexglue/rexglue-sdk at f5337cdc947ff6d4c4196737e2c807a48f2a1fc2,
   initializes its pinned submodules, applies sdk.patch and sdk-extra, compiles
   the licensed Xenia/ReXGlue resolve shaders with Windows SDK FXC, then builds
   and installs a matching Release SDK locally. The old individual SDK patches
   in FM4-analysis are historical; do NOT apply them on top of sdk.patch.
2. generate validates the input executables. A small SDK loader reads the user's
   executable without running guest code. It creates a private image; bounded
   switch dispatches are evaluated from that image to regenerate switch labels.
   ReXGlue generates the main executable and both auxiliary module sources.
   Compiler-template transforms add instruction helpers and reject unresolved
   direct branches/labels. No existing generated tree is required.
3. build compiles the application and both modules, stages the matching runtime
   and GPU DLLs, runs arithmetic instruction tests and writes build-result.json.
4. run verifies the build artifacts and game revision, mounts Disc 1, imports
   Disc 2 packages if provided and starts the interactive game. Shader compilation
   is synchronous; the existing shadow workaround is enabled. Controller input
   uses SDL. Keyboard defaults include W/S for pedals and A/D for steering.

All private output is under BuildRoot: project/generated, loaded-image.bin,
game-build, local.json, user-data, cache and logs. Input discs are read in place;
game-relative writes are disabled. A separate new profile is used. Nothing is
uploaded. BuildRoot defaults to work/portable-fm4, ignored by this repository.
Never commit or publish BuildRoot or a package of its contents.

Tracked content / provenance
----------------------------
No original FM4 instruction bodies, shader binaries, assets or generated game
sources are stored here. TOML function addresses and switch recipes are factual
integration metadata (locations, lengths, registers and counts); switch labels
and instruction contents are read from the user's executable during generation.
guard-generated.py transforms generic compiler output templates locally; it
contains no stored original function bodies. rexglue.cmake is SDK-generated build
boilerplate, covered by the SDK notice. sdk.patch and sdk-extra contain SDK code,
not game code. Shader sources have Xenia BSD notices and PROVENANCE.txt; see also
../FM4-source-control/third-party-notices/ReXGlue-LICENSE.txt.

The root allowlist excludes newly created files by default. Run the tracked
check-staged-content.py before committing future additions. The existing local
pre-commit hook is not installed automatically when cloning; content review is
still required. No additional project-wide license is assigned here.
