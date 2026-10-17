# FM4 end-to-end test harness

`e2e.py` launches the built game from a BuildRoot with the same arguments as `Build.ps1 -Action run`, drives the application-local diagnostic controller (`FM4_TEST_INPUT_FILE`; no desktop input is sent), and collects:

- GPU frame captures every 2 seconds (`FM4_CAPTURE_DIR`), converted to PNG;
- a per-second performance trace (`REX_PERF_TRACE_FILE`, see [traces.md](traces.md));
- crash status and error counts from the runtime log;
- `summary.json` with median FPS and GPU-thread stall times.

The game window shows the settings overlay of the run (render scale, quality, look, shadows, frame rate, shader A/B, ...; [`pipeline.py`](../../outputs/FM4-build/pipeline.py) `info_overlay`) from the start. The presenter draws it over the guest output, so the captures, OCR and `compare.py` never see it. `--env FM4_INFO_SHOW=0` hides it, and F2 toggles it.

Results go to `--out`, which must be outside this repository: captures and logs show game content. Use a directory under the BuildRoot, for example `<BuildRoot>\e2e\<name>`. `e2e.py` rejects an `--out` inside the repository. Never commit results.

Related pages:

- [traces.md](traces.md): debug trace and diagnostic environment variables and what each writes.
- [experiments.md](experiments.md): A/B overrides, shader override diagnostics, FXC notes and other experiment-only switches.
- [setup.md](../setup.md): prerequisites, setup, generate, build and the BuildRoot layout.
- [running.md](../running.md): launcher options and look knobs.
- [status/current.md](../status/current.md): current state and what is verified on the latest build.

## Requirements

- A BuildRoot built by `Build.ps1` (the harness reads `local.json` and `build-result.json` from it).
- Windows OCR with the en-US language (built into Windows 10/11).
- Goal mode only: a TypeSafe API key in the `TYPESAFE_API_KEY` environment variable (user scope is read even when the shell started earlier). Each step sends the OCR text of the current screen, the goal/route description and the recent steps to `https://api.typesafe.ai` (endpoint `/v1/systemone`). No images, saves, user data or personal data are sent. A quickrace run makes roughly 40-50 requests.
- No packages beyond the Python standard library.

## Modes

### Fixed timeline (no network access)

```
python outputs\FM4-build\e2e\e2e.py --build-root D:\FM4-build --out D:\FM4-build\e2e\boot --scenario boot
```

### Goal-driven with Jev (TypeSafe System One)

```
python outputs\FM4-build\e2e\e2e.py --build-root D:\FM4-build --out D:\FM4-build\e2e\race --goal quickrace --tail 40 --measure 30
```

The `quickrace` goal starts the game, opens Free Play > Quick Race, accepts the default car and track, starts the race and holds the throttle. Add `--steps camera` to cycle the race cameras (Back every 6 s) once the race runs.

The run exits with code 1 when the game crashes, the goal is not reached in time, or a step fails. In the last two cases `goal_reached` is `false` in `summary.json`.

Without network access (Jev unreachable), `--scenario quickrace` drives the same route on a fixed timeline; it reaches the race reliably with the copied profile.

## e2e.py options

| Option | Default | Meaning |
| --- | --- | --- |
| `--build-root` | required | BuildRoot used by `Build.ps1` |
| `--game-build` | `game-build` | build directory under the BuildRoot, for example `game-build-prof` (see [traces.md](traces.md)) or `game-build-thinlto` (`pipeline.py --flavor`) |
| `--out` | required | result directory, outside the repository |
| `--scenario` | `boot` | fixed timeline: `boot`, `quickrace`, `photo`, `photo_view`, `career`, `career_finish`, `career_progress` |
| `--profile` | none | copy a user-data root to `<out>\user-data` and run on it; `copy` means the BuildRoot's `user-data`, otherwise a path |
| `--goal` | none | navigate with Jev instead of a timeline (`quickrace`) |
| `--goal-timeout` | 360 | seconds allowed for the goal |
| `--env NAME=value` | none | extra variable for the game process; repeatable |
| `--steps` | none | JSON list of steps, or the preset `camera`; overrides `--scenario`, and after `--goal` runs once the goal is reached |
| `--arg NAME=value` | none | extra or overriding `--cvar=value`; repeatable (`--arg=--readback_resolve=fast` also works) |
| `--look` | launcher's (`pc`) | look as `Build.ps1 -Look`, for example `original` or `pc,sharpen=0.6` |
| `--quality` | launcher's (`high`) | race render quality as `Build.ps1 -Quality`: `xbox`, `high` or `ultra` |
| `--ao` | off | the runtime's ambient occlusion pass (`post_ao`), as `pipeline.py --ao` |
| `--tail` | 10 | seconds to keep running after the last step |
| `--keep-ppm` | off | keep the full-resolution PPM captures next to the PNG files |
| `--measure` | 0 | summarize only the last N perf seconds |
| `--repeat` | 1 | run N times into `<out>\run1..runN` and aggregate into `<out>\summary.json` |
| `--resummarize` | off | recompute `summary.json` of existing runs in `--out` from their `perf.jsonl` |

The harness passes `--cvar` values from `pipeline.launch_cvars`, the look (`pipeline.look_cvars`) and `pipeline.ao_cvars`, then the `--arg` items, so an `--arg` overrides them.

## Gameplay scenarios

Fixed timelines with screen checks, no network access:

| Scenario | What it does |
| --- | --- |
| `photo` | Quick Race, 10 s of driving, then a 30 s measured race phase, pause > PHOTO MODE, take a photo, save it as a new file; checks SAVE SUCCESSFUL. |
| `photo_view` | a new process opens Career > MY PROFILE > MEDIA > MY PHOTOS and loads the saved photo; checks that it replaces the menus without an error (OCR reads its watermark only sometimes; see `final.png`). |
| `career` | Career > GO RACE > WORLD TOUR, the profile's next event (Tsukuba Short Circuit with the Ford Ka for the BuildRoot profile), 40 s of driving, QUIT RACE, 30 s of the replay, CONTINUE to the event. |
| `career_finish` | the same event to the end, driven by the title's AI (`engageaionstart`, needed): it wins the 3 laps in about 3 minutes; CONTINUE, then A every 6 s through YOU PLACED (Total), the driver and Ford affinity level 2 rewards (a gift car: the default) up to EVENT COMPLETE, after the save. |
| `career_progress` | a new process on that profile: WORLD TOUR shows the next event (the TopGear challenge) with the driver at level 2 and credits other than the 8,610 CR before the race. |

They write saves, so run them on a copy of the profile:

```
e2e.py ... --out <BuildRoot>\e2e\photo --scenario photo --profile copy
e2e.py ... --out <BuildRoot>\e2e\view --scenario photo_view --profile <BuildRoot>\e2e\photo\user-data
e2e.py ... --out <BuildRoot>\e2e\career --scenario career --profile copy
e2e.py ... --out <BuildRoot>\e2e\finish --scenario career_finish --profile copy --env FM4_GAME_CMDLINE=engageaionstart
e2e.py ... --out <BuildRoot>\e2e\progress --scenario career_progress --profile <BuildRoot>\e2e\finish\user-data
```

`--profile copy` copies a user-data root (`copy` means `<BuildRoot>\user-data`) to `<out>\user-data` and runs on it. The installed content packages (`user-data\0000000000000000`) are linked with a directory junction, not copied, and the BuildRoot's own packages are never deleted through the copy. With `--repeat`, each run gets its own copy. Without the AI driver the career race only holds the throttle (the Ford Ka ends up at a wall); `career_finish` needs `engageaionstart`.

Every scenario except `boot` starts with one A per splash video read on screen (Microsoft Studios, Dolby), which skips it: the main menu shows after about 30 s instead of 47. A at a fixed rate is not used, because it also skips the short title screen and selects PLAY CAREER in the main menu.

`e2e.py` runs the game with `headless=true`: XAM prompts such as the keyboard for a photo name answer with their default text (nobody can type into the host dialog). Rendering is not affected.

### Scenario steps

A step is `[timeout, action, hold, text(, retry)]`. It repeats the action until one of the `|`-separated texts shows in a capture taken after it, and fails the run (`goal_reached` false, `failed_step` in `summary.json`) otherwise.

- `?action` skips the action when the text already shows.
- `=TEXT` must be a whole OCR line. A leading `!` passes when none of the texts shows.
- Give a step whose screen loads (taking a photo, a race loading) a retry longer than the load: a repeated A would act on the next screen.
- `select:<label>` moves the menu highlight to an OCR'd label. The highlighted entry of a vertical menu is the one with red lines above and below it (`ocr-server.ps1` `red_above`/`red_below`) and a dark band (`bg`); of a horizontal menu, the red-framed tile. It moves only when two clear readings in a row agree, since menus slide and sway (the pause menu at 120 Hz most of all).
- Menus that act on their own when idle are not selected but driven with fixed presses as soon as they show: the career and GO RACE menus hide (Up shows them again), and the post-race menu starts the race replay. The career menu hides about 9 s after the last press at 60, 120 and 240 Hz alike (a wall-clock timer; see [status/current.md](../status/current.md)), so a capture every 2 s can miss a menu that shows only briefly.
- `wait` waits without touching the controller.
- `phase:<name>` ... `phase:end` marks a measured phase. `summary.json` `phases` has the perf statistics per phase (photo: race, photo; career: race, replay).
- `steps.jsonl` records what each check and select saw.

FM4 menus ignore the first press after some screens open, and the career and GO RACE menus hide after a few idle seconds (the next press only shows them again), so the scenarios press until the expected screen shows rather than counting.

## Goal mode

Code owns the workflow; Jev supplies judgments:

1. `ocr-server.ps1` reads each new capture with the Windows OCR engine (en-US) and reports text lines, their positions and how much FM4 selection red surrounds them. `e2e.py` marks the highlighted item from that.
2. One Jev request per step asks, in parallel: which screen is shown, which on-screen item the route needs activated next, and whether a race is running.
3. `e2e.py` turns the judgments into one controller action: move the highlight toward the target, press A, wait during loading, press B on screens the goal does not need (for example Autovista), and nudge menus that ignore A until their focus has moved once.

`nav.jsonl` records every step: frame, judgments, confidences and the action.

## Gates and tiers

`matrix.py` runs a set of cells on a snapshot of the game build (`<BuildRoot>\snapshots\<name>`, checked against `build-result.json`), so `Build.ps1 -Action build` can rebuild `game-build` while a gate runs. Cells still run one at a time, because performance numbers need the machine to themselves. Pick the tier by what a change can break:

| Tier | Cells | Runs per cell | Use |
| --- | --- | --- | --- |
| `smoke` | `photo`, `photo_view`, `career` at 240 Hz | 1 (about 10 min) | every change, before measuring anything |
| `perf` | `photo` at 240 Hz | 3 | performance work, against a baseline gate of the same tier: its race phase (12 cars, 30 s) and photo mode |
| `full` | `photo`, `photo_view`, `career` at 60 and 240 Hz | 3 (about 1 hour) | before committing a stage; `--hz 60,120,240` adds 120 Hz |
| `career` | `career_finish` and `career_progress` at 240 Hz | 1 (about 8 min) | a career race to its rewards and the progress after a restart |

```
python outputs\FM4-build\e2e\matrix.py --build-root <BuildRoot> --name <name> --tier smoke --baseline <older name>
```

Options: `--build-root` and `--name` are required. `--tier` defaults to `smoke`. `--scenarios`, `--hz` and `--repeat` override the tier. `--baseline` compares each cell with the cell of the same Hz and scenario of that gate. `--game-build` (default `game-build`) names the build to snapshot, `--no-snapshot` runs the build directory itself, `--human` keeps the harness throttle, and `--env` and `--arg` pass to every cell.

Cells go to `<BuildRoot>\e2e\<name>-<hz>-<scenario>`, and `<BuildRoot>\e2e\<name>.log` keeps one line per cell. Each cell is compared with `compare.py` when `--baseline` is given. `matrix.py` exits with 1 when any cell fails.

- In every race an AI driver drives the player's car (`FM4_GAME_CMDLINE=engageaionstart`), so each race follows the track instead of ending at a wall. A `FM4_GAME_CMDLINE` given in `--env` replaces this default, and `--human` drops it.
- `photo_view` runs on the profile that the `photo` cell of the same gate saved, and `career_progress` on the profile of `career_finish`. `matrix.py` refuses a gate that includes one of them without the other.
- For a tier at 2x, pass `--arg draw_resolution_scale_x=2 --arg draw_resolution_scale_y=2`. `ocr-server.ps1` averages captures above 720p down to 1280x720 first, so the menu highlight checks keep their 720p pixel distances.
- Unlike the look and the race quality, `e2e.py` does not follow the launcher's `-Display`: it renders at 1x in a 1280x720 window unless `--arg` says otherwise, so the gates keep comparing with their baselines. The launcher's native resolution is 2x on 1080p, 1440p and 2560x1600 monitors and 3x on 4K ones.
- The photo race phase replaces the `quickrace` scenario in the gates; `quickrace` stays for comparisons with older results.

## Frame rate and launcher settings

The harness uses the launcher environment (`pipeline.launch_env`):

- `FM4_HALF_RATE_MIRROR=1`: the rear-view mirror is drawn every other frame in every race. `--env FM4_HALF_RATE_MIRROR=0` renders it every frame, also in the 12 car Quick Race.
- `--env FM4_SHADOW_WORKAROUND=1` skips the shadow map outside the cockpit in races (`Build.ps1 -ShadowWorkaround`).
- The race render quality is the launcher's (`pipeline.py` `QUALITY_PRESETS`, `high` by default, passed through `FM4_RACE_SETTINGS`). `--quality xbox|high|ultra` picks another; with `--env FM4_TILING_SCENARIO=<n>` the preset keeps that tiling scenario.

Frame rates above 60 (`Build.ps1 -FrameRate <n>` sets both of these):

- `--arg=--video_mode_refresh_rate=<n>` changes the refresh rate reported to the game.
- `--env FM4_FRAME_RATE=<n>` replaces the FrameRate 60 of the race render scenarios (countdown, post-race, replays and photo mode keep 30).

The guest vblank then runs at 60 Hz, and at `<n>` Hz only while a race scenario sets the frame rate (`fm4-vblank-rate.cpp`). `--arg=--vblank_rate=<hz>` sets a fixed startup vblank rate (without `FM4_FRAME_RATE`). The vblank diagnostics are in [experiments.md](experiments.md).

At 240 Hz (`--arg=--video_mode_refresh_rate=240 --env FM4_FRAME_RATE=240`) the command processor, not the vblank, sets the frame rate.

## Comparing runs

Single runs vary by about 10-20% in FPS, and a hot laptop drifts: the same binary measured 53.4 and then 48.8 fps (3 runs each) half an hour apart. Bracket a candidate with baseline runs before and after it. Measure each configuration with `--repeat 3`: runs go to `<out>\run1..run3`, and `<out>\summary.json` aggregates them (median of medians, minimum of minimums, union of errors). A/B a cvar on the same binary with `--arg`, for example `--arg=--readback_resolve=fast`. Then:

```
python outputs\FM4-build\e2e\compare.py <BuildRoot>\e2e\baseline-v2 <BuildRoot>\e2e\candidate
```

`compare.py` prints the differences and a verdict. It exits 1 on FAIL and 2 on bad input. FAIL means any of:

- the candidate crashed, or its goal was not reached;
- FPS (overall or of a phase) down more than 10%, using `fps_mean` (or `fps_median` when `fps_mean` is missing);
- `frame_ms_p99` up more than 20%;
- an error key that the baseline does not have.

A wait cause up more than 20% and more than 5 ms/s, or different capture statistics (luma, black and saturated share, luma histogram), only WARN. So does a rise in `clear_gray_frames`. Races are not deterministic, so check captures by eye.

## Output layout

Each run writes to its `--out` directory:

```
<out>\
  summary.json        statistics of the run (see below)
  final.png           the last capture
  capture\            frame-NNN.png (and frame-NNN.ppm with --keep-ppm), captures.txt
  perf.jsonl          per-second performance trace (REX_PERF_TRACE_FILE)
  perf.jsonl.resolves.json   resolve destinations, written with the perf trace (see traces.md)
  runtime.log         runtime log; error counts come from here
  stderr.log          standard error of the game process
  steps.jsonl         what each check and select saw
  nav.jsonl           goal mode: every step with its judgments
  input.txt           the diagnostic controller's input file (FM4_TEST_INPUT_FILE)
  user-data\          the copied profile, with --profile
  run1..runN\         the runs of --repeat, each with the same files
```

`captures.txt` holds the steady-clock time of each capture, to match frames in the runtime traces. Starting a run deletes the old `capture` files and the `perf.jsonl`, `runtime.log`, `nav.jsonl` and `steps.jsonl` of that directory.

## Reading summary.json

The statistics come from the steady part of the perf trace: without `--measure`, the last two thirds of the per-second windows (the first third is dropped as warm-up when there are more than six windows); with `--measure N`, the last N seconds.

| Field | Meaning |
| --- | --- |
| `crashed` | `null`, or the exit or harness error text |
| `goal`, `scenario` | the goal, or the scenario (`null` when `--steps` was given) |
| `goal_reached` | `null` when there is no goal or check; `true` once a check or goal passed; `false` when a step failed or the goal was not reached |
| `failed_step` | the step that timed out, with its text |
| `args`, `profile` | the `--arg` items and the `--profile` given |
| `seconds`, `timeline` | wall time, and the time and action of each step |
| `frames_captured`, `perf_windows` | number of captures and of per-second perf windows |
| `fps_median`, `fps_min` | per-second guest swap rate: median and lowest window |
| `fps_mean` | frames over time across all measured windows (1000 divided by the mean frame time); steadier than the median of per-second FPS, which jumps between 20, 24 and 30 as frames snap to vblanks |
| `frame_ms_p50`, `frame_ms_p95`, `frame_ms_p99` | frame time percentiles, in ms |
| `hitch_50ms_per_min`, `hitch_100ms_per_min` | frames longer than 50 ms and 100 ms, per minute |
| `wait_ms_per_s_median` | time per second the command-processor thread waited, per cause (see [traces.md](traces.md)) |
| `cp_busy_ms_per_s_median` | window time minus all waits: wall time the command-processor thread works |
| `readbacks_per_s_median`, `draws_per_s_median` | resolve readbacks and draws per second |
| `cpu_ms_per_s_median` | command-processor time per zone, per second |
| `occlusion_samples_median` | mean passing samples per occlusion interval, unawaited and awaited |
| `gpu_frame_ms_median`, `gpu_resolve_ms_median`, `gpu_span_ms_median` | GPU time per frame from timestamps: busy time, resolves, and the first-to-last span |
| `counts_per_s_median`, `counts_per_frame_median` | event counts per second and per frame (see [traces.md](traces.md)) |
| `phases` | the statistics above per phase, for the phases the scenario marks |
| `final_capture` | `luma_mean`, `black_pct` (luma below 8), `saturated_pct` (luma 248 and above) and `luma_hist16` (16 bins) of the last capture |
| `clear_gray_frames` | captures in which the exact FM4 clear color (102,102,102) covers at least 1%, i.e. geometry such as the road surface was not drawn |
| `errors`, `top_errors` | runtime log lines that match an error marker, keyed by their text (first 90 characters, numbers replaced by `#`) with counts; `top_errors` lists the ten most frequent |

With `--repeat`, `summary.json` is an aggregate (`repeat`, `runs`, and the same keys): `fps_median` is the median of the runs' medians, `fps_min` the minimum of their minimums, `errors` the union with summed counts, `goal_reached` true only when every run reached it, and `final_captures` lists each run's final statistics. `--resummarize` recomputes these from the `perf.jsonl` files of finished runs, for example after the harness gained new fields.

## Helper scripts

All scripts live in [`outputs/FM4-build/e2e/`](../../outputs/FM4-build/e2e/).

| Script | Use |
| --- | --- |
| `e2e.py` | launch the game, drive it and collect a run |
| `matrix.py` | run a gate tier on a build snapshot |
| `compare.py <baseline> <candidate> [--json]` | compare two run directories and print a verdict |
| `race_clock.py <run>` | race timer of the captures against wall time (1.0: real time); needs `--keep-ppm` captures |
| `cp_profile.py stacks.txt <BuildRoot>\game-build-prof [--function NAME]` | summarize the command-processor stack samples (see [traces.md](traces.md)) |
| `inventory.py --build-root <BuildRoot> a.json [b.json ...] [--json out.json]` | combine `REX_GPU_INVENTORY` files of several scenarios |
| `frame_graph.py --build-root <BuildRoot> trace.jsonl [--frame N \| --list]` | print one frame of a `REX_FRAME_GRAPH` trace |
| `pm4_writers.py <generated dir> <xenos.h> out.json` | static scan of the PM4 packet writers for `FM4_D3D_TRACE` |
| `override_fxc.py <override file or dir> <dump_shaders dir> [--mods ... --define ...]` | compile shader overrides like the runtime (see [experiments.md](experiments.md)) |
| `override_ab.py <run> --period-ms N [--diff out.png]` | list captures of a shader A/B run with their phase |
| `ocr-server.ps1` | the Windows OCR process that reads each capture |
| `jev.py` | the Jev (TypeSafe System One) client for goal mode |

Typical calls:

```
python outputs\FM4-build\e2e\race_clock.py <BuildRoot>\e2e\<run>
python outputs\FM4-build\e2e\inventory.py --build-root <BuildRoot> a.json b.json
python outputs\FM4-build\e2e\pm4_writers.py <BuildRoot>\project\generated\default <BuildRoot>\runtime-source\include\rex\graphics\xenos.h out.json
python outputs\FM4-build\e2e\frame_graph.py --build-root <BuildRoot> trace.jsonl [--frame N | --list]
python outputs\FM4-build\e2e\override_fxc.py <override file or dir> <dump_shaders dir> [--mods work\fm4-rex\look --define FM4_LOOK_SHARPEN=0.5]
```

## Developer options

FM4 keeps its developer command line parameters, but the retail build never fills them (see [status/current.md](../status/current.md)). `--env FM4_GAME_CMDLINE="<option> <option>=<value>"` feeds them through the title's own parser after its defaults. The main one for the gates is `engageaionstart`, an AI driver for the player's car. The full list, the rebuilt options and the other experiment-only switches are in [experiments.md](experiments.md).

## Runtime development loop

The runtime (ReXGlue SDK fork) is [`work/runtime`](../../work/runtime/) in this repository:

1. Edit `work/runtime` directly.
2. `Build.ps1 -Action build -BuildRoot <BuildRoot>`: mirrors `work/runtime` to `<BuildRoot>\runtime-source`, rebuilds and installs the runtime incrementally, builds the game, copies the runtime DLLs, runs the instruction tests and writes `build-result.json`.
3. Add new runtime files to [`outputs/FM4-source-control/tracked-files.json`](../../outputs/FM4-source-control/tracked-files.json) (category `runtime-fork`) and run [`check-staged-content.py`](../../outputs/FM4-source-control/check-staged-content.py) before committing.

Never edit `<BuildRoot>\runtime-source`: the next build overwrites it. Verify a clean setup in a new BuildRoot at the end of each stage.

The integration code ([`work/fm4-rex`](../../work/fm4-rex/)) is copied to `<BuildRoot>\project` by `generate`, not by `build`, and only the files listed in `tracked-files.json`: after editing or adding one, run `generate` and then `build`. Build steps are in [setup.md](../setup.md).
