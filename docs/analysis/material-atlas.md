# FM4 Material Atlas (16 October 2026)

A map of FM4 scene shaders to game materials: a base for material shader mods (car paint, glass, asphalt) and for new passes, in the style of the Custom Shaders Patch. This summary contains only the names and registers from the game's constant tables, not code. The full atlas is generated from user-owned data and stays in BuildRoot.

## How It Is Built

```
outputs/FM4-build/e2e/material_atlas.py --build-root <BuildRoot> --dump <dump_shaders dir> [--inventory <REX_GPU_INVENTORY json>] --json <BuildRoot>\atlas\atlas.json
```

([material_atlas.py](../../outputs/FM4-build/e2e/material_atlas.py))

- Shader container sources (the same as the B4 post-process catalogue): `Media\Shaders` (car `.fxobj` files, particle effects), `default.xex` (`loaded-image.bin`), and the track material shaders in `Media\tracks\<track>\bin.zip`. The `bin.zip` entries use zip method 21: XMemCompress LZX, with a frame of [FF + original size] + block size and a 128 KB window. They are decompressed with the runtime's `lzx_decompress` (`rexruntime.dll`, exported), and the zip CRC matches. 32 tracks, 1,132 files, 235 material templates (`ROAD_*`, `RDEDG`, `RDLINE`, `SHLDR`, `TERR`, `TREE`, `TREECARD`, `GRASS`, `SKY`, `SIGN`, `BARR_SHAD`, `LAKE`/`RIVER`/`OCEAN`, `ANIM_*`, ...).
- Pixel shaders are matched by the md5 of their ucode (big-endian). Vertex shaders cannot be matched that way. The game's Direct3D patches the VS vfetch instructions for the vertex declaration at bind time, so the ucode in memory differs from the file (the first 64 bytes are the same). A VS is matched to the container with the same length and the most identical dwords (75% threshold).
- Class: track shaders from the file-name prefix; car shaders from the constant or sampler name (`GlassColor` -> glass, `Chrome*` -> chrome, `PaintScale` -> paint, ...); VS from their paired pixel shader with the most draws.

## Result (Photo Scenario: 422 PS / 265 VS, Quick Race ROV Inventory)

PS: 421 of 422 have a source. VS: 198 of 265 (similarity 0.75-1.0). Share of draws by class (5.5 million draws, 1,991 frames):

| Class | PS | Share of draws |
|---|---|---|
| engine (default.xex: UI, post, env, shadows, impostors) | 58 | 28.9% |
| track objects (`DIFF_*`, ...) | 75 | 16.0% |
| "car shadow" (248CBDF02EEBD66F, `TrackGeoShadow*`) | 1 | 10.1% |
| car paint | 27 | 9.3% |
| car glass | 10 | 5.8% |
| other cars | 36 | 4.8% |

Smaller classes: car lights 3.3%, rims 2.8%, trees 2.6%, animated objects 2.1%, road shoulder 1.9%, chrome 1.6%, brakes 1.2%, tires 1.0%, cockpit 0.9%, markings 0.9%, asphalt 0.6% (large draws, few in number), barriers 0.6%, sky 0.1%.

Draw count is not screen area: asphalt and sky have few draws but cover most of the screen.

## Car Body Paint (First Target for Material Mods)

PS `B85EF04779ABD438` (106,000 draws in the sample). VS `8D05A46C3584CA9E`. Sources: `shaders_v16` and `shaders_splitscreen_v16`.

Samplers (`s#` = texture fetch):

- `baseSampler` s0, `damageSampler` s1, `DirtSampler` s2, `DirtMaskSampler` s3
- `envBackgroundSampler` s4 (2D), `envSampler` s5 (dynamic cube), `envStaticSampler` s6 (static cube)
- `trackLightmapSampler` s10, `s_varianceShadowMap` s13 (self-shadow)

PS constants: `psLightDirWS` c43, `shDirectionalColour` c42, `psFogColor` c47, `reflectionScaler` c52, `alphaDirtAmount` c57, `c_eyePosWS` c59, `trackLightMapParams` c60, `ShadowDarkeningRamp` c80, `MinDiffuse` c81, `PaintColor` c83, `PaintScale` c84, `Stripe*` c85-c87, `SpecularAmount2`/`Power2` c88-c89, `SpecularAmount3`/`Power3` c90-c91, `DynamicEnvLightmapDampner` c92, `FresnelIndex` c93, `FresnelScalar` c94, `Dirt`/`Damage*Kill` c95-c97, `TwoTone*` c98-c101, `SHBTransition*` c102-c103, `Metal*` c104-c107, `MinShadowValue` c108, `StaticEnvSH*` c109-c110.

PS bool constants: `doselfshadow` b0, `premultipliedAlpha` b1, `useStaticCubeMap` b2, `specular2Everywhere` b3, `psUseBackgroundMap` b5, `psIsStockRaceCar` b6.

Interpolators (read from the VS ucode with its constant table: `matWVP` c0, `matWV` c4, `matW` c8, `matWInvT` c12, `trackLightmapWVPS` c32, SH c36-c40, `c_shadowTexMtxNear` c48):

- r0: UV (`uv1CompressionFactors`) and dirt UV (`DirtOffsetU`/`V`)
- r1: world normal (`matWInvT`, normalized)
- r2: world position (`matW`); W: the directional SH term
- r3: shadow map coordinate (`c_shadowTexMtxNear`)
- r4: track lightmap UV (`trackLightmapWVPS`)
- r5: ambient SH RGB (`shHemisphericalLightR`/`G`/`B`); W: `shCubeMapDampnerLight`

So the normal, world position, eye position, sun direction, cube env, shadow and ambient are already in the pixel shader. A new paint shading (GGX clearcoat, fresnel, flake) does not need a vertex shader override. Other paint variants (E5B1A71249D0CA9A without the env cube, 46CB671D893E6709, AB9B5568DBC34C8F, 74FE953B234A69E6) and the rims use similar register blocks (c80 upward per material, c40-c60 shared).

## False-Color Verification (16 October 2026)

Mods that tint each candidate hash's output with a different color, in Quick Race with camera cycling (photo mode shows the rendered scene buffer):

- Cars: `B85EF04779ABD438` covers the body of all cars (in front and in the mirrors). `E62CFDF09D758B6C` covers the windows. `AF6AA26032BC613D` covers the exhaust tips and mirrors. `46CB671D893E6709` covers only small trim on the bumper. `E5B1A71249D0CA9A` (almost the same draw count as B85E, without the env cube) never appears on screen; it is possibly the paint in the reflection-map render. `AB9B5568DBC34C8F` and `74FE953B234A69E6` do not appear.
- Track: asphalt, markings, run-off, road edges, sky, trees, snowy mountains, stands and barriers match their classes.

The clearcoat mod for `B85E` is in [work/fm4-rex/look](../../work/fm4-rex/look/) ([fm4-paint.hlsli](../../work/fm4-rex/look/fm4-paint.hlsli); `-Look clearcoat=...`, default in the `pc` preset).

## Not Yet Verified

- `248CBDF02EEBD66F` (10% of draws, `TrackGeoShadow*`, from `shaders_16car`) and `A4A965C189287B99` (no source, point/rect).
- The names of the car effect techniques (body, chrome, glass, window, carbon_fiber, cockpit_*, ...) are in the `.fxobj` files, but the mapping from technique to container has not been read (the effect format is not yet parsed).
- The draw inventory comes from an old ROV run (a previous BuildRoot). The dumps are from the photo scenario. Other tracks use the same templates, but their ucode may differ.

## Related

- [B4 shader research](b4-shader-research.md): the HLSL override path and the look mods.
- [Visual style](visual-style.md): where the look of FM4 comes from.
- [Shader override diagnostics](../e2e/experiments.md): the A/B overrides and override diagnostics.
