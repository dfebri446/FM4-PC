# B4 Shader Research: An Editable Shader Path (15-16 October 2026)

Summary of the research session "Check the XenosRecomp license and the feasibility of B4" (RTX 5070 Ti, driver 610.88), plus the results of B4 step 1. These are observations and numbers only; no shaders, ucode or game assets are included. Game data was read locally and is not copied.

## Conclusions

- The translator is our own, not XenosRecomp, and not dxilconv as the main path.
- The target is HLSL, compiled with FXC (d3dcompiler_47.dll, System32) to DXBC SM5.1, not DXC/DXIL SM6. This pairs with the existing geometry shaders and bindless root signature, with no new dependencies.
- The value of B4 for FM4 is in roughly 49 post-process shaders (tonemap, bloom, AA, new effects). Parity for the 383 scene shaders is not needed. Free resolution already exists through `draw_resolution_scale`; most precision questions belong to the render target cache (the 7e3 chain is already float16).
- B4 is not a performance project. The GPU frame time of a race is about 2.5 ms, and overrides use the same translator binding contract, so the CPU cost per draw (`UpdateBindings`, `RequestTextures`, `LoadShader`) does not change. Chasing 240 fps is a separate track (the project plan, kept outside the repo).

## Tests on This Machine

These used our own D3D12 test program, with a mock of the project's root signature.

- dxilconv converts 542/542 DXBC dumps of FM4 (217 VS + 325 PS SM5.1): average 4.3 ms, maximum 23 ms. The DXC validator (dxil.dll, SDK 26100) rejects 542/542 ("Constant values must be in-range" on `dx.op.createHandle`), but the driver still accepts their PSOs.
- A mixed DXBC + DXIL PSO fails with E_INVALIDARG in both orders (DXBC+DXBC and DXIL+DXIL succeed). Replacing a shader per shader with DXIL therefore means the whole PSO, including the hand-assembled GS (`pipeline_cache.cpp`), must be DXIL.
- Cold PSO, 195 inventory pairs that succeeded on both sides: DXBC median 54 ms, DXIL 67 ms (about 13% slower).
- FXC SM5.1 with `D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES` compiles HLSL with the project's bindless contract, and a PSO with the project's root signature succeeds (without the flag: X3596). DXC SM6.0 also succeeds.
- Synthetic compile times, DXC / FXC: 30 ALU 8 / 13 ms, 130 ALU 25 / 41 ms, 400 ALU 81 / 124 ms, 1200 ALU 335 / 500 ms.
- DXC rejects `Sample()` in vs_6_0, which the XenosRecomp tfetch helper uses.

## Runtime Contract ([work/runtime](../../work/runtime/))

- Bindless (`d3d12_bindless=true`, tier 3 on this machine). The root signature has 10 parameters: b0 system, b1 float VS and PS, b2 bool/loop, b3 fetch, b4 descriptor indices for VS and PS, the shared memory SRV/UAV tables, an unbounded sampler table at s0, and unbounded SRVs in space1 (2D array), space2 (3D) and space3 (cube). The enum is in [command_processor.h](../../work/runtime/include/rex/graphics/d3d12/command_processor.h).
- Textures are selected by index in b4 (4 per uint4), with an unsigned/signed pair per fetch constant. The translator builds the binding list.
- Local FM4 patch: `SystemConstants::texture_integer_scale_bits`.
- Host formats: 7e3 -> R16G16B16A16_FLOAT, 2_10_10_10 -> R10G10B10A2_UNORM.

## FM4 Data

- RTV pipeline storage (747 pipelines): 154 unique VS and 241 unique PS. Translations: 192 (VS, modified) + 260 (PS, modified). At most 2 modifications per VS and 4 per PS. No tessellation. Geometry shaders: rect 44, point 17, quad 15. The PS depth mode is never float24.
- Quick Race shader size: median 38 ALU instructions; p90 69 (VS) and 88 (PS); maximum 162. 48-50 unique opcodes cover 100% of the shaders.
- Post-process family: 36 rect/quad pairs (3.6% of draws), 16 VS (all mini-vfetch) and 33 PS. Constant names are available for 166 of the 276 PS through the container.
- The ucode in the PM4 stream is not the container ucode: D3D patches vfetch at runtime. 133 of the 150 Quick Race VS use mini-vfetch (80.8% of draws).
- Quick Race PS features (weight = share of draws): SetTextureLod 25 PS (10.8%), GetTextureGradients 5 PS (16.0%), CondJmp 47 PS (20.6%), loop/switch 10 PS (7.2%), cube 68 PS (20.3%), 3D 4 PS. Memexport: 0.
- FinalCombine_BloomTonemap: VS 50FDB5D3AB1CC2D8 (2 vfetch, 1 of them mini; 1 tfetch; 2 CondJmp). PS E97AA211B556A9B3 (3 tfetch: 2D, 2D, 3D LUT; no flow control).

## XenosRecomp on FM4

- The FM4 container format is read 100% (2,184 containers from 171 .fxobj files and default.xex).
- Its input model does not fit. It depends on containers and vertex declarations, but the runtime only has the patched PM4 ucode. Mini-vfetch is not supported. SetLod and GetGradients are ignored. The SM3 rules (+-0 times anything = +0, NaN on max/min) are absent. At most it is a reference for HLSL idioms. It has not been built or run on FM4.

## B4 Step 1: Per-Hash HLSL Override (16 October 2026)

- `shader_override_path=<dir>`: a file named `shader_<hash>[_<modification>].ps.hlsl` replaces the DXBC translation of that pixel shader ([shader_override.cpp](../../work/runtime/src/graphics/d3d12/shader_override.cpp)). The file defines `xe_pixel_shader(inout XePixel p)`. The runtime adds the contract of the translation being replaced (constants, descriptor slots, interpolators, outputs) and the library [shader_override.hlsli](../../work/runtime/src/graphics/d3d12/shader_override.hlsli) (2D/3D/stacked/cube tfetch, SM3 ALU rules, PWL gamma, PS prolog and epilog: alpha test, alpha to coverage, exp bias). It then compiles with FXC ps_5_1 and `/Gis`. The input and output signatures are compared with the DXBC translation; if that fails, DXBC is kept. With `dump_shaders`, the contract of every PS is written there together with its ucode.
- `shader_override_ab_ms=N`: every second period of N ms of the steady clock uses the DXBC translation (its own modification bit, a separate PSO, not saved to pipeline storage). [override_ab.py](../../outputs/FM4-build/e2e/override_ab.py) compares the captures of both phases.
- The FinalCombine PS was rewritten faithfully in HLSL (in BuildRoot, not committed). FXC takes 120 ms in-game. In every static part of photo mode, the override capture and the DXBC capture are bit-identical ([status history](../status/history.md)). A +1 stop exposure variant looks brighter in the same scene, which shows that this shader is active there.
- FXC quirks: a token-pasting macro whose result starts with the macro's own name is not expanded (`XE_C(n)` uses `XE_FLOAT_CONSTANT_n`), and `#if -1 >= 0` evaluates true (a separate flag). `/Gis` marks all instructions precise. `mad` appears only where the translator also uses `mad`.

## B4 Step 2: HLSL Backend (16 October 2026)

- `HlslShaderTranslator` (the `ShaderTranslator` base, like `DxbcShaderTranslator`; see [hlsl_translator.h](../../work/runtime/include/rex/graphics/pipeline/shader/hlsl_translator.h)) generates an HLSL override from pixel shader ucode, operation by operation, like the DXBC translation:
  - operands are loaded like `LoadOperand` (unused components of a swizzle repeat the first one);
  - results are stored like `StoreResult`;
  - vector and scalar operations read their operands before either result is stored;
  - control flow uses pc, the loop counter and aL, as in the translator;
  - 1D/2D/3D/stacked/cube texture fetches support computed LOD, explicit, basemap, register LOD and register gradient.
- Not supported (DXBC is kept): vertex fetch, getWeights, getCompTexLOD, getBCF, memexport.
- Descriptor slots are named from the DXBC translation bindings. The translator numbers slots starting from 1 (the count after the bindings are added), so the contract uses the maximum index + 1.
- `shader_override_generate_path` writes that HLSL as override material. `shader_override_generated` uses it for every pixel shader that has no file.
- Test on the photo scenario: the generator produces 418 of 418 pixel shaders (84 with a pc loop, 3 with a loop, 3 with kill, 121 with cube fetch, 45 with LOD or register gradient). FXC compiles all of them (offline: 8 seconds with 12 threads; in-game: median 136 ms, maximum 3.6 seconds per shader). With all pixel shaders using the generator's HLSL and a 5-second A/B, the captures of the static part of photo mode and of the boot screen are bit-identical to the DXBC translation. The pairs that differ fall on screen transitions, exactly as in the run with hand-written overrides.

## Post-Process Catalogue (16 October 2026)

The 422 pixel shaders of the photo scenario were matched to the containers in default.xex and the disc .fxobj files by ucode md5 (224 matches, including all listed below), then combined with the Quick Race draw inventory (rect/quad primitives = full screen). Hashes, number of draws in the sample, and constant names (not code):

- Race FinalCombine (scene + bloom, filmic, 3D LUT, gamma 2.0): E97AA211B556A9B3 (4,940 draws). With vignette: 9CDA139F232A8A73 (741). With DOF: 30B67D8B86C37587 (754) and AE3B2E36FCA4958D (87; `dofFocalParams`, `combineQVals`). Constants: `filmicParams1`, `filmicParamsE`, `colorTransform`, `colorGradingTexScale`/`Offset`.
- Auto-exposure: CAF3881F7C4CDCAA (`tonemapAdaptiveLuminanceKeys`, `previousFrameAmount`, `tonemapReset`, `tonemapWeightRange`).
- Bloom: Gaussian blur 1D7A35C8776E7F52 and 4C519B38C0A7FB8B (16,112 draws each; `gaussWeight`); 7-tap 3C7134E656BA9E0A and 2A8603D90CD65ECA (`gaussOffsets7Tap`); combine of 5 textures 9B70AAB58FB9F35E and of 2 textures D31AE23A56F1818D (`srcTexPreMultiply`); downsample 805C1FEAC31575B3 (`srcMipLevel`).
- Motion blur: 4746CAD32EF168CF (`invCurrentVPThenPrevVP`, `QAndNegNearQAndScale`).
- Blit and UI: BCFAE662E7F38473, AE59F518D522BDD1, 618DD627F3D3B9A4, ECBE193E90A42B68 (s0), 6E57A6E305D5D69B (`g_TextureSrc1`), C23E686D7A951CE1 and 9A20F0519E2257A5 (offset, `sourceTexture`), 59371DD872E33420.
- Not post-process, even though they are full screen or quads: road detail (`roadDetailBlur*`), racing lines, skid marks, shadows, impostors (`Ambient`, `FogColor`, `SunColor`), and environment-map processing (`spherical*`).

Overrides that change the look should be the project's own code that uses this shader's inputs and constants (through the contract), not a copy of the game's ucode, so that they can be committed. Faithful copies generated by the generator stay local.

## FinalCombine Formula

The formula, taken from the ucode, for writing mods:

- `x = (scene^2 + bloom^2) * 2^(interpolator 1 W)`
- filmic (Hable) `f(x) = (x(Ax+CB)+DE)/(x(Ax+B)+DF) - E/F` (`filmicParams1`/`E`)
- then `* (c / interpolator 1 Z) + bias` (the white point normalization computed by the vertex shader), 3D LUT, 3x4 affine color transform, saturate, sqrt (gamma 2)
- The vignette variant blends toward `vignetteColor.xyz` after the LUT, with strength `vignetteColor.w * saturate((|T*pos|^2 + offset)^power)`.

Because the white point is normalized in the vertex shader, changing the filmic parameters only in the pixel shader shifts brightness. The curve itself is changed in output space.

## B4 Step 3: Display Mods (16 October 2026)

- The generator leaves neutral hooks: `XE_HOOK_TFETCH` on each fetch result, `XE_CONSTANT` -> `XE_HOOK_CONSTANT` on each constant, `XE_HOOK_INPUT` after the prolog, and `XE_HOOK_OUTPUT` before the epilog. `shader_mod_path=<dir>` with `shader_<hash>.mod.hlsli`: the generator's HLSL for that hash is compiled with the mod after the library (the mod `#undef`s and redefines the hooks). `shader_mod_defines` provides the knobs. There is no game code in the mod files.
- [work/fm4-rex/look](../../work/fm4-rex/look/) (`fm4-look.hlsli` + 5 mods): contrast-adaptive sharpening (CAS) on the scene fetch (tf0) of the four FinalCombines, bloom energy (tf2), exposure (interpolator 1 W), contrast (S-curve) and saturation on the output, vignette strength (c2.w in 9CDA139F232A8A73, c4.w in AE3B2E36FCA4958D), and motion blur 4746CAD32EF168CF mixed with the unblurred scene sample. Usage: `Build.ps1 -Look "sharpen=...,..."` ([Build.ps1](../../outputs/FM4-build/Build.ps1); [pipeline.py](../../outputs/FM4-build/pipeline.py) and `e2e.py --look` in [e2e.py](../../outputs/FM4-build/e2e/e2e.py)).
- Photo test with sharpen 0.6, bloom 0.7, contrast 0.25, saturation 1.1, vignette 0.3 and motion blur 0.3, with a 5-second A/B: all five mods compiled (0.5-1.3 seconds). In photo mode, asphalt and car detail are sharper, the bloom glow is smaller, and curbs and road lines are no longer smeared by motion blur.

## B4 Step 4: Tone Curves (16 October 2026)

All four FinalCombines share the same constant pattern: A and B are in Y and Z of constant `k`; CB, DF, DE and E/F are in constant `k+1`; the LUT scale and offset are in X of constants `s` and `s+1`; F at white is in interpolator 1 Z; and exposure is in interpolator 1 W (inputs, not yet overwritten before use).

| Shader | Constant `k` | Constant `s` |
|---|---|---|
| E97AA211B556A9B3 | 5 | 0 |
| 9CDA139F232A8A73 | 8 | 3 |
| 30B67D8B86C37587 | 19 | 2 |
| AE3B2E36FCA4958D | 22 | 5 |

In the DOF variants, the color that is tone-mapped is the DOF composite.

`-Look tonemap=gt|aces|neutral` (`FM4_LOOK_TONEMAP` 1-3; filmic 0 = original). The fetch 7 (LUT) hook:

- takes F(x) from the LUT coordinate `((coord - offset) * F(white) / scale)`;
- inverts F exactly, using the positive root of the quadratic `A(1-q)x^2 + (CB - qB)x + (DE - q DF) = 0`, with `q = F + E/F` (relative error 4e-13 in double precision);
- scales x so that middle gray (0.18) stays where the game's curve puts it;
- then samples the LUT at the coordinate of the new curve.

The grading LUT, the color matrix, the vignette and auto-exposure are kept. The curves are GT (Uchimura, CEDEC 2017: power toe, linear section, exponential shoulder), ACES (Narkowicz 2015 fit) and Khronos PBR Neutral. F(white) is passed from the input hook through a static variable.

Simulation with the filmicTone of Alps (A 0.6, B 0.04, C 0.63, D 0.22, E 0.003, F 0.14), gamma-2 output at each stop from middle gray:

| Curve | -4 | -2 | +0 | +2 | +3 | +4 |
|---|---|---|---|---|---|---|
| original | 0.072 | 0.164 | 0.424 | 0.857 | 0.971 | 1.000 |
| GT | 0.066 | 0.170 | 0.424 | 0.830 | 0.966 | 0.998 |
| ACES | 0.053 | 0.150 | 0.424 | 0.793 | 0.901 | 0.959 |
| neutral | 0.034 | 0.138 | 0.424 | 0.906 | 0.976 | 0.990 |

ACES darkens the shadows and keeps detail up to about 4.5 stops above middle gray (the original saturates at about 3 stops). GT is closest to the original. The result is the same for white 1, 4 and 11.2, because the anchor is middle gray.

- `override_fxc.py --mods`: 418/418 compiled for tonemaps 0-3. E97A with GT is 691 instructions (original: 605).
- In game, on the photo scenario with a 5-second A/B per curve, all five mods compiled (80-940 ms). The mod/DXBC pairs in the static part of photo mode (bracketed by identical in-phase pairs) differ on average by 4.7 per byte for ACES, 3.0 for GT, 3.5 for neutral, and 6.4 for ACES with saturation 1.1, bloom 0.7 and sharpen 0.4. This matches the simulation: ACES darkens the shadows (trees, tires, asphalt) and keeps gradation in the snow; GT is closest to the original.
- That combination is now the `pc` preset and the default look of the launcher and of `e2e.py`. The presets `aces`, `gt`, `neutral` and `original` (no mods) remain available ([pipeline.py](../../outputs/FM4-build/pipeline.py), `LOOK_PRESETS`).

## Related

- [Visual style](visual-style.md): where the look of FM4 comes from, and the `pc` look.
- [Material atlas](material-atlas.md): the scene shader map used for material mods.
- [GTAO design](gtao-design.md): the ambient occlusion pass used by the look.
- [Shader override diagnostics](../e2e/experiments.md): the A/B overrides and override diagnostics.
- [Running](../running.md): look knobs and launcher options.
