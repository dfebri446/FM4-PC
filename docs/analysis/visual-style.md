# FM4 Visual Style: From Code or Assets (15 October 2026)

Research notes on how the color and mood of Forza Motorsport 4 are formed, and which parts come from game code, game assets, or this project's runtime. Sources: the Disc 1 Japan/Asia data (`Media\...`) and the decompressed XEX image (`BuildRoot\loaded-image.bin`). Game assets and shaders are not included in the repo. The values below are only a summary of observations.

## Summary

- The effect formulas (tone mapping, bloom, vignette, desaturation, output gamma) are in the game's shader code, embedded in `default.xex`.
- The numbers that give each track its feel (color, exposure, fog, shadow color, bloom) are in assets: `Media\tracks\<Track>\TrackSettings.xml`.
- The material shaders (paint, chrome, carbon, interior, particles) are `.fxobj` assets in `Media\Shaders` (compiled Xenos microcode, 172 files).
- The project runtime ([work/runtime](../../work/runtime/), [work/fm4-rex](../../work/fm4-rex/)) does not design the look. It imitates Xbox 360 hardware. Some workarounds still touch the visuals (see the Runtime section).

## Code: Post-Processing in default.xex

Technique names and shader constants found in the XEX image:

- `FinalCombine_BloomTonemap`, with the variants `_Vignette`, `_DOF` and `_Watermark`: the final pass that combines the HDR scene, bloom, tone mapping and vignette.
- `BlitOneTexDegammaToTonemapLinear`, `BlitOneTexDegammaTosRGB`, `BlitOneTexDegammaToTV`, `gamma709`, `GaussianBlur7TapDegamma`: gamma conversion to TV/sRGB, and the bloom blur.
- Constants: `filmicParams1`, `filmicParams2`, `filmicParamsE`, `tonemapAdaptiveExposure`, `vignetteColor`, `vignetteScaleOffsetPower`, `saturatorSaturation`.

These match the race frame graph in the project plan (kept outside the repo), phase B1: an HDR scene in 7e3, a bloom/downsample chain in 2_10_10_10 down to 32x32, a 32_FLOAT 32x32 luminance target for auto-exposure, the composite, and then the HUD. About 15 post-processing passes.

## Assets: TrackSettings.xml per Track

The tag names below also appear in the XEX (read by the code above).

- `filmicTone` (`shoulderStrength`, `linearStrength`, `linearAngle`, `toeStrength`, `toeNumerator`, `toeDenominator`, `white`, `exposure`): the filmic tone curve. The parameter names match the Hable/Uncharted 2 style of curve.
- `ToneMapAdaptiveExposure`, `ToneMapCockpitExposure`, `CameraExposureRange`, `ToneMapeDelay`: auto-exposure. The cockpit has its own settings.
- `bloomCutoff`, `bloomScale`, `bloomWeights`, `bloomPersist`: bloom.
- `DesaturateAmount`, `DesatLuminosity` (Rec.709 coefficients), `DesatPostMod`, `cubeSkySaturation`: saturation.
- `ShadowColor`, `LightAmbient`, `SHLighting` (up/down/sun direction), `DarkColor`, `LightColor`: lighting.
- `Fog\Settings0..7` (color, angle, near/far, density): colored fog per direction.
- `SunSize`, `SunAdjust`, `sunGainScale`: sun glare.
- `imageBasedLighting`, `GlobalSpecularLevel`, `RoadSpecular`, `GlobalFresnel`: reflections and gloss.

## Pattern Across the 32 Tracks and Rooms

- Race tracks: `DesaturateAmount` is 0.85 on all tracks. The filmic curve is almost uniform (shoulder about 0.8, toe about 0.18, exposure about 0.31). `bloomScale` is about 4 (Infin 1.4, Nurburgring 1.5, RoadAmerica 2.0, Motegi/Tsukuba/TopGear about 5). Impression: slightly faded, cinematic, with bright highlights.
- HomeSpace/UI (garage, showroom, thumbnails): `DesaturateAmount` 0.92-1.0, shoulder 0.151, IBL on, weaker bloom. Impression: clean, "studio".
- Shadows and fog lean cool blue. Alps example: `ShadowColor` (0.22, 0.28, 0.34), fog (0.46, 0.60, 0.85). Warm exceptions: Silverstone fog (0.70, 0.63, 0.50) and Autopark sunlight (0.80, 0.79, 0.70).
- The differences between tracks are mainly tint (fog, shadow, sun, bloom), not the base curve. The global style is consistent, and each track gets its own color.
- Not yet proven from the shader code: the exact meaning of `DesaturateAmount`. Guess: 0.85 means 85% of the saturation is kept (the UI thumbnails use 1.0).

## Other Assets That Shape the Look

- Track textures and lightmaps in the `bin.zip` of each track folder; `staticcarcubemap.xpr` for car reflections; the skybox (`SkyboxIndex`).
- `Media\RenderScenarios\*.xml`: DOF, car LOD, crowds, tiling/MSAA, and the frame rate per mode (race, replay, photo, UI).
- `Media\Effects`: particles (smoke, dust, glass, grass) and their textures.

## Runtime: Port Parts That Touch the Visuals

These are not a source of the FM4 style, but they can make the picture differ from the original Xbox:

- The hardware gamma ramp is emulated at present (`apply_gamma_pwl` / `apply_gamma_table` in [work/runtime/src/graphics](../../work/runtime/src/graphics/)). The ramp values are supplied by the game.
- [fm4-render-overrides.cpp](../../work/fm4-rex/src/fm4-render-overrides.cpp) copies the RenderScenarios. Only with `-ShadowWorkaround` (`FM4_SHADOW_WORKAROUND`, off by default since 16 October 2026) does it add `SkipShadowMapUnlessCockpit`: shadow maps only for the cockpit camera, as in split screen. Other options replace `TilingScenario`, `FrameRate` and `HalfRateMirror`.
- The launcher forces 16x anisotropic filtering (`anisotropic_override=5`) for mipmapped textures with linear filtering. The game itself does not request it for most textures, and requests 2x-16x for a few.
- `rov_occlusion_awaited_report_divisor=2` (empirical) affects the strength of the sun glare.
- FXAA swap happens only in single-tile mode. The 7e3 format conversion is done on the host. Resolution scaling is in [pipeline.py](../../outputs/FM4-build/pipeline.py).
- The default `pc` look (`-Look` in [Build.ps1](../../outputs/FM4-build/Build.ps1), `--look` in [pipeline.py](../../outputs/FM4-build/pipeline.py)) does change the style. It uses project-made shader mods in [work/fm4-rex/look](../../work/fm4-rex/look/) (tonemap, saturation, bloom, sharpening, car clearcoat) and, since 17 October 2026, screen-space ambient occlusion. The AO comes from the runtime's GTAO pass on the scene depth (radius 0.75 m) and is applied in FinalCombine with strength 0.6 (the `ao` knob), weakened on bright pixels. The cockpit, the garage and the showroom do not get AO yet. `-Look original` turns all of it off. See the [GTAO design](gtao-design.md).

## Direction of Change

- Without changing code: override `TrackSettings.xml` (`DesaturateAmount`, `filmicTone`, `bloomScale`, fog and shadow colors) through the same mechanism as [fm4-render-overrides.cpp](../../work/fm4-rex/src/fm4-render-overrides.cpp). Not implemented yet.
- Changing the effect formulas (tone mapping, AA, new effects) needs modifiable shaders: phase B4 in the project plan.
- Racing scenarios use leaner settings than replays and photo mode. Racing uses `TilingScenario 1` (2xMSAA, two tiles). Replay, photo mode, menus and the 1-car race 0 use `TilingScenario 0` (4xMSAA, three tiles). Racing has no `SoftParticles` or `CollidableShadows`, `CarMinWheelLOD 1` (photo mode 0), and `CarLODTransDist` near 15 / far 25 / veryfar 50. 16-car races also use `shaders_16car`, have no damage, `EnvMapFrequencyScale 0` and `HalfRateBloom`.
- Tiling scenario names in the image: `FPS_30`, `FPS_60`, `4xMSAA`, `2xMSAA`, `NonTiling`, `Small2xMSAA`, `SplitScreen_NonTiling2xMSAA`.
- `-Quality high` (the default since 16 October 2026) makes races match replays. `ultra` adds `CarKeepLOD0` and full mirrors (`FM4_RACE_SETTINGS` in [fm4-render-overrides.cpp](../../work/fm4-rex/src/fm4-render-overrides.cpp)).

## Related

- [B4 shader research](b4-shader-research.md): the HLSL override path and the look mods.
- [Material atlas](material-atlas.md): the scene shader map used for material mods.
- [Running](../running.md): look knobs and launcher options.
