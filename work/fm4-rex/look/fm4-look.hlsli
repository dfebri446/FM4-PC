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

// Fetches of the final combine shaders: 0 the scene, 2 the bloom.
float4 fm4_combine_fetch(uint fetch, float4 value, float4 coord) {
#ifdef XE_TEXTURE0_2D_U
  if (fetch == 0u) {
    return fm4_sharpen_scene(value, coord.xy);
  }
#endif
  if (fetch == 2u) {
    return fm4_bloom(value);
  }
  return value;
}

// The final combine shaders' exposure: 2 to the power of interpolator 1 W.
void fm4_exposure(inout XePixel p) {
  if (FM4_LOOK_EXPOSURE != 0.0) {
    p.r[1].w = p.r[1].w + FM4_LOOK_EXPOSURE;
  }
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
  p.color[0].rgb = color;
}

// The vignette strength in the W of the vignette color constant.
float4 fm4_vignette(uint n, float4 value, uint vignette_color) {
  if (n == vignette_color && FM4_LOOK_VIGNETTE != 1.0) {
    value.w = value.w * max(FM4_LOOK_VIGNETTE, 0.0);
  }
  return value;
}
