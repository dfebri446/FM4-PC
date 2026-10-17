# Status history

This file is the dated record of the FM4 recompilation: the clean-build verification, the historical gameplay results, the dated end-to-end (e2e) results, and the settings and decisions that were later superseded. For the present state, the open gaps and the next validation steps, read [current status](current.md). Detailed performance measurements (frame rates, GPU and command processor timings) are in [performance history](history-performance.md).

How to read the dates:

- Historical gameplay results (13 October 2026 and earlier) were produced with earlier binaries, profiles and settings. They do not show that a feature works on the current binary. Features re-tested on the latest binary are listed in the e2e sections below.
- Where a later entry changes an earlier one, the later entry says so. Section 5 lists the changes in one table, and section 6 lists the corrections to earlier entries.
- The local analysis reports named below (for example `automated-race-candidate-results.json`) are kept on the development computer. They are not part of the repository and are not needed to build from a clone.
- Current settings are in [running](../running.md). The e2e harness, its gates and scenarios are in [e2e harness](../e2e/README.md). Trace environment variables are in [traces](../e2e/traces.md), and A/B overrides and shader override diagnostics are in [experiments](../e2e/experiments.md).
- Commit hashes are kept as they were recorded: `d1fa29a` (13 October verification), `bce784d` (regression binary of 16 October), `c6420dc` (the short-lived RTV default of 14 October), `f5337cdc` (ReXGlue resolve commit) and `48ed669` (GTAO and `-Look ao`, the latest code commit named in the status log).

## Timeline

| Date | Milestone |
|---|---|
| 13 October 2026 | Clean-build verification at `d1fa29a`. Sun and glare judged reasonable on the ROV path after `--rov_occlusion_awaited_report_divisor=2`. First Quick Race A/B results on the occlusion and readback settings. |
| 14 October 2026 | The project-owned runtime (`work/runtime`) replaces `sdk.patch` and `sdk-extra`. RTV is the launcher default for a short time, then reverted to ROV. The missing road segments on RTV are investigated. `vblank_vrr` becomes the default. |
| 15 October 2026 | The streaming fix (sGPU fallback) resolves the missing road segments, and RTV is the launcher default again. Command processor work: EDRAM transfers, upload removal, event-based wait and readback fixes. |
| 16 October 2026 | Photo, photo_view and career e2e scenarios, the regression and 240 Hz matrices, the dynamic vblank rate, the rebuilt developer AI driver and career race to rewards, the shader override and look knobs, the shadow workaround no longer default, and 16x anisotropic filtering. |
| 17 October 2026 | Clearcoat on the car paint, display and render scale options, the settings overlay, Alt+Enter, and GTAO ambient occlusion. The default launcher (look `pc` with clearcoat and AO) passes the smoke gate. |

## 1. Clean-build verification (13-14 October 2026)

### 13 October 2026, commit `d1fa29a`

- Setup fetches the SDK at a pinned commit, applies the runtime and GPU patches, builds the eight SDK resolve shaders, and then builds and installs the SDK locally.
- Generate and build ran from a clean copy of the Git-tracked files, without the old recompilation folder. Generating and building again also succeeded.
- The main executable, the XMediaFacade and SpeechFacade DLLs, the runtime and the GPU plugin were built. All of this output stays local to BuildRoot.
- 271 switch tables were rebuilt from the user's executable and match the earlier configuration. The function registration sets match too: main 50,233, XMedia 3,212, Speech 6,715. This does not show that every function has run or that its behaviour is correct.
- Five script tests with synthetic data passed. The instruction check passed 295,650 of 295,650: 68,121 float16 checks and 227,529 vector checks.
- All four Disc 2 packages were imported into a new test profile.
- Startup shows the logo and the car intro video. The process stayed alive for 60 seconds and was closed at the tester's request with exit code 0. This is a startup test, not a race test.
- No FM4 original code or assets, recompiled code, game binary, save file or capture was committed to Git. Integration metadata and licensed SDK source are included; game content is read from the user's local data.
- The tests ran on the same Windows computer that was used for development, not on a second physical computer. Evidence: [verification.json](../../outputs/FM4-build/verification.json).
- Sunlight (visual test, 13 October 2026): the launcher halves only the awaited sun occlusion report, with `--rov_occlusion_awaited_report_divisor=2`. Whole-frame queries used for auto-exposure are not changed. Without the divisor, the sun query reported about twice the samples the game expects, and the glare was too bright. With the divisor, the sun appeared and the glare was reasonable on the rear and cockpit cameras. The factor 2 was set empirically; it was not measured from the Xenos hardware. The divisor only applies on the ROV path (`-Rov`). The default RTV path uses a host query, and it was not calibrated at that time.

### 14 October 2026, runtime

- The project runtime `work/runtime` (a fork of the ReXGlue SDK, D3D12 only) replaces `sdk.patch` and `sdk-extra`. A full setup, generate and build in a new BuildRoot passed. The runtime source is identical to the old BuildRoot except for line endings. Setup rejects an old BuildRoot that uses `sdk-source`.
- From 14 October, setup builds the project runtime (`work/runtime`) with third-party libraries from the upstream SDK.
- First quick race with camera cycling on the new runtime: three runs failed navigation. The profile was empty (default car and other pre-race screens), the harness did not recognize the race, and the game kept running. With the profile and shader cache copied, 6 of 6 runs reached the race with 0 frames of lost road.
- Alternating old and new runtime: fps values are in [performance history](history-performance.md).
- Sunlight on RTV (14 October 2026): the sun query (the awaited report) gave 0 samples along the quick race route and from the grid. It was seen once on ROV, when turning right at about lap time 00:31 (225-253 samples per frame after the divisor of 2), but the position of the auto driver could not be repeated. The RTV sun calibration needs a deterministic scene with the sun in view, for example a camera scenario.

## 2. Historical gameplay results (earlier development builds)

These results come from earlier development builds, with different binaries, profiles and settings. They are not re-tested unless a later section says so. The summary is taken from the local FM4-analysis reports, among others `automated-race-candidate-results.json`, `input-menu-recovery-results.json`, `automatic-disc2-speedway-results.json`, `automatic-autovista-results.json`, `photo-sync-clean-results.json`, `career-sync-rewind-results.json` and `resolve-layout-fix-results.json`. These reports and their captures are local.

- Menus and controls: the main menu, Free Play and Quick Race, the career menu and World Tour were opened. Throttle, brake, reverse, steering, camera switching and Pause responded.
- Career, Road Atlanta with the Ford Ka: finished. Continue worked, and the Amateur Division 3/5 progress and a balance of 1,775 CR were read after a restart.
- BMW M1 1981 from Disc 2: driven to the finish and Continue. An automated three-lap run at Sunset Peninsula Speedway also completed and returned to track selection. Race times include development pauses, so they are not performance benchmarks.
- Autovista Ferrari 458 Italia: viewing the car, opening the panel, entering and leaving the cockpit, changing the paint, opening photo mode and returning to the menu all worked.
- After the delete-on-close fix and with synchronous shader use, the first photo was saved, overwritten and loaded in colour in two new process sessions. The shader cache was not emptied during that test.
- Career at Tsukuba with synchronous shaders could be loaded. Rewind, Rewind Further, Resume, throttle and steering, and Pause were tested. That session did not finish the race.
- Texture integer and resolve fixes and the MSAA layout fix produced the real car shadow in certain tests, and removed a random pixel pattern caused by a mismatched layout. Full visual equivalence was not shown.
- Historical saves and progress loaded. Compatibility of all saves was not guaranteed.
- The automatic driver used during development could hit barriers, lose the minimap and fail to recover its heading, and the e2e harness only held the throttle. From 16 October the e2e races use the title's own AI driver (section 4). That AI driver is a test tool, not a player feature.

## 3. Investigation: missing road segments on the RTV path (14-15 October 2026)

On the RTV render target path, certain road pieces were sometimes not drawn. Most of the time they were on the start straight, about 4 to 14 seconds after the start. They showed as a flat clear colour, (102,102,102), while kerbs and other objects remained. On 14 October this was measured at about 6 of 28 RTV race runs, with every occlusion mode (all, previous, off) and every camera. It never happened in 75 ROV runs. The e2e harness detects these frames with `clear_gray_frames`.

### 14 October 2026: first analysis (superseded on 15 October)

- Ruled out at the time: timing and occlusion values (FM4's per-frame occlusion query contains one sun sprite per tile, with the same values on ROV and RTV), the depth EQUAL test (FM4 does not use it), and the pipeline cache. The cause was unknown.
- The ROV path (`--render_target_path_d3d12=rov --rov_occlusion_async=all`) was not affected.
- Classification from `REX_DRAW_LIST_TRACE`: in broken frames the game still sent every draw that was present in the normal frames around them, and the host issued them (outcome ok). The lines and arrows above the road were still drawn on top of the clear colour. The conclusion at that time was that this was not game culling, but lost draw colour in the RTV render target path. This was superseded on 15 October.
- Timing: with `vblank_vrr`, then the default, 0 of 25 RTV runs were affected (13 with trace, 12 without; 53-60 fps). With a fixed vblank, 8 of 36 runs were affected (2 of 8 now, 6 of 28 before), with p about 0.01. The cause was still hidden, not fixed, and RTV remained an option.
- Visual comparison of RTV and ROV (14 October): the main menu is identical (average difference 0.01 level per channel). The static Autovista Ferrari 458 is equivalent: 75% of pixels are identical, 24% differ by one level, and the maximum difference is 5 levels. The luminance distribution and saturated pixels of RTV race captures are within the ROV range. The earlier impression that RTV was darker came from races that were not deterministic.
- The whole-frame occlusion query on RTV reports about 1.5 times the ROV samples (the host query counts every sample that passes), with no visible effect.

### 15 October 2026: re-diagnosis

- The 14 October conclusion (lost colour in the render target path) was wrong. The frame graph of each frame shows no ownership transfer in the scene, and the claim range covers the full 1024 tiles.
- Per-draw pipeline statistics (`REX_DRAW_STATS`) and a census of draw packets in the command processor (`REX_BIN_TRACE`) show that in the start area the game sends far fewer road draws. Pairs 630d5385/8c08d24c have 16-25 packets per frame, against 66-71 normally. Pair 645fd4b2/89964d0 has 0, against 36-46. Pair ff6529eb/e3af5b04 has 5-7, against 16-27. No draw was skipped by bin predication, and everything that was sent was drawn correctly. A vertex shader run on the CPU with the same guest state gave the same clipping result as the GPU.
- Also excluded: CPU writes made after the command processor used the data, stale shared memory pages in the requested range, valid pages that were not watched, vertex pages written by the GPU, resolve readbacks that overwrite guest data, and speed (the bug also appears at 16 fps).
- The cause found so far is in the streaming track. `REX_FILE_READ_TRACE` shows that in normal runs the streamer reads one item, `Media\tracks\Alps\bin.zip`, from offset 0x4725000 to 0x4797000, in 32 KB chunks through two alternating buffers. In broken runs only the first 64 KB are read; then the streamer moves to the next item, and the rest of the item is never read. A 1 MB read at 0x4725000 also happens when no race starts. So the game itself leaves the track data of the start area unloaded; the renderer is not involved.
- Final cause (15 October, `REX_IO_TRACE` and the `FM4_STREAM_TRACE` loader hook): not I/O. The streamer's reads use overlapped events without APCs, and all of them completed and were decompressed without error. Each item request consists of a `.dat` section (0x110 bytes) and a `.gpu` section (1.5-1.8 MB, 4 KB aligned). Allocating the `.gpu` section from the game memory manager's "sGPU" heap failed. The loader marked the request as failed (flag +84, bit 4) and dropped it. When the sGPU allocation failed, 101.6 MB of 119.5 MB were in use (peak 119.3 MB). About 18 MB were free, but no block of 1.8 MB was available, because the first-fit allocator was fragmented by a spike during loading. The load order varied with host thread scheduling. The other GPU heaps in the race pool have fallbacks: nGPU (41 MB, full) and pGPU (front end) fall back to fGPU (118 MB, 33 MB free at that time). sGPU had no fallback. Retrying the allocation for one second did not help, because the memory did not come back.
- Fix (`work/fm4-rex/src/fm4-streaming-fixes.cpp`): when a loader section allocation in sGPU fails, sGPU gets the fGPU fallback (field +236 = 0x22, as nGPU has) and the allocation is retried once. `FM4_STREAMING_FALLBACK=0` disables it.
- Validation with RTV and a fixed vblank, the condition under which the bug was most frequent: 20 of 20 runs had no lost-road frames. Five runs used the fallback, and all of them succeeded (section 0x1C15A4 bytes). Frame rate was 59.7-60.0 fps, with no crash. Before the fix, about 15-25% of runs were affected.
- Consequence: RTV became the launcher and e2e default again on 15 October (section 5).

## 4. E2E gameplay results (16-17 October 2026)

### 16 October 2026: verified automatically on the latest binary

These runs use the default launcher (RTV, with the profile copied for each run):

- Quick Race at 60, 120 and 240 Hz: 60.0 / 118-120 / 224-235 fps.
- Save a photo from photo mode, then load that photo in a new process.
- Career World Tour: QUIT RACE, replay and CONTINUE back to the event.
- Career race to the finish with the rebuilt AI driver (`engageaionstart`, below): prize, driver level and EVENT COMPLETE, and progress read after a restart (240 Hz).

### 16 October 2026: scenarios and the keyboard bug fix

- `e2e.py` gained three scenarios with OCR screen checks and no network access (see [e2e harness](../e2e/README.md)):
  - `photo`: Quick Race, 20 seconds of racing, pause, PHOTO MODE, take a photo, save it as a new file, and check SAVE SUCCESSFUL. The gate later used a race phase of 30 seconds after 10 seconds of driving, which replaced the quickrace scenario in the gate.
  - `photo_view`: a new process opens Career > MY PROFILE > MEDIA > MY PHOTOS > LOAD and shows the photo from the photo run.
  - `career`: GO RACE > WORLD TOUR, the next event on the BuildRoot profile (Tsukuba Short Circuit, Ford Ka), 40 seconds of racing, QUIT RACE, 30 seconds of replay, and CONTINUE back to the event.
- Runs use a copy of the profile (`--profile copy`; the Disc 2 content packages are linked), so the BuildRoot profile does not change. `summary.json` records the measured phases (race, photo, replay) and any failed step. `compare.py` compares the phases.
- Runtime bug found and fixed: in headless mode, `XamShowKeyboardUI` returned its default text with the byte order reversed. `MappedPtr::value()` is a big-endian guest string, which was byte-swapped again when copied. FM4 names the photo folder from that text, so "Forza" became CJK characters and then "?????" (0x3F). `NtCreateFile` rejected that path without a log entry, and the game displayed FILE OPEN FAILED. The text is now loaded in host order before the copy, and `NtCreateFile` logs rejected paths. The ImGui keyboard dialog for players was already correct. `e2e.py` always uses `headless=true`, so the XAM prompt answers with its default; rendering is not affected.
- As of 16 October, the e2e harness did not yet cover: photo overwrite, photo with an empty shader cache, Autovista, the Disc 2 cars, and direct launch to a race. The open gaps are tracked in [current status](current.md).

### 16 October 2026: regression matrix, default launcher

Setup: RTV, `readback_resolve=fast`, submit thread and readback publish thread enabled; binary from `bce784d` plus the fixes above; 3 runs per cell.

| Scenario | 60 Hz | 120 Hz |
|---|---|---|
| quickrace | 3/3, 60.0 fps, p99 17.2 ms | 3/3, 119.9 fps, p99 8.8 ms |
| photo | 3/3; race 59.8 fps, p99 17.3; photo mode 30.0 fps | 3/3; race 118.0 fps, p99 16.6; photo mode 55.5 fps |
| photo_view | 3/3, photo shown in colour | 3/3 |
| career | 3/3; race 60.0 fps, p99 17.1; replay 30.0 fps | 3/3; race 119.7 fps, p99 9.0; replay 60.0 fps |

- Quick Race was compared with the phase B3 validation (v2-60 and v2-120): PASS, with equal fps and p99. The higher command processor busy time at 60 Hz comes only from different draw counts per run (cars facing a wall or the track); the cost per draw is the same. The race phase of photo starts at the start, so its p99 includes the start hitch.
- No crash, no clear-colour frames, and the error keys match the baseline.

### 16 October 2026: 240 Hz matrix, before the dynamic vblank fix

Same binary and default launcher, 3 runs per cell, with `--arg=--video_mode_refresh_rate=240 --env FM4_FRAME_RATE=240`. Quick Race used `--tail 40 --measure 30`, as in the 60 and 120 Hz baseline. The 240 Hz panel is the internal screen, driven through the iGPU (hybrid mode), with rendering on the RTX 5070 Ti Laptop.

- quickrace, 3/3: 223.6 fps (runs 217-230), p99 5.7-6.2 ms, no hitch, command processor busy 657-888 ms/s, GPU 2.0-2.2 ms.
- photo, 3/3: race 221-223 fps, p99 6.8-8.3 ms; photo mode 90-112 fps.
- photo_view, 3/3: photo shown in colour; menu 229 fps.
- career, 2/3: race 230 fps, p99 5.5 ms; replay 111-113 fps. Run 3 failed at GO RACE > WORLD TOUR: the menu was hidden between two-second captures, and OCR never read WORLD TOUR, although the game had reached the pre-race menu (START RACE). It was not a crash. The harness was not changed, because dynamic vblank would return the menu to 60 Hz.
- No crash, no clear-colour frames, and the error keys are the same as at 120 Hz.
- Races were not locked at 240 Hz (the budget is 4.17 ms per frame). Per frame in quickrace, the draw cost stayed at about 2.51 ms as at 120 Hz. Submit and swap rose from about 0.23 ms to 0.39 ms (submit 0.10 to 0.18 ms, swap 0.13 to 0.21 ms), and the command processor still waited 128 ms/s in `wait_reg_mem`.
- One 240 Hz Quick Race run recorded 213 fps during the phase B3 validation.

### 16 October 2026: why the dynamic vblank rate was needed

Before the fix, a refresh rate above 60 Hz also changed other modes. The menus were rendered one frame per vblank, because the UI scenarios have no FrameRate (photo_view ran at 120 fps at 120 Hz and at 229 fps at 240 Hz). `FM4_FRAME_RATE` replaced every FrameRate, so photo mode (55, or 90-112 fps), replay (60, or 112 fps) and post-race no longer ran at 30 fps. The idea that the menu hides faster because a timer counts frames was not proven; see the measurement below.

### 16 October 2026: dynamic vblank rate

- With `-FrameRate N` (N > 60), the game still sees an N Hz refresh (`video_mode_refresh_rate`), but the guest vblank runs at 60 Hz. N Hz applies only during race scenarios. `FM4_FRAME_RATE` now replaces only the FrameRate 60 of race scenarios; countdown, post-race, replay and photo mode keep 30 fps.
- Signal: the game keeps a stack of render scenarios and forwards each change to the render thread through `CRenderThreadLink` Push (sub_82442600, `TReference<CDynamicRenderSettings>`) and Pop (sub_82438A38), in LIFO order. `CDynamicRenderSettings` holds the name (MSVC `std::string`) at +8, and the FrameRate control at +44, with its value at +48 (60 when the scenario does not set it). `fm4-vblank-rate.cpp` mirrors that stack. The frame rate of the stack is the topmost scenario that sets FrameRate. The race phases are arranged by sub_826D9508 (race+372: 0 PreRace, 1/3 321_and_PostRace, 2 Race, 4 Replay, 5 PhotoMode on top of the race). All RenderScenarios files are read once at boot (`REX_FILE_READ_TRACE`), so that read is not a signal.
- Runtime: the cvar `vblank_rate` and `GraphicsSystem::SetVblankRate`. The vsync thread uses the new rate immediately, and runtime.log records "Guest vblank rate".
- Measured: the career menu hides about 9 seconds after the last press at 60 Hz (9.0), at 240 Hz dynamic (9.1) and at 240 Hz fixed (9.1). That is a real-time timer, not a frame count.
- Matrix of this binary (240 and 120 Hz, 3 runs per cell; 60 Hz smoke, 1 run):

| Scenario | 240 Hz | 120 Hz | 60 Hz |
|---|---|---|---|
| quickrace | 3/3, 224-235 fps, p99 5.3-6.2 ms | 3/3, 118-120 fps, p99 8.8-9.8 ms | 60.0 fps, p99 17.1 ms |
| photo | 3/3; race 220-228 fps, p99 5.4-5.8; photo mode 30.0 fps | 3/3; race 117-118 fps, p99 9.6-10.1; photo mode 30.0 fps | race 58.8 fps; photo mode 30.0 fps |
| photo_view | 3/3, menu 57 fps | 3/3, menu 57 fps | passed |
| career | 3/3; race 232 fps, p99 5.4 ms; replay 30.0 fps | 3/3; race 118-120 fps; replay 30.0 fps | race 60.0 fps; replay 30.0 fps |

- Career at 240 Hz is now 3/3. Previously it was 2/3, because the menu, at 240 fps, was hidden between captures.
- Menu input (the source gives no date; it was recorded with these measurements): FM4 ignores the first press after several screens have opened. The career menu and GO RACE hide about 9 seconds after the last press (real-time timer). So the e2e scenarios press until OCR reads the expected screen, instead of counting presses. With this, menu navigation is reliable.
- No crash, no clear-colour frames, and the error keys match the baseline. The log of the photo_view baseline was truncated by rotation, so it appeared to have no errors.
- `compare.py` FAILs only on total fps. Photo mode and replay return to their original 30 and 60 fps. The race phase is equal or better (240 Hz Quick Race 223.6 to 231 fps).
- Pacing at 120 Hz showed two patterns between runs, in both the baseline and this binary. Either the command processor waited on the guest for about 450 ms/s with a late vblank, or it waited for the vblank (`wait_reg_mem`) with guest_idle about 17 ms/s. The fps was the same in both.

### 16 October 2026: e2e gate tiers

- `e2e/matrix.py` runs these tiers:
  - smoke: photo, photo_view and career at 240 Hz, one run, about 10 minutes.
  - perf: photo at 240 Hz, three runs.
  - full: photo, photo_view and career at 60 and 240 Hz, three runs each, about 1 hour.
  - career: career_finish and career_progress (below).
- The matrix runs on a copy of the build in `<BuildRoot>\snapshots\<name>`, so the game build can be rebuilt while a gate runs. The race phase of photo (30 seconds after 10 seconds of driving) replaced the quickrace scenario in the gate.
- The boot sequence presses A once for each recognized video splash (Microsoft Studios, Dolby). The main menu now appears at about second 30 instead of second 47.
- The earlier matrix of three refresh rates, four scenarios and three runs took about 113 minutes.

### 16 October 2026: developer options and the career race to rewards

- Earlier attempt: the image still loads `CForzaCommandLineParameters` (singleton sub_8237D468, data 0x82DC4420) and its parser (vtable 0x8200ED8C, slots 2-5; `name=value`, with switches that have no value inverted). The options include launch, track, car, numaicars, engageaionstart, launchcarexperience, launchcareerphotomode, disablesplashintro, showframerate, forceEnableDebugMenus, camoverride and runperfpredautomation. Retail builds never fill them. The loader for `GAME:\cmdline.txt` is compiled out (there is no file read), and `ExLoadedCommandLine` is not imported, so the `cl` cvar never arrives. The options can be injected through the constructor hook sub_823E2338 and the default sub_823E7A10 (the default overwrites values from the constructor). However, the code that reads track, car, numaicars, engageaionstart, showframerate, forceEnableDebugMenus, camoverride and unlockframerate is absent. launch, launchcarexperience, disablesplashintro, noattractvideo, nohud and skiptutorials are only read as guards that skip certain screens. Tests of launch, launchcarexperience, disablesplashintro and noattractvideo left the boot, the video and the title screen unchanged. Direct launch to a race or an automatic race was not available from these options.
- Rebuilt (16 October): `FM4_GAME_CMDLINE` fills the parameter object through the game's own parser (`fm4-command-line.cpp`), and `fm4-dev-features.cpp` reads the options whose code is missing.
  - `engageaionstart`: the driver factory sub_82A93880 (slot, setup) creates a CHumanDriver when slot+104 = 0 (the player's car; opponents use 2), and AI drivers (CMetaAIDriver with CAIDrivatar, sub_82A935B8) for the other slots. The player's setup (+0 type 3, +360 filled, opponents 2 and 0) makes sub_82A935B8 set the Drivatar as belonging to the player (sub_82A96900, sub_82A992E8 with 104). That driver does not steer or press the throttle. Without throttle, the harness car stopped at the first fence; with throttle, it left the track (lap 1:25.5, position 8/8). The hook now marks the player's slot like an opponent (+104 = 2). Only while the drivers are being created, it makes the player's setup look like an opponent's, and then restores it.
  - Career race at Tsukuba Short Circuit (Ford Ka, 8 cars, 3 laps) with no input: laps 1:02.1 and 0:56.5, finished in position 1, 22.6 seconds ahead, and the YOU PLACED menu appeared. Pause, photo mode, saving a photo, QUIT RACE, replay and CONTINUE kept working (the first version passed smoke at 240 Hz, 3/3). `matrix.py` uses the AI driver by default (`--human` for the harness throttle). Holding the throttle did not change the result (still position 1). `e2e.py` no longer sends driving input while this option is active.
  - nohud (still read by retail, sub_8251DEB8): the HUD disappears entirely in Quick Race. Do not use it in scenarios that read the HUD through OCR; career waits for LAPS. camoverride and its fixed camera code are absent in retail.
  - Race loading path (`FM4_TRACE_RACE_SETUP`): main loop sub_826CFA68, then sub_82430C08, then the state sub_82403C60 (a new mode subsystem; race data from the global game object 0x82DC36FC), then sub_826DA8E0 (race initialisation), then sub_823D1BB0 (car), and so on to sub_82A93880. This is the basis for `launch` (direct race), which had not been built yet.
- Career tier (career_finish and career_progress, 240 Hz). The source gives no exact time for these runs; they were added after the matrices above. career_finish won at Tsukuba (race 219 fps, p99 6.9 ms). YOU PLACED showed a total of +5,170 CR, the driver and Ford affinity reached level 2 (car reward: 1994 Mazda MX-5 Miata, with a 2,000 CR bonus), and EVENT COMPLETE appeared. career_progress, in a new process, saw the next event (TopGear Bowling Challenge) with Ford Ka at LVL 2 and 15,740 CR (previously LVL 1 and 8,610 CR).

### 16 October 2026: shader overrides (phase B4), look and shadows

Shader overrides, phase B4 (research and design in [B4 shader research](../analysis/b4-shader-research.md); usage in [experiments](../e2e/experiments.md)):

- Step 1, per-hash HLSL override. `shader_override_path` replaces the DXBC translation of a pixel shader with HLSL that is compiled by FXC as ps_5_1 against the contract of that translation and the library `shader_override.hlsli`. The signatures are compared; on a mismatch the DXBC is kept. Without this cvar the path is unchanged. The FinalCombine_BloomTonemap pixel shader (E97AA211B556A9B3) was rewritten faithfully in BuildRoot. FXC took 120 ms in the game, and the PSO was accepted. A/B within one process (`shader_override_ab_ms=5000`, `override_ab.py`, photo scenario): in every static part of photo mode after a photo is taken, the override and DXBC captures are bit-identical in pairs (4 of 4, bracketed by identical in-phase pairs). The differing pairs fall on screen transitions (the SAVE PHOTO dialog and the save screen). With a +1 stop exposure variant, each override/DXBC pair in the same static part differs by the same figure (2.75 million bytes, average 29.2), and the variant looks brighter, so the shader is active there. One faithful run failed the OCR step for SAVE PHOTO although the dialog was in the capture. This was not a render problem.
- Step 2, HLSL backend. `HlslShaderTranslator` generates an HLSL override from the pixel shader ucode, operation by operation, as the DXBC translation does. In the photo scenario, 418 of 418 pixel shaders were generated and compiled by FXC without failure (median 136 ms per shader in the game). With `shader_override_generated` (every pixel shader uses the generated HLSL) and 5-second A/B, the override and DXBC captures in the static part of photo mode and on the boot screen are bit-identical. The differing pairs are only in screen transitions (photo LOADING, boot), with the same figures as the hand-written override run. The e2e run passed. Moving race parts cannot be compared with A/B.
- Step 3, look mod. `-Look "sharpen=..,bloom=..,exposure=..,contrast=..,saturation=..,vignette=..,motion_blur=.."` uses the mods in `work/fm4-rex/look`, which is project code: hooks on the generated HLSL for all four FinalCombine variants and for motion blur (details in the B4 research document). Photo scenario, 5-second A/B: with neutral knobs, the mod and DXBC captures are bit-identical in the static parts. With sharpen 0.6, bloom 0.7, contrast 0.25, saturation 1.1, vignette 0.3 and motion blur 0.3, the asphalt and the car look sharper, the bloom glow is smaller, and the motion blur on the kerbs is reduced. The preset values were not yet chosen.
- Step 4, tone curve. `-Look tonemap=gt|aces|neutral` replaces the game's filmic curve before the track LUT grading (details in the B4 research document). The filmic curve is inverted from the LUT coordinates. Mid-grey stays where the game curve puts it. The LUT, the colour matrix, the vignette and the auto exposure stay as they are. Photo scenario, 5-second A/B for each curve: all five mods compiled in the game (80-940 ms). In the static part of photo mode, the average per-byte difference from DXBC was 4.7 for ACES, 3.0 for GT and 3.5 for neutral, and 6.4 for ACES with saturation 1.1, bloom 0.7 and sharpen 0.4. ACES darkens the shadows (trees, tyres, asphalt) and leaves gradation in the snow. GT and neutral are closer to the original. The changes are subtle, because the game's curve is already filmic.

Default look (16 October):

- The launcher and `e2e.py` use the preset `pc` (tonemap=aces, saturation=1.1, bloom=0.7, sharpen=0.4). The presets aces, gt, neutral and original (no mods; the original FM4 look) remain. Knobs given after a preset override it, for example `-Look "pc,sharpen=0.6"`.
- Gate smoke at 240 Hz with `pc` (run lookpc, compared with vrr30): 3/3, race 227-228 fps, p99 5.6-5.9 ms, photo mode 30 fps, replay 30 fps. All five mods compiled in every cell, no clear-colour frames. `compare` gave only WARN on capture statistics.

Shadows and anisotropic filtering (16 October, new default launcher):

- The shadow workaround (`SkipShadowMapUnlessCockpit` in race scenarios, xenia-canary/game-patches#398) is no longer the default. `Build.ps1 -ShadowWorkaround` turns it on. The game itself uses it only in split screen. In the Quick Race frame graph with camera cycling, the shadow map (a depth pass with pitch 560 and 70-160 draws, resolved to 512x512, then a 16_16 MSAA 4x pass to 512x512) exists only for the cockpit camera with the workaround. Without it, on every camera, the GPU spends about 0.2-0.4 ms per frame more. Captures from the cockpit, hood, bumper and chase cameras show no square shadows (the Xenia symptom that motivated the workaround). The car now gets self-shadowing and crisp shadows on the asphalt, including between cars. The flag is read when the scenario is pushed (`CDynamicRenderSettings`: bit 4 of the second mask at +40, byte value at +380), so it cannot be A/B-tested in the middle of a race. Photo mode shows the scene buffer, so scene-level changes are not visible in photo-mode A/B tests.
- `anisotropic_override=5` (16x). The runtime default forced 4x. Sampler census (photo scenario): about 19 of 31 million sampler binds are mipmapped linear-filtered textures affected by the override. The game itself requests no anisotropic filtering for most of them, and 2x to 16x for a few. GPU time of the race frame: 2.28 ms versus 2.29 ms.
- Gate smoke at 240 Hz (run sh, compared with b4o): 3/3, race 230 fps, p99 5.3 ms, career 225 fps, replay 30 fps, no clear-colour frames. compare FAILed only on an XAM `app=FE` message, which also appears in the baseline with different argument addresses.
- Smoke at 2x (run sh2x, the smoke tier with `draw_resolution_scale` 2): 3/3, after `ocr-server.ps1` averages captures above 720p down to 1280x720 first (menu highlight detection uses 720p pixel distances). Race 202-206 fps, GPU 4.1-4.2 ms. Photo mode 30 fps, GPU 7.5 ms, and the photo load passed. Career replay ran at 23.5 fps (30 fps at 1x), because the command processor waited for guest commands for about 880 ms/s. The cause and fix are in the next item.
- Replay at 2x, 23.5 fps (16 October): the load was not caused by the scale, but by `vblank_vrr`. Replay starts at 30 fps on the 60 Hz grid (the command processor waits on the swap flag for about 920 ms/s). Then one long frame (a hitch of about 200 ms, several seconds after replay starts) moves the game into VRR mode, which never returned. Per second: 0 vblanks on the grid, 23 late, 23 forced, and a frame of about 43 ms, which is two intervals plus about 10 ms until the swap. A 30 fps scenario uses two vblanks per frame, and the guest render thread waits for that vblank on the CPU itself, not through `PM4_WAIT_REG_MEM`. The vsync thread only sees the command processor waiting. So after a late vblank (the swap arrives late), the next vblank is held until it is forced two intervals later, while the command processor sits idle waiting for the guest (guest_idle 880 ms/s). The 1x run did not hit this hitch, but the trap does not depend on the scale. Evidence: at 2x with `vblank_vrr=false`, replay ran at 30.0 fps, with `wait_reg_mem` at 831 ms/s and all vblanks on the grid. The GPU time was 8.3 ms, and the readback per frame was the same as at 1x. Fix: `GraphicsSystem::SetVblankVrrPaused` holds VRR to the fixed grid, and `fm4-vblank-rate.cpp` (which now always follows the render scenario stack) uses it while a scenario's frame rate is below 60 (countdown, post-race, replay and photo mode). Races and menus keep VRR. `FM4_VRR_HALF_RATE=1` restores VRR in the 30 fps scenarios, for comparison. Reproduced at 2x: the same 207 ms hitch occurred, replay immediately returned to the grid, and it ran at 30.0 fps (average 29.98), with `wait_reg_mem` at 830 ms/s. Gate smoke at 240 Hz: 1x (run vrr30, compared with sh), 3/3, race 216-223 fps, photo mode 30 fps, replay 30.0 fps p99 34.0 ms. 2x (run vrr30x2, compared with sh2x), 3/3, race 213-214 fps GPU 4.1-4.4 ms, photo mode 30 fps GPU 6.8 ms, replay 29.97 fps p99 33.9 ms GPU 8.6 ms. compare FAILed on vrr30-240-photo only with the XAM `app=FE` message, which is identical in the baseline log. No clear-colour frames.

### 16 October 2026: race quality presets

- `Build.ps1 -Quality xbox|high|ultra` (default `high`) applies race settings through `FM4_RACE_SETTINGS`. It overrides the controls of the N-Car, Autocross and Bowling render scenarios (not split screen), in `fm4-render-overrides.cpp`.
- `high` makes races match replay and photo mode: TilingScenario 0 (the game's 4x MSAA tiling scenario, three tiles) instead of 1 (2x MSAA, two tiles); SoftParticles; CollidableShadows; CarMinWheelLOD 0; CarLODTransDist 30/50/100 m (original 15/25/50); and it cancels the 16-car race trimming (`shaders_16car`, no damage, EnvMapFrequencyScale 0, HalfRateBloom, CollidablesInMirror 0).
- `ultra` adds CarKeepLOD0, a mirror every frame, and MaxCarsInMirror 11.
- Frame graph of `high`: the scene passes are 4x MSAA at offsets 0/-256/-512 (`xbox`: 2x at 0/-512). The gondola cable seen from the cockpit is no longer jagged like a staircase.
- Photo race phase at 240 Hz, with no other GPU process running:
  - `xbox`: 224, 220 and 219 fps; p99 5.9-6.4 ms; GPU 2.3 ms; predicated_run about 5,200 per frame.
  - `high`: 214, 187 and 225 fps, and 178 fps in the gate smoke (average 201); GPU 2.8 ms; predicated_run 4,500-8,300 per frame, depending on the car position (three tiles).
  - `ultra`: median 144.8 fps (-34%); p99 8.58 ms; GPU 3.09 ms; CP busy 974 ms/s.
- Camera cycle at 60 Hz: all three presets 60.0 fps; GPU 2.4, 2.7 and 3.1 ms.
- Gate smoke with `high` (run qhigh, compared with lookpc): 3/3, career race 216 fps, replay 30 fps, photo mode 30 fps, photo load passed, no clear-colour frames.

### 17 October 2026: clearcoat, display, overlay, Alt+Enter and ambient occlusion

Clearcoat on the car paint (17 October):

- The car body paint shader mod B85EF04779ABD438 (see [material atlas](../analysis/material-atlas.md), verified with a false-colour run) lives in `work/fm4-rex/look/fm4-paint.hlsli`. It adds a GGX sun highlight with a soft edge, a cube-map environment reflection at grazing angles (Schlick, F0 0.04) and metallic flakes. It is applied inside the paint's own shadow and before the game's fog.
- Knobs: `-Look clearcoat`, `clearcoat_roughness`, `clearcoat_glint`, `flakes`, `flake_scale`. The presets pc, aces, gt and neutral use setting A (clearcoat=1, roughness 0.3, glint 4). The original look keeps no mod. Setting A was chosen from three race A/B runs. Setting B (roughness 0.25, glint 8) produced white blobs on the roof. Setting C (roughness 0.35, glint 2) was hardly visible.
- The scene was frozen with rewind (Y) for same-frame A/B tests. The sky reflection shows on the roof and boot (chase camera) and on the fender edge (hood camera).
- Flakes are not used in any preset. Debug runs (fade, cells and glint shown as colours) show that flake cells are smaller than a pixel at the chase camera (they fade out completely), and only 3-5 pixels at the hood camera. No flake enters the narrow lobe (roughness 0.1, maximum tilt 19 degrees). A flake model needs LOD per cell by footprint, and many flakes per cell, before it is useful.
- The mod compiles in the game in 2.4 seconds (FXC warning X4000 from `xe_cube`). Gate smoke at 240 Hz (run paintpc, compared with qhigh): 3/3, race photo 214 fps, career 213 fps, race phase GPU 2.72-2.79 ms (qhigh 2.83-2.88 ms). Replay and photo mode 30 fps, no clear-colour frames. compare gave only WARN on capture statistics.

Display and render scale (17 October):

- `Build.ps1 -Display native|720p|1080p|1440p|4k` (default `native`: borderless fullscreen on the primary monitor), `-Fullscreen`, `-Supersample` and `-ResolutionScale 1-4`.
- The render scale is the smallest whole multiple of 1280x720 that covers the 16:9 picture on that screen: 720p 1x, 1080p, 1440p and 2560x1600 2x, 4k 3x. `-Supersample` raises it by one step. The window size accounts for Windows display scaling.
- When the render is larger than the window, the presenter uses the new shader `guest_output_area.ps.hlsl` (`present_area_downscale`, on by default). It averages all texels that fall on each screen pixel in linear light (gamma 2.2), instead of taking one bilinear tap. Verified with a 1920x1080 window at 2x on a frozen rewind scene: it matches the area simulation (average difference 0.12-0.13 of 255) and not bilinear (0.71-4.02). The bridge fence rail stays evenly thick, while bilinear broke it into dashes.
- Fullscreen 2560x1600 at 2x: a 2560x1440 image at 1:1, with 80-pixel bars at the top and bottom.
- Gate smoke at 240 Hz with `high` and clearcoat (compared with vrr30x2, which used the xbox quality, so compare FAILs only on fps): all cells passed. Replay and photo mode ran at 30 fps, and there were no clear-colour frames. Race at 1x: 213 fps (GPU 2.7 ms); 2x: 177 fps (5.4 ms); 3x: 109 fps (8.9 ms); 4x: 63-65 fps (15 ms, p99 21 ms; replay GPU 25.6 ms). 4x is only suitable for 60 fps, with more GPU headroom than this computer has.

Settings overlay (17 October):

- A small panel in the top-left corner shows the screen and render scale, quality, look and its knobs, shadows, anisotropic filtering, frame rate and race pacing, mirror, render target path, the shader A/B when it is active, the window size, and the game's fps (the number of guest output refreshes counted by the presenter, `GetGuestOutputRefreshCount`).
- It is built by `pipeline.py` (`info_overlay`, `FM4_INFO`) from the cvars and environment that are actually sent. The presenter draws it (ImGui, `fm4-info-overlay.h`), so it does not appear in e2e captures.
- `e2e.py` shows it from the start. The launcher shows it with `-Info`. F2 toggles it.

Alt+Enter (17 October):

- Alt+Enter toggles between borderless fullscreen and a window, in every run (`bind_fullscreen` in `rex_app.cpp`).
- Runtime keybinds now accept modifier prefixes (Alt+, Ctrl+, Shift+). The keyboard driver (mnk) ignores keys that are pressed together with Alt, because it received Enter before the keybind, and Enter is Start.
- Tested with real keys in Quick Race in a 1920x1080 window at 2x render: 1920x1080, then 2560x1600, then 1920x1080. OCR afterwards only saw the race HUD (timer running, no pause menu).

GTAO ambient occlusion (17 October; design and results in [GTAO design](../analysis/gtao-design.md)):

- The runtime runs the GTAO pass (`post_ao`) on the scene depth texture that the game resolves itself, right after the resolve of the last depth tile. The camera is taken from the car paint and road vertex shader constants. The FinalCombine mods read the result through `xe_post_ao` and darken the scene before tonemapping, more weakly on bright pixels.
- `-Look ao=<strength>` enables the pass and applies it. The presets pc, aces, gt and neutral use ao=0.6 with a radius of 0.75 m (chosen from a 2x grid on a frozen frame). The original look has no AO.
- Earlier in the work: depth and camera were matched to the scene edges on all six cameras (offset 0 at 1x and 2x). An old runtime defect was also fixed: duplicate rows and columns at the scene tile boundaries at 2x and 3x. The half-pixel fill of the resolve now applies only to 1x MSAA surfaces.
- New tests that do not need the game: `fm4_post_ao_camera_tests`, `fm4_post_ao_shader_tests` (on WARP) and `fm4_shader_mod_tests` (all look mods compiled with FXC).
- The 240 Hz gate ran in a separate BuildRoot, with the pc look without AO, compared with look pc. Smoke at 1x: 3/3 (photo_view and career PASS, photo WARN on capture statistics). Replay and photo mode 30 fps, no clear-colour frames.
- Performance, 3 runs of the race phase: at 1x, GPU 2.82 to 3.01 ms and 196 to 200 fps (limited by the command processor). At 2x, GPU 5.32 to 6.09 ms and 174 to 162 fps (limited by the GPU). A 2x 60 Hz run with the launcher gave 60.0 fps.
- AO is visible at contacts and under the car, in the lamp recesses, the bumper creases and the wall bases. Open asphalt and sky are unchanged, and AO also applies in replay and photo mode.
- Not yet: the cockpit interior (its depth range is compressed by the implementation itself), and the garage and showroom (no camera vertex shader).

## 5. Superseded settings and decisions

Each row gives the date of the change, the setting that was replaced, and what happened later. The current values are in [running](../running.md) and [current status](current.md).

| Date | Decision or setting | Replaced | Later change |
|---|---|---|---|
| 13 Oct 2026 | Launcher combination `rov_occlusion_async=all` with `readback_resolve=fast`, adopted by the project owner before the full gate | `readback_resolve=full` (synchronous readback) and the earlier occlusion default | 14 Oct: RTV as default (short-lived). 15 Oct: RTV as default again |
| 13 Oct 2026 | `--rov_occlusion_awaited_report_divisor=2` for the awaited sun occlusion report | No divisor: the sun glare was too bright | Still set, and effective on the ROV path only; RTV not calibrated |
| 14 Oct 2026 | RTV path as the launcher default (commit `c6420dc`) | ROV with `rov_occlusion_async=all` | Reverted the same day because of missing road segments. RTV default again on 15 Oct |
| 14 Oct 2026 | `vblank_vrr=true` as the launcher default (`Build.ps1 -FixedVblank` restores the fixed 60 Hz grid) | Fixed 60 Hz vblank grid | 16 Oct: VRR is held to the fixed grid in scenarios below 60 fps (`FM4_VRR_HALF_RATE=1` restores VRR there) |
| 14 Oct 2026 | Predicated tiling 1 (2 tiles, 2x MSAA) stays the race default. `FM4_TILING_SCENARIO=2` (one tile, no MSAA, FXAA) was tested, not made default | Not applicable | 16 Oct: races at `-Quality high` use TilingScenario 0 (4x MSAA, three tiles) |
| 14 Oct 2026 | SDK ThinLTO and `-march=x86-64-v3` not adopted (no measurable gain) | Not applicable | None recorded |
| 14 Oct 2026 | Synchronous shader compilation stays the default | Asynchronous compilation (it discarded about 2,000 draws per run) | None recorded |
| 14 Oct 2026 | `--d3d12_submit_thread` available, default off | Not applicable | 15 Oct: enabled in the launcher |
| 15 Oct 2026 | Launcher: `clear_memory_page_state=false` | ReXGlue default `true` | None recorded |
| 15 Oct 2026 | Launcher: `d3d12_submit_thread=true` and `d3d12_readback_publish_thread=true` | Off | None recorded |
| 15 Oct 2026 | `--rtv_uniform_draws` on by default (`--rtv_uniform_draws=false` disables it) | Full EDRAM transfer for each clear | None recorded |
| 15 Oct 2026 | `execute_unclipped_draw_vs_on_cpu` true in the runtime | `false` in the ReXGlue SDK | None recorded |
| 15 Oct 2026 | Launcher and e2e: half-rate rear mirror (`FM4_HALF_RATE_MIRROR=1`). `-FullRateMirror` restores every frame | The title's own behaviour: every frame in 1-car and 8-car races (every other frame in 12-car and 16-car races) | None recorded |
| 15 Oct 2026 | `-FrameRate` no longer forces RTV | Earlier, `-FrameRate` forced RTV | None recorded |
| 15 Oct 2026 | `FM4_FRAME_RATE` (launcher `-FrameRate`) replaces the FrameRate of all render scenarios | Not applicable | 16 Oct: only the race FrameRate 60 is replaced. Countdown, post-race, replay and photo mode keep 30 fps |
| 15 Oct 2026 | `-ResolutionScale 2` or `3` available; the 1x render was the baseline | Not applicable | 17 Oct: `-Display` chooses the scale that covers the screen |
| 16 Oct 2026 | Shadow workaround (`SkipShadowMapUnlessCockpit`) no longer default. `-ShadowWorkaround` (`FM4_SHADOW_WORKAROUND=1`) turns it on | Workaround on by default | Shadow map on every race camera by default |
| 16 Oct 2026 | `anisotropic_override=5` (16x) | Runtime default that forced 4x | None recorded |
| 16 Oct 2026 | e2e and matrix races are driven by the title's AI driver (`engageaionstart`). `--human` selects the harness throttle | Harness held the throttle, and the car hit barriers | `--human` remains available |
| 16 Oct 2026 | Look preset `pc` as the default (tonemap aces, saturation 1.1, bloom 0.7, sharpen 0.4) | Not recorded in this log | 17 Oct: pc includes clearcoat (setting A) and AO (ao=0.6, radius 0.75 m) |
| 16 Oct 2026 | Race quality preset `high` as the default (`-Quality xbox`, `high` or `ultra`) | Race settings of the `xbox` preset (TilingScenario 1, 2x MSAA, two tiles) | None recorded; gate runs from 16 October use `high` |
| 16 Oct 2026 | Gate tiers smoke, perf, full and career. The matrix runs on a copy of the build in `<BuildRoot>\snapshots\<name>` | Matrix of three refresh rates, four scenarios and three runs (about 113 minutes) | None recorded |
| 17 Oct 2026 | `-Display native` default (borderless fullscreen on the primary monitor) | `-Display 720p` default: 1x in a 1280x720 window, as stated in the earlier build guide | None recorded |
| 17 Oct 2026 | Presenter area downscale (`guest_output_area.ps.hlsl`, `present_area_downscale`, on by default) | One bilinear tap per screen pixel | None recorded |
| 17 Oct 2026 | Clearcoat setting A in presets pc, aces, gt and neutral. Flakes are not used in any preset | No clearcoat (the original look) | The `flakes` knob remains, unused |
| 17 Oct 2026 | GTAO `ao=0.6` (radius 0.75 m) in presets pc, aces, gt and neutral | No AO | The original look keeps no AO |

## 6. Corrections and qualifications of earlier entries

- 14 October: the conclusion that the missing road segments came from lost colour in the RTV render target path was wrong. It was corrected on 15 October (section 3).
- 14 October: the 114.7 fps figure for 120 Hz Quick Race came from the last 20 seconds of a run, when the car (throttle held, no steering) faced a wall, with about 850 draws per frame. On the open track the figure is 70-75 fps (correction of 15 October; details in [performance history](history-performance.md)).
- 16 October: the hypothesis that the menu hides faster because a timer counts frames was not proven. The timer is real-time: about 9 seconds at 60 Hz, at 240 Hz dynamic and at 240 Hz fixed.
- 16 October: the first definition of the photo scenario (20 seconds of racing) was replaced in the gate by a race phase of 30 seconds after 10 seconds of driving.
- 13 October: the sun glare judgment applies to the ROV path only. On 14 October the RTV sun query still gave 0 samples on the quick race route.
