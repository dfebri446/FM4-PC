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
default car and track, starts the race and holds the throttle. Add
--steps camera to cycle the race cameras (Back every 6 s) once the race runs.
The run exits
with code 1 when the game crashes or the goal is not reached in time.

Gameplay scenarios (fixed timelines with screen checks, no network access):
  photo       Quick Race, 10 s of driving, then a 30 s measured race phase,
              pause > PHOTO MODE, take a photo, save it as a new file; checks
              SAVE SUCCESSFUL.
  photo_view  a new process opens Career > MY PROFILE > MEDIA > MY PHOTOS and
              loads the saved photo; checks that it replaces the menus without
              an error (OCR reads its watermark only sometimes; see final.png).
  career      Career > GO RACE > WORLD TOUR, the profile's next event (Tsukuba
              Short Circuit with the Ford Ka for the BuildRoot profile), 40 s of
              driving, QUIT RACE, 30 s of the replay, CONTINUE to the event.
  career_finish  the same event to the end, driven by the title's AI
              (engageaionstart, needed): it wins the 3 laps in about 3 minutes;
              CONTINUE, then A every 6 s through YOU PLACED (Total), the
              driver and Ford affinity level 2 rewards (a gift car: the
              default) up to EVENT COMPLETE, after the save.
  career_progress  a new process on that profile: WORLD TOUR shows the next
              event (the TopGear challenge) with the driver at level 2 and
              credits other than the 8,610 CR before the race.
They write saves, so run them on a copy of the profile:
  e2e.py ... --out <BuildRoot>\e2e\photo --scenario photo --profile copy
  e2e.py ... --out <BuildRoot>\e2e\view --scenario photo_view --profile <BuildRoot>\e2e\photo\user-data
  e2e.py ... --out <BuildRoot>\e2e\career --scenario career --profile copy
  e2e.py ... --out <BuildRoot>\e2e\finish --scenario career_finish --profile copy --env FM4_GAME_CMDLINE=engageaionstart
  e2e.py ... --out <BuildRoot>\e2e\progress --scenario career_progress --profile <BuildRoot>\e2e\finish\user-data
--profile copies a user-data root ("copy": <BuildRoot>\user-data) to
<out>\user-data and runs on it; the installed content packages
(user-data\0000000000000000) are linked, not copied. With --repeat each run gets
its own copy. Without the AI driver the career race only holds the throttle (the
Ford Ka ends up at a wall); career_finish needs engageaionstart.
Every scenario except boot starts with one A per splash video read on screen
(Microsoft Studios, Dolby), which skips it: the main menu shows after
about 30 s instead of 47. A at a fixed rate is not used, because it also skips
the short title screen and selects PLAY CAREER in the main menu.

A step [timeout, action, hold, text(, retry)] repeats the action until one of the
'|'-separated texts shows in a capture taken after it, and fails the run
(goal_reached false, failed_step in summary.json) otherwise; '?action' skips the
action when the text already shows, '=TEXT' must be a whole OCR line and a
leading '!' passes when none of the texts shows. Give a step whose screen loads
(taking a photo, a race loading) a retry longer than the load: a repeated A
would act on the next screen. 'select:<label>' moves the menu highlight to
an OCR'd label. The highlighted entry of a vertical menu is the one with red
lines above and below it (ocr-server.ps1 red_above/red_below) and a dark band
(bg), of a horizontal menu the red-framed tile; it moves only when two clear
readings in a row agree, since menus slide and sway (the pause menu at 120 Hz
most of all). Menus that act on their own when idle are not selected but driven
with fixed presses as soon as they show: the career and GO RACE menus hide (Up
shows them again), the post-race menu starts the race replay. The career menu hides about
9 s after the last press at 60, 120 and 240 Hz alike (a wall-clock timer, see
STATUS.txt), so a capture every 2 s can miss a menu that shows only briefly.
'wait' waits without
touching the controller and
'phase:<name>' ... 'phase:end' marks a measured phase. summary.json "phases" has
the perf statistics per phase (photo: race, photo; career: race, replay), and
compare.py fails a phase whose fps_mean drops more than 10%. steps.jsonl records
what each check and select saw.

FM4 menus ignore the first press after some screens open, and the career and GO
RACE menus hide after a few idle seconds (the next press only shows them again),
so the scenarios press until the expected screen shows rather than counting.

e2e.py runs the game with headless=true: XAM prompts such as the keyboard for
a photo name answer with their default text (nobody can type into the host
dialog). Rendering is not affected.

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
work/runtime/src/graphics/util/perf_trace.h writes one JSON
line per second when REX_PERF_TRACE_FILE is set; otherwise it does nothing.
Fields: fps (guest swaps), frame_max_ms, frame_ms (every frame in the window),
draws, copies (resolves), readbacks, readback_kb, and wait_ms per cause for the
GPU command-processor thread: guest_idle (waiting for guest commands, i.e.
CPU-bound), frame_latency (GPU-bound), resolve_readback, occlusion, memexport
wait_reg_mem (PM4_WAIT_REG_MEM sleeping until its condition holds; in FM4
the swap waiting for the flag the guest vblank interrupt clears),
readback_publish (waiting for the resolve readback copies of
d3d12_readback_publish_thread: before a readback slot is reused, or with
d3d12_readback_publish_await_signals at fence writes, interrupts and the swap)
and other. vblanks counts the guest vblanks the vsync thread delivered: on the
refresh grid, late (vblank_vrr: the grid vblank passed while the command
processor worked, delivered when it waited) and forced (two intervals without
a wait). counts holds events per window: occlusion_awaited/unawaited (ROV
occlusion snapshots whose report the guest spins on, or not), viz_queries,
viz_draws (draws conditional on a viz query), bin_selects (SET_BIN_SELECT and
_LO; tiling), predicated_run/predicated_skipped (bin-predicated packets),
upload_ranges (shared memory uploads). upload_kb is the guest memory uploaded
to the GPU, invalidations and invalidated_kb the shared memory invalidations
by guest CPU writes (any thread; with the pages around the written one).
cpu_ms holds command-processor time per zone (draw and its parts, copy,
submit, swap, shared_memory_upload split into shared_memory_copy and
shared_memory_protect). Compare stall times as well as FPS: FPS varies with
what happens in a race. Group seconds by draws per frame to compare runs at the
same load (a car facing a wall draws a third as much).

summary.json adds frame_ms_p50/p95/p99, hitch_50ms_per_min and
hitch_100ms_per_min, cp_busy_ms_per_s_median (window minus all waits: wall time
the command-processor thread works), counts per second and per frame, and
final_capture statistics of the last capture (mean luma, % black < 8,
% saturated > 247, 16-bin luma histogram) and clear_gray_frames: captures in
which the exact FM4 clear color (102,102,102) covers at least 1%, i.e. geometry
such as the road surface was not drawn. compare.py warns when it rises.

Diagnostics (environment variables, off by default; files are local)
------------------------------------------------------------------
Pass with --env NAME=value. REX_CP_PROFILE=<file> samples the GPU
command-processor thread stacks about every millisecond (REX_CP_PROFILE_DELAY /
_SECONDS, default 0 / 30 s). Build the runtime with line tables first
(Build.ps1 -Action profbuild: BuildRoot\game-build-prof, the default build is
not touched), run with --game-build game-build-prof, and summarize:

  python outputs\FM4-build\e2e\cp_profile.py stacks.txt <BuildRoot>\game-build-prof
  python outputs\FM4-build\e2e\cp_profile.py stacks.txt <BuildRoot>\game-build-prof --function IssueDraw

(per runtime function, or per source line of one function). For a Quick Race,
REX_CP_PROFILE_DELAY=156 starts sampling in the race; at 240 Hz
(--arg=--video_mode_refresh_rate=240 --env FM4_FRAME_RATE=240) the command
processor, not the vblank, sets the frame rate.
REX_WAIT_REG_MEM_TRACE=<file> summarizes WAIT_REG_MEM conditions (how often
each waited, its polls, how many sleeps a vblank ended early, its wait time). REX_OCCLUSION_SEGMENT_TRACE, REX_OCCLUSION_DRAW_TRACE and
REX_DRAW_DEPTH_HIST trace occlusion snapshot segments, the draw after each
snapshot and per-frame depth state; rtv_occlusion_debug=noqueries|fake changes
RTV occlusion reports for experiments. REX_DRAW_LIST_TRACE=<file> lists every
draw of every REX_DRAW_LIST_EVERY-th frame (default 10; about 3,000 lines per
race frame, use 20 to keep the timing close to an untraced run) with shaders,
buffer addresses, render target state and the backend outcome; captures write
their steady-clock time to capture\captures.txt to match frames.
REX_IB_TRACE=<file> lists, for every 60th frame, each indirect buffer with its
nesting depth, guest address, length, content hash and the draws inside it.
REX_DRAW_STATS=<file> (REX_DRAW_STATS_EVERY, default 60): per draw of a sampled
frame, the shader pair, buffers, render targets and window offset with the
D3D12 pipeline statistics: input assembler primitives, clipper invocations and
primitives, pixel shader invocations. REX_DRAW_DUMP=<file> with
REX_DRAW_DUMP_VS=<vertex shader hash, low 32 bits> (REX_DRAW_DUMP_EVERY,
default 30): the vertex fetch constants, index and vertex data hashes and all
vertex shader float constants of matching draws, to compare the same draw
between runs, and the positions of the first 24 vertices computed on the CPU.

Stage 3.1 inventory
-------------------
REX_GPU_INVENTORY=<file.json> aggregates the state per shader pair (render
target and depth formats, MSAA, blend, depth state, texture formats, primitive
types, memexport), rewritten every 600 frames. Summarize one file per scenario
and the combination with:

  python outputs\FM4-build\e2e\inventory.py --build-root <BuildRoot> a.json b.json

FM4_D3D_TRACE=<file> counts calls (and the top callers) of the guest functions
that build PM4 packets, rewritten every 5 seconds. The function list in
work\fm4-rex\src\fm4-d3d-trace.cpp comes from a static scan of the generated
code (addresses and packet names only):

  python outputs\FM4-build\e2e\pm4_writers.py <BuildRoot>\project\generated\default <BuildRoot>\runtime-source\include\rex\graphics\xenos.h out.json

Headers whose packet count is computed at run time (DRAW_INDX, SET_CONSTANT)
are not found by the scan.

REX_FRAME_GRAPH=<file.jsonl> (REX_FRAME_GRAPH_EVERY, default 600 frames) records
the passes of sampled frames (surface pitch, MSAA, color and depth targets with
EDRAM base and format, window offset), every resolve (source, clears,
destination) and which resolve outputs each pass samples, also from earlier
frames. With the host render target path each pass also has the smallest and
largest EDRAM length its draws claimed per binding slot and its ownership
transfers. GPU timestamps give each pass and resolve its GPU time (including
the transfers and texture loads issued for it), plus the time between
submissions and after the swap. It is cheap enough for REX_FRAME_GRAPH_EVERY=1;
"frame" lines have the steady clock ("ms") to match captures\captures.txt.
Print a frame with:

  python outputs\FM4-build\e2e\frame_graph.py --build-root <BuildRoot> trace.jsonl [--frame N | --list]

REX_TRANSFER_DRAW_TRACE=<file> (REX_TRANSFER_DRAW_EVERY, default 60 frames)
lists every draw whose host render target update needed ownership transfers:
shader hashes, primitive type and index count, pixel shader flags, normalized
depth control and color mask, the registers that decide whether the draw
overwrites what it claims (depth/stencil control and reference, color mask,
blend, surface, render target infos, viewport, scissors, window offset) and the
transfers as [slot, start tile, end tile, source render target key], then the
clear check (u<code>) and the clear rectangle bounds and transfer cutout that
the render target cache used; see kTransferDrawRegisters in
work\runtime\src\graphics\d3d12\command_processor.cpp for the column order and
the codes. --arg=--rtv_uniform_draws=false turns the clear handling (transfer
cutouts and 4x MSAA clear remapping) off for comparisons.

Data race and stale data checks (for the RTV missing-road issue, usable for any
renderer work; all write files outside the repository):
- REX_LATE_WRITE_TRACE=<file>: hashes the memory the command processor
  consumes (indirect buffers, constants and shaders loaded from memory, vertex
  ranges up to 256 KB) and checks it again at the next signal the guest CPU can
  observe; "L" lines are ranges the CPU changed in between.
- REX_STALE_PAGE_TRACE=<file>: keeps a shadow copy of everything uploaded to
  the GPU and, every 4th frame, compares the bytes that draws request in valid
  pages with guest memory ("S" lines: the GPU copy is out of date); "K" lines
  are vertex ranges whose reuse without the invalidation epoch would have drawn
  out-of-date data. REX_STALE_PAGE_UNWATCHED=1 (slow, about 15 fps) also lists
  valid pages that the guest could write without a write watch ("U" lines).
  REX_STALE_PAGE_FIX=1 invalidates the stale pages so they are uploaded again.
- REX_READBACK_SKIP_TRACE=<file>: resolve readbacks that left pages alone
  because the guest wrote them after the resolve.
- REX_READBACK_DUMP=<directory> with REX_READBACK_DUMP_ADDRESS=<guest address,
  hexadecimal> (REX_READBACK_DUMP_EVERY, default 120): the bytes a resolve
  readback copies into guest memory, after a line with RB_COPY_DEST_INFO,
  RB_COPY_DEST_PITCH and the length; for checking the downscaled readbacks of
  resolution scaling against 1x.
- REX_BIN_TRACE=<file> (REX_BIN_TRACE_EVERY, default 10): every draw packet the
  command processor reaches, per shader pair, bin select and mask, and whether
  predication skipped it; tells "the game did not send it" from "skipped".
- REX_FILE_READ_TRACE=<file>: every guest file read (time, status, offset,
  sizes, buffer, async flag, path); compare runs to find streaming differences.
- REX_IO_TRACE=<file>: NtReadFile calls with the event, APC, IOSB, returned
  status and the guest return addresses of the call chain, and waits on read
  events.
- FM4_STREAM_TRACE=<file>: the title's resource loader (request steps, the
  section allocation and its failure flags, archive stream reads, ReadFile and
  GetOverlappedResult results, decompressor results, and the memory manager's
  pool table when a section allocation fails). The streaming fix itself is
  always on; FM4_STREAMING_FALLBACK=0 turns it off for comparisons.

Without network access (Jev unreachable), --scenario quickrace drives the same
route on a fixed timeline; it reaches the race reliably with the copied profile.

The harness uses the launcher environment (pipeline.launch_env): the shadow
workaround and FM4_HALF_RATE_MIRROR=1 (the rear-view mirror every other frame in
every race); --env FM4_HALF_RATE_MIRROR=0 renders it every frame, also in the 12
car Quick Race.

Frame rates above 60 (Build.ps1 -FrameRate <n> sets both of these):
--arg=--video_mode_refresh_rate=<n> changes the refresh rate reported to the
game, and --env FM4_FRAME_RATE=<n> replaces the FrameRate 60 of the race render
scenarios (countdown, post-race, replays and photo mode keep 30). The guest
vblank then runs at 60 Hz and at <n> Hz only while a race scenario sets the frame
rate (fm4-vblank-rate.cpp); --env FM4_VBLANK_TRACE=1 prints each render
scenario push and pop with the vblank rate, --env FM4_VBLANK_SWITCH=0 keeps the
vblank at the reported refresh rate, and --arg=--vblank_rate=<hz> sets a fixed
startup vblank rate (without FM4_FRAME_RATE). race_clock.py reads the race timer of the captures
(run with --keep-ppm) and prints its rate against wall time (1.0: real time):

  python outputs\FM4-build\e2e\race_clock.py <BuildRoot>\e2e\<run>

Comparing runs
--------------
Single runs vary by about 10-20% in FPS, and a hot laptop drifts: the same
binary measured 53.4 and then 48.8 fps (3 runs each) half an hour apart, so
bracket a candidate with baseline runs before and after it. Measure each configuration with
--repeat 3: runs go to <out>\run1..run3 and <out>\summary.json aggregates them
(median of medians, minimum of minimums, union of errors). A/B a cvar on the
same binary with --arg, for example --arg=--readback_resolve=fast. Then:

  python outputs\FM4-build\e2e\compare.py <BuildRoot>\e2e\baseline-v2 <BuildRoot>\e2e\candidate

compare.py prints the differences and a verdict and exits 1 on FAIL: crash,
goal not reached, FPS (overall or of a phase) down > 10%, frame p99 up > 20%,
or an error key that the baseline does not have. A wait cause up > 20% or
different capture statistics only WARN; races are not deterministic, so check
captures by eye.

Gates
-----
matrix.py runs a set of cells on a snapshot of the game build
(<BuildRoot>\snapshots\<name>, checked against build-result.json), so
Build.ps1 -Action build can rebuild game-build while a gate runs; cells still
run one at a time. Pick the tier by what a change can break:
  smoke  photo, photo_view, career at 240 Hz, one run each (about 10 min):
         every change, before measuring anything
  perf   photo at 240 Hz, three runs: its race phase (12 cars, 30 s) and photo
         mode; for performance work, against a baseline gate of the same tier
  full   photo, photo_view, career at 60 and 240 Hz, three runs each (about
         1 hour): before committing a stage; --hz 60,120,240 adds 120 Hz
  career career_finish and career_progress at 240 Hz, one run (about 8 min):
         a career race to its rewards and the progress after a restart

  python outputs\FM4-build\e2e\matrix.py --build-root <BuildRoot> --name <name> --tier smoke --baseline <older name>

Cells go to <BuildRoot>\e2e\<name>-<hz>-<scenario>; with --baseline each is
compared (compare.py) with the cell of the same Hz and scenario of that gate,
and <BuildRoot>\e2e\<name>.log keeps one line per cell. --scenarios, --hz and
--repeat override the tier, --env and --arg pass to every cell. The photo race
phase replaces the quickrace scenario in the gates; quickrace stays for
comparisons with older results. In the gates an AI driver drives the player's
car (engageaionstart below; --human for the harness throttle), so each race
follows the track instead of ending at a wall.

Developer options
-----------------
FM4 keeps its developer command line parameters, but the retail build never
fills them (STATUS.txt). --env FM4_GAME_CMDLINE="<option> <option>=<value>"
feeds them through the title's own parser after its defaults
(fm4-command-line.cpp; a bare name means name=1). Options whose code the retail
build no longer has are rebuilt in fm4-dev-features.cpp:
  engageaionstart  an AI driver drives the player's car from the start; pause,
                   photo mode, QUIT RACE, replays and CONTINUE work as usual.
                   e2e.py then sends no driving input: throttle, brake and
                   steering steps become a wait ('rt=255') or the remaining
                   button ('rt=255+back' becomes 'back'), and goal mode does
                   not hold the throttle.
Options the retail build still reads work without a rebuild, for example nohud
(no HUD at all; do not use it with scenarios that read the HUD, such as career
waiting for LAPS). camoverride and its fixed cameras are gone from the retail
build and not rebuilt.
--env FM4_TRACE_RACE_SETUP=1 prints the guest return addresses of each race's
player driver creation (the main loop state that loads a race, for finding the
code behind a game mode), every car's driver setup and the pending session
type; FM4_TRACE_RACE_SETUP=<file> also appends 4 KB of the race settings
(sub_822DEC50) to that file when a session starts.

Shader overrides (B4)
---------------------
--arg shader_override_path=<dir> replaces the DXBC translation of a guest pixel
shader with <dir>\shader_<ucode hash>[_<modification>].ps.hlsl (the hash and
modification as in dump_shaders file names). The file defines
  void xe_pixel_shader(inout XePixel p)
reading p.r[] (r# with the interpolators loaded) and writing p.color[i] (oC#)
and p.depth; the runtime compiles it with FXC ps_5_1 after the contract of the
translation it replaces (XE_C(n) for c<n>, XE_TFETCH2D/3D/CUBE(fetch, coord) for
the default tfetch, XE_TEXTURE*/XE_SAMPLER* slots) and the library
work/runtime/src/graphics/d3d12/shader_override.hlsli, which mirrors the DXBC
translator: tfetch, Shader Model 3 multiplication (xe_mul, xe_mad, xe_dp4,
xe_mad_identical where the ucode multiplies identical operands), PWL gamma, and
the epilogue (alpha test, alpha to coverage, exponent bias). runtime.log says
"Shader override ... compiled" or why it kept the DXBC translation (FXC errors,
a signature that differs from the translation's, float24 depth or memexport).
With --arg dump_shaders=<dir>, the contract of every translated pixel shader,
with its ucode, is written there (shader_<hash>_<modification>.ps.contract.hlsli).
Overrides of game shaders are derived from game code: keep them in the
BuildRoot, never in the repository.

Starting points come from the generator (HlslShaderTranslator,
work/runtime/src/graphics/pipeline/shader/hlsl_translator.cpp): for every pixel
shader without an override file, --arg shader_override_generate_path=<dir> writes
HLSL doing the operations of its DXBC translation, each block after the guest
instruction in a comment; copy one to the override directory and edit it.
--arg shader_override_generated=true uses the generated HLSL for all of them, to
test the generator against the DXBC translations with the A/B below.
override_fxc.py compiles overrides like the runtime without starting the game,
against the contracts of a dump_shaders run (about 8 s for 418 shaders):

  python outputs\FM4-build\e2e\override_fxc.py <override file or dir> <dump_shaders dir> [--mods work\fm4-rex\look --define FM4_LOOK_SHARPEN=0.5]


Shader mods change a generated override without copying it: with --arg
shader_mod_path=<dir>, a pixel shader with <dir>\shader_<hash>.mod.hlsli gets its
generated HLSL with that file included after the library, where it redefines
the hooks the generator leaves at each texture fetch (XE_HOOK_TFETCH), float
constant (XE_HOOK_CONSTANT), the registers after the prologue (XE_HOOK_INPUT)
and the outputs (XE_HOOK_OUTPUT); --arg shader_mod_defines=NAME=VALUE;... sets
its defines. The defaults change nothing. work/fm4-rex/look holds the look mods
of FM4's post-processing (original code, see fm4-look.hlsli): Build.ps1 -Action
run -Look "sharpen=0.5,bloom=0.8,contrast=0.2,saturation=1.05,vignette=0.5,
motion_blur=0.5,exposure=0" (pipeline.py and e2e.py --look) copies them to
<BuildRoot>\look and sets the knobs; knobs left out keep the original look.

--arg shader_override_ab_ms=<N> draws with the DXBC translations instead of the
overrides in every other period of N ms of the steady clock, so one static scene
shows both. override_ab.py lists consecutive captures of a --keep-ppm run with
their phase: in a static scene same-phase pairs are identical, and on/off pairs
are identical if the override renders exactly like the translation.

  python outputs\FM4-build\e2e\e2e.py --build-root <BuildRoot> --out <dir> --scenario photo --profile copy --keep-ppm --env FM4_GAME_CMDLINE=engageaionstart --arg shader_override_path=<overrides> --arg shader_override_ab_ms=5000
  python outputs\FM4-build\e2e\override_ab.py <dir> --period-ms 5000 --diff <dir>\ab-diff.png

With 5000 ms and captures every 2 s, each phase has same-phase pairs; photo mode
after the photo is taken (the SAVE/EXIT screen) is static for about 30 s.

Runtime development loop
------------------------
The runtime (ReXGlue SDK fork) is work/runtime in this repository:
1. Edit work/runtime directly.
2. Build.ps1 -Action build -BuildRoot <BuildRoot>: mirrors work/runtime to
   <BuildRoot>\runtime-source, rebuilds and installs the runtime incrementally,
   builds the game, copies the runtime DLLs, runs the instruction tests and
   writes build-result.json.
3. Add new runtime files to outputs\FM4-source-control\tracked-files.json
   (category runtime-fork) and run check-staged-content.py before committing.
Never edit <BuildRoot>\runtime-source: the next build overwrites it. Verify a
clean setup in a new BuildRoot at the end of each stage.
The integration code (work/fm4-rex) is copied to <BuildRoot>\project by
generate, not by build, and only the files listed in tracked-files.json: after
editing or adding one, run generate and then build.
