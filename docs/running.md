# Running FM4-PC

This guide covers launching a finished build with `Build.ps1 -Action run`: what
the launcher checks and sets up, its options, the look knobs, the controls and the
data directories. Build the game first; see [setup.md](setup.md).

Performance numbers in this file describe the launcher defaults, and the option
details keep the measurements that explain each option. Measurements that depend on
a specific build are dated in the status documents: [status/current.md](status/current.md)
and [status/history.md](status/history.md).

## Launching

```powershell
.\outputs\FM4-build\Build.ps1 -Action run -BuildRoot 'D:\FM4-build' -LLVMRoot 'C:\Program Files\LLVM' -Python 'C:\Python311\python.exe'
```

If you open a new PowerShell session, define `$common` again as shown in
[setup.md](setup.md#commands). `run` uses the Disc locations saved by `generate`;
it does not read `-Disc1` or `-Disc2`.

The launcher then:

1. Checks the build artifacts and the game revision. It compares the artifact
   hashes with `build-result.json`, validates the Disc paths in `local.json` and
   checks the three executables.
2. Starts with a new local profile in `BuildRoot\user-data` (see
   [Data directories](#data-directories)).
3. Mounts Disc 1 and, if Disc 2 was given, imports its four content packages. The
   first import takes time and extra disk space.
4. Prints the display and render scale that it chose and starts the game.

The game window runs fullscreen on the primary monitor by default. Use Alt+Enter to
switch to a window (see [Controls](#controls)).

## Launcher defaults

Running with no options gives this configuration:

- **Shader compilation** is synchronous (`async_shader_compilation=false`).
- **Anisotropic filtering** is 16x for every mipmapped linear texture
  (`anisotropic_override=5`). The title asks for none on most textures and 2x to 16x
  on a few; the runtime default is 4x.
- **Rear-view mirror** is rendered every other frame in every race, as the title does
  for 12- and 16-car races. One- and eight-car races render it every frame. See
  [-FullRateMirror](#-fullratemirror).
- **Shadows** use the title's shadow map for every camera. See
  [-ShadowWorkaround](#-shadowworkaround).
- **Race quality** is `high`. See [-Quality](#-quality).
- **Look** is the `pc` preset, with screen-space ambient occlusion. See [Look](#look).
- **Display** is `native`: borderless fullscreen on the primary monitor, with a render
  scale of 2x on 1080p, 1440p and 2560x1600 screens and 3x on 4K. See
  [-Display](#-display-and-render-scale).
- **Render path** is the host render target path (RTV) with the frame pacing and
  memory settings described in [Render path and frame pacing](#render-path-and-frame-pacing).

Performance on the defaults: Quick Race holds 60 fps on the development computer.
The GPU time per race frame with the defaults (`high` quality, `pc` look with
ambient occlusion) is about 3.0 ms at 1x (1280x720) and about 6.1 ms at 2x. The 2x
scale is the one that `native` uses on 1080p, 1440p and 2560x1600 screens.

### Render path and frame pacing

The launcher uses these runtime settings (set in `launch_cvars` in
[pipeline.py](../outputs/FM4-build/pipeline.py)):

- Rendering uses the host render target path `render_target_path_d3d12=rtv` with
  `edram_single_rt_no_wrap=true`. Earlier measurements put the GPU time at about 2.5 ms
  per race frame on this path, against about 7 ms on the pixel shader interlock path
  (`-Rov`). Quick Race runs at a stable 60 fps.
- Guest memory that the CPU wrote is uploaded to the GPU once, not every frame
  (`clear_memory_page_state=false`).
- Command lists are recorded and executed on a thread of their own
  (`d3d12_submit_thread=true`). The copies of resolve readbacks into guest memory run
  on their own thread too (`d3d12_readback_publish_thread=true`).
- Occlusion reports are published at once from the previous frame's samples
  (`rov_occlusion_async=previous`). Resolve readbacks do not wait for the GPU
  (`readback_resolve=fast`).
- A frame that misses a guest vblank gets it as soon as it is ready
  (`vblank_vrr=true`). See [-FixedVblank](#-fixedvblank).
- The road sections that this path used to leave undrawn came from the title's track
  streaming. They are fixed for every render path.

Known limits of this path: the sun glare has not been calibrated on it, because the
sun occlusion query correction (`rov_occlusion_awaited_report_divisor=2`) applies
only to `-Rov`. Interactive Autovista has not been tested on it. If rendering
misbehaves, compare with `-Rov`. Which scenarios have passed on this path is recorded
in [status/current.md](status/current.md).

## Options

Pass the options to `Build.ps1 -Action run`. Each option is a switch unless it
takes a value.

| Option | Default | Description |
| --- | --- | --- |
| `-FrameRate 30-240` | 60 | Race frame rate. Values above 60 lift the race limit; menus, replays and photo mode keep their own timing. |
| `-Display native\|720p\|1080p\|1440p\|4k` | `native` | Screen and render size. `native` is borderless fullscreen on the primary monitor. Other values are a window of that size. |
| `-Fullscreen` | off | Fullscreen with the `-Display` size and its render scale. |
| `-Supersample` | off | Render one scale above the screen (up to 4x), then average the result down to the screen. |
| `-ResolutionScale 1-4` | from `-Display` | Render at this multiple of 1280x720. Overrides the render scale of `-Display`. |
| `-Quality xbox\|high\|ultra` | `high` | Render quality of races. |
| `-Look <preset/knobs>` | `pc` | Post-processing look and car paint. See [Look](#look). |
| `-Info` | off | Show the settings overlay at start. F2 toggles it in any run. |
| `-Rov` | off | Use the earlier pixel shader interlock render target path, for comparisons. |
| `-SingleTile` | off | Render races as one 720p tile without MSAA, with FXAA on the swap. |
| `-FixedVblank` | off | Hold every swap to the fixed 60 Hz vblank grid. |
| `-FullRateMirror` | off | Render the rear-view mirror in every frame of every race. |
| `-ShadowWorkaround` | off | Render the shadow map only with the cockpit camera, as the old Xenia workaround did. |

`-Action warmcache` is not a run option; it is described in
[Shader cache](#shader-cache). The `--ao` flag of `pipeline.py` and `e2e.py` has no
switch in `Build.ps1`. It runs the GTAO pass without a look applying it; see
[Look](#look).

### Option details

#### -FrameRate

`-FrameRate 120` lifts the 60 fps limit of races. The title sees a 120 Hz display,
the `FrameRate` of its race render scenarios becomes 120, and the title scales its
time step (race clock against wall time 1.001). The guest vblank stays at 60 Hz in
menus, countdown, post-race, replays and photo mode (30 fps, as on the Xbox 360). It
runs at 120 Hz only while a race scenario sets the frame rate (`fm4-vblank-rate.cpp`).

Measured with Quick Race, three runs: 118 to 120 fps, frame p99 8.8 to 9.8 ms, and
every frame fully rendered. At 240, races run at 220 to 235 fps (p99 5.3 to 6.2 ms).
At 240 the command processor sets the limit: it needs about 4.3 ms per race frame,
against about 2.2 ms of GPU time. Values from 30 to 240 are accepted.

#### -Display and render scale

`native` (the default) is borderless fullscreen on the primary monitor. The other
sizes (`720p`, `1080p`, `1440p`, `4k`) are a window of that many pixels. They go
fullscreen with `-Fullscreen`, or when they do not fit the monitor. Window sizes take
the Windows display scale into account.

The render scale is the smallest whole multiple of 1280x720 that covers the 16:9
picture of the screen: 720p 1x, 1080p and 1440p 2x, 2560x1600 2x, and 4k 3x. `-Display
720p` was the earlier default (1x in a 1280x720 window). If the render is larger than
the window, the presenter averages it over each screen pixel in linear light
(`work/runtime/src/ui/shaders/guest_output_area.ps.hlsl`; `present_area_downscale=false`
selects the earlier single bilinear tap). So 2x on a 1080p screen is supersampling,
not a shrink that skips texels.

`-Supersample` renders one scale above the screen, up to 4x (5120x2880), and averages
it down. Edges are smoother and thin objects are more stable, at the GPU cost of that
scale.

`-ResolutionScale 2` renders at 2560x1440 and `-ResolutionScale 3` at 3840x2160,
instead of the size from `-Display` (`draw_resolution_scale_x` and
`draw_resolution_scale_y`; CPU readbacks of resolves are downscaled). Measured with
Quick Race: 59.7 fps with about 3.7 ms of GPU time per frame at 2x, and 59.9 fps with
about 6.8 ms at 3x. The HUD and the scene are sharp. The smoke gate at 2x (240 Hz)
passes: races run at 202 to 206 fps, photo mode at 30 fps (GPU 7.5 ms), a photo can be
saved and loaded, and a career race with its replay runs at 30 fps (GPU 8.2 ms). The
replay used to drop to 23.5 fps after a long frame; see `-FixedVblank`. These numbers
predate `-Quality high` and the clear coat.

Alt+Enter switches between borderless fullscreen and the window in any run
(`bind_fullscreen`). The render scale stays the same.

#### -Quality

Sets the render quality of races through their render scenarios
(`FM4_RACE_SETTINGS`, `fm4-render-overrides.cpp`). It applies to the N-Car, Autocross
and Bowling scenarios, not to split screen.

- `xbox` keeps the title's own settings.
- `high` (the default) gives races what the title gives its replays and photo mode: 4x
  MSAA in three predicated tiles (`TilingScenario` 0, the title's 4xMSAA scenario,
  instead of 1, which is 2xMSAA in two), soft particles, shadows of collidable objects,
  full-detail wheels, and car LOD transitions at twice the distance (30/50/100 m). It
  also undoes the cuts that the title makes for 16-car races: the cheaper car shader,
  no damage, an environment map that is never updated, half-rate bloom, and no
  collidables in the mirror.
- `ultra` also keeps every car at its top LOD (`CarKeepLOD0`, as 1-car races do) and
  renders the mirror every frame, with up to 11 cars.

Measured at 240 Hz in the 12-car race phase of the photo scenario: `xbox` 219 to 224
fps in three runs (GPU 2.3 ms a frame). `high` 178 to 225 fps in four runs, 201 on
average (GPU 2.8 ms), because the three tiles execute 4,500 to 8,300 draws a frame
depending on where the cars are (about 5,200 on `xbox`). `ultra` 144.8 fps (median of
three runs, GPU 3.1 ms, command processor busy 974 ms a second). All three hold 60 fps
with room to spare. `high`, at 178 fps in the worst run, leaves room for 120 Hz (not
measured at 120 Hz). `ultra` is meant for 60 to 120 Hz.

#### -Info

Shows the settings overlay at the top left from the start. It lists the display and
render scale, the race quality, the look and its knobs, the ambient occlusion, the
shadows, the anisotropic filtering, the race frame rate and pacing, the mirror rate,
the render target path, and the window size and present rate, as the launcher set them
up (`info_overlay` in `pipeline.py`, `FM4_INFO`; `work/fm4-rex/src/fm4-info-overlay.h`).
The presenter draws the overlay over the guest output, so captures never show it.
F2 toggles it in any run.

#### -Rov

Uses the earlier pixel shader interlock render target path
(`render_target_path_d3d12=rov`, `rov_occlusion_async=all`) for comparisons. Measured
with Quick Race: 60.0 fps, with about 7.1 ms of GPU time per frame (2.5 ms on the
default path). Only this path uses the sun occlusion query correction
(`rov_occlusion_awaited_report_divisor=2`). Use it to compare when rendering misbehaves
on the default path.

#### -SingleTile

Renders races as one 720p tile without MSAA. This is the title's own UI tiling
scenario, selected through the render scenario override. The swap applies FXAA.
Measured with Quick Race on `-Rov`: 58.6 fps instead of 54.3, frame p95 20 ms instead of
33 ms, GPU 13 ms instead of 17.5 ms. Edges are visibly more jagged than with MSAA (2x
with `-Quality xbox`, 4x with the default `high`): thin cables break up. Photo mode and
split screen keep their own settings.

#### -FixedVblank

Holds every swap to the fixed 60 Hz guest vblank grid (`vblank_vrr=false`). By default
(`vblank_vrr=true`), a frame that misses a vblank gets it as soon as it is ready,
instead of waiting for the next one. Measured with Quick Race on `-Rov`, three runs
interleaved for each setting: the same average fps (52.5 against 52.7), frame p95 of
23 ms instead of 33 ms, frame p99 of 26 ms instead of 35 ms, and unchanged game speed.

Scenarios that render at 30 fps (countdown, post-race, replays and photo mode) keep
the fixed grid either way. Their render thread waits for the vblanks itself. With
`vblank_vrr`, one long frame left every later vblank forced or late: the career replay
at `-ResolutionScale 2` stayed at 23 fps instead of 30.

#### -FullRateMirror

Renders the rear-view mirror in every frame of every race. By default every race renders
it every other frame, as the title does for 12- and 16-car races
(`HalfRateMirror` in the race render scenarios). One- and eight-car races render it in
every frame. Measured with Quick Race (12 cars) and the mirror in every frame, before the
upload change described in [Render path and frame pacing](#render-path-and-frame-pacing):
55.6 fps instead of 59.7, and GPU 9.5 ms instead of 8.1 ms a frame. At `-FrameRate 120`:
75 fps instead of 84.

#### -ShadowWorkaround

Adds `SkipShadowMapUnlessCockpit` to the race render scenarios (`fm4-render-overrides.cpp`),
as the title's split screen does. The shadow map, a depth pass of 70 to 160 draws
resolved to 512x512 plus a 16_16 4x MSAA pass, is then rendered with the cockpit camera
only. Without it, the hood, bumper and chase cameras also get the shadow map. Their car
shadows are sharp, with the wheels apart instead of soft, at a cost of about 0.2 to 0.4
ms of GPU time a frame. Xenia needed this workaround against square shadows. Captures of
every race camera on this runtime show none.

## Look

The look mods are original shader code hooked into the HLSL that the runtime
generates from the game's post-processing shaders and car body paint shaders
(`work/fm4-rex/look`; see [Shader overrides](e2e/experiments.md)). `-Look` takes a
preset, knobs, or both. A preset sets only its own knobs. Later items win, so
`-Look "pc,sharpen=0.6"` uses the `pc` preset with sharpening at 0.6.

| Preset | Settings |
| --- | --- |
| `pc` (default) | `tonemap=aces`, `saturation=1.1`, `bloom=0.7`, `sharpen=0.4`, `clearcoat=1`, `ao=0.6` |
| `aces` | `tonemap=aces`, `clearcoat=1`, `ao=0.6` |
| `gt` | `tonemap=gt`, `clearcoat=1`, `ao=0.6` |
| `neutral` | `tonemap=neutral`, `clearcoat=1`, `ao=0.6` |
| `original` | No mods: the game's own look. Clears the knobs given before it in the same value. |

Knobs that are not set keep the original look. Examples: `-Look "aces,clearcoat=0,ao=0"`
and `-Look original`.

| Knob | Values and default | Effect |
| --- | --- | --- |
| `tonemap` | `filmic`, `gt`, `aces`, `neutral` | Tone curve. |
| `sharpen` | number | Sharpening. |
| `bloom` | number | Bloom. |
| `exposure` | number | Exposure. |
| `contrast` | number | Contrast. |
| `saturation` | number | Saturation. |
| `vignette` | number | Vignette. |
| `motion_blur` | number | Motion blur. |
| `clearcoat` | number | Clear coat of the car body paint (`fm4-paint.hlsli`). |
| `clearcoat_roughness` | number, default 0.3 | Roughness of the clear coat. |
| `clearcoat_glint` | number, default 4 | Glint of the clear coat. |
| `flakes` | number | Flakes in the car paint. |
| `flake_scale` | number, default 1200 | Scale of the paint flakes. |
| `ao` | number, `0` turns it off | Strength of the screen-space ambient occlusion. |

### Ambient occlusion

`ao` above 0 turns on the runtime's GTAO pass on the scene depth (`post_ao`, see
[GTAO design](analysis/gtao-design.md)) with a radius of 0.75 m. The final combine
shaders darken the scene by the occlusion, less on bright pixels. `pipeline.py --ao`
(and `e2e.py --ao`) runs the pass without a look applying it.

The pass costs about 0.2 ms of GPU time per race frame at 1x, and 0.7 to 0.8 ms at 2x
(2560x1440). Races stay at 60 fps. At 240 Hz, races at 2x drop from about 174 fps to
about 162 fps. Replays and photo mode have occlusion too. The cockpit interior, the
garage and the showroom get none. The six mod shaders are compiled with FXC when they
are first drawn, which takes 80 to 940 ms each, and 2.4 s for the paint.

## Controls

The controller uses the SDL backend. The keyboard mapping is the launcher's default,
set by `launch_cvars` in [pipeline.py](../outputs/FM4-build/pipeline.py).

| Key | Function |
| --- | --- |
| `W` / `S` | Gas / brake |
| `A` / `D` | Steer left / right |
| `Space` / `Backspace` | Controller A / B |
| `Enter` / `Tab` | Start / Back |
| Arrow keys | D-pad |
| `Alt+Enter` | Switch between borderless fullscreen and the window (`bind_fullscreen`) |
| `F2` | Show or hide the settings overlay |

The keyboard controller ignores keys pressed with Alt, so Alt+Enter does not also
press Start.

## Shader cache

`-Action warmcache` runs the e2e Quick Race timeline once, without a network connection,
so that the shader and pipeline cache under `BuildRoot\cache` is filled before you play.
Later runs then skip those compilations. The run details are written to
`BuildRoot\e2e\warmcache`, and the log to `BuildRoot\logs\warmcache.log`.

```powershell
.\outputs\FM4-build\Build.ps1 -Action warmcache @common
```

## Data directories

Everything the launcher writes is under `BuildRoot`. The full layout is in
[BuildRoot layout](setup.md#buildroot-layout).

- **Profile:** each run uses a new local profile in `BuildRoot\user-data`. Saves from
  earlier builds are not copied into it automatically.
- **Disc 2 content:** the packages imported from Disc 2 are stored in the profile
  under `BuildRoot\user-data`.
- **Discs:** the discs are read in place. Writes relative to the game directory are
  disabled (`allow_game_relative_writes=false`).
- **Cache:** game and shader cache files are in `BuildRoot\cache`.
- **Logs:** the launcher log is `BuildRoot\logs\play.log`, and the runtime log is
  `BuildRoot\logs\runtime.log`.
- **e2e output:** captures, logs and `summary.json` of e2e runs are in
  `BuildRoot\e2e`. The captures show the game, so keep them out of the repository.
  See [e2e/README.md](e2e/README.md).

Nothing is uploaded by `run`. Never commit or publish `BuildRoot`.
