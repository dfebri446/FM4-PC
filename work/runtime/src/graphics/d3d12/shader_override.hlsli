// Shared code of HLSL shader overrides (shader_override.cpp). Embedded in
// rexgpu-xenos as "xenos_override.hlsli". The runtime compiles an override as
//   <contract of the DXBC translation it replaces: XE_* defines, XePsInput,
//    XePsOutput, XE_LOAD_INTERPOLATORS>
//   #include "xenos_override.hlsli"
//   <the override file, which defines void xe_pixel_shader(inout XePixel p)>
//   #define XE_OVERRIDE_MAIN
//   #include "xenos_override.hlsli"
// with FXC ps_5_1, IEEE strictness and unbounded descriptor tables.
//
// Everything here mirrors DxbcShaderTranslator (dxbc_translator*.cpp) for the
// bindless, host render target path - keep both in sync. Helpers return all
// four components of a fetch in texture component order; the override applies
// the destination swizzle of the instruction.

#ifndef XE_OVERRIDE_MAIN

// Constant buffers and bindless resources of the translator's root signature.

cbuffer xe_system_cbuffer : register(b0) {
  uint4 xe_system_constants[XE_SYSTEM_CONSTANT_VECTORS];
};
#if XE_FLOAT_CONSTANT_COUNT
cbuffer xe_float_cbuffer : register(b1) {
  float4 xe_float_constants[XE_FLOAT_CONSTANT_COUNT];
};
#endif
cbuffer xe_bool_loop_cbuffer : register(b2) {
  uint4 xe_bool_constants[2];
  uint4 xe_loop_constants[8];
};
cbuffer xe_fetch_cbuffer : register(b3) {
  uint4 xe_fetch_constants[48];
};
#if XE_DESCRIPTOR_INDEX_VECTORS
cbuffer xe_descriptor_indices_cbuffer : register(b4) {
  uint4 xe_descriptor_indices[XE_DESCRIPTOR_INDEX_VECTORS];
};
#endif
SamplerState xe_samplers[] : register(s0, space0);
Texture2DArray<float4> xe_textures_2d[] : register(t0, space1);
Texture3D<float4> xe_textures_3d[] : register(t0, space2);
TextureCube<float4> xe_textures_cube[] : register(t0, space3);

uint xe_system_uint(uint offset) {
  return xe_system_constants[offset >> 4u][(offset >> 2u) & 3u];
}
float xe_system_float(uint offset) { return asfloat(xe_system_uint(offset)); }
uint xe_flags() { return xe_system_uint(XE_SYS_FLAGS); }

// Guest float constant c<n> as read by this translation (XE_C(5) for c5). FXC
// doesn't expand a pasted name that begins with the macro's own name.
#define XE_C(n) XE_FLOAT_CONSTANT_##n
// c<n> through XE_HOOK_CONSTANT, as generated overrides read constants.
#define XE_CONSTANT(n) XE_HOOK_CONSTANT(n, XE_C(n))

bool xe_bool_constant(uint index) {
  return ((xe_bool_constants[index >> 7u][(index >> 5u) & 3u] >> (index & 31u)) & 1u) != 0u;
}
uint xe_loop_constant(uint index) { return xe_loop_constants[index >> 2u][index & 3u]; }

uint xe_fetch_word(uint fetch, uint word) {
  uint dword = fetch * 6u + word;
  return xe_fetch_constants[dword >> 2u][dword & 3u];
}

uint xe_descriptor_index(uint slot) {
#if XE_DESCRIPTOR_INDEX_VECTORS
  return xe_descriptor_indices[slot >> 2u][slot & 3u];
#else
  return 0u;
#endif
}

// Signed bit field extraction, as DXBC ibfe.
int xe_ibfe(uint value, uint width, uint offset) {
  return int(value << (32u - width - offset)) >> (32u - width);
}

// ---------------------------------------------------------------------------
// ALU (ProcessVectorAluOperation, ProcessScalarAluOperation).
// mul, mad and dot products: Shader Model 3 +-0 or denormal * anything = +0
// when the multiplicands differ. Where the ucode multiplies identical operands
// the translator uses a plain mul: use * and xe_mad_identical there. mad is a
// mul followed by a separate add, never a fused or DXBC mad.
// max and min: a >= b ? a : b and a < b ? a : b (Shader Model 3 NaN behavior).

#define XE_DEFINE_ALU(T)                                                       \
  T xe_mul(T a, T b) {                                                         \
    T product = a * b;                                                         \
    return (min(abs(a), abs(b)) == 0.0) ? (T)0.0 : product;                    \
  }                                                                            \
  T xe_mad(T a, T b, T c) { return xe_mul(a, b) + c; }                         \
  T xe_mad_identical(T a, T b, T c) {                                          \
    precise T product = a * b;                                                 \
    precise T result = product + c;                                            \
    return result;                                                             \
  }                                                                            \
  T xe_max(T a, T b) { return (a >= b) ? a : b; }                              \
  T xe_min(T a, T b) { return (a < b) ? a : b; }                               \
  T xe_seq(T a, T b) { return (a == b) ? (T)1.0 : (T)0.0; }                    \
  T xe_sgt(T a, T b) { return (b < a) ? (T)1.0 : (T)0.0; }                     \
  T xe_sge(T a, T b) { return (a >= b) ? (T)1.0 : (T)0.0; }                    \
  T xe_sne(T a, T b) { return (a != b) ? (T)1.0 : (T)0.0; }                    \
  T xe_cndeq(T a, T b, T c) { return (a == 0.0) ? b : c; }                     \
  T xe_cndge(T a, T b, T c) { return (a >= 0.0) ? b : c; }                     \
  T xe_cndgt(T a, T b, T c) { return (0.0 < a) ? b : c; }
XE_DEFINE_ALU(float)
XE_DEFINE_ALU(float2)
XE_DEFINE_ALU(float3)
XE_DEFINE_ALU(float4)
#undef XE_DEFINE_ALU

float xe_dp2add(float2 a, float2 b, float c) {
  float result = xe_mul(a.x, b.x);
  result = result + xe_mul(a.y, b.y);
  return result + c;
}
float xe_dp3(float3 a, float3 b) {
  float result = xe_mul(a.x, b.x);
  result = result + xe_mul(a.y, b.y);
  return result + xe_mul(a.z, b.z);
}
float xe_dp4(float4 a, float4 b) {
  float result = xe_mul(a.x, b.x);
  result = result + xe_mul(a.y, b.y);
  result = result + xe_mul(a.z, b.z);
  return result + xe_mul(a.w, b.w);
}

// Scalar operations without a plain HLSL equivalent (exp is exp2, log is log2,
// rcp, rsq is rsqrt, sqrt, frc is frac, floor, trunc, sin and cos are direct).
float xe_logc(float x) {
  float result = log2(x);
  return (result == -1.#INF) ? -3.402823466e+38 : result;
}
// Infinity becomes +-FLT_MAX.
float xe_rcpc(float x) {
  float result = rcp(x);
  return (abs(result) == 1.#INF) ? asfloat(asuint(result) - 1u) : result;
}
float xe_rsqc(float x) {
  float result = rsqrt(x);
  return (abs(result) == 1.#INF) ? asfloat(asuint(result) - 1u) : result;
}
// Infinity becomes +-0.
float xe_rcpf(float x) {
  float result = rcp(x);
  return (abs(result) != 1.#INF) ? result : asfloat(asuint(result) & 0x80000000u);
}
float xe_rsqf(float x) {
  float result = rsqrt(x);
  return (abs(result) != 1.#INF) ? result : asfloat(asuint(result) & 0x80000000u);
}

// cube: T coordinate, S coordinate, 2 * major axis, face ID, from the operand
// loaded as .zzxy (x = source.z, y = source.w, z = source.x).
float4 xe_cube(float4 source) {
  float x = source.z, y = source.w, z = source.x;
  if (abs(z) >= abs(x) && abs(z) >= abs(y)) {
    bool negative = z < 0.0;
    return float4(-y, negative ? -x : x, 2.0 * z, negative ? 5.0 : 4.0);
  }
  if (abs(y) >= abs(x)) {
    bool negative = y < 0.0;
    return float4(negative ? -z : z, x, 2.0 * y, negative ? 3.0 : 2.0);
  }
  bool negative = x < 0.0;
  return float4(-y, negative ? z : -z, 2.0 * x, negative ? 1.0 : 0.0);
}

// ---------------------------------------------------------------------------
// Piecewise linear gamma (PWLGammaToLinear, PreSaturatedLinearToPWLGamma).

float xe_pwl_gamma_to_linear(float gamma) {
  float scale, offset;
  if (gamma >= 96.0 / 255.0) {
    bool high = gamma >= 192.0 / 255.0;
    scale = high ? 8.0 / 1024.0 : 4.0 / 1024.0;
    offset = high ? -1024.0 : -256.0;
  } else {
    bool high = gamma >= 64.0 / 255.0;
    scale = high ? 2.0 / 1024.0 : 1.0 / 1024.0;
    offset = high ? -64.0 : 0.0;
  }
  float linear_value = saturate(gamma) * (255.0 * 1024.0);
  linear_value = mad(linear_value, scale, offset);
  linear_value = linear_value + trunc(linear_value * scale);
  return linear_value * (1.0 / 1023.0);
}

float xe_pre_saturated_linear_to_pwl_gamma(float linear_value) {
  float scale, offset;
  if (linear_value >= 128.0 / 1023.0) {
    bool high = linear_value >= 512.0 / 1023.0;
    scale = high ? 1023.0 / 8.0 : 1023.0 / 4.0;
    offset = high ? 128.0 / 255.0 : 64.0 / 255.0;
  } else {
    bool high = linear_value >= 64.0 / 1023.0;
    scale = high ? 1023.0 / 2.0 : 1023.0;
    offset = high ? 32.0 / 255.0 : 0.0;
  }
  return mad(trunc(linear_value * scale), 1.0 / 255.0, offset);
}

// ---------------------------------------------------------------------------
// Texture fetch (ProcessTextureFetchInstruction, kTextureFetch).
//
// The slots are the bindless descriptor index slots of the translation:
// XE_TEXTURE<fetch>_<2D|3D|CUBE>_<U|S> and
// XE_SAMPLER<fetch>_<mag><min><mip>[_A<aniso>] with P point, L linear,
// B basemap, F fetch constant. XE_TFETCH2D/3D/CUBE(fetch, coord) are the
// instruction defaults (filters from the fetch constant, computed LOD in pixel
// shaders, normalized coordinates, no offsets, no LOD bias).

#define XE_LOD_COMPUTED 1u  // UseComputedLOD (gradients from the coordinates).
#define XE_LOD_REGISTER 2u  // UseRegisterLOD (the setTexLOD value is added).
#define XE_LOD_BASEMAP 4u   // MipFilter=basemap (LOD 0).

#define XE_FILTER_POINT 0u
#define XE_FILTER_LINEAR 1u
#define XE_FILTER_FETCH_CONSTANT 3u

#define XE_SIGN_SIGNED 1u
#define XE_SIGN_UNSIGNED_BIASED 2u
#define XE_SIGN_GAMMA 3u

// Rounding offset added to the instruction offsets (1.5/4 of a fixed-point
// texture coordinate ULP).
#define XE_TEXTURE_ROUNDING_OFFSET (1.5 / 1024.0)

uint4 xe_texture_signs(uint fetch) {
  uint signs = xe_system_uint(XE_SYS_TEXTURE_SWIZZLED_SIGNS + 4u * (fetch >> 2u));
  return (signs >> ((fetch & 3u) * 8u + uint4(0u, 2u, 4u, 6u))) & 3u;
}

bool xe_texture_resolution_scaled(uint fetch) {
  return (xe_system_uint(XE_SYS_TEXTURES_RESOLUTION_SCALED) & (1u << fetch)) != 0u;
}

// LOD sources in D3D11.3 order: fetch constant bias + register LOD + instruction
// bias.
float xe_texture_lod(uint fetch, uint lod_flags, float lod_bias, float register_lod) {
  float lod = float(xe_ibfe(xe_fetch_word(fetch, 4u), 10u, 12u));
  if (lod_flags & XE_LOD_REGISTER) {
    lod = mad(lod, 1.0 / 32.0, register_lod);
    if (lod_bias != 0.0) {
      lod = lod + lod_bias;
    }
  } else if (lod_bias != 0.0) {
    lod = mad(lod, 1.0 / 32.0, lod_bias);
  } else {
    lod = lod * (1.0 / 32.0);
  }
  return lod;
}

// Signedness, integer num_format scale and the exponent bias, after sampling.
float4 xe_texture_finish(uint fetch, float4 value, uint4 signs) {
  [unroll] for (uint i = 0u; i < 4u; ++i) {
    if (signs[i] == XE_SIGN_UNSIGNED_BIASED) {
      value[i] = mad(value[i], 2.0, -1.0);
    } else if (signs[i] == XE_SIGN_GAMMA) {
      value[i] = xe_pwl_gamma_to_linear(value[i]);
    }
  }
  uint integer_scale_bits = xe_system_uint(XE_SYS_TEXTURE_INTEGER_SCALE_BITS + 4u * fetch);
  if (integer_scale_bits != 0u) {
    uint4 bits = (integer_scale_bits >> uint4(0u, 5u, 10u, 15u)) & 31u;
    uint4 scale = (bits & 0xFu) + 1u;
    scale = scale - (bits >> 4u);
    value *= float4((1u << scale) - 1u);
  }
  int exp_adjust = xe_ibfe(xe_fetch_word(fetch, 3u), 6u, 13u);
  return value * asfloat(exp_adjust * (1 << 23) + asint(1.0));
}

// XY coordinates normalized and offset, for 2D, 3D-or-stacked and cube.
float2 xe_texture_xy(uint fetch, float2 coord, float2 offsets, float2 size, bool unnormalized) {
  float2 scaled_offsets = offsets;
  if (xe_texture_resolution_scaled(fetch)) {
    if (XE_REVERT_RESOLUTION_SCALE_AXES & 1u) {
      scaled_offsets.x = offsets.x / float(XE_RESOLUTION_SCALE_X);
    }
    if (XE_REVERT_RESOLUTION_SCALE_AXES & 2u) {
      scaled_offsets.y = offsets.y / float(XE_RESOLUTION_SCALE_Y);
    }
  }
  if (unnormalized) {
    return (coord + scaled_offsets) / size;
  }
  float2 normalized_offsets = offsets / size;
  float2 result = coord + normalized_offsets;
  if (xe_texture_resolution_scaled(fetch)) {
    if (XE_REVERT_RESOLUTION_SCALE_AXES & 1u) {
      result.x = mad(normalized_offsets.x, 1.0 / float(XE_RESOLUTION_SCALE_X), coord.x);
    }
    if (XE_REVERT_RESOLUTION_SCALE_AXES & 2u) {
      result.y = mad(normalized_offsets.y, 1.0 / float(XE_RESOLUTION_SCALE_Y), coord.y);
    }
  }
  return result;
}

// Gradients for a computed LOD, scaled by 2^lod: from the coordinates
// (ddx/ddy_coarse), or from setTexGradientsH/V (XE_LOD_REGISTER_GRADIENTS),
// which the translator normalizes like unnormalized coordinates.
#define XE_LOD_REGISTER_GRADIENTS 8u

void xe_texture_gradients(uint lod_flags, float lod, float3 location, float3 size,
                          bool unnormalized, uint normalized_mask, float3 register_gradient_h,
                          float3 register_gradient_v, out float3 gradient_x,
                          out float3 gradient_y) {
  float gradient_scale = exp2(lod);
  if (lod_flags & XE_LOD_REGISTER_GRADIENTS) {
    gradient_x = register_gradient_h * gradient_scale;
    gradient_y = register_gradient_v * gradient_scale;
    if (unnormalized) {
      [unroll] for (uint i = 0u; i < 3u; ++i) {
        if (normalized_mask & (1u << i)) {
          gradient_x[i] = gradient_x[i] / size[i];
          gradient_y[i] = gradient_y[i] / size[i];
        }
      }
    }
  } else {
    gradient_x = ddx_coarse(location) * gradient_scale;
    gradient_y = ddy_coarse(location) * gradient_scale;
  }
}

// 1D and 2D textures are fetched from 2D arrays (layer 0).
float4 xe_tfetch_2d_array(uint fetch, uint texture_unsigned, uint texture_signed,
                          uint sampler_slot, float3 location, float3 size, bool unnormalized,
                          uint normalized_mask, uint gradient_mask, uint lod_flags,
                          float lod_bias, float register_lod, float3 register_gradient_h,
                          float3 register_gradient_v) {
  uint4 signs = xe_texture_signs(fetch);
  bool4 is_signed = signs == XE_SIGN_SIGNED;
  SamplerState texture_sampler = xe_samplers[xe_descriptor_index(sampler_slot)];
  float4 value = 0.0;
  if (lod_flags & XE_LOD_BASEMAP) {
    if (!all(is_signed)) {
      value = xe_textures_2d[xe_descriptor_index(texture_unsigned)].SampleLevel(
          texture_sampler, location, 0.0);
    }
    if (any(is_signed)) {
      float4 signed_value = xe_textures_2d[xe_descriptor_index(texture_signed)].SampleLevel(
          texture_sampler, location, 0.0);
      value = is_signed ? signed_value : value;
    }
  } else {
    float lod = xe_texture_lod(fetch, lod_flags, lod_bias, register_lod);
    if (lod_flags & XE_LOD_COMPUTED) {
      float3 gradient_x, gradient_y;
      xe_texture_gradients(lod_flags, lod, location, size, unnormalized, normalized_mask,
                           register_gradient_h, register_gradient_v, gradient_x, gradient_y);
      // 1D: the gradients are padded to 2D with 0.
      float2 gradient_x_2d = (gradient_mask & 2u) ? gradient_x.xy : float2(gradient_x.x, 0.0);
      float2 gradient_y_2d = (gradient_mask & 2u) ? gradient_y.xy : float2(gradient_y.x, 0.0);
      if (!all(is_signed)) {
        value = xe_textures_2d[xe_descriptor_index(texture_unsigned)].SampleGrad(
            texture_sampler, location, gradient_x_2d, gradient_y_2d);
      }
      if (any(is_signed)) {
        float4 signed_value = xe_textures_2d[xe_descriptor_index(texture_signed)].SampleGrad(
            texture_sampler, location, gradient_x_2d, gradient_y_2d);
        value = is_signed ? signed_value : value;
      }
    } else {
      if (!all(is_signed)) {
        value = xe_textures_2d[xe_descriptor_index(texture_unsigned)].SampleLevel(
            texture_sampler, location, lod);
      }
      if (any(is_signed)) {
        float4 signed_value = xe_textures_2d[xe_descriptor_index(texture_signed)].SampleLevel(
            texture_sampler, location, lod);
        value = is_signed ? signed_value : value;
      }
    }
  }
  return xe_texture_finish(fetch, value, signs);
}

float4 xe_tfetch_1d(uint fetch, uint texture_unsigned, uint texture_signed, uint sampler_slot,
                    float coord, float offset, bool unnormalized, uint lod_flags, float lod_bias,
                    float register_lod, float3 register_gradient_h = 0.0,
                    float3 register_gradient_v = 0.0) {
  float width = float((xe_fetch_word(fetch, 2u) & 0xFFFFFFu) + 1u);
  float x = xe_texture_xy(fetch, float2(coord, 0.0), float2(offset + XE_TEXTURE_ROUNDING_OFFSET,
                          0.0), float2(width, 1.0), unnormalized).x;
  return xe_tfetch_2d_array(fetch, texture_unsigned, texture_signed, sampler_slot,
                            float3(x, 0.0, 0.0), float3(width, 1.0, 1.0), unnormalized, 1u, 1u,
                            lod_flags, lod_bias, register_lod, register_gradient_h,
                            register_gradient_v);
}

float4 xe_tfetch_2d(uint fetch, uint texture_unsigned, uint texture_signed, uint sampler_slot,
                    float2 coord, float2 offset, bool unnormalized, uint lod_flags,
                    float lod_bias, float register_lod, float3 register_gradient_h = 0.0,
                    float3 register_gradient_v = 0.0) {
  uint size_word = xe_fetch_word(fetch, 2u);
  float2 size = float2((uint2(size_word, size_word >> 13u) & 0x1FFFu) + 1u);
  float3 location = float3(
      xe_texture_xy(fetch, coord, offset + XE_TEXTURE_ROUNDING_OFFSET, size, unnormalized), 0.0);
  return xe_tfetch_2d_array(fetch, texture_unsigned, texture_signed, sampler_slot, location,
                            float3(size, 1.0), unnormalized, 3u, 3u, lod_flags, lod_bias,
                            register_lod, register_gradient_h, register_gradient_v);
}

// tfetch3D samples a 3D texture, or a stack of 2D layers (2D array) with the
// layer interpolation of the volume filters.
float4 xe_tfetch_3d(uint fetch, uint texture_unsigned, uint texture_signed,
                    uint stacked_unsigned, uint stacked_signed, uint sampler_slot, float3 coord,
                    float3 offset, bool unnormalized, uint lod_flags, float lod_bias,
                    float register_lod, uint vol_mag_filter, uint vol_min_filter,
                    float3 register_gradient_h = 0.0, float3 register_gradient_v = 0.0) {
  uint size_word = xe_fetch_word(fetch, 2u);
  bool is_3d = ((xe_fetch_word(fetch, 5u) >> 9u) & 3u) == 2u;
  float3 size = is_3d ? float3((uint3(size_word, size_word >> 11u, size_word >> 22u) &
                                uint3(0x7FFu, 0x7FFu, 0x3FFu)) + 1u)
                      : float3((uint3(size_word, size_word >> 13u, size_word >> 26u) &
                                uint3(0x1FFFu, 0x1FFFu, 0x3Fu)) + 1u);
  float3 offsets = offset + XE_TEXTURE_ROUNDING_OFFSET;
  float3 location;
  location.xy = xe_texture_xy(fetch, coord.xy, offsets.xy, size.xy, unnormalized);
  if (unnormalized) {
    location.z = coord.z + offsets.z;
    if (is_3d) {
      location.z = location.z / size.z;
    }
  } else {
    location.z = coord.z + offsets.z / size.z;
    if (!is_3d) {
      location.z = mad(coord.z, size.z, offsets.z);
    }
  }
  uint4 signs = xe_texture_signs(fetch);
  bool4 is_signed = signs == XE_SIGN_SIGNED;
  SamplerState texture_sampler = xe_samplers[xe_descriptor_index(sampler_slot)];

  bool basemap = (lod_flags & XE_LOD_BASEMAP) != 0u;
  bool computed = !basemap && (lod_flags & XE_LOD_COMPUTED) != 0u;
  float lod = basemap ? 0.0 : xe_texture_lod(fetch, lod_flags, lod_bias, register_lod);
  float3 gradient_x = 0.0, gradient_y = 0.0;
  if (computed) {
    // Unnormalized register gradients: XY normalized always, Z only for 3D.
    xe_texture_gradients(lod_flags, lod, location, size, unnormalized, is_3d ? 7u : 3u,
                         register_gradient_h, register_gradient_v, gradient_x, gradient_y);
  }

  // Interpolation between stacked layers.
  float layer_lerp = 0.0;
  bool layer_lerp_possible = false;
  uint filters_word = xe_fetch_word(fetch, 4u);
  bool mag_from_fetch = vol_mag_filter == XE_FILTER_FETCH_CONSTANT;
  bool min_from_fetch = vol_min_filter == XE_FILTER_FETCH_CONSTANT;
  if (computed && (mag_from_fetch || min_from_fetch ||
                   (vol_mag_filter == XE_FILTER_LINEAR) != (vol_min_filter == XE_FILTER_LINEAR))) {
    layer_lerp_possible = true;
    if (!is_3d) {
      float layer_gradient = max(gradient_x.z, gradient_y.z);
      if (!unnormalized) {
        layer_gradient *= size.z;
      }
      bool minifying = 1.0 < layer_gradient;
      bool linear_filter;
      if (minifying) {
        linear_filter = min_from_fetch ? ((filters_word >> 1u) & 1u) != 0u
                                       : vol_min_filter == XE_FILTER_LINEAR;
      } else {
        linear_filter = mag_from_fetch ? (filters_word & 1u) != 0u
                                       : vol_mag_filter == XE_FILTER_LINEAR;
      }
      if (linear_filter) {
        location.z = location.z + -0.5;
        layer_lerp = frac(location.z);
      }
    }
  } else if (mag_from_fetch || vol_mag_filter == XE_FILTER_LINEAR) {
    layer_lerp_possible = true;
    if (!is_3d && (!mag_from_fetch || (filters_word & 1u) != 0u)) {
      location.z = location.z + -0.5;
      layer_lerp = frac(location.z);
    }
  }

  float4 value = 0.0;
  if (is_3d) {
    if (!all(is_signed)) {
      Texture3D<float4> t = xe_textures_3d[xe_descriptor_index(texture_unsigned)];
      value = computed ? t.SampleGrad(texture_sampler, location, gradient_x, gradient_y)
                       : t.SampleLevel(texture_sampler, location, lod);
    }
    if (any(is_signed)) {
      Texture3D<float4> t = xe_textures_3d[xe_descriptor_index(texture_signed)];
      float4 signed_value = computed
                                ? t.SampleGrad(texture_sampler, location, gradient_x, gradient_y)
                                : t.SampleLevel(texture_sampler, location, lod);
      value = is_signed ? signed_value : value;
    }
  } else {
    location.z = floor(location.z);
    [unroll] for (uint layer = 0u; layer < 2u; ++layer) {
      if (layer && (!layer_lerp_possible || !(layer_lerp != 0.0))) {
        break;
      }
      float3 layer_location = float3(location.xy, location.z + float(layer));
      float4 layer_value = 0.0;
      if (!all(is_signed)) {
        Texture2DArray<float4> t = xe_textures_2d[xe_descriptor_index(stacked_unsigned)];
        layer_value = computed ? t.SampleGrad(texture_sampler, layer_location, gradient_x.xy,
                                              gradient_y.xy)
                               : t.SampleLevel(texture_sampler, layer_location, lod);
      }
      if (any(is_signed)) {
        Texture2DArray<float4> t = xe_textures_2d[xe_descriptor_index(stacked_signed)];
        float4 signed_value = computed ? t.SampleGrad(texture_sampler, layer_location,
                                                      gradient_x.xy, gradient_y.xy)
                                       : t.SampleLevel(texture_sampler, layer_location, lod);
        layer_value = is_signed ? signed_value : layer_value;
      }
      if (layer) {
        value = mad(layer_value + -value, layer_lerp, value);
      } else {
        value = layer_value;
      }
    }
  }
  return xe_texture_finish(fetch, value, signs);
}

// coord: SC + 1, TC + 1 and the face index, as written by the cube instruction.
float4 xe_tfetch_cube(uint fetch, uint texture_unsigned, uint texture_signed, uint sampler_slot,
                      float3 coord, float3 offset, bool unnormalized, uint lod_flags,
                      float lod_bias, float register_lod, float3 register_gradient_h = 0.0,
                      float3 register_gradient_v = 0.0) {
  uint size_word = xe_fetch_word(fetch, 2u);
  float2 size = float2((uint2(size_word, size_word >> 13u) & 0x1FFFu) + 1u);
  float3 location;
  location.xy = xe_texture_xy(fetch, coord.xy, offset.xy + XE_TEXTURE_ROUNDING_OFFSET, size,
                              unnormalized);
  location.xy = mad(location.xy, 2.0, -3.0);
  uint face = min(uint(offset.z != 0.0 ? coord.z + offset.z : coord.z), 5u);
  bool negative = (face & 1u) != 0u;
  float sc = location.x, tc = location.y;
  switch (face >> 1u) {
    case 0u:
      location = float3(negative ? -1.0 : 1.0, -tc, negative ? sc : -sc);
      break;
    case 1u:
      location = float3(sc, negative ? -1.0 : 1.0, negative ? -tc : tc);
      break;
    default:
      location = float3(negative ? -sc : sc, -tc, negative ? -1.0 : 1.0);
      break;
  }
  uint4 signs = xe_texture_signs(fetch);
  bool4 is_signed = signs == XE_SIGN_SIGNED;
  SamplerState texture_sampler = xe_samplers[xe_descriptor_index(sampler_slot)];
  bool basemap = (lod_flags & XE_LOD_BASEMAP) != 0u;
  bool computed = !basemap && (lod_flags & XE_LOD_COMPUTED) != 0u;
  float lod = basemap ? 0.0 : xe_texture_lod(fetch, lod_flags, lod_bias, register_lod);
  float3 gradient_x = 0.0, gradient_y = 0.0;
  if (computed) {
    // Cube register gradients are already in the cube space.
    xe_texture_gradients(lod_flags, lod, location, 1.0, false, 0u, register_gradient_h,
                         register_gradient_v, gradient_x, gradient_y);
  }
  float4 value = 0.0;
  if (!all(is_signed)) {
    TextureCube<float4> t = xe_textures_cube[xe_descriptor_index(texture_unsigned)];
    value = computed ? t.SampleGrad(texture_sampler, location, gradient_x, gradient_y)
                     : t.SampleLevel(texture_sampler, location, lod);
  }
  if (any(is_signed)) {
    TextureCube<float4> t = xe_textures_cube[xe_descriptor_index(texture_signed)];
    float4 signed_value = computed
                              ? t.SampleGrad(texture_sampler, location, gradient_x, gradient_y)
                              : t.SampleLevel(texture_sampler, location, lod);
    value = is_signed ? signed_value : value;
  }
  return xe_texture_finish(fetch, value, signs);
}

// Size of a 2D texture from its fetch constant, for mods sampling neighbors.
float2 xe_texture_size_2d(uint fetch) {
  uint size_word = xe_fetch_word(fetch, 2u);
  return float2((uint2(size_word, size_word >> 13u) & 0x1FFFu) + 1u);
}

// Ambient occlusion of the frame's scene from the runtime's post_ao pass, 0
// (none) to 1, at a texture coordinate of the guest scene (the frame's resolved
// depth texture spans 0...1). 0 without post_ao, before it runs in a frame and
// outside the scene, so mods may apply it unconditionally.
float xe_post_ao(float2 uv) {
#ifdef XE_POST_AO_VIEW
  uint3 size;
  xe_textures_2d[XE_POST_AO_VIEW].GetDimensions(size.x, size.y, size.z);
  int2 texel = clamp(int2(uv * float2(size.xy)), int2(0, 0), int2(size.xy) - 1);
  return xe_textures_2d[XE_POST_AO_VIEW].Load(int4(texel, 0, 0)).x;
#else
  return 0.0;
#endif
}

#if XE_PIXEL_SHADER
#define XE_LOD_DEFAULT XE_LOD_COMPUTED
#define XE_SAMPLER_DEFAULT(fetch) XE_SAMPLER##fetch##_FFF
#else
#define XE_LOD_DEFAULT 0u
#define XE_SAMPLER_DEFAULT(fetch) XE_SAMPLER##fetch##_FFF_A0
#endif
#define XE_TFETCH2D(fetch, coord)                                                         \
  xe_tfetch_2d(fetch, XE_TEXTURE##fetch##_2D_U, XE_TEXTURE##fetch##_2D_S,                 \
               XE_SAMPLER_DEFAULT(fetch), coord, 0.0, false, XE_LOD_DEFAULT, 0.0, 0.0)
#define XE_TFETCH3D(fetch, coord)                                                         \
  xe_tfetch_3d(fetch, XE_TEXTURE##fetch##_3D_U, XE_TEXTURE##fetch##_3D_S,                 \
               XE_TEXTURE##fetch##_2D_U, XE_TEXTURE##fetch##_2D_S, XE_SAMPLER_DEFAULT(fetch), \
               coord, 0.0, false, XE_LOD_DEFAULT, 0.0, 0.0, XE_FILTER_FETCH_CONSTANT,     \
               XE_FILTER_FETCH_CONSTANT)
#define XE_TFETCHCUBE(fetch, coord)                                                       \
  xe_tfetch_cube(fetch, XE_TEXTURE##fetch##_CUBE_U, XE_TEXTURE##fetch##_CUBE_S,           \
                 XE_SAMPLER_DEFAULT(fetch), coord, 0.0, false, XE_LOD_DEFAULT, 0.0, 0.0)

// ---------------------------------------------------------------------------
// Pixel shader state (StartPixelShader, CompletePixelShader for host render
// targets).

#if XE_PIXEL_SHADER

struct XePixel {
  // r# with the interpolators (and PsParamGen) loaded, the rest zero.
  float4 r[XE_REGISTER_COUNT];
  // oC0..oC3 (only the targets written by the guest shader are output).
  float4 color[4];
  // oDepth, guest 0...1.
  float depth;
};

XePixel xe_ps_prologue(XePsInput input) {
  XePixel p;
  [unroll] for (uint i = 0u; i < XE_REGISTER_COUNT; ++i) {
    p.r[i] = 0.0;
  }
  XE_LOAD_INTERPOLATORS(p, input)
#if XE_PARAM_GEN_ENABLED
  {
    // X - pixel X .0 in the magnitude, back-facing in the sign bit.
    // Y - pixel Y .0 in the magnitude, point in the sign bit.
    float4 param_gen;
    param_gen.xy = floor(input.xe_position.xy);
    if (XE_RESOLUTION_SCALE_X > 1) {
      param_gen.x *= 1.0 / float(XE_RESOLUTION_SCALE_X);
    }
    if (XE_RESOLUTION_SCALE_Y > 1) {
      param_gen.y *= 1.0 / float(XE_RESOLUTION_SCALE_Y);
    }
#if XE_PARAM_GEN_POINT
    param_gen.x = abs(param_gen.x);
    param_gen.y = -abs(param_gen.y);
    param_gen.zw = saturate(input.xe_point_coordinates);
#else
    param_gen.xy = abs(param_gen.xy);
    if (xe_flags() & XE_SYSFLAG_PRIMITIVE_POLYGONAL) {
      param_gen.x = input.xe_is_front_face ? param_gen.x : -param_gen.x;
    }
    param_gen.z = asfloat(((xe_flags() >> XE_SYSFLAG_PRIMITIVE_LINE_SHIFT) & 1u) << 31u);
    param_gen.w = 0.0;
#endif
    p.r[XE_PARAM_GEN_INTERPOLATOR] = param_gen;
  }
#endif
  p.color[0] = 0.0;
  p.color[1] = 0.0;
  p.color[2] = 0.0;
  p.color[3] = 0.0;
  p.depth = 0.0;
  return p;
}

// Alpha to coverage for one sample (CompletePixelShader_AlphaToMaskSample).
uint xe_alpha_to_mask_sample(float alpha, uint sample_index, float threshold_base,
                             float threshold_offset, float threshold_offset_scale) {
  float threshold = mad(threshold_offset, -threshold_offset_scale, threshold_base);
  return (alpha >= threshold) ? (1u << sample_index) : 0u;
}

XePsOutput xe_ps_epilogue(XePixel p, XePsInput input) {
  XePsOutput output;
  uint flags = xe_flags();
#if (XE_COLOR_TARGETS_WRITTEN & 1) && !XE_FORCE_EARLY_DEPTH_STENCIL
  // Alpha test.
  uint alpha_test = (flags >> XE_SYSFLAG_ALPHA_PASS_IF_LESS_SHIFT) & 7u;
  if (alpha_test != 7u) {
    float alpha = p.color[0].w;
    float reference = xe_system_float(XE_SYS_ALPHA_TEST_REFERENCE);
    bool passed;
    if (alpha_test == 5u) {
#if XE_FUZZY_ALPHA_EPSILON
      passed = !(abs(alpha + -reference) < 1e-3);
#else
      passed = alpha != reference;
#endif
    } else {
#if XE_FUZZY_ALPHA_EPSILON
      bool less = (alpha + -1e-3) < reference;
      bool equal = abs(alpha + -reference) < 1e-3;
      bool greater = reference < (alpha + 1e-3);
#else
      bool less = alpha < reference;
      bool equal = alpha == reference;
      bool greater = reference < alpha;
#endif
      passed = (less && (alpha_test & 1u)) || (equal && (alpha_test & 2u)) ||
               (greater && (alpha_test & 4u));
    }
    if (!passed) {
      discard;
    }
  }
  // Alpha to coverage.
  output.xe_coverage = 0xFFFFFFFFu;
  uint alpha_to_mask = xe_system_uint(XE_SYS_ALPHA_TO_MASK);
  if (alpha_to_mask != 0u) {
    uint2 pixel = uint2(input.xe_position.xy);
    uint offset_index = ((pixel.y & 1u) | ((pixel.x & 1u) << 1u)) << 1u;
    float offset = float((alpha_to_mask >> offset_index) & 3u);
    float alpha = p.color[0].w;
    uint coverage;
    if (xe_system_uint(XE_SYS_SAMPLE_COUNT_LOG2 + 4u) != 0u) {
      if (xe_system_uint(XE_SYS_SAMPLE_COUNT_LOG2) != 0u) {
        coverage = xe_alpha_to_mask_sample(alpha, 0u, 0.75, offset, 1.0 / 16.0) |
                   xe_alpha_to_mask_sample(alpha, 1u, 0.25, offset, 1.0 / 16.0) |
                   xe_alpha_to_mask_sample(alpha, 2u, 0.5, offset, 1.0 / 16.0) |
                   xe_alpha_to_mask_sample(alpha, 3u, 1.0, offset, 1.0 / 16.0);
      } else {
        coverage = xe_alpha_to_mask_sample(alpha, XE_MSAA_2X_SUPPORTED ? 1u : 0u, 0.5, offset,
                                           1.0 / 8.0) |
                   xe_alpha_to_mask_sample(alpha, XE_MSAA_2X_SUPPORTED ? 0u : 3u, 1.0, offset,
                                           1.0 / 8.0);
      }
    } else {
      coverage = xe_alpha_to_mask_sample(alpha, 0u, 1.0, offset, 1.0 / 4.0);
    }
    if (coverage == 0u) {
      discard;
    }
    output.xe_coverage = coverage;
  }
#endif
  // Exponent bias, then gamma for k_8_8_8_8_GAMMA stored as 8-bit.
#define XE_WRITE_COLOR(i)                                                               \
  {                                                                                     \
    float4 color = p.color[i] * xe_system_float(XE_SYS_COLOR_EXP_BIAS + 4u * i);        \
    if (XE_GAMMA_RENDER_TARGET_AS_UNORM8 &&                                             \
        (flags & (1u << (XE_SYSFLAG_CONVERT_COLOR0_TO_GAMMA_SHIFT + i)))) {             \
      color.rgb = saturate(color.rgb);                                                  \
      color.r = xe_pre_saturated_linear_to_pwl_gamma(color.r);                          \
      color.g = xe_pre_saturated_linear_to_pwl_gamma(color.g);                          \
      color.b = xe_pre_saturated_linear_to_pwl_gamma(color.b);                          \
    }                                                                                   \
    output.xe_color##i = color;                                                         \
  }
#if XE_COLOR_TARGETS_WRITTEN & 1
  XE_WRITE_COLOR(0)
#endif
#if XE_COLOR_TARGETS_WRITTEN & 2
  XE_WRITE_COLOR(1)
#endif
#if XE_COLOR_TARGETS_WRITTEN & 4
  XE_WRITE_COLOR(2)
#endif
#if XE_COLOR_TARGETS_WRITTEN & 8
  XE_WRITE_COLOR(3)
#endif
#undef XE_WRITE_COLOR
#if XE_WRITES_DEPTH
  // Saturated like any oDepth write, then guest 0...1 to host 0...0.5 for
  // float24 (viewport scaling doesn't apply to oDepth).
  float depth = saturate(p.depth);
  output.xe_depth = (flags & XE_SYSFLAG_DEPTH_FLOAT24) ? depth * 0.5 : depth;
#endif
  return output;
}

#endif  // XE_PIXEL_SHADER

// Hooks of shader mods (shader_mod_path), included after this part: they #undef
// and redefine these. The defaults change nothing, so generated overrides stay
// exact without a mod.
//   XE_HOOK_TFETCH(fetch, value, coord): the result of each texture fetch of a
//     generated override, with its fetch constant and coordinate operand.
//   XE_HOOK_CONSTANT(n, value): float constant c<n> as XE_CONSTANT(n) reads it.
//   XE_HOOK_INPUT(p): the registers after the prologue.
//   XE_HOOK_OUTPUT(p, input): the outputs before the epilogue (input: the
//     registers after the prologue and XE_HOOK_INPUT).
#define XE_HOOK_TFETCH(fetch, value, coord) (value)
#define XE_HOOK_CONSTANT(n, value) (value)
#define XE_HOOK_INPUT(p)
#define XE_HOOK_OUTPUT(p, input)

#else  // XE_OVERRIDE_MAIN

#if XE_PIXEL_SHADER
XE_PS_ATTRIBUTES
XePsOutput main(XePsInput xe_input) {
  XePixel p = xe_ps_prologue(xe_input);
  XE_HOOK_INPUT(p);
  XePixel xe_input_pixel = p;
  xe_pixel_shader(p);
  XE_HOOK_OUTPUT(p, xe_input_pixel);
  return xe_ps_epilogue(p, xe_input);
}
#endif

#endif  // XE_OVERRIDE_MAIN
