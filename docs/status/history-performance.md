# Performance history (13-15 October 2026)

Dated performance measurements of the FM4 recompilation, taken on the development computer (RTX 5070 Ti Laptop, Windows). The main tools are `e2e/e2e.py` with its per-second perf trace (`REX_PERF_TRACE_FILE`) and the frame graph (`REX_FRAME_GRAPH`). The trace variables are described in [traces](../e2e/traces.md). These numbers are baselines measured at the time, not targets. The present state is in [current status](current.md), and the chronology of results is in [status history](history.md).

Terms used below:

- RTV is the host render target path (`--render_target_path_d3d12=rtv`). ROV is the pixel shader interlock path (`--render_target_path_d3d12=rov`, selected by `Build.ps1 -Rov`).
- "Command processor" (CP) is the thread that parses PM4 command packets.
- `fps_mean` is frames divided by time, over the last 30 seconds of the race, unless stated otherwise.
- p95 and p99 are the 95th and 99th percentile frame times.

Measurement caveats recorded during the work:

- The median per run varied by up to 7% between runs of the same binary. Differences below 10% were not readable.
- A hot laptop changed the same binary from 53.4 to 48.8 fps. Sequential A/B comparisons must be bracketed by controls or interleaved.

## 13 October 2026

### Baseline

- Measured with `e2e/e2e.py --goal quickrace` (Quick Race Bernese Alps, cockpit camera, last 30 seconds of the race) on the development computer: median fps 24-30, minimum about 19-24.
- The GPU command processor thread spent about 150-260 ms per second waiting for synchronous resolve readback (`--readback_resolve=full`), and 250-335 ms per second waiting for the ROV occlusion query. It almost never waited for commands from the game. So the main bottleneck was GPU-CPU synchronisation, not the recompiled game code. This is a baseline, not a target.

### Baseline v2

- Baseline-v2 (new counters, `quickrace --repeat 3`, fps_mean, last 30 seconds): 27.3 fps.
- Per frame: 1 occlusion report awaited by the game and 3 that are not awaited, 0 viz queries, 4 `SET_BIN_SELECT`, and about 40% of predicated packets skipped (tiling happens).
- Re-run of the same binary: 27.0 fps.

### A/B settings on the same binary

fps_mean, median of 3 runs:

| Setting | fps_mean |
|---|---|
| `rov_occlusion_async=all` | 30.0 |
| `readback_resolve=fast` | 31.0 |
| `readback_resolve=none` | 35.6 |
| `rov_occlusion_async=all` + `readback_resolve=fast` | 55.8 (p99 52 to 34 ms) |

- Removing only one synchronisation moves the wait to the other one, so both had to go. The quick race cockpit capture looked equal to the baseline.
- On the project owner's decision, this combination became the launcher default before the full gate. It was replaced on 14 October by RTV, then restored, and since 15 October RTV is the default again (see [status history](history.md)).

### GPU timestamps

- With the combination above, the GPU was busy about 15.6 ms per frame on the RTX 5070 Ti Laptop, and the resolve took about 0.2 ms.
- The command processor thread was almost fully busy. So a stable 60 fps needed a reduction of GPU and CPU work (stage 2).

## 14 October 2026

### RTV and ROV

- RTV ran stably at 60 fps on quickrace with `--render_target_path_d3d12=rtv --edram_single_rt_no_wrap=true --rov_occlusion_async=previous`: fps_mean 59.8-59.9, p99 about 19.6 ms, GPU about 10 ms per frame.
- ROV default: 54.3 fps, p99 35 ms, GPU 17.5 ms per frame.
- Cause of the black menu, as estimated at the time: depth-only draws per tile bind one render target. Its height was estimated at 720, so its EDRAM region wraps past the end of the EDRAM and takes the colour tiles at base 0. `edram_single_rt_no_wrap` prevents this.
- Report occlusion is computed as a cumulative snapshot with a host query (`rtv_occlusion_snapshots`).
- Occlusion modes: off and unawaited 35-49 fps; previous 60 fps.

### Predicated tiling (phase 2.1)

- FM4 chooses the tiling per render scenario (`media/RenderScenarios`, `TilingScenario`). Races of 8-16 cars use 1 (2 tiles, 2x MSAA, 4 bin selects per frame). Pre-race, replay and 1-car races use 0 (more tiles). The UI uses 2 (one 720p tile, no MSAA).
- Override `FM4_TILING_SCENARIO=2` (`Build.ps1 -Action run -SingleTile`, with FXAA) on ROV, 3 runs against baseline-v3:
  - fps_mean 54.3 to 58.6.
  - p95 32.9 to 19.5 ms.
  - Hitches above 50 ms: 13.8 to 2.0 per minute.
  - GPU 17.5 to 13.0 ms.
  - Draws per frame about 2,190 to 1,680.
  - No new errors and no clear-colour frames.
- Value 0 was slower (44.5 fps, 5 bin selects), and 3 was also slower (48.5 fps).
- Edges were clearly jagged without MSAA. FXAA smoothed part of it, but thin cables broke up.
- Not made default. The choice between quality and fps was left to the project owner. Careers, photos and Autovista were not tested with this option.

### Compiler flags (phase 1.6)

- The limit was the command processor thread in the SDK (busy about 960-990 ms/s, guest_idle about 1 ms/s), not the game code. So LTO and PGO of the game were not measured.
- SDK ThinLTO and SDK `-march=x86-64-v3` (DLLs installed in the game-build copy), 3 runs each: 49.8 and 49.7 fps. Baseline before and after: 53.4 and 48.8 fps.
- No measurable gain. CPU time per zone was also equal.
- The drop from 53.4 to 48.8 fps on the same binary came from the laptop's condition (heat).

### Command processor profile and vblank_vrr

- Sampling profile of the CP thread (14 October): about 30% of its time was in `PM4_WAIT_REG_MEM`, mainly the swap waiting for the guest vblank (address 0x1fca4004, the D3D swap routine sub_826E53E8). This time was previously counted as busy.
- After it was separated (`wait_reg_mem` in the perf trace), the CP thread was busy about 70%.
- On ROV the limit is the GPU (about 19 ms per frame), so optimising CP code did not raise ROV fps.
- A frame slightly above 16.7 ms was held to the next vblank (33.3 ms). `vblank_vrr=true` sends a vblank as soon as a late frame is ready, and stays on the 60 Hz grid when the frame is on time.
- Three runs, bracketed by baselines of 50.6 and 50.7 fps:
  - `vblank_vrr`: 52.9 fps, p95 33.2 to 24.7 ms, p99 34.9 to 28.9 ms, frames of 25-34 ms from 15.5% to 4.3%.
  - With `-SingleTile`: 57.9 fps, p99 26.8 ms (without VRR: 58.8 fps and p99 33.1 ms).
- Race timer over wall clock ratio: 0.996.
- `vsync=false` (1 kHz vblank) crashed.
- Final version (the vblank is forced after 2 fixed intervals, so the next late frame gets a vblank immediately), A/B interleaved in 3 pairs: fps 52.7 vs 52.5, p95 33.2 to 23.3 ms, p99 35.4 to 26.0 ms, frames of 34-50 ms from 0.8% to 0.1%. Same errors, no clear-colour frames.
- Default launcher since 14 October 2026, on the project owner's decision. `Build.ps1 -FixedVblank` restores the fixed grid.
- Careers, photos and Autovista were not tested with VRR at that time. Careers and photos later passed e2e with the default launcher.

### Project runtime (14 October)

- The project runtime (`work/runtime`) replaced `sdk.patch` and `sdk-extra` on this day (see [status history](history.md)).
- e2e quickrace with camera cycling, alternating old and new runtime: 49.4 vs 52.0 fps, and 57.4 vs 58.7 fps.

### GPU time per pass (phase B2, 14 October)

- `REX_FRAME_GRAPH` with timestamps, ROV race frame: 20.6 ms. About 15 ms was in the upper scene tile pass (772 and 525 draws, about 5.7 ms each). The bottom tile took 0.75 ms. Resolve and post-processing took under 1 ms.
- RTV, same scene passes: about 4.2 ms. The mirror (635 draws) took 3.4 ms. The first pass on a new surface took about 0.13-0.15 ms (EDRAM ownership transfer).
- The main ROV cost is scene shading with interlock, not redrawing the tiles.

## 15 October 2026

### Vertex buffer residency cache and readback

- The vertex buffer residency cache now uses an invalidation epoch of shared memory.
- Before, a vertex range request was skipped while the address and fetch constant size stayed the same, also across frames. So a buffer rewritten in place kept its old GPU copy. `REX_STALE_PAGE_TRACE` line K showed one 32 KB buffer during loading, with 76-192 stale bytes.
- Readback resolve no longer overwrites pages that the guest wrote since the resolve.
- Neither change was the cause of the lost road segments.

### Frame rate caps and 120 Hz

- The 30/60 caps: race scenarios lock FrameRate to 60, and countdown, post-race, replay and photo mode lock it to 30. The game follows the refresh rate that the kernel reports.
- With `video_mode_refresh_rate` 120 and FrameRate 120 (`FM4_FRAME_RATE`, launcher `-FrameRate 120`) on RTV, Quick Race timeline: 114.7 fps, median frame 8.4 ms, p95 11.6 ms, p99 16.1 ms, no hitch above 50 ms, same errors as at 60 fps.
- Race clock over wall clock: 1.001 (`race_clock.py`). The game scales its time step, so it does not run faster.
- Each frame of the frame graph contains a full scene pass (about 1,500-1,900 draws), not a duplicate frame.
- Refresh 120 alone: 100 fps, clock 1.001. FrameRate 120 at 60 Hz alone: stays at 60.
- GPU about 5 ms per frame. The limit is the command processor.
- Not tested at that time: careers, a full replay, photo mode, split screen, and subjective physics and handling. Careers, replay and photo mode later passed e2e at 120 and 240 Hz.
- Correction (15 October): the 114.7 fps figure came from the last 20 seconds of a run, when the car (throttle held, no steering) faced a wall. That part had about 850 draws per frame. Grouped per second by draws per frame, about 2,400-2,800 draws (open track) give 70-75 fps, with the CP busy about 880 ms/s and the GPU at 8-9 ms per frame. 120 fps occurs only when there are far fewer draws. For 120 fps on the track, CP work per draw must fall (phase B3).

### RTV default again (15 October)

- Quick Race timeline with the new default, 3 runs: fps_mean 58.1, 58.3 and 59.8; p95 19-20 ms; p99 20-21 ms; no hitch above 50 ms; GPU 8.4-9.4 ms per frame; 0 clear-colour frames; no crash; errors as in the baseline.
- One run used the fGPU fallback (section 0x1C15A4 bytes, succeeded), and its start straight was drawn.

### EDRAM transfers on RTV (phase B2)

- A third of the GPU time in race frames (frame graph: 10.8 ms) was in passes that trigger an ownership transfer, mainly a clear draw.
- `REX_TRANSFER_DRAW_TRACE`: FM4's clears are rectangle lists of 3 vertices with screen coordinates, with clipping off, scissor 8192, depth/stencil ALWAYS and REPLACE. They are often at 4x MSAA with half pitch (640 for 1280 at 2x, 280 for 560 at 1x).
- Without vertex positions, the draw height estimator treats such a clear as using EDRAM to the end (up to 2048 tiles). So each clear transferred the whole EDRAM. Depth transfers are expensive on NVIDIA (stencil per bit, no `SV_StencilRef`).
- `execute_unclipped_draw_vs_on_cpu` (default true in Xenia, false in the ReXGlue SDK) evaluates the vertex shader of that clear on the CPU. It is now true by default in the runtime.
- Race frame at 1x: GPU 10.8 to 7.8 ms, transfer passes 4.5 to 1.6 ms. At 2x: GPU 16.0 to 11.0 ms, transfer passes 9.5 to 3.5 ms.
- Command processor time did not change: 60 fps 600 to 573 ms/s. At 120 Hz with 2,400-2,800 draws per frame: 74.5 fps vs 74.0 fps.
- Quick Race, 3 runs: 57.3, 58.2 and 59.2 fps; p99 20-22 ms; GPU 7.2-8.3 ms. Camera cycle: 59.9 fps. ROV: 58.1 fps, p99 27 ms (earlier 54.3 fps and 35 ms). All without a crash, with no clear-colour frames, and with errors as in the baseline.
- Remaining transfer: real-size clears in the 4x half-pitch format, and the next pass in the native format (about 0.5-0.8 ms at 1x).
- Next candidate: draw the clear directly to a render target in the native format (viewport x2), so that the transfer in and out disappears.

### Remaining clear transfers (`rtv_uniform_draws`, default on)

- The FM4 clear is a rectangle list of 3 vertices, with clipping off and depth/stencil ALWAYS and REPLACE, with no blending. Its pixel shader reads the colour from the interpolator (the same at all three vertices). A depth-only clear uses the EDRAM mode `kDepthOnly`. One target is often cleared with several rectangles (240 + 16 and 480 + 32 pixels).
- Part 1: tiles that are completely overwritten by a rectangle are not transferred. A cutout is applied per slot. Depth is cut out only when depth and stencil are written in full, and colour only when all channels are written.
- Part 2: a 4x MSAA half-pitch clear that writes the same value to all samples (a pixel shader without texture or position, with the same interpolator and Z at every vertex) is drawn directly to the 2x or 1x render target that uses that EDRAM next. This is learned per clear signature. For that draw only, `RB_SURFACE_INFO` is changed to pitch x2, and the viewport and scissor are scaled as for resolution scaling (x2 on both axes for 1x). So each guest sample falls on the same host pixel or sample position in the EDRAM.
- Rectangles must be axis-aligned (3 vertices at 3 different corners).
- Race frame (frame graph): the transfer pass at 1x went from 1.57 to 0.45 ms, and GPU from about 7.7 to 6.4-7.5 ms. At 3x, the total went from 18.9 to 15.2 ms, and the transfer passes from 9.4 to 2.0 ms.
- At the same draw load (2,400-2,800 per frame): 57.2 fps vs 52.5 fps.
- Menu A/B: identical, except the scrolling text. Quick Race, 3 runs: 56.7-57.8 fps. Camera cycle: 58.3 fps. Scale 3x without clear-colour frames, with errors as in the baseline.
- `--rtv_uniform_draws=false` disables it for comparison.

### Pass 16_16 and composite transfers (phase B2)

- Split clears (strip 480..512 first, then 0..480; shadow, 16_16, env): the transfer into the slot overwritten by the first rectangle is deferred, with that rectangle as a cutout. The next adjacent rectangle to the same render target extends the cutout. Any other draw, a resolve or the end of the submission performs the transfer outside the cutout only (`D3D12RenderTargetCache::PendingTransfers`).
- Overwriting is not only for clears. A rectangle list of up to 8 rectangles that form one rectangle, or one quad made of two triangles (strip, fan, list, quad list), also counts, with clipping (cut by the viewport, Z must be inside the clip volume) and culling (a culled primitive rejects the draw). 1x targets use pixel centres with the top-left rule.
- The FM4 composite vertex shader reads a texture (exposure). A position is accepted when it equals the result of the fetch that returns 0, 16384 or -16384 (the interpreter returns these values for texture fetches).
- The composite pass at 1280/1x (0.5 ms at 3x) and the shadow clear (0.28 to 0.01 ms at 3x) no longer transfer.
- Race frame at 3x (frame graph): 15.2 to 13.1 ms; transfer passes 2.0 to 1.0 ms. Race GPU (perf): 2x 10.6 to 9.8 ms; 3x 13.8 to 13.0 ms.
- What remains is mainly the depth pass 16_16. Its clear writes Z without stencil, and stencil must be kept. This GPU has no `SV_StencilRef`, so the stencil is transferred in 8 passes, about 0.5 ms at 3x.
- Deferring that transfer until the stencil is read did not help, because the next pass 560/1x does not cover the 512..560 padding, and it reads it.
- Validation: menu A/B identical, except the scrolling text and the showroom fade-in, which were captured at different points. Quick Race, 3 runs: 57.4-58.8 fps. Camera cycle: 59.9 fps. 2x: 57.4 fps. 3x: 57.3 fps. No crash, no clear-colour frames.

### Half-rate rear mirror (phase B2)

- FM4 draws the rear mirror every other frame in 12-car and 16-car races, and every frame in 1-car and 8-car races (frame graph: about 220 mirror draws on odd frames and 0 on even frames in the scenarios that halve it).
- The override `FM4_HALF_RATE_MIRROR=0|1` sets the value for all race scenarios. The launcher and the e2e harness use 1 (`Build.ps1 -FullRateMirror` sets 0).
- Quick Race with 12 cars, mirror every frame vs half rate: 55.6 vs 59.7 fps; GPU 9.5 vs 8.1 ms; CP 717 vs 667 ms/s. At 120 Hz: 75.4 vs 83.6 fps.
- The saving that 1-car and 8-car races now get was not measured directly, because the e2e timeline only covers Quick Race with 12 cars.
- The mirror renders normally.

### Resolution scale 2 and 3

- `draw_resolution_scale` 2 and 3 ran on RTV, but all content that goes through a full resolve (scene 7e3 to 2_10_10_10, and the post-processing chain) looked jagged like a comb, also without MSAA.
- Cause: FM4's full resolve shader (from Xenia Canary) writes scaled data in Canary groups (32 bpp: 16x2 blocks). ReXGlue's texture loads and fast resolve (ReXGlue commit `f5337cdc`) read the older groups, one row of 16 bytes (8 bytes for 8 bpp).
- Fix: `fm4-resolve/texture_address.xesli` (ReXGlue grouping) and `resolve_full_64bpp.xesli` (the second pixel of the pair, at its group address). The 1x bytecode is identical.
- Quick Race, scale 2 (2560x1440): 57.3 fps, p99 22 ms, GPU 11.3 ms.
- Quick Race, scale 3 (3840x2160): 56.5 fps, p99 24.5 ms, GPU 16.7 ms.
- The image was sharp, and the menus, pre-race and race looked normal, with no clear-colour frames.
- Launcher: `Build.ps1 -Action run -ResolutionScale 2|3`. Not yet tested: photo mode, replay, careers and Autovista.

### Readback CPU downscale on resolution scale

- `readback_resolve=fast` downscales resolved data to 1x for guest memory with `resolve_downscale.cs.hlsl`. That shader assumes that scale_x * scale_y host pixels are in sequence for each guest pixel.
- But the data are written in groups of one 16-byte row (8 bytes for 8 bpp), and the host groups are column-major. So the odd pixels of each group copied their neighbour, which made zig-zag edges that alternate in guest memory.
- The shader now uses the group layout, and it is compiled at build time with the FM4 resolve shaders. The ReXGlue bytecode of the old version was removed from the repository.
- Readback dump (`REX_READBACK_DUMP`) of resolve 2_10_10_10, 1280 x 224, at 0x1ba91000. Luma difference between even and odd columns: old 2x 0.44; new 2x 1.01; new 1x 1.02 (1 means a normal image).
- Quick Race: 2x 58.6 fps, 3x 58.6 fps, no clear-colour frames.

### Command processor: upload removal (phase B3)

- At 120 Hz the CP was busy about 870 ms/s, at about 70 fps on the track. The largest item was guest memory upload: 29-30 MB per frame (2.1 GB/s, about 940 ranges of 32 KB, almost all `memcpy` at 175 ms/s; page protection 18 ms/s). It was triggered by vertex buffer requests.
- Watched CPU writes were only about 3 invalidations per frame (0.7 MB). Exact invalidation (`shared_memory_exact_invalidation`, an experiment, default off) did not change the upload.
- Cause: `clear_memory_page_state` (default true in ReXGlue, and set for certain game hacks in Xenia) removes the valid bits of all pages uploaded by the CPU at the end of every frame (`SetSystemPageBlocksValidWithGpuDataWritten`). So all vertex and texture data in use was uploaded again every frame.
- `REX_STALE_PAGE_TRACE` with `clear_memory_page_state=false`: 0 `S` lines, so no stale bytes in valid pages requested by draws, in 2,280 frames checked. FM4 writes memory only through watched pages.
- The launcher now uses `clear_memory_page_state=false` and `d3d12_submit_thread=true`. The second moves replay and command list execution to another thread, which alone gave +10% fps at 120 Hz.
- Result: upload 30 to 0.1 MB per frame. Race GPU frame (frame graph) 7.7 to 2.5 ms. The scene pass went from 4-5 ms to 0.8 ms, because the GPU had waited for hundreds of small copies from system memory, with barriers in the middle of the pass.
- Quick Race at 60 Hz, 3 runs: 59.3-59.8 fps, p99 20-21 ms, CP 466-506 ms/s (from about 600). Camera cycle: 59.7 fps.
- 120 Hz: 104.2 and 95.0 fps average; on the track 88-98 fps (from 70-75 fps); CP about 780 ms/s.
- Scale 2x: 59.7 fps, GPU 3.7 ms. Scale 3x: 59.9 fps, GPU 6.75 ms (from 13 ms). ROV: 59.9 fps, GPU 9.5 ms (from 16.6 ms).
- No crash, no clear-colour frames, errors as in the baseline, and the menu A/B identical.
- The perf trace now also records `upload_kb`, `upload_ranges`, `invalidations`, `invalidated_kb` and the `shared_memory_copy` and `protect` zones.
- For 120 fps on the track, CP work per draw still has to fall (draws about 420 ms/s and resolve 105 ms/s at 120 Hz). The `WAIT_REG_MEM` wait (about 230 ms/s at 120 Hz) still needed investigation.

### Command processor follow-up: three targets (phase B3)

1. Wait in `WAIT_REG_MEM` at 120 Hz (about 200-230 ms/s on the track).
   - `REX_WAIT_REG_MEM_TRACE`: only one condition waits, a guest memory flag (0x1fca4006 equal to 0), which the guest's vblank interrupt clears. Every frame waits for it, with an average of 5 `Sleep(1)` polls.
   - Two hand-overs used polling. The vsync thread only saw the CP waiting at its next 1 ms sleep, and the CP only saw the flag at the end of its own `Sleep(1)`. So a frame that crossed a vblank grid line lost about 2 ms.
   - Now event-based: the CP wakes the vsync thread when it starts waiting (`WakeVsyncWorker`), the vsync thread wakes the CP after the interrupt (`WakeWaitRegMem`), and the vblank grid uses a high-resolution waitable timer.
   - Trace: about 1.1 wake-ups per wait, so the interrupt does clear the flag.
   - At 120 Hz, race: 88-92 fps to 100-106 fps, at 2,800-3,000 draws per frame. Wait time: 200 to 25-50 ms/s.
   - Quick Race: 104.2 and 95.0 fps to 113.0 and 113.4 fps; p99 13.6-14.8 ms to 11.9 ms. At 60 Hz, p99 20-21 ms to 17.5 ms.
2. CPU profile with symbols (`Build.ps1 -Action profbuild`, `e2e/cp_profile.py`) at 120 Hz:
   - Register writes: 22% of samples. `ExecutePacketType0` calls two virtual `WriteRegister` functions, and `GetRegisterInfo` has a switch of 3,443 cases that is only used for debug logging (4.8%).
   - `CopyReadbackToGuest`: 10% (the `memcpy` of 10.8 MB of readback resolve per frame).
   - `RequestRanges`: 5.9% (2.65% for index buffers).
   - `UpdateBindings`: 5.5%. `RequestTextures`: 4.9%.
3. Fixes:
   - Registers without side effects are stored directly (`WriteRegisterFast`). A bit table marks the hooked registers: scratch, COHER, DC_LUT_*, unknown registers, and the D3D12 constant and fetch ranges.
   - Type-0 packets in sequence go through `WriteRegistersFromMem`, so shader constants are copied per range with one dirty check. Before, 10.8% of samples were in `WriteRegister`, one call per constant.
   - `RequestRanges` runs without a lock. It sorts and scans only when not all pages are valid.
   - The readback resolve is copied into guest memory by its own thread (`d3d12_readback_publish_thread`, true in the launcher). The first data for a destination stays synchronous. The slot is waited on before it is reused. Only pages that still hold GPU data are overwritten, each under its own mutex, which guest write invalidation also takes. It is not the process-wide lock: with the global lock, the CP waited on it during texture loads (2.4% + 1.6% of samples).
   - The temporary vectors of `RequestTextures` are reused.
   - The viewport cache key now includes the depth format. This was a latent bug, because the float24 range could be stale.
   - Waiting for the readback copy at every fence (`d3d12_readback_publish_await_signals`, default off) took almost all of the copy time, because nearly every resolve is followed by a fence. The data came from submissions that had already finished, in both modes.
   - Race frame at 120 Hz (2,400-2,800 draws): CP 8.6-8.9 ms to 4.6 ms. The draws alone went from about 3.5 ms to 2.9-3.0 ms (1.35 to 1.13 us per draw). Work outside the draws (PM4 parsing, registers) went from about 4.2 ms to 1.5 ms. Resolve went from 1.15 ms to 0.12 ms.
4. Final validation:
   - Quick Race at 120 Hz, 3 runs: 119.94, 119.96 and 119.91 fps; p99 8.8-9.8 ms; 0 hitches.
   - 240 Hz: 213 fps average; race 191-201 fps (CP busy about 890-940 ms/s).
   - 60 Hz: 60.0 fps, p99 17.2 ms. Camera cycle: 60.0 fps. Scale 3x: 60.0 fps, GPU 6.7 ms. ROV: 59.99 fps, GPU 7.1 ms.
   - No crash, no clear-colour frames, and errors as in the baseline. Camera-cycle captures looked normal (HUD, mirror, cockpit).
   - Final profile at a flat 240 Hz: `UpdateBindings` 6%; texture path about 8%; register copy 9%; submit thread flush at swap 5%; shader hash in `LoadShader` 2.4%; the perf trace's own zones about 7% (only with `REX_PERF_TRACE_FILE`).
   - The perf trace now records vblanks (grid, late and forced) and the wait in `readback_publish`.

## Undated entries (13-15 October 2026)

These entries in the source have no explicit date. They are grouped here, and each is placed with the period it belongs to.

### Sun query (13-15 October)

- The four vertex sprites measured for the sun query all sit at the same position, so the quad is degenerate. This gives 0 samples on both ROV and RTV.
- Calibrating the glare needs to trace where the corner positions of the sprite should come from.

### Shader cache (13-15 October)

- Synchronous shaders became the default to avoid missed draws that had once caused black photos (the source gives no date).
- Shader cache empty (`--store_shaders=false`): synchronous shaders add a little hitch on loading, about 9 frames above 100 ms per run, with the worst at 400-640 ms, without lowering the race fps.
- Asynchronous shaders discarded about 2,000 draws per run without reducing the hitches. With `async_shader_draw_wait_us=2000`, the discarded draws fell to about 40.
- Synchronous shaders remain the default.

### Command processor time, new default (13-15 October)

- The new default had about 930 ms busy per second and 2,100 draws per frame. Draws took 378 ms/s (vertex buffers 104, bindings 34, primitives 33, textures 28). Command list submission took 199, copy 51 and swap 38. The rest, about 260, went to PM4 parsing.
- This was the next target for a stable 60 fps.

### Submit thread (13-15 October)

- `--d3d12_submit_thread=true` (optional, default off) moves replay and command list execution to another thread. Submission on the CP thread fell from 199 to 32 ms/s, total busy from 935 to 900 ms/s, and fps_mean rose from 54.3 to 55.7 (within noise).
- With this setting the GPU is busy 17.2 ms per frame, above the 16.7 ms budget for 60 fps, so the GPU now limits too.
- `--d3d12_submit_on_primary_buffer_end=false`: 56.6 fps, but one of the three runs failed to reach the race. It was not made default.

### Vertex buffer upload path (13-15 October)

- The upload of guest memory to the GPU took 116 ms/s. The global lock took only 20 ms/s.
