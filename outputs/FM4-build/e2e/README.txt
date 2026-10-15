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
mostly the swap waiting for vblank) and other. counts holds events per window: occlusion_awaited/unawaited (ROV
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
command-processor thread stacks (REX_CP_PROFILE_DELAY / _SECONDS, default 0 /
30 s; symbolize with llvm-symbolizer against an SDK built with
-gline-tables-only). REX_WAIT_REG_MEM_TRACE=<file> summarizes WAIT_REG_MEM
conditions. REX_OCCLUSION_SEGMENT_TRACE, REX_OCCLUSION_DRAW_TRACE and
REX_DRAW_DEPTH_HIST trace occlusion snapshot segments, the draw after each
snapshot and per-frame depth state; rtv_occlusion_debug=noqueries|fake changes
RTV occlusion reports for experiments. REX_DRAW_LIST_TRACE=<file> lists every
draw of every REX_DRAW_LIST_EVERY-th frame (default 10; about 3,000 lines per
race frame, use 20 to keep the timing close to an untraced run) with shaders,
buffer addresses, render target state and the backend outcome; captures write
their steady-clock time to capture\captures.txt to match frames.
REX_IB_TRACE=<file> lists, for every 60th frame, each indirect buffer with its
nesting depth, guest address, length, content hash and the draws inside it.

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

Frame rate experiments: FM4_FRAME_RATE=<n> replaces the FrameRate value of the
render scenarios (races 60, countdown/replays/photo mode 30), and
--arg=--video_mode_refresh_rate=<hz> changes the refresh rate reported to the
game and the vblank rate. race_clock.py reads the race timer of the captures
(run with --keep-ppm) and prints its rate against wall time (1.0: real time):

  python outputs\FM4-build\e2e\race_clock.py <BuildRoot>\e2e\<run>
- REX_DRAW_STATS=<file> (REX_DRAW_STATS_EVERY, default 60): per draw of a
  sampled frame, the shader pair, buffers, render targets and window offset
  with the D3D12 pipeline statistics: input assembler primitives, clipper
  invocations and primitives, pixel shader invocations.
- REX_DRAW_DUMP=<file> with REX_DRAW_DUMP_VS=<vertex shader hash, low 32 bits>
  (REX_DRAW_DUMP_EVERY, default 30): the vertex fetch constants, index and
  vertex data hashes and all vertex shader float constants of matching draws,
  to compare the same draw between runs, and the positions of the first 24
  vertices computed on the CPU.

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
goal not reached, median FPS down > 5%, frame p99 up > 20%, or an error key
that the baseline does not have. A wait cause up > 20% or different capture
statistics only WARN; races are not deterministic, so check captures by eye.

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
