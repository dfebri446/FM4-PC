# Setup and build

This guide covers building FM4-PC from source on Windows x64 with your own
Forza Motorsport 4 discs: prerequisites, the `doctor` check, the setup, generate
and build stages, optional build flavors, the BuildRoot layout, what is kept out
of the repository, and troubleshooting. To launch the built game and its options,
see [running.md](running.md).

FM4-PC is an experimental integration. Building it does not turn Forza Motorsport 4
into a complete or bug-free native port; existing rendering, mode-coverage and
performance limitations still apply. For what has been verified, the historical
gameplay results and the open gaps, see [status/current.md](status/current.md) and
[status/history.md](status/history.md). [verification.json](../outputs/FM4-build/verification.json)
records the latest clean build and startup test; it does not certify full gameplay
compatibility.

## Supported game version

- **Forza Motorsport 4 Japan/Asia**, Play Disc with media ID `633772A0`.
- The three executables are checked by SHA-256 hash (`XEX_HASHES` in
  [pipeline.py](../outputs/FM4-build/pipeline.py)) before generation. Other
  releases, modified executables and title updates are rejected. Do not rename
  another release to bypass the check.
- **Disc 1 is required.** It must be the extracted root of the Play Disc with all
  of its assets: the three executables, `Database.xmplr` and the `Media` folder.
  A folder that contains only the three XEX files is rejected.
- **Disc 2 is optional** for a base-game launch. It is required to import its
  additional cars. Supply its extracted root, which must contain the four content
  packages listed below.
- ISO extraction is outside this workflow. Extract the discs yourself. No game
  data is downloaded by any step.

Example layout of the input directories:

```text
D:\Games\FM4\
├── Disc 1\
│   ├── default.xex
│   ├── XMediaFacade_default.xex
│   ├── SpeechFacade_default.xex
│   ├── Database.xmplr
│   ├── Media\
│   └── ... all other Disc 1 files
└── Disc 2\
    └── content\0000000000000000\4d530910\00000002\
        ├── 4d53091000000001
        ├── 4d53091000000002
        ├── 4d53091000000003
        └── 4d53091000000004
```

Input discs are read in place; see [running.md](running.md#data-directories).

## Prerequisites

| Component | Requirement |
| --- | --- |
| System | Windows x64, a CPU compatible with x86-64-v2, a GPU that supports Direct3D 12 |
| Git | Git for Windows, with HTTPS access to GitHub (for the SDK and pinned submodules) |
| Python | **Python 3.11+ x64**. Use a real `python.exe`, not the Microsoft Store alias |
| Visual Studio | Visual Studio 2022 or Build Tools, workload **Desktop development with C++**, MSVC x64 tools |
| Windows SDK | Includes `fxc.exe`. Validated version: **10.0.26100.0** |
| Build tools | CMake **3.25+** and Ninja. Both can be installed through the Visual Studio Installer |
| LLVM | **LLVM 20.x x64**. Validated version: **20.1.8** |

Allow substantial disk space and time for the SDK source, its dependencies, the
generated source, compilation and the Disc 2 import. The build directory can be on
another drive.

The `doctor` action checks the prerequisites and reports what is missing: Windows
x64, 64-bit Python 3.11 or newer, `git`, `cmake`, `ninja`, `clang`, `clang++` and
`link` on the path, LLVM 20.x (from `clang++ --version`), CMake 3.25 or newer, and
`fxc.exe` in the Visual Studio developer environment. `Build.ps1` sets up the
Visual Studio x64 environment itself. The scripts do not silently install system
software or change security settings.

## Commands

Run the commands in PowerShell from the root of the cloned repository. Replace the
example paths with your own. Every stage must use the same `BuildRoot`. Quote paths
that contain spaces.

```powershell
git clone https://github.com/dfebri446/FM4-PC.git
Set-Location FM4-PC

$common = @{
    BuildRoot = 'D:\FM4-build'
    LLVMRoot  = 'C:\Program Files\LLVM'
    Python    = 'C:\Python311\python.exe'
    Jobs      = 4
}

.\outputs\FM4-build\Build.ps1 -Action doctor @common

$discs = @{
    Disc1 = 'D:\Games\FM4\Disc 1'
    Disc2 = 'D:\Games\FM4\Disc 2'
}

.\outputs\FM4-build\Build.ps1 -Action all @common @discs
```

Remove the `Disc2` entry if you only want to use the Disc 1 content. If PowerShell
blocks a script you have reviewed, launch it with a process-only policy:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\outputs\FM4-build\Build.ps1 -Action doctor -LLVMRoot 'C:\Program Files\LLVM' -Python 'C:\Python311\python.exe'
```

### Actions

| `-Action` | Purpose |
| --- | --- |
| `doctor` | Checks the prerequisites. Makes no changes. |
| `setup` | Stage 1: builds and installs the runtime. |
| `generate` | Stage 2: generates the project from the discs. Requires `-Disc1`, optionally `-Disc2`. |
| `build` | Stage 3: builds the game executables, modules and runtime. |
| `all` | `setup`, then `generate`, then `build`. Does not launch the game. |
| `run` | Launches the game. See [running.md](running.md). |
| `warmcache` | Fills the shader cache by running the e2e Quick Race timeline once. See [running.md](running.md). |
| `profbuild` | Builds `game-build-prof`, a copy with line tables and PDBs for profiling. Run `build` first. |

The stages can also run separately:

```powershell
.\outputs\FM4-build\Build.ps1 -Action setup @common
.\outputs\FM4-build\Build.ps1 -Action generate @common @discs
.\outputs\FM4-build\Build.ps1 -Action build @common
```

### Parameters

| Parameter | Meaning |
| --- | --- |
| `-BuildRoot` | Private build directory. Defaults to `work\portable-fm4` in the repository, which Git ignores. It must be a dedicated directory: not the repository root or one of its parents, and not inside `outputs`, `work\fm4-rex` or `work\runtime`. |
| `-LLVMRoot` | LLVM 20.x install directory that contains `bin`. Its `bin` folder is placed first on `PATH`. |
| `-Python` | Python 3.11+ x64 executable. Default: `python`. |
| `-Jobs` | Number of parallel jobs, 1 to 64. Default: 4. |
| `-Disc1` | Extracted Disc 1 root. Required by `generate` and `all`. |
| `-Disc2` | Extracted Disc 2 root. Optional. |
| `-Flavor` | Compiler experiment for `build`, for example `thinlto,v3`. See [Build flavors](#build-flavors). |

## Stages

### 1. Setup

`setup` builds the project's runtime, a D3D12-only fork of the ReXGlue SDK kept in
[work/runtime](../work/runtime). It:

- mirrors `work/runtime` to `BuildRoot\runtime-source`;
- takes the third-party libraries from `rexglue/rexglue-sdk` at commit
  `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`: the vendored directories and the 13
  submodules that the D3D12 build needs. Vulkan, SPIR-V, Tracy and the tests are not
  fetched;
- compiles the licensed Xenia/ReXGlue resolve shaders with the Windows SDK FXC;
- builds and installs the Release runtime locally.

The individual SDK patches in `outputs/FM4-analysis` are historical. They do not
need to be applied.

### 2. Generate

`generate` validates the input executables and creates the private project:

- A small SDK loader reads your `default.xex` without running any guest code and
  creates a private image (`loaded-image.bin`). Bounded switch dispatches are
  evaluated from that image to regenerate the switch labels.
- ReXGlue generates the main executable and both auxiliary module sources
  (`XMediaFacade_default` and `SpeechFacade_default`).
- Compiler-template transforms add instruction helpers and reject unresolved direct
  branches and labels.

No existing generated tree is required. The generated sources stay in
`BuildRoot\project`. The integration sources are copied from the tracked files
listed in [tracked-files.json](../outputs/FM4-source-control/tracked-files.json)
(`work/fm4-rex/src` and `work/fm4-rex/tests`), and nothing else from a developer
cache. The Disc paths are saved in `BuildRoot\local.json`.

### 3. Build

`build` first mirrors `work/runtime` again and rebuilds the runtime incrementally, so
runtime edits need only this stage. It then:

- compiles the application and both modules;
- stages the matching runtime and GPU DLLs;
- runs the arithmetic instruction tests and the post_ao and shader-mod CPU tests
  (no GPU needed; see [GTAO design](analysis/gtao-design.md));
- writes `build-result.json`, which lists the SHA-256 hashes of the artifacts and
  records `gameplay_verified: false`.

### Rebuilding after edits

- After editing `work/fm4-rex`, run `generate` and then `build` to refresh the
  private project.
- After editing `work/runtime`, `build` is enough.
- A `BuildRoot` created before the runtime fork (one that contains `sdk-source`) is
  rejected. Choose a new `BuildRoot`.

## Build flavors

`build -Flavor` builds an opt-in compiler experiment into
`BuildRoot\game-build-<flavor>` without touching the default `game-build`. The
flavor names are sorted and joined with hyphens, and the build writes
`build-result-<flavor>.json`.

| Flavor | Effect |
| --- | --- |
| `thinlto` | ThinLTO (`-flto=thin`). |
| `v3` | `-march=x86-64-v3`. Newer CPUs only. |
| `pgo-gen` | PGO instrumentation (`-fprofile-generate`). |
| `pgo-use` | PGO use of `BuildRoot\pgo\fm4.profdata`. Merge the `pgo-gen` `*.profraw` files with `llvm-profdata` first. |

Example: `-Flavor thinlto,v3` builds `game-build-thinlto-v3`. `pgo-gen` and
`pgo-use` cannot be combined in one build; the launcher rejects the request.

Profiles stay in `BuildRoot`. Run a flavor build with
`e2e.py --game-build game-build-<flavor>` (see [e2e/README.md](e2e/README.md)). The
default build keeps `-march=x86-64-v2` without LTO or PGO.

## BuildRoot layout

Every private output is written under `BuildRoot`: `project/generated`,
`loaded-image.bin`, `game-build`, `local.json`, `user-data`, `cache` and `logs`.
The default location is `work\portable-fm4`, which the repository ignores. The
layout is:

```text
FM4-build/
├── runtime-source/   # Copy of work/runtime + third-party libraries
├── runtime-upstream/ # Upstream SDK clone (only for third-party libraries)
├── runtime-build/    # Runtime build
├── runtime-install/  # Locally installed runtime
├── project/          # Integration source and private generated/
├── game-build/       # Executable, modules, runtime, GPU plugins
├── user-data/        # Profiles, saves, imported Disc 2 content
├── cache/            # Game and shader cache
├── logs/             # Logs for setup, generate, build and startup
├── e2e/              # e2e run output (captures, logs, summary.json)
├── snapshots/        # game-build copies used by the matrix.py gates
├── loaded-image.bin  # Private image of your executable
├── local.json        # Local Disc paths
└── build-result.json # Artifact hashes and build result
```

Nothing is uploaded by `setup`, `generate`, `build` or `run`. Never commit or
publish `BuildRoot`, or a package of its contents.

## What is and is not committed

The repository contains the integration code, the tooling, the synthetic tests,
the function location and boundary metadata, the switch recipes, and the runtime
fork in `work/runtime`. Specifically:

- The TOML function addresses and switch recipes are factual integration metadata
  (locations, lengths, registers and counts). Switch labels and instruction
  contents are read from your executable during `generate`.
- `guard-generated.py` transforms generic compiler output templates locally. It
  stores no original function bodies.
- `rexglue.cmake` is SDK-generated build boilerplate, covered by the SDK notice.
- `work/runtime` contains SDK code (BSD-3, with Xenia-derived portions), not game
  code.

The repository does not contain original FM4 instruction bodies, shader binaries,
game assets, generated game sources, recompilation output, binaries, saves or
captures. Licenses and notices are listed in
[sources-and-licensing.md](sources-and-licensing.md).

The root allowlist excludes newly created files by default. Before committing a
change, run the tracked content check:

```powershell
& 'C:\Python311\python.exe' .\outputs\FM4-source-control\check-staged-content.py
```

The check helps prevent accidental additions, but it does not replace reviewing
where a change came from. Adding source files requires updating
[tracked-files.json](../outputs/FM4-source-control/tracked-files.json) and reviewing
their contents. The existing local pre-commit hook is not installed automatically
when you clone the repository. No project-wide license is assigned by this
repository.

## Troubleshooting

- **Setup or build fails:** run `doctor` first. Check that `Python` points to a real
  Python executable and that `LLVMRoot` points to LLVM 20.x.
- **An executable is rejected** (`Unsupported or missing executable`): check the
  game version and that the extraction is complete. Renaming a file does not make
  another revision compatible.
- **Assets are not found:** `Disc1` must point to the complete extracted root, not
  to a folder that contains only the three XEX files.
- **Disc 2 package is missing** (`Missing Disc 2 content package`): check that the
  four packages are present under the `content` path shown in
  [Supported game version](#supported-game-version).
- **`Run setup first.` or `Run generate first.`:** the later stage needs the output
  of the earlier one in the same `BuildRoot`. Run `setup`, then `generate`, then
  `build`, or use `all`.
- **`BuildRoot must be a dedicated private directory`:** choose a directory outside
  the repository's `outputs`, `work\fm4-rex` and `work\runtime` folders.
- **Old layout rejected** (`old sdk-source/sdk.patch layout`): create a new
  `BuildRoot`.
- **Check the logs:** open the logs of the failed stage in `BuildRoot\logs`.
- **`Build artifact changed` at launch:** the artifacts no longer match
  `build-result.json`. Run `build` again. Do not mix runtime or GPU plugin files from
  different builds; `run` checks the artifact hashes.
- **PowerShell blocks the script:** use the process-only execution policy shown in
  [Commands](#commands).
