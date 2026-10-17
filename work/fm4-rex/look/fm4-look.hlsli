// FM4 look mods: knobs and functions shared by the shader_<hash>.mod.hlsli files
// of this directory (Build.ps1 -Look, pipeline.py --look: shader_mod_path and
// shader_mod_defines). Original code of this project: the game's own shaders
// are generated from the user's game at run time, and these only hook into
// their texture fetches, constants, inputs and outputs (shader_override.hlsli in
// work/runtime). Every knob defaults to the original look.
//   FM4_LOOK_SHARPEN      0...1  adaptive sharpening of the scene before tonemapping
//   FM4_LOOK_BLOOM        >= 0   bloom energy (1: original)
//   FM4_LOOK_EXPOSURE     stops  added to the auto exposure (0: original)
//   FM4_LOOK_CONTRAST     0...1  S-curve on the final image (0: original)
//   FM4_LOOK_SATURATION   >= 0   saturation of the final image (1: original)
//   FM4_LOOK_VIGNETTE     >= 0   vignette strength (1: original, 0: off)
//   FM4_LOOK_MOTION_BLUR  0...1  motion blur (1: original, 0: off)
//   FM4_LOOK_TONEMAP      0...3  tone curve before the color grading LUT: 0 the game's
//                                filmic curve, 1 GT (Uchimura), 2 ACES (Narkowicz fit),
//                                3 neutral (Khronos PBR Neutral)
//   FM4_LOOK_AO           0...1  ambient occlusion of the runtime's post_ao pass on the scene
//                                (0: off; needs post_ao=true)
//   FM4_LOOK_AO_DEBUG     0, 1   1: the final combine shaders show the runtime's post_ao
//                                texture as it is (xe_post_ao; post_ao_debug picks its
//                                view) instead of the scene (0: off)

#ifndef FM4_LOOK_SHARPEN
#define FM4_LOOK_SHARPEN 0.0
#endif
#ifndef FM4_LOOK_BLOOM
#define FM4_LOOK_BLOOM 1.0
#endif
#ifndef FM4_LOOK_EXPOSURE
#define FM4_LOOK_EXPOSURE 0.0
#endif
#ifndef FM4_LOOK_CONTRAST
#define FM4_LOOK_CONTRAST 0.0
#endif
#ifndef FM4_LOOK_SATURATION
#define FM4_LOOK_SATURATION 1.0
#endif
#ifndef FM4_LOOK_VIGNETTE
#define FM4_LOOK_VIGNETTE 1.0
#endif
#ifndef FM4_LOOK_MOTION_BLUR
#define FM4_LOOK_MOTION_BLUR 1.0
#endif
#ifndef FM4_LOOK_TONEMAP
#define FM4_LOOK_TONEMAP 0.0
#endif
#ifndef FM4_LOOK_AO
#define FM4_LOOK_AO 0.0
#endif
#ifndef FM4_LOOK_AO_DEBUG
#define FM4_LOOK_AO_DEBUG 0.0
#endif

#ifdef XE_TEXTURE0_2D_U

// Contrast adaptive sharpening (after AMD FidelityFX CAS, cross neighbors only)
// of the scene texture, fetch 0 of the final combine shaders. Its values are
// HDR with gamma 2 (the shaders square them), so the weight comes from the
// range clamped to 0...1, and highlights stay unsharpened.
float4 fm4_sharpen_scene(float4 center, float2 uv) {
  if (FM4_LOOK_SHARPEN <= 0.0) {
    return center;
  }
  float2 texel = 1.0 / xe_texture_size_2d(0u);
  float3 up = XE_TFETCH2D(0, uv + float2(0.0, -texel.y)).rgb;
  float3 left = XE_TFETCH2D(0, uv + float2(-texel.x, 0.0)).rgb;
  float3 right = XE_TFETCH2D(0, uv + float2(texel.x, 0.0)).rgb;
  float3 down = XE_TFETCH2D(0, uv + float2(0.0, texel.y)).rgb;
  float3 middle = center.rgb;
  float3 low = saturate(min(middle, min(min(up, down), min(left, right))));
  float3 high = saturate(max(middle, max(max(up, down), max(left, right))));
  float3 amount = sqrt(saturate(min(low, 1.0 - high) / max(high, 1.0 / 256.0)));
  float3 weight = amount * (-1.0 / lerp(8.0, 5.0, saturate(FM4_LOOK_SHARPEN)));
  float3 sharpened = (middle + weight * (up + down + left + right)) / (1.0 + 4.0 * weight);
  return float4(max(sharpened, 0.0), center.a);
}

// The motion blur shader replaced by the unblurred scene at the pixel, in part
// or completely (its output and the scene texture are both gamma 2).
void fm4_motion_blur(inout XePixel p, XePixel input) {
  if (FM4_LOOK_MOTION_BLUR == 1.0) {
    return;
  }
  float3 scene = XE_TFETCH2D(0, input.r[0].xy).rgb;
  p.color[0].rgb = lerp(scene, p.color[0].rgb, saturate(FM4_LOOK_MOTION_BLUR));
}

#endif  // XE_TEXTURE0_2D_U

// The bloom texture, squared by the final combine shaders before it is added.
float4 fm4_bloom(float4 bloom) {
  if (FM4_LOOK_BLOOM == 1.0) {
    return bloom;
  }
  return float4(bloom.rgb * sqrt(max(FM4_LOOK_BLOOM, 0.0)), bloom.a);
}

// Tone curves. The final combine shaders map the exposed scene color x with a
// filmic curve F(x) = (x(Ax + CB) + DE) / (x(Ax + B) + DF) - E/F (A and B in Y
// and Z of the first of two constants, CB, DF, DE and E/F in the second), normalize it so
// white reaches 1 (F at white comes in interpolator 1 Z) and look the result up
// in the track's color grading LUT at t * scale + offset. These replace F
// between the two: the LUT coordinate gives F(x) back, F is inverted for x, and
// the new curve maps x, scaled so that middle gray (0.18) stays where the game's
// curve puts it, which keeps the auto exposure and the grading as they are.

// F at white, interpolator 1 Z of the final combine shaders (fm4_combine_input).
static float fm4_white_filmic = 1.0;
// The scene coordinate of the pixel, fetch 0 of the final combine shaders.
static float2 fm4_scene_uv = 0.0;
// Their exposure, 2 to the power of interpolator 1 W (fm4_combine_input).
static float fm4_exposure_scale = 1.0;

// Ambient occlusion of the runtime's post_ao pass on the scene (gamma 2) at the
// pixel. The scene does not separate ambient light from sunlight, so it fades
// out on bright pixels: from an exposed luminance of 0.2 to 0.8 (middle gray is
// 0.18).
float4 fm4_ambient_occlusion(float4 scene, float2 uv) {
  if (FM4_LOOK_AO <= 0.0) {
    return scene;
  }
  float3 linear_scene = scene.rgb * scene.rgb;
  float luminance = dot(linear_scene, float3(0.2126, 0.7152, 0.0722)) * fm4_exposure_scale;
  float weight = saturate(FM4_LOOK_AO) * (1.0 - smoothstep(0.2, 0.8, luminance));
  return float4(scene.rgb * sqrt(saturate(1.0 - weight * xe_post_ao(uv))), scene.a);
}

// x for which the game's filmic curve gives y (the positive root of the
// quadratic, in the form that stays precise for either sign of its B term).
float3 fm4_filmic_inverse(float3 y, float4 ab, float4 cdef) {
  float3 q = min(y + cdef.w, 1.0 - 1.0e-6);
  float3 a = ab.y * (1.0 - q);
  float3 b = cdef.x - q * ab.z;
  float3 c = cdef.z - q * cdef.y;
  float3 root = sqrt(max(b * b - 4.0 * a * c, 0.0));
  float3 x = (b >= 0.0) ? (2.0 * c) / min(-b - root, -1.0e-20) : (root - b) / max(2.0 * a, 1.0e-20);
  return max(x, 0.0);
}

// GT tone curve (Hajime Uchimura, CEDEC 2017): a power toe, a linear middle and
// an exponential shoulder towards 1; P 1, a 1, m 0.22, l 0.4, c 1.33, b 0.
float3 fm4_curve_gt(float3 x) {
  const float m = 0.22, c = 1.33;
  const float l0 = (1.0 - m) * 0.4;
  const float s0 = m + l0, s1 = m + l0;
  const float cp = -1.0 / (1.0 - s1);
  float3 w0 = 1.0 - smoothstep(0.0, m, x);
  float3 w2 = step(s0, x);
  float3 w1 = 1.0 - w0 - w2;
  float3 toe = m * pow(max(x, 0.0) / m, c);
  float3 shoulder = 1.0 - (1.0 - s1) * exp(cp * (x - s0));
  return toe * w0 + x * w1 + shoulder * w2;
}

// ACES filmic curve fit (Krzysztof Narkowicz, 2015).
float3 fm4_curve_aces(float3 x) {
  return (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
}

// Khronos PBR Neutral: linear up to 0.76, then the brightest channel compressed
// towards 1 and the color desaturated by how much was compressed, so hues keep.
float3 fm4_curve_neutral(float3 color) {
  const float start = 0.8 - 0.04, desaturation = 0.15;
  float low = min(color.r, min(color.g, color.b));
  color -= (low < 0.08) ? low - 6.25 * low * low : 0.04;
  float peak = max(color.r, max(color.g, color.b));
  if (peak < start) {
    return color;
  }
  const float d = 1.0 - start;
  float new_peak = 1.0 - d * d / (peak + d - start);
  color *= new_peak / peak;
  float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
  return lerp(color, new_peak.xxx, g);
}

// The LUT coordinate with the selected curve instead of the game's. ab and cdef
// are the shader's filmic constants, scale and offset those of the LUT.
float3 fm4_tonemap_lut_coord(float3 coord, float4 ab, float4 cdef, float scale, float offset) {
  float to_filmic = fm4_white_filmic / (abs(scale) > 1.0e-20 ? scale : 1.0e-20);
  float3 x = fm4_filmic_inverse((coord - offset) * to_filmic, ab, cdef);
  // Each curve's input for 0.18, solved offline.
  float curve_gray = FM4_LOOK_TONEMAP == 1.0 ? 0.180942 : FM4_LOOK_TONEMAP == 2.0 ? 0.130171 : 0.22;
  float gray = fm4_filmic_inverse((0.18 * fm4_white_filmic).xxx, ab, cdef).x;
  x *= curve_gray / max(gray, 1.0e-6);
  float3 t = FM4_LOOK_TONEMAP == 1.0 ? fm4_curve_gt(x) :
             FM4_LOOK_TONEMAP == 2.0 ? fm4_curve_aces(x) : fm4_curve_neutral(x);
  return saturate(t) * scale + offset;
}

// Fetches of the final combine shaders: 0 the scene, 2 the bloom, 7 the color
// grading LUT (its coordinate is the tone curve's output). filmic_ab, filmic_cdef,
// lut_scale and lut_offset are the shader's constants for fm4_tonemap_lut_coord.
float4 fm4_combine_fetch(uint fetch, float4 value, float4 coord, float4 filmic_ab,
                         float4 filmic_cdef, float lut_scale, float lut_offset) {
#ifdef XE_TEXTURE0_2D_U
  if (fetch == 0u) {
    fm4_scene_uv = coord.xy;
    return fm4_ambient_occlusion(fm4_sharpen_scene(value, coord.xy), coord.xy);
  }
#endif
  if (fetch == 2u) {
    return fm4_bloom(value);
  }
#ifdef XE_TEXTURE7_3D_U
  if (fetch == 7u && FM4_LOOK_TONEMAP != 0.0) {
    return XE_TFETCH3D(7, fm4_tonemap_lut_coord(coord.xyz, filmic_ab, filmic_cdef, lut_scale,
                                                lut_offset));
  }
#endif
  return value;
}

// The final combine shaders' exposure: 2 to the power of interpolator 1 W.
void fm4_exposure(inout XePixel p) {
  if (FM4_LOOK_EXPOSURE != 0.0) {
    p.r[1].w = p.r[1].w + FM4_LOOK_EXPOSURE;
  }
}

// Inputs of the final combine shaders: the exposure, and F at white for the
// tone curves.
void fm4_combine_input(inout XePixel p) {
  fm4_exposure(p);
  fm4_white_filmic = p.r[1].z;
  fm4_exposure_scale = exp2(p.r[1].w);
}

// Saturation and contrast of the final image (gamma 2, 0...1).
void fm4_grade(inout XePixel p) {
  float3 color = p.color[0].rgb;
  if (FM4_LOOK_SATURATION != 1.0) {
    float3 linear_color = color * color;
    float luminance = dot(linear_color, float3(0.2126, 0.7152, 0.0722));
    color = sqrt(saturate(lerp(luminance.xxx, linear_color, max(FM4_LOOK_SATURATION, 0.0))));
  }
  if (FM4_LOOK_CONTRAST != 0.0) {
    float3 s_curve = color * color * (3.0 - 2.0 * color);
    color = lerp(color, s_curve, saturate(FM4_LOOK_CONTRAST));
  }
  if (FM4_LOOK_AO_DEBUG != 0.0) {
    color = xe_post_ao(fm4_scene_uv).xxx;
  }
  p.color[0].rgb = color;
}

// The vignette strength in the W of the vignette color constant.
float4 fm4_vignette(uint n, float4 value, uint vignette_color) {
  if (n == vignette_color && FM4_LOOK_VIGNETTE != 1.0) {
    value.w = value.w * max(FM4_LOOK_VIGNETTE, 0.0);
  }
  return value;
}
