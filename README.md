# FM4-PC

Experimental recompilation of **Forza Motorsport 4 for Windows x64**, built on the ReXGlue SDK with runtime and GPU patches derived from ReXGlue and Xenia. The project provides integration code and a setup, generate, build, and run workflow that works from **game data you own**. ReXGlue translates the Xbox 360 executable into C++ locally, and the toolchain builds it into an executable and DLLs for PC. Xbox services and graphics are handled by the SDK runtime.

The repository does not include the game, original assets, recompiled code, or a ready-to-run binary. Cloning is not enough: prepare the game data and build prerequisites first.

## Status

- **Latest verification:** 16 October 2026, by the automated e2e harness on the current build. Setup, generate, and build from a clean copy of the Git files were checked on 13 and 14 October 2026.
- **Status:** Builds from a clean source tree and runs Quick Race, photo mode, and career races through to the reward on the latest build (tested automatically). It is not yet a full port with all features and verified stability.
- **Details:** [current status](docs/status/current.md) and [history](docs/status/history.md).

## Supported game version

- **Forza Motorsport 4 Japan/Asia**, Play Disc with media ID `633772A0`. The three executables are checked by SHA-256 in [`pipeline.py`](outputs/FM4-build/pipeline.py). Other regions, other revisions, modified executables, and title updates that do not match are rejected.
- **Disc 1 is required**, extracted, and it must contain all game assets.
- **Disc 2 is optional.** It is not needed for basic startup, but it is required to import its additional car content.
- You extract the discs yourself. The scripts do not download game data.

Full details are in the [setup guide](docs/setup.md).

## Quick start

Prerequisites (Windows x64, Git, Python 3.11+ x64, the Visual Studio 2022 C++ tools, and LLVM 20.x) are listed in [docs/setup.md](docs/setup.md). The scripts check them but do not install them.

Run the following in PowerShell. Replace the example paths with the locations on your computer.

### 1. Clone the repository

```powershell
git clone https://github.com/dfebri446/FM4-PC.git
Set-Location FM4-PC
```

### 2. Choose directories and check the toolchain

```powershell
$common = @{
    BuildRoot = 'D:\FM4-build'
    LLVMRoot  = 'C:\Program Files\LLVM'
    Python    = 'C:\Python311\python.exe'
    Jobs      = 4
}

.\outputs\FM4-build\Build.ps1 -Action doctor @common
```

`Build.ps1` sets up the Visual Studio x64 environment automatically. `LLVMRoot` is the LLVM installation directory, the one that contains a `bin` folder.

### 3. Set up, generate, and build

```powershell
$discs = @{
    Disc1 = 'D:\Games\FM4\Disc 1'
    Disc2 = 'D:\Games\FM4\Disc 2'
}

.\outputs\FM4-build\Build.ps1 -Action all @common @discs
```

Remove the `Disc2` entry if you want to use only the Disc 1 content. The `all` action runs three stages: setup (copies the runtime, fetches third-party libraries, and builds the runtime), generate (checks your executables and generates the C++ source from your discs), and build (compiles the executable, modules, and DLLs). Each stage can also be run on its own with `-Action setup`, `-Action generate`, or `-Action build`. Use the same `BuildRoot` for every stage. `all` does not start the game.

Generated source, binaries, and logs stay in `BuildRoot`. Do not commit or upload that directory.

## Running the game

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common
```

By default the game runs in full screen on the primary monitor (`-Display native`), at `high` quality, with the `pc` look. That look adds the project's post-processing, car paint clearcoat, and screen-space ambient occlusion. `-Look original` returns the unmodified FM4 look. Quick Race holds a steady 60 fps on the development computer. Frequently used options include `-FrameRate 120` (30 to 240), `-Quality xbox|high|ultra`, `-Supersample`, and `-Info`, which shows a settings overlay that F2 toggles. The launcher reads the disc locations saved during `generate`. Discs are read from their input locations, and the game directory is not written to. Old saves are not copied automatically. Run options, look knobs, keyboard and controller controls, and data directories are described in [docs/running.md](docs/running.md).

## Testing

The end-to-end harness, [`e2e.py`](outputs/FM4-build/e2e/e2e.py), runs the game from `BuildRoot` with a diagnostic controller, records captures and per-second performance traces, and writes `summary.json`. Its scenarios cover Quick Race, photo save and load, career races, and career races through to the reward. They follow fixed timelines and check the screen with Windows OCR. [`matrix.py`](outputs/FM4-build/e2e/matrix.py) runs them as tiered gates (smoke, perf, full, and career) on a copy of the build. The harness needs no network access. See the [e2e overview](docs/e2e/README.md) for usage, gates, and scenarios.

## Known limitations

- **Limited compatibility:** only Windows x64 and the Japan/Asia revision whose hashes are checked. Other regions and title updates are not supported.
- **Graphics not fully accurate:** the empirical sun occlusion query correction (factor 2) applies only to the ROV path (`-Rov`). On the default RTV path, sun glare is not calibrated yet. Noise, reflections, object edges, and combinations of MSAA and resolution scale need wider testing.
- **Synchronous shaders:** shader sync is on by default to avoid skipped draws that once caused black photos. An empty shader cache only adds load hitches, not race fps loss, but photos taken with an empty cache have not been retested.
- **Runtime incomplete:** some Xbox services are still stubs. The instruction tests do not prove that the kernel, audio, timing, or synchronization is correct.
- **Gameplay coverage incomplete:** not every car, track, and mode, long sessions, audio, Kinect, multiplayer, online, and DLC beyond Disc 2 have been validated. Save migration between builds or computers, other GPUs, and performance are not guaranteed. "Not verified" does not mean "broken".
- **Test driver:** the harness drives cars with FM4's own AI, through the rebuilt `engageaionstart` developer option. This is a test tool, not a player feature. Some FM4 menus ignore the first press, so the scenarios press until the expected screen is read.

The next validation priorities are a second computer, Disc 2 car races on the latest build, photo overwrite and photos with an empty shader cache, and Autovista. The full list is in [current status](docs/status/current.md).

## Documentation

| Document | Contents |
| --- | --- |
| [AGENTS.md](AGENTS.md) | Rules for AI agents working in this repository: language, documentation layout, and repository rules |
| [docs/setup.md](docs/setup.md) | Prerequisites, setup, generate, build, BuildRoot layout, and troubleshooting |
| [docs/running.md](docs/running.md) | Launcher run, options, look knobs, controls, and data directories |
| [docs/status/current.md](docs/status/current.md) | Current state, what is verified on the latest build, open gaps, and next validation |
| [docs/status/history.md](docs/status/history.md) | Dated history, past results, and dated test runs |
| [docs/e2e/README.md](docs/e2e/README.md) | End-to-end harness overview, usage, gates, and scenarios |
| [docs/e2e/architecture.md](docs/e2e/architecture.md) | How the e2e harness and its game-side hooks are built, platform and backend dependencies, and a porting checklist |
| [docs/e2e/traces.md](docs/e2e/traces.md) | Debug trace environment variables |
| [docs/e2e/experiments.md](docs/e2e/experiments.md) | A/B overrides and shader override diagnostics |
| [docs/analysis/gtao-design.md](docs/analysis/gtao-design.md) | Design of the GTAO (post_ao) ambient occlusion pass, from frame graph observations |
| [docs/analysis/b4-shader-research.md](docs/analysis/b4-shader-research.md) | Research on the editable shader path (HLSL through FXC, with overrides per hash) |
| [docs/analysis/visual-style.md](docs/analysis/visual-style.md) | Research on the origin of FM4's visual style |
| [docs/analysis/material-atlas.md](docs/analysis/material-atlas.md) | Summary of the material atlas: names and registers of the game's constant tables |
| [docs/sources-and-licensing.md](docs/sources-and-licensing.md) | Repository contents, attribution, licenses, and the pre-commit check |

## Licensing and attribution

The repository contains reviewed integration code and a ReXGlue SDK fork (BSD-3-Clause, with Xenia-derived portions), and it keeps the upstream notices. The project does not assign a blanket license of its own, and it provides no rights to FM4 game content. See [docs/sources-and-licensing.md](docs/sources-and-licensing.md).

## Pre-commit check

Before committing, check the staged content:

```powershell
& 'C:\Python311\python.exe' .\outputs\FM4-source-control\check-staged-content.py
```

The check guards against accidental additions. It does not replace a review of where the content came from.
