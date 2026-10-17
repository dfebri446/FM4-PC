# Debug trace environment variables

These variables switch on diagnostic output in the runtime and in the FM4 integration code. They are off by default. Pass them with `--env NAME=value` on [`e2e.py`](README.md) (or `matrix.py`, which passes them to every cell). The files they write are local and must go outside the repository, for example under `<BuildRoot>\e2e\<name>`.

`e2e.py` strips the `FM4_*` and `REX_*` variables that it inherits from the shell, so only `--env` values and the launcher's own variables reach the game. Experiment switches that change rendering or timing (A/B overrides, workarounds) are in [experiments.md](experiments.md).

Summary of the variables on this page:

| Variable | Writes | Default cadence | Section |
| --- | --- | --- | --- |
| `REX_PERF_TRACE_FILE` | one JSON line per second (and `.resolves.json`) | every second | [Performance trace](#performance-trace-rex_perf_trace_file) |
| `REX_CP_PROFILE` | stack samples of the command-processor thread | about every millisecond | [Command-processor profiler](#command-processor-profiler-rex_cp_profile) |
| `REX_WAIT_REG_MEM_TRACE` | summary of WAIT_REG_MEM conditions | per run | [WAIT_REG_MEM](#wait_reg_mem-rex_wait_reg_mem_trace) |
| `REX_OCCLUSION_SEGMENT_TRACE`, `REX_OCCLUSION_DRAW_TRACE`, `REX_DRAW_DEPTH_HIST` | occlusion and depth state | per snapshot or frame | [Occlusion and depth](#occlusion-and-depth) |
| `REX_DRAW_LIST_TRACE` | every draw of a sampled frame | `REX_DRAW_LIST_EVERY`, default 10 | [Draw traces](#draw-traces) |
| `REX_IB_TRACE` | indirect buffers | every 60th frame | [Draw traces](#draw-traces) |
| `REX_DRAW_STATS` | pipeline statistics per draw | `REX_DRAW_STATS_EVERY`, default 60 | [Draw traces](#draw-traces) |
| `REX_DRAW_DUMP` | vertex fetch data of matching draws | `REX_DRAW_DUMP_EVERY`, default 30 | [Draw traces](#draw-traces) |
| `REX_BIN_TRACE` | draw packets per bin select | `REX_BIN_TRACE_EVERY`, default 10 | [Draw traces](#draw-traces) |
| `REX_GPU_INVENTORY` | state per shader pair (JSON) | every 600 frames | [Inventory](#inventory-rex_gpu_inventory) |
| `FM4_D3D_TRACE` | calls of PM4 packet writers | every 5 seconds | [PM4 writers](#pm4-writers-fm4_d3d_trace) |
| `REX_FRAME_GRAPH` | passes, resolves and GPU times (JSON lines) | `REX_FRAME_GRAPH_EVERY`, default 600 | [Frame graph](#frame-graph-rex_frame_graph) |
| `REX_TRANSFER_DRAW_TRACE` | draws that needed ownership transfers | `REX_TRANSFER_DRAW_EVERY`, default 60 | [Transfer draws](#transfer-draws-rex_transfer_draw_trace) |
| `REX_LATE_WRITE_TRACE`, `REX_STALE_PAGE_TRACE`, `REX_READBACK_SKIP_TRACE` | data race and stale data findings | every frame or every 4th frame | [Data race and stale data](#data-race-and-stale-data) |
| `REX_READBACK_DUMP` | bytes of one resolve readback | `REX_READBACK_DUMP_EVERY`, default 120 | [Readback dumps](#readback-dumps-rex_readback_dump) |
| `REX_RESOLVE_PUBLICATION_TRACE` | one JSON line per resolve publication, after `<file>.arm` exists | every publication | [Resolve publication](#resolve-publication-rex_resolve_publication_trace) |
| `REX_FILE_READ_TRACE`, `REX_IO_TRACE`, `FM4_STREAM_TRACE` | guest file and I/O activity | every call | [File and I/O](#file-and-io) |

## Performance trace (REX_PERF_TRACE_FILE)

[`work/runtime/src/graphics/util/perf_trace.h`](../../work/runtime/src/graphics/util/perf_trace.h) writes one JSON line per second to the file named by `REX_PERF_TRACE_FILE`. Without the variable it does nothing. `e2e.py` sets it to `<out>\perf.jsonl` for every run, so the harness always has this trace.

Each line (one window) has:

- `t`, `window_ms`: the time of the window and its length.
- `fps`: guest swaps per second in the window. `frame_max_ms`, and `frame_ms` (the time of every frame in the window).
- `draws`, `copies` (resolves), `readbacks`, `readback_kb`, `upload_kb`.
- `invalidations`, `invalidated_kb`: shared memory invalidations by guest CPU writes (any thread; with the pages around the written one).
- `vblanks`: guest vblanks the vsync thread delivered. `grid` is on the refresh grid, `late` is a grid vblank that passed while the command processor worked and was delivered when it waited (`vblank_vrr`), and `forced` is a vblank delivered after two intervals without a wait.
- `wait_ms`: time per cause for the GPU command-processor thread, and `waits`: the number of waits per cause. The causes are:
  - `guest_idle`: waiting for guest commands, i.e. CPU-bound.
  - `frame_latency`: GPU-bound.
  - `resolve_readback`.
  - `occlusion`.
  - `memexport`.
  - `wait_reg_mem`: PM4_WAIT_REG_MEM sleeping until its condition holds. In FM4 this is the swap waiting for the flag that the guest vblank interrupt clears.
  - `readback_publish`: waiting for the resolve readback copies of `d3d12_readback_publish_thread`, either before a readback slot is reused or, with `d3d12_readback_publish_await_signals`, at fence writes, interrupts and the swap.
  - `other`.
- `counts`: events per window:
  - `occlusion_awaited` and `occlusion_unawaited`: ROV occlusion snapshots whose report the guest spins on, or not.
  - `viz_queries` and `viz_draws` (draws conditional on a viz query).
  - `bin_selects` (SET_BIN_SELECT and _LO; tiling).
  - `predicated_run` and `predicated_skipped` (bin-predicated packets).
  - `upload_ranges` (shared memory uploads).
  - `pipeline_draws_waited`, `pipeline_draws_dropped`, and `submissions`.
- `cpu_ms`: command-processor time per zone, inclusive (`draw` contains the `draw_*` zones): `draw`, `draw_primitives`, `draw_render_targets`, `draw_pipeline`, `draw_textures`, `draw_bindings`, `draw_vertex_buffers`, `copy`, `submit`, `swap`, `shared_memory_lock`, `shared_memory_upload` (split into `shared_memory_copy` and `shared_memory_protect`), `submit_record` and `submit_execute`.
- `gpu`: GPU timestamps of the frames in the window (`frames`, busy time, `resolve_ms`, `span_ms`).
- `occlusion_samples`: mean passing samples per occlusion interval, `unawaited` and `awaited`.

Compare stall times as well as FPS: FPS varies with what happens in a race. Group seconds by draws per frame to compare runs at the same load (a car facing a wall draws a third as much). `summary.json` has the medians of these per run (see [README](README.md#reading-summaryjson)).

### Resolve inventory

Next to the trace, the runtime rewrites `<REX_PERF_TRACE_FILE>.resolves.json` every 10 windows. It lists every resolve destination seen in the run (address, length, format, pitch and height), with the number of resolves, and the first and last time (seconds) each was seen. It is used to find which resolves the CPU actually needs. (The harness writes this file as `perf.jsonl.resolves.json` in the run directory.)

## Command-processor profiler (REX_CP_PROFILE)

`REX_CP_PROFILE=<file>` samples the stacks of the GPU command-processor thread about every millisecond ([`cp_sampler.h`](../../work/runtime/src/graphics/util/cp_sampler.h)). Sampling starts after `REX_CP_PROFILE_DELAY` seconds (default 0) and lasts `REX_CP_PROFILE_SECONDS` (default 30). The stacks are written as lines of `<count> <module>+0x<offset> ...`, innermost frame first, for offline symbolization.

Build the runtime with line tables first. `Build.ps1 -Action profbuild` builds `<BuildRoot>\game-build-prof` from a mirror of `runtime-source` (`runtime-source-prof`); the default build is not touched. Then run with `--game-build game-build-prof` and summarize:

```
python outputs\FM4-build\e2e\e2e.py --build-root <BuildRoot> --game-build game-build-prof ... --env REX_CP_PROFILE=<file>
python outputs\FM4-build\e2e\cp_profile.py stacks.txt <BuildRoot>\game-build-prof
python outputs\FM4-build\e2e\cp_profile.py stacks.txt <BuildRoot>\game-build-prof --function IssueDraw
```

`cp_profile.py` reports per runtime function, or with `--function` per source line of the matching functions. For a Quick Race, `REX_CP_PROFILE_DELAY=156` starts sampling in the race. At 240 Hz (`--arg=--video_mode_refresh_rate=240 --env FM4_FRAME_RATE=240`) the command processor, not the vblank, sets the frame rate.

## WAIT_REG_MEM (REX_WAIT_REG_MEM_TRACE)

`REX_WAIT_REG_MEM_TRACE=<file>` summarizes the WAIT_REG_MEM conditions of the command processor ([`work/runtime/src/graphics/command_processor.cpp`](../../work/runtime/src/graphics/command_processor.cpp)): how often each condition was waited on, its polls, how many sleeps a vblank ended early, and its wait time.

## Occlusion and depth

- `REX_OCCLUSION_SEGMENT_TRACE=<file>`: the occlusion snapshot segments.
- `REX_OCCLUSION_DRAW_TRACE=<file>`: the draw after each occlusion snapshot.
- `REX_DRAW_DEPTH_HIST=<file>`: the per-frame depth state.

These are read in [`work/runtime/src/graphics/d3d12/command_processor.cpp`](../../work/runtime/src/graphics/d3d12/command_processor.cpp). The related cvar `rtv_occlusion_debug=noqueries|fake`, which changes the RTV occlusion reports for experiments, is in [experiments.md](experiments.md).

## Draw traces

All of these write text files. The sampling variables select frames; the defaults are listed.

- `REX_DRAW_LIST_TRACE=<file>` (`REX_DRAW_LIST_EVERY`, default 10): lists every draw of every Nth frame, with shaders, buffer addresses, render target state and the backend outcome. A race frame has about 3,000 lines; use 20 to keep the timing close to an untraced run. Each frame starts with `F <frame> <steady clock ms>`, and each draw has the vertex and pixel shader, index count and base, the color and depth info, the window offset, the EDRAM mode and the outcome. Captures write their steady-clock time to `capture\captures.txt` to match frames.
- `REX_IB_TRACE=<file>`: for every 60th frame, each indirect buffer with its nesting depth, guest address, length, content hash and the draws inside it.
- `REX_DRAW_STATS=<file>` (`REX_DRAW_STATS_EVERY`, default 60): for each draw of a sampled frame, the shader pair, buffers, render targets and window offset, with the D3D12 pipeline statistics: input assembler primitives, clipper invocations and primitives, and pixel shader invocations.
- `REX_DRAW_DUMP=<file>` with `REX_DRAW_DUMP_VS=<vertex shader hash, low 32 bits>` (`REX_DRAW_DUMP_EVERY`, default 30): for matching draws, the vertex fetch constants, index and vertex data hashes, all vertex shader float constants, and the positions of the first 24 vertices computed on the CPU. Use it to compare the same draw between runs.
- `REX_BIN_TRACE=<file>` (`REX_BIN_TRACE_EVERY`, default 10): every draw packet the command processor reaches, per shader pair, bin select and mask, and whether predication skipped it. It tells "the game did not send it" from "skipped".

## Inventory (REX_GPU_INVENTORY)

`REX_GPU_INVENTORY=<file.json>` aggregates the guest GPU state per shader pair: render target and depth formats, MSAA, blend, depth state, texture formats, primitive types and memexport. The file is rewritten every 600 frames. Summarize one file per scenario and the combination with:

```
python outputs\FM4-build\e2e\inventory.py --build-root <BuildRoot> a.json b.json
```

This is the "Stage 3.1 inventory" of the project plan (kept outside the repo), used to plan a native renderer.

## PM4 writers (FM4_D3D_TRACE)

`FM4_D3D_TRACE=<file>` counts calls, and the top callers, of the guest functions that build PM4 packets. The file is rewritten every 5 seconds. The function list in [`work/fm4-rex/src/fm4-d3d-trace.cpp`](../../work/fm4-rex/src/fm4-d3d-trace.cpp) comes from a static scan of the generated code (addresses and packet names only):

```
python outputs\FM4-build\e2e\pm4_writers.py <BuildRoot>\project\generated\default <BuildRoot>\runtime-source\include\rex\graphics\xenos.h out.json
```

Headers whose packet count is computed at run time (DRAW_INDX, SET_CONSTANT) are not found by the scan.

## Frame graph (REX_FRAME_GRAPH)

`REX_FRAME_GRAPH=<file.jsonl>` (`REX_FRAME_GRAPH_EVERY`, default 600 frames) records the passes of sampled frames, as JSON lines:

- surface pitch, MSAA, color and depth targets with EDRAM base and format, and window offset;
- every resolve (source, clears, destination);
- which resolve outputs each pass samples, also from earlier frames.

With the host render target path, each pass also has the smallest and largest EDRAM length that its draws claimed per binding slot, and its ownership transfers. GPU timestamps give each pass and resolve its GPU time (including the transfers and texture loads issued for it), plus the time between submissions and after the swap. It is cheap enough for `REX_FRAME_GRAPH_EVERY=1`. `frame` lines have the steady clock (`ms`) to match `captures\captures.txt`.

Print a frame with:

```
python outputs\FM4-build\e2e\frame_graph.py --build-root <BuildRoot> trace.jsonl [--frame N | --list]
```

## Transfer draws (REX_TRANSFER_DRAW_TRACE)

`REX_TRANSFER_DRAW_TRACE=<file>` (`REX_TRANSFER_DRAW_EVERY`, default 60 frames) lists every draw whose host render target update needed ownership transfers:

- shader hashes, primitive type and index count, pixel shader flags, normalized depth control and color mask;
- the registers that decide whether the draw overwrites what it claims: depth/stencil control and reference, color mask, blend, surface, render target infos, viewport, scissors and window offset;
- the transfers as `[slot, start tile, end tile, source render target key]`;
- then the clear check (`u<code>`), and the clear rectangle bounds and transfer cutout that the render target cache used.

The column order of the registers is `kTransferDrawRegisters` in [`work/runtime/src/graphics/d3d12/command_processor.cpp`](../../work/runtime/src/graphics/d3d12/command_processor.cpp), which also explains the codes. To compare runs with the clear handling off, see `--arg=--rtv_uniform_draws=false` in [experiments.md](experiments.md).

## Data race and stale data

These checks were written for the RTV missing-road issue, and they are usable for any renderer work. All write files outside the repository.

- `REX_LATE_WRITE_TRACE=<file>`: hashes the memory that the command processor consumes (indirect buffers, constants and shaders loaded from memory, and vertex ranges up to 256 KB), and checks it again at the next signal that the guest CPU can observe. `L` lines are ranges that the CPU changed in between.
- `REX_STALE_PAGE_TRACE=<file>`: keeps a shadow copy of everything uploaded to the GPU and, every 4th frame, compares the bytes that draws request in valid pages with guest memory. `S` lines mean the GPU copy is out of date. `K` lines are vertex ranges whose reuse without the invalidation epoch would have drawn out-of-date data. Each check frame writes an `F` line.
  - `REX_STALE_PAGE_UNWATCHED=1` (slow, about 15 fps) also lists valid pages that the guest could write without a write watch (`U` lines).
  - `REX_STALE_PAGE_FIX=1` invalidates the stale pages so that they are uploaded again. This changes rendering; use it for A/B comparisons only.
- `REX_READBACK_SKIP_TRACE=<file>`: resolve readbacks that left pages alone because the guest wrote them after the resolve.

## Readback dumps (REX_READBACK_DUMP)

`REX_READBACK_DUMP=<directory>` with `REX_READBACK_DUMP_ADDRESS=<guest address, hexadecimal>` (`REX_READBACK_DUMP_EVERY`, default 120) writes the bytes that a resolve readback copies into guest memory, for every Nth readback of that address. Each file, `<address>_<sequence>.bin`, starts with a header line `RBDUMP <RB_COPY_DEST_INFO> <RB_COPY_DEST_PITCH> <length>` (hexadecimal), followed by the bytes. Use it to check the downscaled readbacks of resolution scaling against 1x.

## Resolve publication (REX_RESOLVE_PUBLICATION_TRACE)

`REX_RESOLVE_PUBLICATION_TRACE=<file>` appends one JSON line per resolve publication, up to 100,000 lines, with `sequence`, `address`, `length`, the performance counter `ticks` and `frequency`, and `nonzero_samples_of_64` (how many of 64 bytes sampled across the resolved range are nonzero). Recording starts only once the file `<file>.arm` exists: the runtime checks for it every 250 ms, and once it finds the file it keeps recording (the code does not disarm again, so deleting `.arm` does not stop the trace). Create the `.arm` file while the game runs to start the trace at that point. Source: [`command_processor.cpp`](../../work/runtime/src/graphics/d3d12/command_processor.cpp).

## File and I/O

- `REX_FILE_READ_TRACE=<file>`: every guest file read, with time, status, offset, sizes, buffer, async flag and path. Compare runs to find streaming differences.
- `REX_IO_TRACE=<file>`: `NtReadFile` calls with the event, APC, IOSB and returned status, the guest return addresses of the call chain, and the waits on read events.
- `FM4_STREAM_TRACE=<file>`: the title's resource loader ([`work/fm4-rex/src/fm4-stream-trace.cpp`](../../work/fm4-rex/src/fm4-stream-trace.cpp)): request steps, the section allocation and its failure flags, archive stream reads, `ReadFile` and `GetOverlappedResult` results, decompressor results, and the memory manager's pool table when a section allocation fails.

The streaming fix itself is always on. `FM4_STREAMING_FALLBACK=0` turns it off for comparisons; see [experiments.md](experiments.md).
