# GTAO design: injecting the ambient occlusion pass

Design of the screen-space ambient occlusion (GTAO) pass that the runtime injects into FM4 frames, together with the frame graph data that determines it. M0-M4 are complete (section 6): the pc default look has used AO since M4. For the current state see [status/current.md](../status/current.md); for dated results see [status/history.md](../status/history.md).

This note contains no game code, shaders or ucode. Addresses, hashes and registers are observations only.

Sources: `REX_FRAME_GRAPH` traces of the race in BuildRoot (see [setup.md](../setup.md) for the layout; q-high: `-Quality high`, 4xMSAA, three tiles; fg-on and fg-off: camera cycle; b3-fg: 2xMSAA, two tiles), the [material atlas](material-atlas.md), the local ucode disassembly of motion blur 4746CAD32EF168CF, the FinalCombine DOF shaders 30B67D8B86C37587 and AE3B2E36FCA4958D, the VS 8D05A46C3584CA9E and C81F8D92FF6529EB, and the code in `work/runtime`.

## Summary

- **Depth**: the full-screen depth texture that the game itself resolves in every race frame, read through the texture cache. It is not copied per tile from EDRAM.
- **Camera**: the projection matrix P, taken from the vertex shader constants of the scene, plus the viewport registers. GTAO needs only P, not the view matrix.
- **Injection point**: right after the resolve of the last depth tile (`IssueCopy`), before the whole post chain. Four compute passes (depth prefilter, GTAO, denoise once or twice) produce an R8 occlusion texture, which is cleared again on swap.
- **Use**: mods read `xe_post_ao(uv)`. The FinalCombine mod ([fm4-look.hlsli](../../work/fm4-rex/look/fm4-look.hlsli)) darkens the scene before tonemapping, less strongly on bright pixels.
- **Default**: off in the runtime (`post_ao*` cvars). The launcher turns it on through the look knob `-Look ao=<strength>`. The presets pc, aces, gt and neutral use `ao=0.6` with a radius of 0.75 m (M4). `original` and `ao=0` run without the pass. See [running.md](../running.md).
- **v1 limits**: bloom and auto-exposure do not see AO. Menus without AO (no scene depth), the garage and the showroom do not get it either (they do not draw the camera VS, M1), and neither does the cockpit interior (depth range compressed, M3-M4). Races, replays and photo mode get AO. Only the RTV path can use it (the override shader acts as a consumer and needs host render targets).

## 1. Goals and constraints

- **Visual goal**: contacts and gaps, for example tires against asphalt, wheel arches, the grille and vents, barrier legs and stands, buildings and trees.
- The track lightmaps already contain large-scale static occlusion, and the cars have a drop shadow (255C1BF932417B16, `c_dropShadow`). For that reason the radius is kept small (runtime default 1 m, launcher 0.75 m since M4) and the strength moderate. The aim is contact, not uniform darkness.
- Game code is not changed. The GTAO shader is written from the paper (Jimenez, Wu, Pesce, Jarabo 2016), not copied from XeGTAO.
- Off by default. Tested with A/B runs (`post_ao_ab_ms`, see [experiments.md](../e2e/experiments.md)).

## 2. Facts from the frame graph and ucode

- **Scene depth**: in every sampled race frame, the game resolves the scene depth (EDRAM depth@1024, D24FS8) per tile into one 1280x720 texture.
  - q-high: frames 4800-9480, three resolves per frame (six in the motion blur frame).
  - fg-on: frames 4800-6000.
  - b3-fg: frames 4170-4500.
  - Destination addresses continue from tile to tile:
    - 4xMSAA: tile 0 at `1c0c1000` (256 rows), tile 1 at `+0x140000` (256 rows), tile 2 at `+0x280000` (224 rows).
    - 2xMSAA: 512 rows, then the remaining 224 rows at `+0x280000`.
    - The last tile ends at base + 1280 x 736 x 4 bytes.
  - The destination format is recorded as `8_8_8_8` (a D3D9 quirk, `xenos.h`). Its content is 24-bit float depth plus stencil.
- **Users of that depth**:
  - Motion blur `4746CAD32EF168CF`, only in some frames (q-high 4800-6000; in those frames the depth is resolved twice per tile). Slot s2 holds the depth. Its constants are `QAndNegNearQAndScale` c69, viewport c68, and `invCurrentVPThenPrevVP` c0[68]: 17 matrices selected per pixel through a0, probably per object.
  - FinalCombine DOF `30B67D8B86C37587` and `AE3B2E36FCA4958D`: tf1 holds the depth, and its linear depth is `c1.y / (c1.x - (1 - d))`. This is the usual D3D projection form, z = Q*n / (Q - ndc_z), with `combineQVals` = (Q, Q*n) and stored depth d = 1 - ndc_z. So Z is inverted through the viewport (zscale -1, zoffset 1), not through the matrix, and post_ao reads that viewport. Confirmed in M1.
- **Frames without a depth resolve**: frames with a 3D scene before the race (84 to 2,898 scene draws per frame at q-high 1080-4080: menus, showroom, loading screens). The exact scenes have not yet been mapped to e2e steps.
- **Race frame order** (q-high, frame 9600):
  1. shadow map;
  2. env/reflection;
  3. side mirrors;
  4. three times per scene tile: draw, depth resolve, color resolve;
  5. motion blur when active (reads depth and scene, rewrites the scene);
  6. bloom downsample from the scene, 32x32 luminance, bloom chain;
  7. FinalCombine (scene, luminance, bloom, LUT);
  8. HUD and side mirrors to the front buffer.
- **Scene format**: 7e3 (2_10_10_10_FLOAT; host R16G16B16A16_FLOAT in EDRAM), stored in gamma 2. FinalCombine squares it ([B4-SHADER-RESEARCH.txt](b4-shader-research.md)).
- **No post-process pixel shader carries the projection.** The projection is in the vertex shader constants of the scene:
  - Car body VS `8D05A46C3584CA9E` (pair B85E): matWVP c0-c3 and matWV c4-c7, row-vector convention (oPos = x*c0 + y*c1 + z*c2 + c3, view position with c4-c7), so P = inverse(WV) * WVP.
  - Road VS `C81F8D92FF6529EB`: wvp c128-c131 (dp4, column vectors), world c140-c142, viewInv c243-c245 (the W column is the eye position).
  - P changes only with FOV and near/far.
- **Override shader (AO consumer)**: active only in the RTV path, not with `-Rov` (`ShaderOverrides::Create`, [shader_override.cpp](../../work/runtime/src/graphics/d3d12/shader_override.cpp)).

## 3. Design decisions (as implemented in M0)

a. **Depth source (option A)**: the game's own depth texture. From the depth resolve, the runtime builds fetch constants the same way the game's shaders use them:
   - base = address of the first tile;
   - pitch and height from `RB_COPY_DEST_PITCH` (1280x720);
   - format `k_24_8_FLOAT` (`k_24_8` if the source is D24S8), endian from `RB_COPY_DEST_INFO`, tiled.

   The texture cache loads it as R32_FLOAT ([texture_cache.cpp](../../work/runtime/src/graphics/d3d12/texture_cache.cpp), format table entry `k_24_8_FLOAT`). Data resolved at a resolution scale is scaled accordingly.

   Rejected for v1:
   - per-tile AO in EDRAM before transparent draws: a tile has no neighbors across its boundary, and the default is three tiles;
   - the NonTiling scenario: MSAA is lost.

b. **Option B, fallback** for menus, showroom and photo mode if needed: when each tile's color is resolved, the same tile's depth is copied from the host render target into a full-screen R32F texture. It does not depend on the game's depth resolve, but it goes through the render target cache (EDRAM dump, sample layout). It is done only if option A fails or AO is needed outside races.

c. **Camera**:
   - On draws with a VS hash in `post_ao_camera`, the runtime reads that VS's matrix constants from the register file, plus `PA_CL_VPORT_*` (with `PA_CL_VTE_CNTL`) and the pixel center (`PA_SU_VTX_CNTL`).
   - Only draws whose depth buffer equals the base of the last scene depth EDRAM are taken (not the side mirrors or the env map), and only once per frame.
   - From P and the viewport ([post_ao_camera.h](../../work/runtime/include/rex/graphics/d3d12/post_ao_camera.h)) the following are derived: W from the stored depth (the constants are folded in double, because in float `1 - d - Q` loses precision far from the camera), the view position from the pixel position, the far-plane W for the sky, and the focal length in pixels for the radius.

d. **Injection point**:
   - Depth resolves (`copy_src_select >= 4`, destination pitch >= `post_ao_min_width`) are merged per frame into one group of contiguous tiles. Re-resolves of the same tile (the motion blur frame) stay in that group.
   - When the group covers all 32x32 tiles of the texture, the pass runs right then, inside `IssueCopy`, once per frame. The depth used is complete, because the second resolve in the motion blur frame only changes stencil (an assumption, from two draws in between).
   - The off phase of `post_ao_ab_ms` skips the pass, so the texture stays empty.
   - Swap (`IssueSwap`) clears the occlusion. As a result, frames without scene depth (menus, photo mode when it does not re-resolve) never use stale AO.

e. **Pass** (compute, `cs_5_1`, [post_ao.hlsl](../../work/runtime/src/graphics/d3d12/post_ao.hlsl), compiled with FXC at startup):
   1. **Prefilter**: depth to 1/W, 5 R32F mips. 1/W is linear in screen space on flat planes, so bilinear samples stay on the surface. The coarse mips use a weighted average that favors the farthest W, so thin edges do not spread.
   2. **GTAO**: slices x steps per side according to quality, fixed spatial noise (no TAA), normals from depth (neighboring sides that connect), samples denser near the center, falloff at the edge of the radius. The slice directions are spread evenly around the view vector and sampled along their projection on screen. Only this way does the slice average cover the full hemisphere; directions spread evenly in screen space are off by 3-7% away from the axis. Output: visibility raised to the power `post_ao_power` (R8, divided by 1.5 so that values above 1 from a few slices survive until denoise), and 2-bit edges per side (R8).
   3. **Denoise**: 3x3, edge-weighted, once (low/medium) or twice (high/ultra). The last pass writes the occlusion (1 - visibility) to R8 at host depth resolution.

   The textures are zero-filled, so occlusion 0 (no AO) is the safe default.

f. **Binding to mods**:
   - A new SystemBindlessView `kPostAoOcclusion` after `kNullTextureCube`, in the unbounded SRV range. At start it holds a null SRV (reads as 0), and then the SRV of the occlusion texture once that texture is created.
   - SystemConstants are not changed: validity is guarded by the texture contents (cleared on swap). The DXBC translation and the pipeline cache stay intact.
   - The override contract defines `XE_POST_AO_VIEW`. The library [shader_override.hlsli](../../work/runtime/src/graphics/d3d12/shader_override.hlsli) adds `float xe_post_ao(float2 uv)`: the occlusion at scene texture coordinates, 0 without post_ao.

g. **Use in FinalCombine** (M3-M4, `fm4_combine_fetch`, fetch 0 at the pixel itself): occlusion o, and weight w = strength x (1 - smoothstep(l0, l1, exposed luminance)). AO really only attenuates ambient light, but the scene does not separate ambient from sunlight, so bright pixels are attenuated less. Then `scene.rgb *= sqrt(1 - w*o)`, because the scene is stored in gamma 2. The sky sits at the far plane, so o = 0.

h. **v1 limits**:
   - Bloom and auto-exposure are computed from the scene without AO. The difference is small, because AO mostly falls in dark areas.
   - In motion blur frames, sharp AO lies over the blurred scene. Planned fix (v1.1): mod 4746 multiplies the AO on each scene tap, and FinalCombine skips it in that frame.
   - Side mirrors and HUD have no AO.

## 4. Components (branch gtao-m0)

- [`post_ao_camera.h`](../../work/runtime/include/rex/graphics/d3d12/post_ao_camera.h): camera math (header only): the matrices from the second constant layout, P, and the unpack constants.
- [`post_ao.h`](../../work/runtime/include/rex/graphics/d3d12/post_ao.h), [`post_ao.cpp`](../../work/runtime/src/graphics/d3d12/post_ao.cpp): class `PostAo`.
  - cvars: `post_ao`, `post_ao_quality`, `post_ao_radius`, `post_ao_falloff`, `post_ao_power`, `post_ao_camera`, `post_ao_min_width`, `post_ao_ab_ms`, `post_ao_debug` (1: W 0-100, 2: before denoising, 3: depth discontinuities, that is the denoise edges, 4: W 0-2), `post_ao_log_every`, and for tuning `post_ao_variants` with `post_ao_variant_ms` (radius, occlusion scale and debug view switch by the steady clock, to compare settings on one frozen scene).
  - `OnDraw` (camera capture), `OnResolve` (depth group, pass trigger), `OnSwap` (clear).
  - Its own root signature (constants b0, 22 dwords; SRV tables t0-t2; UAV tables u0-u4; one static clamp sampler, `MIN_MAG_LINEAR_MIP_POINT`, so linear in x/y and point between mips), the `resolve_downscale` dispatch pattern, and `SetExternalPipeline`, so the next guest draw reinstalls its PSO.
  - Textures that change size: the old resource goes to `resources_for_deletion_`, and the old descriptors are freed after the GPU has passed their submission.
- [`src/graphics/d3d12/post_ao.hlsl`](../../work/runtime/src/graphics/d3d12/post_ao.hlsl): the four entry points (`post_ao_prefilter`, `post_ao_main`, `post_ao_denoise`, `post_ao_clear`). CMake embeds it as `kPostAoHlsl`, the same pattern as `shader_override.hlsli` ([CMakeLists.txt](../../work/runtime/src/graphics/CMakeLists.txt)).
- [`command_processor.h`](../../work/runtime/include/rex/graphics/d3d12/command_processor.h) / [`command_processor.cpp`](../../work/runtime/src/graphics/d3d12/command_processor.cpp): `kPostAoOcclusion` (null SRV at start), `friend PostAo`, `post_ao_`; hooks in `IssueDraw` (after the VS analysis), in both `IssueCopy` paths (after `CountResolveDestination`), and in `IssueSwap` (after `BeginSubmission`).
- [`texture_cache.h`](../../work/runtime/include/rex/graphics/d3d12/texture_cache.h) / [`texture_cache.cpp`](../../work/runtime/src/graphics/d3d12/texture_cache.cpp): `RequestResolvedTexture`, like `RequestSwapTexture` for a synthesized fetch constant, in the same SRV state as guest draws.
- [`shader_override.cpp`](../../work/runtime/src/graphics/d3d12/shader_override.cpp) / `shader_override.hlsli`: `XE_POST_AO_VIEW` and `xe_post_ao(uv)`.
- Tests in [`work/fm4-rex/tests`](../../work/fm4-rex/tests/):
  - [`post-ao-camera-tests.cpp`](../../work/fm4-rex/tests/post-ao-camera-tests.cpp) (target `fm4_post_ao_camera_tests`): CPU tests of the camera math;
  - [`post-ao-shader-tests.cpp`](../../work/fm4-rex/tests/post-ao-shader-tests.cpp) (target `fm4_post_ao_shader_tests`): the four passes on WARP;
  - [`shader-mod-tests.cpp`](../../work/fm4-rex/tests/shader-mod-tests.cpp) (target `fm4_shader_mod_tests`): all look mods compiled with FXC under a synthetic contract, with and without the AO knob.
- FM4 configuration in the launcher ([pipeline.py](../../outputs/FM4-build/pipeline.py) `ao_cvars`, used by the `ao` knob and `--ao`):

  ```
  post_ao=true
  post_ao_camera=8D05A46C3584CA9E:rows:0:4;C81F8D92FF6529EB:world:128:140:243
  post_ao_radius=0.75
  ```

## 5. GTAO parameters

- **Algorithm**: Jimenez et al. 2016, "Practical Real-Time Strategies for Accurate Indirect Occlusion". For each pixel and each screen-space slice direction, the horizon angles on both sides are found within a world-space radius projected to the screen. Cosine-weighted visibility is integrated analytically against the projected normal (equation 10 of the paper), plus falloff at the edge of the radius.
- **`post_ao_quality` presets** (full resolution, slices x steps per side): 0 = 2x2, 1 = 2x4 (denoise once), 2 = 3x6 (default), 3 = 4x8 (denoise twice).
- **Cvar defaults**: radius 1 m, falloff 0.6, power 1.5, denoise center weight 2. Launcher (M4): radius 0.75 m, look strength 0.6.
- **Shader size** (FXC `/O3`): prefilter 193 instructions; GTAO 457 / 614 / 1094 / 1725 per preset (loops unrolled); denoise 74 and 88 (final); clear 6.
- **Estimated cost** at 2x (2560x1440, 3.7 million pixels), high preset: about 55 texture reads per pixel, roughly 0.3-0.8 ms on an RTX 5070 Ti. Measured in M3 through the frame graph (section 6). The race GPU frame is about 2.5 ms at 1x (the project plan, kept outside the repo, phase B3), so there is room. At 240 Hz the limit is the CP, not the GPU.
- **World units** are assumed to be meters. Confirmed in M1: for example, the car length in view space is about 4.5 m.

## 6. Stages and verification

Stages that run the game begin only after the owner confirms the GPU is free.

**M0**: complete on branch `gtao-m0` (17 October 2026), without running the game.
- CPU test `fm4_post_ao_camera_tests` passes: P from both constant layouts (error 3e-7); view position from the stored depth with inverted Z (error 2e-5 up to 500 m, 3e-4 up to 6 km); orthographic projection rejected.
- Offline FXC `cs_5_1`: all five variants and all four presets without warnings; register bindings match the root signature.
- WARP shader test `fm4_post_ao_shader_tests` (Direct3D 12 on the CPU, no GPU): all four passes with the root signature and constants of `post_ao.cpp`, on a synthetic 640x360 scene (floor, wall, 1 m box, sky) stored the way FM4 stores it (Z inverted through the viewport). Average occlusion: sky 0; open floor 0.000; top of the wall 0.000; floor at the wall corner 0.261; floor at the foot of the box 0.214. The W debug output is exact (difference 0.5/255). This test found three bugs, all fixed: every pixel was treated as sky (sign of the W denominator); slice directions were spread evenly in screen space; and W was point-sampled at sample positions that were not texel centers (open floor reached 0.07).
- Runtime plumbing (section 4), all disabled without `post_ao=true`. Without that cvar only the bindless slot `kPostAoOcclusion` grows.
- A full runtime build of this branch in a separate root (`I:\FM4-build-gtao`, not installed into the game build) passes without new warnings, and both CPU tests from the CMake test build pass.
- Not yet done at M0: `pipeline.py` ran the new tests (only `fm4_instruction_tests`), and the launcher (`post_ao_camera`, `-Look ao`) waited for M1/M4.

**M1**: complete 17 October 2026. One Quick Race camera cycle (`e2e --ao`, `post_ao_log_every=30`, `post_ao_log_constants` for c1 of the second FinalCombine DOF and c69 of motion blur), in a game build separate from this branch (render 1x). Results:
- Scene depth: `0x1C0C1000-0x1C459000`, 1280x720, EDRAM 1024, host 1280x720. The pass ran on every frame from the pre-race intro (frame 4798) to the end of the run (9868), with no frame skipped.
- The camera was captured in all cameras: cinematic intro (near 0.3, FOV 80, 70 and 38; telephoto near 3, FOV 7-8), bumper, near and far chase (near 0.3, far 7.5 km, FOV 93-98), cockpit (near 0.1, far 1 km, FOV 94), and hood (near 0.1, far 7.5 km, FOV 100-104, widening with speed).
- The linearization matches the game's own constants: DOF `combineQVals` (1.00004; -0.300012) and (1.0004; -3.0012), and motion blur `QAndNegNearQAndScale` (1.00004; -0.300012), equal to Q and Q*near from P. When near changes (frames 5960-5998, 0.52-0.61) both move together. W(d=0) equals the far W, so Z is indeed inverted through the viewport. Units are meters (cockpit near 0.1 m).
- The garage and showroom (DOF `30B67D8B86C37587`) have scene depth but do not draw either camera VS: no AO, with one warning. They need an additional camera VS.
- The first run found a bug in the override library: `xe_post_ao` called `GetDimensions` with 4 arguments, so all mods failed to compile and fell back to DXBC. Fixed. `override_fxc.py` with `XE_POST_AO_VIEW`: 418/418 compiled (old library: 0/1).

**M2**: complete 17 October 2026. The mod knob `FM4_LOOK_AO_DEBUG=1` (default 0) makes all four FinalCombine shaders show `xe_post_ao` as it is, at fetch coordinate 0 (see [fm4-look.hlsli](../../work/fm4-rex/look/fm4-look.hlsli)). Quick Race run (AI driver) with `post_ao_debug=1` and `shader_override_ab_ms=2000`: each camera is frozen with Y (rewind, the scene is still rendered) for 20 s, so the W display and the original scene are available from the same frozen frame. The strongest Sobel edges of W were matched to the scene gradient at shifts of +-6 px (1x) and +-12 px (2x), per 3x3 tile, without HUD. Results:
- 1x (40 pairs) and 2x (36 pairs; render and host depth 2560x1440): the best shift is (0, 0) for the whole image, in all six cameras (cockpit, hood, bumper, near chase, far chase). 85-98% of the W edges have a scene edge within 1 px.
- The deviating tiles are only tiles with few W edges over repeating textures (asphalt, billboard text; the score at 0 stays 73-99% of the peak), or the player car in the chase camera, where the strongest color edge is the roof and glass boundary rather than the silhouette. Column profile: W jumps 0-1 render pixels before the color edge, consistent with a one-sample MSAA depth resolve against an averaged color.
- No sign of a wrong format, endian, tiling, or 736 vs 720. Option B is not needed.

**M3**: complete 17 October 2026. The mod knob `FM4_LOOK_AO` (default 0) applies section 3g, with the bright weight `smoothstep(0.2, 0.8)` on the exposed luminance (2^r1.w x scene^2). A/B run with `post_ao_ab_ms=4000`, the pc look and a per-camera freeze, at 1x and 2x, plus `REX_FRAME_GRAPH`:
- GPU time of the pass (median difference of the last depth tile resolve between frames with AO on and off, high preset): 0.176 ms at 1x (GPU frame 3.00 -> 3.10 ms); 0.684 ms at 2x (5.28 -> 5.91 ms).
- At 2x there is a full-width occlusion line at host rows 925-928. The cause is an existing runtime defect that predates post_ao: at 2x and 3x, the last sub-row of guest row local 207 in each scene tile (host 415, 927 and 1439 at 2x), and the right column, are exact copies of their neighbors, in color and depth (`post_ao_debug=3`). The root cause is the Xenia hack `resolve_resolution_scale_fill_half_pixel_offset`, which fills the first row and column of every resolve region. FM4 draws the scene rotated 180 degrees within the tile, so the first row of each tile lands on local row 207 and holds real content. The fix is in `GetResolveInfo` ([src/graphics/util/draw.cpp](../../work/runtime/src/graphics/util/draw.cpp)): no fill for MSAA surfaces (3D scene, no gaps). 1x resolves are still filled (a boot screen without fill had black row and column 0). Verified with an A/B of the fill on a frozen frame, runs at 1x/2x/3x, GPU time unchanged, and the other MSAA paths at 2x: photo mode, replay (MSAA 2x), car screen and pre-race, with no black gaps at the edges and no duplicate rows.
- After the fix, the 2x A/B per camera (render 2560x1440, pc look, `FM4_LOOK_AO=1`, radius 1, power 1.5): AO is clear at the contacts and under the car, barrier legs, lamp and plate recesses, and hood folds; no visible noise or denoise halo in the raw occlusion. For M4: nearly flat body panels darken evenly (occlusion about 0.2); a halo on the asphalt around the car as wide as the radius; the sides of the hood bulge are strong; and the cockpit interior has no AO at all (W very small).

**M4**: complete 17 October 2026 (not yet committed when written). The pc default look uses AO.
- Tuning from a 2x grid (render 2560x1440, frozen frame, pc look, `post_ao_variants` with radius 0.5 / 0.75 / 1 x occlusion scale 0.6 / 1): the strength controls the darkness of the panels and the hood bulge (flat panel 82.0 -> 78.1-78.4 at 0.6 and 74.4-75.2 at 1; bulge 44.2 -> about 39.4 and 35.9). The radius controls the halo on the asphalt around the car (under the car 46.5 -> 45.1 / 43.3 / 41.0 for 0.5 / 0.75 / 1). Chosen: radius 0.75 m and strength 0.6. Flat panels are unbiased: their occlusion is 0.00-0.01, and the darkness comes from nearby features. The cockpit does not change in any variant.
- `pipeline.py`: the look knob `ao` (`FM4_LOOK_AO`) above 0 also turns on the pass (`ao_cvars`: `post_ao`, `post_ao_camera`, `post_ao_radius` 0.75). The presets pc, aces, gt and neutral use `ao=0.6`; `original` has no mod. The `-Info` overlay writes "Ambient occlusion: GTAO, strength 0.6, radius 0.75 m, high". `--ao` still runs the pass without the look.
- Gate in this branch's BuildRoot, 240 Hz, pc look compared with a no-AO baseline (`post_ao=false`, pc define without `FM4_LOOK_AO`): smoke 1x 3/3 and 3/3 (photo_view and career PASS; photo WARN only for capture statistics); replay and photo mode at 30 fps; no clear-color frames. Performance (photo, 3 runs; race phase, 12 cars): 1x GPU 2.82 -> 3.01 ms (+0.19), race 196 -> 200 fps (CP-limited; the difference is noise); 2x GPU 5.32 -> 6.09 ms (+0.77), race 174 -> 162 fps (at 2x 240 Hz the GPU is the limit). Photo mode 2x 7.74 -> 8.16 ms; replay 1x 3.90 -> 4.05 ms. The WARN `black_pct` on the final capture (SAVE SUCCESSFUL dialog over the dimmed photo) rises consistently: correct AO plus a different race scene.
- Visual run at 2x with its own launcher (pc look, 60 Hz, median 60.0 fps, GPU 5.82 ms), `post_ao_ab_ms=4000`, six cameras frozen, and photo mode: 27 on/off pairs. AO at the contacts and under the car, lamp recesses, bumper curves and exhaust, the sides of the hood bulge, and wall legs; open asphalt and sky are unchanged. The cockpit interior stays without AO (the world beyond the glass gets it). Photo mode also gets AO, both in the live view and in the rendered photo shown: depth is resolved again there and the camera is captured.

**Later**:
- AO in the cockpit interior: the interior is drawn with a compressed depth range (W close to constant at about 0.024 m), so the interior viewport's `ZSCALE`/`ZOFFSET` must be captured separately.
- Garage and showroom: an additional camera VS (M1).
- Option B for menus and the showroom.
- AO before bloom (the first consumer in the post chain).
- Ambient-only AO through a sun-visibility mask from the material shaders (host MRT).
- Specular occlusion for clearcoat. This needs AO before the scene pass, so the previous frame's AO is reprojected.
- GTAO step loop without unrolling, if compile time or shader size becomes a problem.

## 7. Risks

- The runtime-built depth texture description (format, endian, tiling, height 736 vs 720) could differ from what motion blur uses. Checked in M2; the fallback is option B.
- Some cameras do not draw the VS in `post_ao_camera` (for example a bumper with no other car on screen). The VS almost always draws; M1 records the cases.
- The first frame with scene depth does not yet know the EDRAM base. The camera may be captured from another buffer, and the pass is skipped once (a warning in the log).
- Photo mode shows a stored scene. In practice the depth is resolved again there and the camera is captured (M4): both the live view and the rendered photo get AO.
- Split screen has two scene views. Not handled yet: the second group in the same frame is skipped.
- MSAA: the depth resolve takes one sample (`copy_sample_select`), while color is averaged. AO edges are slightly jagged; the edge-aware denoise softens them.
- Smoke does not write depth, so AO on surfaces behind it shows through the smoke. Glass that writes depth is flat and gets almost no AO.
- The car drop shadow plus AO may be too dark under the car. At M4, radius 0.75 m and strength 0.6 keep the halo under the car moderate (2x grid).
- FXC at startup adds load time (estimated at hundreds of ms; only with `post_ao=true`; logged).
- D3D12 state after the dispatch (PSO, unchanged viewport, shared heaps) follows the `resolve_downscale` pattern. The M4 gate (smoke and performance) found no regression.
