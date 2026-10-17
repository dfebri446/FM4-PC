# Experiments, A/B switches and shader overrides

This page covers the switches that exist for experiments: A/B comparisons of one binary, workarounds, shader overrides and the look-mod diagnostics. None of them is needed for a normal run. Run results with a switch on are for comparison against a baseline gate, not for a release. Derived game shaders are kept in the BuildRoot, never in the repository.

Diagnostic output (files that record what happened) is on the [traces page](traces.md). The commands that run the harness are on the [harness overview](README.md). Launcher options and look knobs for players are in [running.md](../running.md).

## A/B switches and workarounds

Pass them with `--arg NAME=value` (runtime cvars) or `--env NAME=value` (environment variables).

| Switch | Effect |
| --- | --- |
| `--arg=--readback_resolve=fast` | A/B of the resolve readback cvar on the same binary. The launcher uses `fast`. |
| `--arg=--rtv_uniform_draws=false` | Turns off the clear handling of the RTV path (transfer cutouts and 4x MSAA clear remapping), for comparisons. Default `true`. |
| `--env FM4_HALF_RATE_MIRROR=0` | Renders the rear-view mirror every frame in every race, also the 12 car Quick Race. The launcher's `1` renders it every other frame only in the 12 and 16 car races. |
| `--env FM4_SHADOW_WORKAROUND=1` | Skips the shadow-map pass outside the cockpit in races (`Build.ps1 -ShadowWorkaround`). The workaround is off by default. |
| `--env FM4_TILING_SCENARIO=<n>` | Replaces the TilingScenario value 0 or 1 of the race scenarios. The title uses 1 (two predicated tiles with 2x MSAA) for 8 to 16 car races and 0 (more tiles) for pre-race, replays and 1 car races. 2 is the title's UI value: one 720p tile without MSAA. Split screen (4) and photo mode keep their own values. |
| `--env FM4_STREAMING_FALLBACK=0` | Turns off the streaming fix, which is always on by default, for comparisons. |
| `--arg rtv_occlusion_debug=noqueries` | RTV occlusion snapshots report 0 samples without host queries. |
| `--arg rtv_occlusion_debug=fake` | Host queries still run, but awaited intervals report N samples, with N from `query_occlusion_fake_sample_count`. |

Source: [`fm4-render-overrides.cpp`](../../work/fm4-rex/src/fm4-render-overrides.cpp), [`command_processor.cpp`](../../work/runtime/src/graphics/d3d12/command_processor.cpp) (the RTV cvars) and [`fm4-streaming-fixes.cpp`](../../work/fm4-rex/src/fm4-streaming-fixes.cpp).

### Frame rate and vblank experiments

These go with the frame rate options on the [harness overview](README.md#frame-rate-and-launcher-settings) (`--arg=--video_mode_refresh_rate=<n>`, `--env FM4_FRAME_RATE=<n>`).

| Switch | Effect |
| --- | --- |
| `--env FM4_VBLANK_SWITCH=0` | Keeps the guest vblank at the reported refresh rate, even in race scenarios. |
| `--env FM4_VBLANK_TRACE=1` | Prints each render scenario push and pop with the vblank rate. |
| `--env FM4_VRR_HALF_RATE=1` | Keeps `vblank_vrr` in the 30 fps scenarios. By default they are held to the fixed grid. |
| `--arg=--vblank_rate=<hz>` | Sets a fixed startup vblank rate, without `FM4_FRAME_RATE`. |

Source: [`fm4-vblank-rate.cpp`](../../work/fm4-rex/src/fm4-vblank-rate.cpp).

## Developer command line options

FM4 keeps its developer command line parameters, but the retail build never fills them (see [status/current.md](../status/current.md)). `--env FM4_GAME_CMDLINE="<option> <option>=<value>"` feeds them through the title's own parser ([`fm4-command-line.cpp`](../../work/fm4-rex/src/fm4-command-line.cpp)) after its defaults. A bare name means `name=1`.

Options whose code the retail build no longer has are rebuilt in [`fm4-dev-features.cpp`](../../work/fm4-rex/src/fm4-dev-features.cpp):

- `engageaionstart`: an AI driver drives the player's car from the start. Pause, photo mode, QUIT RACE, replays and CONTINUE work as usual. `e2e.py` then sends no driving input: throttle, brake and steering steps become a wait (`rt=255`), or the remaining button (`rt=255+back` becomes `back`), and goal mode does not hold the throttle. The gates use it by default (see [the harness overview](README.md#gates-and-tiers)).

Options that the retail build still reads work without a rebuild, for example `nohud` (no HUD at all; do not use it with scenarios that read the HUD, such as career waiting for LAPS). `camoverride` and its fixed cameras are gone from the retail build and are not rebuilt.

Race setup trace:

- `--env FM4_TRACE_RACE_SETUP=1` prints the guest return addresses of each race's player driver creation (the main loop state that loads a race, for finding the code behind a game mode), every car's driver setup and the pending session type.
- `--env FM4_TRACE_RACE_SETUP=<file>` also appends 4 KB of the race settings (`sub_822DEC50`) to that file when a session starts.

## Shader overrides (phase B4)

Shader overrides replace the DXBC translation of a guest pixel shader with HLSL that you write. Research notes are in [b4-shader-research.md](../analysis/b4-shader-research.md).

### Override files

`--arg shader_override_path=<dir>` replaces the DXBC translation of a guest pixel shader with `<dir>\shader_<ucode hash>[_<modification>].ps.hlsl` (the hash and modification as in the `dump_shaders` file names). The file defines:

```hlsl
void xe_pixel_shader(inout XePixel p)
```

It reads `p.r[]` (`r#` with the interpolators loaded) and writes `p.color[i]` (`oC#`) and `p.depth`. The runtime compiles it with FXC `ps_5_1` after the contract of the translation that it replaces:

- `XE_C(n)` for `c<n>`;
- `XE_TFETCH2D`, `XE_TFETCH3D` and `XE_TFETCHCUBE` (`fetch, coord`) for the default tfetch;
- the `XE_TEXTURE*` and `XE_SAMPLER*` slots.

It also uses the library [`work/runtime/src/graphics/d3d12/shader_override.hlsli`](../../work/runtime/src/graphics/d3d12/shader_override.hlsli), which mirrors the DXBC translator: tfetch; Shader Model 3 multiplication (`xe_mul`, `xe_mad`, `xe_dp4`, `xe_mad_identical` where the ucode multiplies identical operands); PWL gamma; and the epilogue (alpha test, alpha to coverage, exponent bias).

`runtime.log` says `Shader override ... compiled`, or why it kept the DXBC translation: FXC errors, a signature that differs from the translation's, float24 depth, or memexport.

### Contracts

`--arg dump_shaders=<dir>` writes the contract of every translated pixel shader, with its ucode, as `shader_<hash>_<modification>.ps.contract.hlsli`. Each override is compiled against its contract.

### Generated starting points

`--arg shader_override_generate_path=<dir>` writes HLSL for every pixel shader that has no override file. The HLSL does the operations of its DXBC translation, with each block after the guest instruction in a comment. The generator is [`hlsl_translator.cpp`](../../work/runtime/src/graphics/pipeline/shader/hlsl_translator.cpp). Copy one file to the override directory and edit it.

`--arg shader_override_generated=true` uses the generated HLSL for all of them, to test the generator against the DXBC translations with the A/B below.

### FXC notes

[`override_fxc.py`](../../outputs/FM4-build/e2e/override_fxc.py) compiles overrides the way the runtime does, without starting the game, against the contracts of a `dump_shaders` run (about 8 s for 418 shaders):

```
python outputs\FM4-build\e2e\override_fxc.py <override file or dir> <dump_shaders dir> [--mods work\fm4-rex\look --define FM4_LOOK_SHARPEN=0.5]
```

Options: `--asm <dir>` writes the FXC disassembly of each override there, `--mods` gives a `shader_mod_path` directory, `--define NAME=VALUE` (repeatable) sets the defines of the mods, and `--workers` (default 8) sets the parallel compiles.

The runtime compiles with FXC `ps_5_1`, IEEE strictness and unbounded descriptor tables.

### Shader mods

Shader mods change a generated override without copying it. With `--arg shader_mod_path=<dir>`, a pixel shader with `<dir>\shader_<hash>.mod.hlsli` gets its generated HLSL with that file included after the library. The mod file redefines the hooks that the generator leaves in the code:

- `XE_HOOK_TFETCH`: the result of each texture fetch;
- `XE_HOOK_CONSTANT`: each float constant (`c<n>`);
- `XE_HOOK_INPUT`: the registers after the prologue;
- `XE_HOOK_OUTPUT`: the outputs.

`--arg shader_mod_defines=NAME=VALUE;...` sets its defines. The defaults change nothing.

The look mods (`work/fm4-rex/look`, see below) are shader mods of this kind.

## Shader A/B (same scene, two shaders)

`--arg shader_override_ab_ms=<N>` draws with the DXBC translations instead of the overrides in every other period of N ms of the steady clock, so one static scene shows both. [`override_ab.py`](../../outputs/FM4-build/e2e/override_ab.py) lists consecutive captures of a `--keep-ppm` run with their phase. In a static scene, same-phase pairs are identical, and on/off pairs are identical if the override renders exactly like the translation.

```
python outputs\FM4-build\e2e\e2e.py --build-root <BuildRoot> --out <dir> --scenario photo --profile copy --keep-ppm --env FM4_GAME_CMDLINE=engageaionstart --arg shader_override_path=<overrides> --arg shader_override_ab_ms=5000
python outputs\FM4-build\e2e\override_ab.py <dir> --period-ms 5000 --diff <dir>\ab-diff.png
```

With 5000 ms and captures every 2 s, each phase has same-phase pairs. Photo mode after the photo is taken (the SAVE/EXIT screen) is static for about 30 s, but it shows the scene that it rendered once, so only post-processing changes show there.

For the scene shaders (car paint, track), freeze a race with Y (rewind) on the camera to compare. The title keeps rendering the frozen scene every frame (GPU 2.4 ms a frame at 60 fps) under only a small hint bar, until A resumes; same-phase captures are identical. The pause menu (Start) also keeps rendering, but blurs and covers the scene.

## Look mods and the GTAO pass

The look mods are the post-processing of FM4 (original code, see `fm4-look.hlsli`) in [`work/fm4-rex/look/`](../../work/fm4-rex/look/). `Build.ps1 -Action run -Look "sharpen=0.5,bloom=0.8,contrast=0.2,saturation=1.05,vignette=0.5,motion_blur=0.5,exposure=0,tonemap=aces"` (and `pipeline.py` and `e2e.py --look`) copies them to `<BuildRoot>\look` and sets the knobs. Knobs left out keep the original look. The knobs themselves are described in [running.md](../running.md).

Details that matter for experiments:

- `tonemap=gt|aces|neutral` replaces the game's filmic curve before its color grading LUT, with middle gray where the game puts it. `filmic` is the original.
- The car body paint shader (`B85EF04779ABD438`, see [material-atlas.md](../analysis/material-atlas.md)) gets a clear coat from `fm4-paint.hlsli`. `clearcoat=1` adds a GGX highlight of the sun, soft limited by `clearcoat_glint` (in units of the sun's diffuse light; default 4, roughness 0.3), and the environment cube at grazing angles, in the paint's own shadow.
- `flakes=0...1` adds metallic flakes (`flake_scale` cells per unit of the paint UV). No preset uses them, as in races they do not show (see `fm4-paint.hlsli`).
- `ao=<strength>` applies the runtime's GTAO pass (`post_ao`, see [gtao-design.md](../analysis/gtao-design.md)) in the final combine shaders. Above 0 it also turns the pass on, with `post_ao_camera` and a 0.75 m radius (`pipeline.py` `ao_cvars`). `--ao` (on `pipeline.py` and `e2e.py`) runs the pass alone.
- Presets name sets of knobs and mix with them, later items winning: `pc` (`tonemap=aces`, `saturation=1.1`, `bloom=0.7`, `sharpen=0.4`, `clearcoat=1`, `ao=0.6`); `aces`, `gt` and `neutral` (the tone curve, `clearcoat=1` and `ao=0.6`); and `original`, which runs without mods.
- The launcher and `e2e.py` use `pc` unless `-Look`/`--look` says otherwise, so the gates run with it. `--look original` compares with the game's own look.

### Post AO diagnostics

- `FM4_LOOK_AO_DEBUG=1` in `shader_mod_defines`: the final combine shaders show the GTAO texture as it is.
- `--arg post_ao_debug=1|2|3|4` makes that texture view:
  - `1`: W over 0-100 units, for checking the depth and its alignment;
  - `2`: the occlusion before denoising;
  - `3`: the depth discontinuities (1 across a depth discontinuity on any side, the edges of the denoise);
  - `4`: view W over 0-2 units, for close surfaces.
- `--arg post_ao_ab_ms=<N>` skips the pass in every other period of N ms.
- `--arg post_ao_variants="<radius>,<occlusion scale>[,<debug view>];...;off"` with `--arg post_ao_variant_ms=<N>` cycles settings, to compare them in one frozen frame. Freeze a race with Y (rewind) on the camera first, as above.

Source: [`post_ao.cpp`](../../work/runtime/src/graphics/d3d12/post_ao.cpp), [`post_ao.hlsl`](../../work/runtime/src/graphics/d3d12/post_ao.hlsl), and the GTAO notes in [gtao-design.md](../analysis/gtao-design.md).

## Mod and post AO tests

The tests for the shader mods and the post AO pass are in [`work/fm4-rex/tests/`](../../work/fm4-rex/tests/): `shader-mod-tests.cpp`, `post-ao-shader-tests.cpp` and `post-ao-camera-tests.cpp`.
