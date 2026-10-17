# FM4 Recompilation: Current Status

As of 17 October 2026. Latest code commit: `48ed669` (GTAO ambient occlusion, `-Look ao`). The dated chronology and earlier results are in [history](history.md).

This page describes the state now. Results from earlier builds came from different binaries, profiles and settings, so a past pass does not show that a feature was retested on the newer binary. Retested results are listed under [Verified on the latest build](#verified-on-the-latest-build).

## Summary

The project is an experimental build. On the latest binary, the e2e harness checks that Quick Race, photo mode and career races run through to rewards. It is not yet a full port: the full feature set, visuals and stability have not been verified.

- The default launcher (`look pc` with clearcoat and ambient occlusion) passed the smoke gate on 17 October 2026.
- The latest full matrix (240, 120 and 60 Hz) ran on 16 October 2026. The 17 October changes were checked with smoke gates, plus a three-run race-phase measurement for ambient occlusion.
- The clean build was verified on 13 October 2026 (commit `d1fa29a`). The project-owned runtime was verified on 14 October 2026.
- The clean build test covered startup and the intro only. It was not a race test, and it did not cover full mode coverage or performance.
- All verification ran on the same Windows computer used for development. No second computer has been tested.
- No original FM4 code or assets, recompiled code, game binaries, saves or captures are in Git. The game content is read from the user's local data.

### Status by area

| Area | State | Evidence |
|---|---|---|
| Boot, logos, intro video, main menu | Works | Clean build test (13 October 2026); menu reached about 30 seconds after start in the e2e boot |
| Quick Race at 60, 120 and 240 Hz | Verified on the latest binary | Matrix, 16 October 2026 |
| Photo mode: take, save, load in a new process | Verified on the latest binary | `photo` and `photo_view`, 16 October 2026 |
| Photo mode: overwrite, and photo with an empty shader cache | Not verified on the latest build | See [Current gaps](#current-gaps) |
| Career: World Tour, quit race, replay, continue | Verified on the latest binary | `career`, 16 October 2026 |
| Career race to rewards (AI driver) | Verified at 240 Hz | `career_finish` and `career_progress`, 16 October 2026 |
| Autovista | Worked on an earlier development build | Not covered by e2e |
| BMW M1 1981 (Disc 2) | Finished and continued on an earlier development build | Not covered by e2e |
| Look presets, clearcoat, ambient occlusion, display options | Checked with smoke gates | 17 October 2026 |
| Split screen, Rivals, Hot Lap, Kinect, multiplayer/online, audio, DLC beyond Disc 2 | Not validated | See [Current gaps](#current-gaps) |
| All cars and tracks, a full career season | Not validated | Status not verified; this does not mean they are broken |

## Verified on the latest build

### Test environment

- Same Windows computer as development. The GPU is an RTX 5070 Ti Laptop. The 240 Hz internal panel is driven through the iGPU (hybrid mode), and rendering runs on the RTX 5070 Ti Laptop.
- Gate tiers come from [outputs/FM4-build/e2e/matrix.py](../../outputs/FM4-build/e2e/matrix.py). Each run uses a snapshot of the build (`<BuildRoot>\snapshots\<name>`), so the game build can be rebuilt while a gate runs.

| Tier | Contents | Approximate time |
|---|---|---|
| `smoke` | `photo`, `photo_view` and `career` at 240 Hz, one run each | about 10 minutes |
| `perf` | `photo` at 240 Hz, three runs (race phase and photo mode) | - |
| `full` | `photo`, `photo_view` and `career` at 60 and 240 Hz, three runs each | about 1 hour |
| `career` | `career_finish` and `career_progress` at 240 Hz, one run | - |

The race phase of `photo` is 30 seconds of racing after 10 seconds of driving, with 12 cars. `career` is a World Tour race with quit, replay and continue.

### Automated gates, 17 October 2026

- **Default launcher** (`look pc` with clearcoat and ambient occlusion, commit `48ed669`): smoke gate passed.
- **Clearcoat** (smoke, 240 Hz, compared with a `qhigh` baseline): 3 of 3 runs. Race fps 214 in the `photo` race phase and 213 in `career`. Race-phase GPU 2.72-2.79 ms (2.83-2.88 ms for the baseline). Replay and photo mode 30 fps. No clear-gray frames. `compare` reports WARN on capture statistics only.
- **Ambient occlusion** (smoke 1x, 240 Hz, compared with a `look pc` baseline without AO): 3 of 3 runs. `photo_view` and `career` PASS, `photo` WARN on capture statistics. Replay and photo mode 30 fps. No clear-gray frames.
- **Ambient occlusion performance** (`perf`, race phase, three runs):

  | Render scale | Before AO | With AO | Limit |
  |---|---|---|---|
  | 1x | GPU 2.82 ms, 196 fps | GPU 3.01 ms, 200 fps | command processor |
  | 2x | GPU 5.32 ms, 174 fps | GPU 6.09 ms, 162 fps | GPU |

  At 2x with the launcher at 60 Hz: 60.0 fps.

- **Display and render scale** (smoke, high quality with clearcoat, 240 Hz): all cells pass. Replay and photo mode 30 fps. No clear-gray frames. `compare` reports FAIL on fps only, because this banding uses the `xbox` quality setting.

  | Render scale | Race fps | GPU per frame | Notes |
  |---|---|---|---|
  | 1x (1280x720) | 213 | 2.7 ms | |
  | 2x (2560x1440 or 2560x1600 display) | 177 | 5.4 ms | |
  | 3x (3840x2160) | 109 | 8.9 ms | |
  | 4x | 63-65 | 15 ms (p99 21 ms) | Replay GPU 25.6 ms. Only suitable for 60 fps with more GPU headroom than this computer has. |

- **Manual checks**:
  - Alt+Enter, tested with real key presses in Quick Race at a 1920x1080 window with 2x render: 1920x1080 to 2560x1600 to 1920x1080. OCR afterwards saw only the race HUD (timer running, no pause menu).
  - Presenter downscale: a 1920x1080 window capture of a frozen rewind scene at 2x matched the area-averaging simulation (mean difference 0.12-0.13 of 255), not bilinear (0.71-4.02). Bridge railings stay continuous.
  - The settings overlay is drawn by the presenter, so it is not in e2e captures.

### Full matrix, 16 October 2026

Binary after the dynamic refresh-rate change. Values are fps and p99 frame time. 240 and 120 Hz have three runs per cell; the 60 Hz column has one run (smoke).

| Scenario | 240 Hz | 120 Hz | 60 Hz |
|---|---|---|---|
| `quickrace` | 3/3, 224-235 fps, p99 5.3-6.2 ms | 3/3, 118-120 fps, p99 8.8-9.8 ms | 60.0 fps, p99 17.1 ms |
| `photo` | 3/3; race 220-228 fps, p99 5.4-5.8 ms; photo mode 30.0 fps | 3/3; race 117-118 fps, p99 9.6-10.1 ms; photo mode 30.0 fps | race 58.8 fps; photo mode 30.0 fps |
| `photo_view` | 3/3, menu 57 fps | 3/3, menu 57 fps | passed |
| `career` | 3/3; race 232 fps, p99 5.4 ms; replay 30.0 fps | 3/3; race 118-120 fps; replay 30.0 fps | race 60.0 fps; replay 30.0 fps |

Notes:

- No crashes and no clear-gray frames. The error keys match the baseline.
- `compare.py` reports FAIL only on total fps. Photo mode and replay now run at their original 30 fps; before this change they followed the global frame rate.
- The race phase of `photo` starts at the start of the race, so its p99 includes the start hitch.

### Race outcomes and features, 16 October 2026

- **Quick Race** at 60, 120 and 240 Hz: 60.0, 118-120 and 224-235 fps, as in the matrix above.
- **Photo mode**: a photo saved from photo mode loads in a new process. The `photo_view` run reads the photo back from `MY PHOTOS` and shows it in color.
- **Career**: World Tour, `QUIT RACE`, replay and `CONTINUE` back to the event all run.
- **Career race to rewards** (`career_finish`, 240 Hz): the AI driver wins Tsukuba Short Circuit (Ford Ka). The race ran at 219 fps, p99 6.9 ms. The `YOU PLACED` screen showed a total of +5,170 CR. Driver and Ford affinity reached level 2. The car reward was a 1994 Mazda MX-5 Miata, with a 2,000 CR bonus. `EVENT COMPLETE` appeared.
- **Progress after restart** (`career_progress`, a new process): the next event is TopGear Bowling Challenge. The Ford Ka is at LVL 2 with 15,740 CR.
- **AI driver without input** (developer test option): the AI driver wins Tsukuba Short Circuit (Ford Ka, 8 cars, 3 laps) with laps of 1:02.1 and 0:56.5. It finishes first, 22.6 seconds ahead, and the `YOU PLACED` screen appears. Pause, photo mode, photo save, `QUIT RACE`, replay and `CONTINUE` still work with this driver (smoke at 240 Hz, 3 of 3, with the first version of the option).

### Clean build, 13-14 October 2026

Source: [outputs/FM4-build/verification.json](../../outputs/FM4-build/verification.json) (date 13 October 2026), with additions from the runtime work on 14 October 2026.

- **Toolchain**: LLVM 20.1.8, Windows SDK 10.0.26100.0.
- **Setup**: passed. It takes the SDK at a pinned commit, applies the patch, builds the eight SDK resolve shaders, then builds and installs the SDK locally.
- **Generate and build**: passed from a clean export of the Git index, with no pre-existing generated game source. A regenerate and rebuild also passed. The main executable, the XMediaFacade and SpeechFacade DLLs, the runtime and the GPU plugin built as local output in BuildRoot.
- **Switch tables**: 271 switch tables were regenerated from the user's executable. They match the previous build.
- **Function registration sets**: match the previous build. Default 50,233, XMedia 3,212, Speech 6,715. This does not show that each function has run or that its behavior is correct.
- **Script unit tests**: 5 tests, 0 failures, with synthetic inputs.
- **Instruction tests**: 295,650 checks, 0 failures. These are 68,121 float16 checks and 227,529 vector checks.
- **Startup**: the Disc 2 packages were imported into a new isolated profile (4 packages). The logos and the intro car video rendered. The process stayed alive for 60 seconds and was closed at the tester's request with exit code 0.
- **Not covered**: `full_gameplay_verified` is `false`. No race was run in this test.
- **Repository contents**: no game code or assets, recompiled code, binaries, saves or captures. License files and integration metadata for the licensed SDK source are included.
- **Project-owned runtime (14 October 2026)**: `work/runtime`, a ReXGlue SDK fork with D3D12 only, replaces `sdk.patch` and `sdk-extra`. Full setup, generate and build pass in a new BuildRoot. The runtime source matches the old BuildRoot except for line endings. Setup rejects an old BuildRoot that has `sdk-source`.

## Current launcher defaults

These are the defaults of `Build.ps1 -Action run`. The shared cvars are in [outputs/FM4-build/pipeline.py](../../outputs/FM4-build/pipeline.py) (`launch_cvars`) and are also used by `e2e.py` and `matrix.py`. Full option reference: [running](../running.md).

| Setting | Default | Change with |
|---|---|---|
| Look | `pc` preset: `tonemap=aces`, `saturation=1.1`, `bloom=0.7`, `sharpen=0.4`, clearcoat 1, `ao=0.6` | `-Look <preset or knobs>`; `original` turns off all mods |
| Clearcoat | Setting A: `clearcoat=1`, `clearcoat_roughness` 0.3, `clearcoat_glint` 4. Chosen from three race A/B runs on 17 October 2026. Flakes are not used by any preset. | `-Look clearcoat=..,clearcoat_roughness=..,clearcoat_glint=..` |
| Ambient occlusion | GTAO on, strength 0.6, radius 0.75 m (presets `pc`, `aces`, `gt`, `neutral`) | `-Look ao=<strength>` |
| Render target path | RTV: `render_target_path_d3d12=rtv`, `edram_single_rt_no_wrap=true` | `-Rov` (ROV, with `rov_occlusion_async=all`) |
| Occlusion query | `rov_occlusion_async=previous`; `rov_occlusion_awaited_report_divisor=2` | `-Rov` |
| Vblank pacing | Variable (`vblank_vrr=true`) | `-FixedVblank` (fixed 60 Hz grid) |
| Frame rate | 60 in races; 30 for countdown, post-race, replay and photo mode | `-FrameRate <30-240>` (race only) |
| Resolve readback | `readback_resolve=fast`; readback publish on its own thread (`d3d12_readback_publish_thread=true`) | - |
| Submit thread | `d3d12_submit_thread=true` | - |
| Guest memory page state | `clear_memory_page_state=false` | - |
| Shader compilation | Synchronous (`async_shader_compilation=false`) | - |
| Anisotropic filtering | 16x (`anisotropic_override=5`) | - |
| Shadow workaround | Off | `-ShadowWorkaround` |
| Rear-view mirror | Every other frame (`FM4_HALF_RATE_MIRROR=1`) | `-FullRateMirror` (every frame) |
| Race quality | `high` (4x MSAA scenario, three tiles, SoftParticles and related settings) | `-Quality xbox\|high\|ultra` |
| Display | `native`: borderless fullscreen on the primary monitor | `-Display native\|720p\|1080p\|1440p\|4k`, `-Fullscreen`, `-Supersample`, `-ResolutionScale 1-4` |
| Render scale | Smallest whole multiple of 1280x720 that covers the 16:9 image on the display (720p 1x, 1080p, 1440p and 2560x1600 2x, 4K 3x), one step higher with `-Supersample` | as above |
| Presenter downscale | Area averaging (`present_area_downscale`) | - |
| Settings overlay | Hidden | `-Info`; F2 toggles it |
| Window mode | Toggled with Alt+Enter | - |

Notes on the defaults:

- The shadow workaround (`SkipShadowMapUnlessCockpit`, from xenia-canary/game-patches#398) is no longer a default as of 16 October 2026. Without it, the shadow map is rendered on every race camera. The captures checked show no box shadows. Cars now have self-shadowing and crisp shadows on the asphalt. The flag is read when the scenario is pushed, so it cannot be A/B-tested in the middle of a race.
- `-SingleTile` (sets `FM4_TILING_SCENARIO=2`, with FXAA) is not a default. Its trade-off is in [Open issues](#open-issues).
- The `rov_occlusion_awaited_report_divisor` cvar only affects ROV occlusion samples. The code confirms this in `RetireROVOcclusionSnapshots`, in [work/runtime/src/graphics/d3d12/command_processor.cpp](../../work/runtime/src/graphics/d3d12/command_processor.cpp).

## Current performance

These numbers are baselines, not targets.

- **Quick Race**: 60.0 fps (p99 17.1 ms), 118-120 fps at 120 Hz (p99 8.8-9.8 ms), and 224-235 fps at 240 Hz (p99 5.3-6.2 ms). All three are from the 16 October 2026 matrix.
- **240 Hz**: the race is limited by the command processor thread at 1x. GPU time is about 2.8 ms per frame without AO and 3.01 ms with AO.
- **2x with AO**: about 162 fps, limited by the GPU (6.09 ms per frame).
- **Track driving at 120 Hz**: the last track fps measurement (15 October 2026, before the final command processor changes that day) was 100-106 fps with 2,800-3,000 draws per frame. After those changes, command processor time per frame fell from 8.6-8.9 ms to 4.6 ms at 2,400-2,800 draws, but no track fps figure was recorded. The 16 October matrix measures Quick Race only, so 120 fps on track is not yet shown.
- **Refresh-rate scaling**: the race clock runs at 1.001 times the wall clock at high refresh rates. The game scales its time step, so it does not run faster.
- **Shader cache**: with an empty cache, synchronous shaders add a little hitch on load (about 9 frames over 100 ms per run, worst 400-640 ms). Race fps is not reduced.

## Known limitations

- **Supported input**: only three Japan/Asia XEX executables are supported. Their hashes are recorded in [outputs/FM4-build/pipeline.py](../../outputs/FM4-build/pipeline.py). Other regions or revisions, modified executables and title updates are rejected. The user must extract the ISO first.
- **Platform**: Windows x64 only, with a matching toolchain and GPU. Setup checks the prerequisites and does not install Visual Studio, Python or LLVM. A clone alone is not a playable game package. See [setup](../setup.md).
- **Save data**: saves and progress from historical builds loaded. Compatibility of all saves and profile migration between builds or computers is not guaranteed. The workflow uses its own local profile and does not copy old saves automatically.
- **Profile state in e2e**: on 14 October 2026, three runs with a new, empty profile did not reach a race in the harness. The game then shows the default car and different pre-race screens, which the harness did not recognize. With a copied profile and shader cache, 6 of 6 runs reached the race. The e2e scenarios therefore use `--profile copy`. See [e2e overview](../e2e/README.md).
- **Harness menu navigation**: the game ignores the first press after several screens open. The career menu and `GO RACE` also hide about 9 seconds after the last press (a real-time timer, not a frame count). The e2e scenarios press until OCR reads the expected screen, instead of counting presses.
- **Developer AI driver**: `engageaionstart` (rebuilt on 16 October 2026) is a test tool, not a player feature. `matrix.py` uses it by default unless `--human` is given. Direct launch into a race is not available, because the code that reads the `track`, `car` and `launch` options is missing from the retail build.
- **Shadows**: with the workaround off (the default), the shadow map is rendered on all race cameras. Its parity with the original Xbox has not been compared.
- **Stubs**: some Xbox and runtime service functions are still stubs. The synthetic instruction tests do not prove that the kernel, audio, timing, synchronization or shaders are correct.
- **Synthetic tests**: the 295,650 instruction checks and 5 script tests use synthetic data. They do not prove that the game behaves correctly.

## Open issues

- **RTV sun glare is not calibrated.** The sun occlusion query (the awaited report) returned 0 samples along the Quick Race route and on the grid, on both ROV and RTV. The four vertices of the sun sprite measured so far all sit at one position (a degenerate quad). The one time the query returned samples on ROV was when looking right at about lap time 00:31: 225-253 samples per frame after the divisor of 2. That position could not be repeated. The divisor of 2 was set empirically, not from measured Xenos behavior. Calibration needs a scene that can be repeated with the sun in view, for example a frame frozen with rewind (`Y`), and a trace of where the correct sprite corner positions come from.
- **Full-frame occlusion on RTV**: the full-frame query used for auto-exposure reports about 1.5 times the ROV sample count, because the host query counts every sample that passes. No visible effect was seen. The auto-exposure query was not changed.
- **RTV missing road pieces (mitigated, not fully explained)**: on some runs of the RTV path, road pieces (mostly on the start straight, 4-14 seconds after the start) were not drawn and showed the flat clear color (102,102,102). Kerbs and other objects were still drawn. The cause was found on 15 October 2026. The loader's sGPU heap could not place a 1.8 MB section because the heap was fragmented after a load spike. The loader then failed and discarded the request. The fix is a fallback: when the sGPU allocation fails, the section is retried from fGPU. Set `FM4_STREAMING_FALLBACK=0` to turn it off. See [work/fm4-rex/src/fm4-streaming-fixes.cpp](../../work/fm4-rex/src/fm4-streaming-fixes.cpp). After the fix, 20 of 20 runs with fixed vblank (the more frequent failure case) showed no missing road, and 5 of those runs used the fallback successfully. Three Quick Race runs with the current defaults also showed no clear-gray frames. The e2e harness detects the symptom through `clear_gray_frames`.
- **Look preset values**: the B4 step 3 note of 16 October 2026 says the preset values had not been chosen yet. The defaults in the table above were set later that day, and that note is superseded (see [history](history.md)).
- **Tiling quality versus speed**: on ROV (3 runs, 14 October 2026), `-SingleTile` raised fps from 54.3 to 58.6 and cut p95 from 32.9 ms to 19.5 ms. Without MSAA, edges are jagged. FXAA smooths some of them, but thin wires break up. Whether to make it the default has not been decided. The choice between quality and fps is left to the project owner.
- **Ambient occlusion scope**: AO is missing in the cockpit interior (the depth range is compressed there) and in the garage and showroom (no camera vertex-shader constants). See [GTAO design](../analysis/gtao-design.md).

## Current gaps

Everything still open, grouped by area. The long-term plan is the project plan (kept outside the repo).

### Display

- The RTV sun glare is not calibrated (see [Open issues](#open-issues)).
- AO is not in the cockpit interior, garage or showroom. See the "Later" section of [GTAO design](../analysis/gtao-design.md).
- Parity with the original Xbox (shadows, color, reflections, noise and object edges) has not been compared. The combinations of MSAA and render scale are not all verified.
- Not done: modern anti-aliasing on top of MSAA (SMAA or FXAA), HDR10 output, track lighting, materials other than car paint, and car flake paint. Flake cells in UV space are too small for race cameras and need an LOD model.
- Widescreen and ultrawide (the project plan (kept outside the repo), phase 4.1) need a FOV and projection hook, culling and HUD changes. The image is always 16:9, with bars on other screens.

### Performance

- At 240 Hz, the race is limited by the command processor (pacing, swap synchronization with the submit thread, and cost per draw). At 2x with AO, it is limited by the GPU (about 162 fps).
- The race clock does not run faster above 60 Hz (ratio 1.001). But lap time and handling are not yet compared between 60 and 240 fps (the project plan (kept outside the repo), phase 4.2).

### Gameplay coverage

- Not covered by e2e: photo overwrite, photo with an empty shader cache, Autovista, a Disc 2 car, and direct launch into a race.
- Not validated: all cars and tracks, one full career season, Rivals, Hot Lap, split screen, audio (XMA, 5.1 output, intro sync), Kinect, multiplayer and online play, and DLC beyond Disc 2.

### Settings and input

- The in-game PC settings overlay (saved to TOML), key rebinding and full controller support (rumble, triggers and wheel) are not done or verified (the project plan (kept outside the repo), phase 4.4).

### Stability and runtime

- A 2-hour soak test (memory, handles and VRAM) has never been run.
- The XAM and kernel stubs called across all scenarios have not been listed from `runtime.log`.
- Save compatibility between builds and profile migration between computers are not guaranteed.

### Platform and distribution

- The clone and the full workflow have not been run on a second Windows computer.
- AMD and Intel GPUs and lower-end CPUs have not been tested.
- Only the three Japan/Asia XEX files are supported. US and EU releases and title updates are not (the project plan (kept outside the repo), phase 5.5).
- There is no CI, no setup wizard, and no license and provenance review for a release. This includes the pending `XenonRecomp-FM4-fixes.patch`. See [sources and licensing](../sources-and-licensing.md).

## Next validation steps

The proposed order is:

1. **Calibrate the RTV sun glare.** Use a repeatable scene with the sun in view (for example a frame frozen with rewind, `Y`). Trace where the sun sprite's correct corner positions come from, then compare the RTV and ROV query results.
2. **Run the 2-hour soak test and the 60 vs 240 fps handling test.** The soak test watches memory, handles and VRAM. The handling test compares lap times and handling at both rates (the project plan (kept outside the repo), phase 4.2).
3. **Run the clean setup on a second Windows computer.** Run `setup`, `generate`, `build` and `run` from a clean clone.
4. **Extend e2e coverage.** Add photo overwrite, photo with an empty shader cache, Autovista, a Disc 2 car, and direct launch. Re-test photo overwrite and the empty shader cache on the latest build, because neither has been repeated since the 16 October changes.
5. **Inventory the stubs.** List every XAM and kernel stub called in `runtime.log` across all scenarios.
6. **Re-run the gate tiers after each change.** Use `smoke` for quick checks, `perf` for performance, `full` before committing a stage, and `career` for the rewards path. See the [e2e overview](../e2e/README.md) for the commands and gates.
7. **Decide the default tiling scenario.** The `-SingleTile` quality versus fps trade-off is for the project owner.

This page documents the status. It does not report that all further tests have been run, and it is not an instruction to run them automatically.

## Related documents

- [history](history.md): dated chronology and past results
- [setup](../setup.md), [running](../running.md)
- [e2e overview](../e2e/README.md), [traces](../e2e/traces.md), [experiments](../e2e/experiments.md)
- Analysis: [GTAO design](../analysis/gtao-design.md), [B4 shader research](../analysis/b4-shader-research.md), [visual style](../analysis/visual-style.md), [material atlas](../analysis/material-atlas.md)
- [sources and licensing](../sources-and-licensing.md)
- [Build script](../../outputs/FM4-build/Build.ps1), [verification record](../../outputs/FM4-build/verification.json), [e2e harness](../../outputs/FM4-build/e2e/e2e.py), [matrix](../../outputs/FM4-build/e2e/matrix.py)
- [Project overview](../../README.md), [agent guide](../../AGENTS.md)
