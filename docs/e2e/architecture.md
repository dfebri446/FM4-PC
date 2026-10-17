# How the e2e harness is built

This page describes the internals of the end-to-end harness: the hooks inside the game, the files and processes that connect them to the Python harness, and every part that depends on Windows or on the D3D12 backend. It is the reference for porting the harness to another platform or GPU backend, for example a Vulkan build. For usage, scenarios and gates, see [README.md](README.md).

The harness runs on Windows x64 with the D3D12 runtime only. The runtime fork has no other GPU backend: `work/runtime/src/graphics` contains `d3d12` and backend-neutral code, and no `vulkan` directory.

## Overview

```
 e2e.py (Python)                                   fm4_rex.exe (game process)
 ---------------                                   --------------------------
 writes  <out>\input.txt  ---------------------->  FM4TestInput: reads it every 50 ms
                                                     (FM4_TEST_INPUT_FILE)
 reads   <out>\capture\frame-NNN.ppm  <---------  FM4CaptureFrames: guest output every 2 s
                                                     (FM4_CAPTURE_DIR)
 reads   <out>\perf.jsonl  <----------------------  perf_trace.h: one JSON line per second
                                                     (REX_PERF_TRACE_FILE)
 reads   <out>\runtime.log, stderr.log  <---------  runtime log (log_file cvar), stderr
 watches the process exit code

 ocr-server.ps1 (PowerShell, Windows OCR)
   stdin:  "<path.ppm>|2"
   stdout: one JSON line of text lines with boxes and color measurements

 jev.py (goal mode only) --HTTPS--> api.typesafe.ai
```

The harness and the game share no sockets, pipes or shared memory. Every channel is a file in the run directory or an environment variable or cvar set at launch. The game never receives desktop input, and the harness never reads the screen: captures come from the GPU presenter's guest output.

## Launch

`run_once` in [`e2e.py`](../../outputs/FM4-build/e2e/e2e.py) builds the command line and the environment, then starts the game with `subprocess.Popen` (working directory: the BuildRoot; stdout and stderr: `<out>\stderr.log`).

Command line: `<BuildRoot>\<game-build>\fm4_rex.exe --name=value ...`. The values come, in this order (later ones win), from:

1. `pipeline.launch_cvars` in [`pipeline.py`](../../outputs/FM4-build/pipeline.py), shared with `Build.ps1 -Action run`. `game_data_root` comes from `<BuildRoot>\local.json` (`disc1`), and `log_file` is `<out>\runtime.log`.
2. `headless=true`, so XAM prompts such as the keyboard dialog answer with their default text (`work/runtime/src/kernel/xam/xam_ui.cpp`).
3. `user_data_root=<out>\user-data` with `--profile`.
4. `pipeline.look_cvars` (shader mods of the look) and `pipeline.ao_cvars` (`post_ao`).
5. The `--arg` items.

The runtime does not reject an unknown `--name=value`. `work/runtime/src/core/cvar.cpp` stores it for a cvar that may register later, and the value is dropped without an error if no cvar of that name ever registers. A backend that lacks a cvar of the launcher therefore runs with its default, silently.

Environment:

1. The parent environment without the `FM4_*` and `REX_*` variables.
2. `pipeline.launch_env()` (`FM4_HALF_RATE_MIRROR=1`).
3. `FM4_CAPTURE_DIR=<out>\capture`, `FM4_TEST_INPUT_FILE=<out>\input.txt`, `REX_PERF_TRACE_FILE=<out>\perf.jsonl`.
4. `FM4_RACE_SETTINGS` of the quality preset (`pipeline.race_settings`).
5. The `--env` items.
6. `FM4_INFO` (the settings overlay text, `pipeline.info_overlay`) and `FM4_INFO_SHOW=1` unless `--env` sets it.

With `FM4_GAME_CMDLINE` containing `engageaionstart`, the title's AI drives the player's car, and `without_driving_input` removes throttle, brake and steering from the steps.

## Game-side hooks

All hooks are opt-in. Without their variable they do nothing, and a normal launcher run never sets them.

| Variable | Source | What it does |
| --- | --- | --- |
| `FM4_TEST_INPUT_FILE` | [`fm4-test-input.h`](../../work/fm4-rex/src/fm4-test-input.h), registered in `OnPostSetup` of [`fm4_rex_app.h`](../../work/fm4-rex/src/fm4_rex_app.h) | adds a synthetic controller (an `InputDriver`) next to the SDL input |
| `FM4_CAPTURE_DIR` | [`fm4-frame-capture.h`](../../work/fm4-rex/src/fm4-frame-capture.h), thread started in `OnPostLaunchModule` of `fm4_rex_app.h` | writes the guest output as PPM files every 2 s |
| `FM4_CAPTURE_DIR` (side effects) | `fm4-crash-diagnostics.cpp`, `fm4-ui-diagnostics.cpp`, `fm4-jpeg-diagnostics.cpp`, `fm4-gpu-probe.cpp` | crash handler with a minidump, UI and input traces on stderr, photo JPEG diagnostics, a GPU register probe per capture |
| `REX_PERF_TRACE_FILE` | [`perf_trace.h`](../../work/runtime/src/graphics/util/perf_trace.h) and its callers | one JSON line per second of command-processor waits, zones, counts and GPU times ([traces.md](traces.md#performance-trace-rex_perf_trace_file)) |
| `FM4_INFO`, `FM4_INFO_SHOW` | [`fm4-info-overlay.h`](../../work/fm4-rex/src/fm4-info-overlay.h) | settings overlay drawn by the presenter over the guest output, so never in the captures |
| `FM4_GAME_CMDLINE` | [`fm4-command-line.cpp`](../../work/fm4-rex/src/fm4-command-line.cpp) | the title's developer options, for example `engageaionstart` |
| `FM4_RACE_SETTINGS`, `FM4_HALF_RATE_MIRROR`, `FM4_FRAME_RATE` | `fm4-render-overrides.cpp`, `fm4-vblank-rate.cpp` | race render quality, mirror rate and race frame rate |

### Diagnostic controller (FM4_TEST_INPUT_FILE)

The file holds one line of seven integers:

```
<buttons> <left trigger> <right trigger> <lx> <ly> <rx> <ry>
```

- `buttons` is the XInput bit mask: up `0x1`, down `0x2`, left `0x4`, right `0x8`, start `0x10`, back `0x20`, LB `0x100`, RB `0x200`, A `0x1000`, B `0x2000`, X `0x4000`, Y `0x8000` (`BUTTONS` in `e2e.py`).
- Triggers are 0 to 255. Sticks are -32,768 to 32,767.
- The driver reads the file at most every 50 ms. A missing, partial or out-of-range file means all controls are released.
- It also produces keystroke events (`GetDeviceKeystroke`) from button changes, for callers that read keystrokes instead of the pad state.
- Every button change is logged to stderr as `FM4 test input: buttons=...`. `fm4-ui-diagnostics.cpp` logs the game's own reads (`FM4 guest input`, `FM4 converted input`).

The harness writes the file through a temporary file and `os.replace`, retrying for up to 0.5 s while the game holds the file open (Windows refuses the replace then). A press is: write the state, hold (0.12 s by default), write all released, wait 0.5 s. A hold shorter than the 50 ms read interval can be missed.

### Frame captures (FM4_CAPTURE_DIR)

`FM4CaptureFrames` runs on its own thread:

1. It polls every 0.5 s until the first capture succeeds, then every 2 s.
2. It calls `Presenter::CaptureGuestOutput`, which copies the last guest output image (before the overlay and before host scaling) to CPU memory as RGBA8 rows. Only `D3D12Presenter` implements it ([`d3d12_presenter.cpp`](../../work/runtime/src/ui/d3d12/d3d12_presenter.cpp)). The method is pure virtual in [`presenter.h`](../../work/runtime/include/rex/ui/presenter.h).
3. It writes a binary PPM (`P6`, RGB, maxval 255) to `capture-in-progress.tmp` and renames it to `frame-NNN.ppm`. On Windows the rename is `MoveFileExW` with retries while the harness has the target open. Other platforms use `std::filesystem::rename`.
4. After `FM4_CAPTURE_ARCHIVE_LIMIT` frames (default and maximum 450), it overwrites `live-latest.ppm` instead.
5. It appends `<index> <steady clock ms>` to `captures.txt`, to match captures with runtime traces.

On Windows the same thread also serves RenderDoc capture requests (`renderdoc-request.txt`, see [experiments.md](experiments.md)).

The harness treats a capture as complete only when its size matches the PPM header (`complete_ppm`), and uses the newest complete one written after the last input (`newest_capture`).

### Performance trace (REX_PERF_TRACE_FILE)

`perf_trace.h` is a header in the backend-neutral part of the runtime. Its callers are partly backend-neutral and partly D3D12:

| File | Hooks | What they record |
| --- | --- | --- |
| `src/graphics/command_processor.cpp` | 17 | guest idle and `WAIT_REG_MEM` waits, predication, bin selects, viz queries |
| `src/graphics/graphics_system.cpp` | 4 | vblanks |
| `src/graphics/shared_memory.cpp` | 4 | invalidations and uploads |
| `src/graphics/d3d12/command_processor.cpp` | 51 | the swap (`OnSwap`), fence waits (frame latency, readbacks, occlusion), CPU zones, draws, resolves, readbacks, submissions, GPU timestamps (`AddGpuFrame`) and occlusion samples |
| `src/graphics/d3d12/shared_memory.cpp` | 5 | upload zones |

`OnSwap` is the only writer: at each guest swap it records the frame time, and once a window of at least one second has passed it writes the line and starts the next window. Its only caller is the D3D12 command processor, so a backend that does not call it writes no perf trace at all.

`summarize` in `e2e.py` needs `fps`, `window_ms`, `wait_ms`, `readbacks` and `draws` in every line. `frame_ms`, `cpu_ms`, `counts`, `occlusion_samples` and `gpu` are optional: without them the matching `summary.json` keys are missing, and `compare.py` shows them as `-`.

### Logs

`log_file` names `<out>\runtime.log`. The harness counts lines that match `\b(E|error|ERROR)\b|\[error\]|\[E\]`, keyed by their text without the first two fields (time and thread), with numbers replaced by `#`. `compare.py` fails a candidate that has an error key the baseline does not have, so a backend that logs different error text needs its own baseline.

## Harness internals

### Steps

A scenario is a list of steps (the syntax is in [README.md](README.md#scenario-steps)). `run_once` executes them in order:

- a plain step waits, then sets or taps the input;
- a checked step (`checked_step`) repeats the action until one of its texts shows in a capture taken after it;
- `select:<label>` (`select_label`) moves the menu highlight to a label;
- `phase:<name>` records the current number of perf lines, so `summary.json` `phases` covers the perf windows between two marks.

The game exiting at any point raises `exited with <code>`, which becomes `crashed` in `summary.json`.

### OCR protocol

`Ocr` in `e2e.py` keeps one [`ocr-server.ps1`](../../outputs/FM4-build/e2e/ocr-server.ps1) process for the run. The protocol is line-based, UTF-8:

1. The server prints `{"ready":true}` once.
2. Each request is one line, `<path to .ppm>|<scale>`. `e2e.py` always sends scale 2.
3. Each answer is one JSON line, `{"lines":[...]}` or `{"error":"..."}`.

Before recognition the server:

1. averages the image down by the integer factor `max(1, height / 720)` (2560x1440 and 3840x2160 captures become 1280x720);
2. converts it to grayscale (`(77 R + 150 G + 29 B) >> 8`) and upscales it by `scale`, since small white text on dark backgrounds recognizes better at 2x;
3. runs the Windows OCR engine (`Windows.Media.Ocr`, en-US) on it.

Each line in the answer has:

| Field | Meaning (all in 720p pixels) |
| --- | --- |
| `text` | the recognized line |
| `x`, `y`, `w`, `h` | the bounding box of its words |
| `bg` | mean luma of the box grown by 6 px left and right and 4 px up and down |
| `red` | per-mille of red pixels in the box grown by 70 px left and right, 14 px up and 20 px down |
| `red_text` | per-mille of red pixels in the box itself |
| `red_above` | per-mille of red pixels in a 12 px band from 16 px above the box, 70 px wider on each side |
| `red_below` | per-mille of red pixels in a 14 px band from 2 px below the box, 70 px wider on each side |

A red pixel is one with R above 150, G below 80 and B below 80: FM4's selection color. These measurements are computed on the image, not by the OCR engine, so they do not depend on which engine reads the text.

### Highlight and menu logic

The constants are tuned to FM4's 720p layout:

- `annotate`: the highlighted line is the one with the highest `max(red, red_text)`, at least 15 and at least 1.3 times the next one. Lines containing `FORZA` (the red logo) and lines at `y >= 640` (`FOOTER_Y`, button hints) are left out.
- `menu_peers`: lines within 40 px of the target's `x` form a column. Their highlighted entry is a dark one (`bg` at most 0.8 times the column median) with red lines above and below, scored by `min(red_above, red_below)`. Otherwise lines within 12 px of the target's `y` form a row, scored like `annotate`.
- `select_label` moves only after two consecutive readings agree on the highlighted entry.
- `shows`: a case-insensitive substring match on any line, a whole-line match with `=`, and negation with `!`.

The scenario texts (`SCENARIOS` in `e2e.py`) were tuned against what Windows OCR reads. Some of them are alternatives because OCR reads a label in more than one way, for example `LVL2|LVL 2`.

### Goal mode

`navigate` sends the OCR'd lines, the goal, the route and the last six steps to Jev ([`jev.py`](../../outputs/FM4-build/e2e/jev.py)), and `choose_action` turns the answers into one controller action. The protocol is plain HTTPS from the Python standard library. Only the API key lookup has a Windows path: without `TYPESAFE_API_KEY` in the environment it reads the user's registry environment (`winreg`), guarded by `os.name == 'nt'`.

### Results

After the game exits or is killed, `run_once`:

1. converts every PPM to a PNG at half size (`ppm_to_png`, its own PNG writer, no image library), and deletes the PPM unless `--keep-ppm` is given;
2. computes luma statistics of the last capture (`capture_stats`) and counts captures in which the FM4 clear color (102,102,102) covers at least 1% (`clear_gray_frames`);
3. reads `perf.jsonl` and `runtime.log`, and writes `summary.json` (fields in [README.md](README.md#reading-summaryjson)).

### Gates and comparison

[`matrix.py`](../../outputs/FM4-build/e2e/matrix.py) verifies the build against `build-result.json` (`pipeline.digest`), copies the `.exe`, `.dll`, `.pdb` and `.map` files of the game build to `<BuildRoot>\snapshots\<name>`, and runs one `e2e.py` process per cell. [`compare.py`](../../outputs/FM4-build/e2e/compare.py) reads two `summary.json` files. Neither calls platform APIs.

## Platform and backend dependencies

### Windows

| Dependency | Where | Used for |
| --- | --- | --- |
| `fm4_rex.exe` | `e2e.py` `run_once`; `pipeline.launch` | the executable name |
| `.exe`, `.dll`, `.pdb`, `.map` | `matrix.py` `snapshot` | files copied into a snapshot (a build that names its binaries otherwise copies nothing) |
| Windows OCR (`Windows.Media.Ocr`, WinRT) through `powershell.exe` | `ocr-server.ps1`; `e2e.py` `Ocr` | every checked step, select and goal step |
| `cmd /c mklink /J` (directory junction) | `e2e.py` `prepare_profile` | linking the content packages into a copied profile |
| `PermissionError` retry on `os.replace` | `e2e.py` `set_input` | sharing violations of Windows; harmless elsewhere |
| `winreg` | `jev.py` `api_key` | API key fallback; already guarded |
| `MoveFileExW` | `fm4-frame-capture.h` | publishing a capture; a `std::filesystem::rename` branch exists |
| RenderDoc API by `GetModuleHandleW` | `fm4-frame-capture.h` | interval GPU captures; inside `#if defined(_WIN32)` |
| Vectored exception handler, `MiniDumpWriteDump` | `fm4-crash-diagnostics.cpp` | null-access crash reports with `FM4_CAPTURE_DIR` |
| `ReadProcessMemory`, `_byteswap_ulong` | `fm4-ui-diagnostics.cpp`, `fm4-gpu-probe.cpp` | safe reads of guest memory and GPU registers for diagnostics |
| `<Windows.h>` without a platform guard | `fm4-crash-diagnostics.cpp`, `fm4-ui-diagnostics.cpp`, `fm4-gpu-probe.cpp`, `fm4-jpeg-diagnostics.cpp`, `fm4-stream-trace.cpp` | these files compile only on Windows |
| `ctypes.windll` | `pipeline.py` `primary_monitor` | the launcher's `-Display native`; `e2e.py` does not call it |
| Windows SDK `fxc.exe` | `override_fxc.py`; the look and `post_ao` shaders | HLSL compilation |

`compare.py`, `race_clock.py`, `frame_graph.py`, `inventory.py` and the summary code of `e2e.py` (`--resummarize`) are plain Python and work on any platform, on results copied from a Windows run.

### D3D12 backend

The launcher cvars include settings that only the D3D12 backend defines. Another backend would silently ignore them (see [Launch](#launch)):

| Cvar | Defined in |
| --- | --- |
| `render_target_path_d3d12` | `src/graphics/d3d12/render_target_cache.cpp` |
| `rov_occlusion_awaited_report_divisor`, `rov_occlusion_async` | `src/graphics/d3d12/command_processor.cpp` |
| `d3d12_submit_thread`, `d3d12_readback_publish_thread` | `src/graphics/d3d12/command_processor.cpp` |
| `post_ao` and its settings (`--ao`, the `ao` look knob) | `src/graphics/d3d12/post_ao.cpp` |
| `shader_mod_path`, `shader_mod_defines` (every look except `original`) | `src/graphics/d3d12/shader_override.cpp` |

These launcher cvars are backend-neutral: `edram_single_rt_no_wrap`, `readback_resolve`, `gpu_allow_invalid_fetch_constants`, `async_shader_compilation`, `vblank_vrr`, `anisotropic_override`, `clear_memory_page_state`, `draw_resolution_scale_x` and `_y`, `video_mode_refresh_rate`, `input_backend`, `headless`.

Other backend parts the harness relies on:

- `Presenter::CaptureGuestOutput`: without it the capture thread writes nothing, and every checked step times out.
- `perf_trace::OnSwap` at the swap: without it `perf.jsonl` stays empty, and `summary.json` has no FPS or frame times. The other D3D12 hooks of the perf trace (table above) supply the frame latency and readback waits, CPU zones and GPU times.
- The default look `pc` uses HLSL shader mods compiled with FXC, and the gates run with it.

## Porting checklist

The steps below are the work a port needs, in dependency order. None of them has been done or tested yet.

### 1. Runtime backend

1. Implement `CaptureGuestOutput` in the new presenter. It must return the guest output before the overlay as RGBA8 rows (`RawImage`: `width`, `height`, `stride`, `data`).
2. Call the `perf_trace` helpers at the matching points of the new command processor and shared memory: `OnSwap` at the swap (first, since it writes the trace), `AddFenceWait` and `CauseScope` around GPU waits, `ZoneStart`/`ZoneEnd` around the same zones, `CountDraw`, `CountCopy`, `CountReadback`, `CountResolve`, `Increment(Count::kSubmissions)`, and `AddGpuFrame` from GPU timestamps. Use the same causes and zones, so `compare.py` compares like with like.
3. Decide for each D3D12-only cvar of `launch_cvars` whether the backend gets an equivalent, and give the equivalent its own name. Then make `pipeline.launch_cvars` choose the cvars by backend, so neither backend gets cvars that it would ignore.
4. Until the shader mods and `post_ao` exist for the backend, run the harness with `--look original` and without `--ao`.

### 2. Integration code (work/fm4-rex)

1. Put the Windows-only diagnostics (crash handler, UI and input traces, GPU probe, JPEG and stream traces) behind `#if defined(_WIN32)`, or port their memory reads. None of them is needed for a run to pass; the diagnostic controller and the capture thread are.
2. Keep the capture's temporary-file-and-rename publication: the harness relies on never reading a partly written frame under a final name.

### 3. Harness

1. Name the executable by platform (for example `fm4_rex.exe` on Windows, `fm4_rex` elsewhere) in `e2e.py`, `pipeline.py` and the suffix list of `matrix.py`.
2. Replace `ocr-server.ps1` by a server with the same protocol and fields (see [OCR protocol](#ocr-protocol)). Keep the 720p downscale, the grayscale 2x upscale and the red and `bg` measurements exactly as they are, since `annotate`, `menu_peers` and their thresholds depend on them. Only the recognition engine changes. Select the server in `Ocr.__init__` by platform.
3. Replace the junction in `prepare_profile` by a symbolic link on other platforms (`os.symlink(..., target_is_directory=True)`). Keep the safety rule: removing the copy must never delete the BuildRoot's packages through the link.
4. Recheck every scenario text with the new OCR engine: a different engine reads some labels differently, and `shows` matches substrings.

### 4. Validation

1. Run the `smoke` tier on the new build with the same scenarios as the D3D12 gates.
2. Check the captures by eye against a D3D12 run of the same scenario.
3. Record new baselines per platform and backend. Do not judge a Vulkan run against a D3D12 baseline with `compare.py` as a pass/fail gate: error keys, waits and GPU times differ by design.
4. Record the results in [status/current.md](../status/current.md).

A Vulkan build on Windows is the smaller first step: it keeps Windows OCR, the junction and the executable name, and needs only the runtime and cvar work of step 1. The harness steps (3) are needed only when the platform changes.
