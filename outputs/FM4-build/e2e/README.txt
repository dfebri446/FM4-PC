FM4 end-to-end test harness
===========================

e2e.py launches the built game from a BuildRoot with the same arguments as
Build.ps1 -Action run, drives the application-local diagnostic controller
(FM4_TEST_INPUT_FILE; no desktop input is sent), and collects:
- GPU frame captures every 2 seconds (FM4_CAPTURE_DIR), converted to PNG;
- a per-second performance trace (REX_PERF_TRACE_FILE, see below);
- crash status and error counts from the runtime log;
- summary.json with median FPS and GPU-thread stall times.

Results go to --out, which must be outside this repository: captures and logs
show game content. Use a directory under the BuildRoot, for example
<BuildRoot>\e2e\<name>. Never commit them.

Modes
-----
Fixed timeline (no network access):
  python outputs\FM4-build\e2e\e2e.py --build-root D:\FM4-build --out D:\FM4-build\e2e\boot --scenario boot

Goal-driven with Jev (TypeSafe System One):
  python outputs\FM4-build\e2e\e2e.py --build-root D:\FM4-build --out D:\FM4-build\e2e\race --goal quickrace --tail 40 --measure 30

The quickrace goal starts the game, opens Free Play > Quick Race, accepts the
default car and track, starts the race and holds the throttle. The run exits
with code 1 when the game crashes or the goal is not reached in time.

How goal mode works
-------------------
Code owns the workflow; Jev supplies judgments:
1. ocr-server.ps1 reads each new capture with the Windows OCR engine (en-US)
   and reports text lines, their positions and how much FM4 selection red
   surrounds them. e2e.py marks the highlighted item from that.
2. One Jev request per step asks, in parallel: which screen is shown, which
   on-screen item the route needs activated next, and whether a race is running.
3. e2e.py turns the judgments into one controller action: move the highlight
   toward the target, press A, wait during loading, press B on screens the goal
   does not need (for example Autovista), and nudge menus that ignore A until
   their focus has moved once.
nav.jsonl records every step: frame, judgments, confidences and the action.

Requirements and data sent
--------------------------
- Windows OCR with the en-US language (built into Windows 10/11).
- Goal mode only: a TypeSafe API key in the TYPESAFE_API_KEY environment
  variable (user scope is read even when the shell started earlier). Each step
  sends the OCR text of the current screen, the goal/route description and the
  recent steps to https://api.typesafe.ai. No images, saves, user data or
  personal data are sent. A quickrace run makes roughly 40-50 requests.
- No packages beyond the Python standard library.

Performance trace
-----------------
sdk-extra/src/graphics/util/perf_trace.h (wired by sdk.patch) writes one JSON
line per second when REX_PERF_TRACE_FILE is set; otherwise it does nothing.
Fields: fps (guest swaps), frame_max_ms, frame_ms (every frame in the window),
draws, copies (resolves), readbacks, readback_kb, and wait_ms per cause for the
GPU command-processor thread: guest_idle (waiting for guest commands, i.e.
CPU-bound), frame_latency (GPU-bound), resolve_readback, occlusion, memexport
and other. counts holds events per window: occlusion_awaited/unawaited (ROV
occlusion snapshots whose report the guest spins on, or not), viz_queries,
viz_draws (draws conditional on a viz query), bin_selects (SET_BIN_SELECT and
_LO; tiling), predicated_run/predicated_skipped (bin-predicated packets).
Compare stall times as well as FPS: FPS varies with what happens in a race.

summary.json adds frame_ms_p50/p95/p99, hitch_50ms_per_min and
hitch_100ms_per_min, cp_busy_ms_per_s_median (window minus all waits: wall time
the command-processor thread works), counts per second and per frame, and
final_capture statistics of the last capture (mean luma, % black < 8,
% saturated > 247, 16-bin luma histogram).

Comparing runs
--------------
Single runs vary by about 10-20% in FPS. Measure each configuration with
--repeat 3: runs go to <out>\run1..run3 and <out>\summary.json aggregates them
(median of medians, minimum of minimums, union of errors). A/B a cvar on the
same binary with --arg, for example --arg=--readback_resolve=fast. Then:

  python outputs\FM4-build\e2e\compare.py <BuildRoot>\e2e\baseline-v2 <BuildRoot>\e2e\candidate

compare.py prints the differences and a verdict and exits 1 on FAIL: crash,
goal not reached, median FPS down > 5%, frame p99 up > 20%, or an error key
that the baseline does not have. A wait cause up > 20% or different capture
statistics only WARN; races are not deterministic, so check captures by eye.

SDK development loop
--------------------
For performance work on the SDK, edit the BuildRoot copy directly instead of
re-running setup:
1. Edit <BuildRoot>\sdk-source.
2. cmake --build <BuildRoot>\sdk-build, then cmake --install <BuildRoot>\sdk-build
   (inside a VS x64 developer shell with LLVM on PATH, as Build.ps1 sets up).
3. Build.ps1 -Action build -BuildRoot <BuildRoot>: incremental game build,
   copies the SDK DLLs, runs the instruction tests, writes build-result.json.
4. Before committing, regenerate the tracked patch and extra files:
     git -C <BuildRoot>\sdk-source diff -- . ':(exclude)src/graphics/shaders/bytecode' > outputs\FM4-build\sdk.patch
   and copy changed files that sdk.patch does not carry (for example
   src/graphics/util/perf_trace.h) to outputs\FM4-build\sdk-extra.
Only setup checks the sdk.patch digest (.fm4-patch-sha256). Do not re-run setup
on a development BuildRoot; verify a clean setup in a new BuildRoot at the end
of each stage.
