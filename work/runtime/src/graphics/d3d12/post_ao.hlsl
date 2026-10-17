// post_ao: screen-space ambient occlusion (GTAO) of the scene's resolved depth,
// computed between the scene and the post-processing of the guest frame (see
// post_ao.cpp). Original code of this project, after "Practical Real-Time
// Strategies for Accurate Indirect Occlusion" (Jimenez, Wu, Pesce, Jarabo,
// SIGGRAPH 2016): per pixel and slice through the view vector (directions
// evenly around it, sampled along their projection onto the screen), the
// horizon angles on both sides within a view-space radius, and the
// cosine-weighted visibility between them integrated analytically against the
// normal projected into the slice.
//
// Compiled by the runtime with FXC cs_5_1, one entry point at a time:
//   post_ao_prefilter  stored depth -> 1 / view W (clip W), 5 mips; 1 / W is
//                      linear in screen space on a plane, so bilinear samples
//                      of it stay on the surface
//   post_ao_main       1 / W mips -> visibility (R8) and edges (R8, 2 bits per
//                      side)
//   post_ao_denoise    edge-aware 3x3 blur of the visibility; with
//                      POST_AO_FINAL, writes the occlusion (1 - visibility)
//   post_ao_clear      occlusion 0 (no AO)
// POST_AO_SLICES and POST_AO_STEPS set the quality of post_ao_main.

#ifndef POST_AO_SLICES
#define POST_AO_SLICES 3
#endif
#ifndef POST_AO_STEPS
#define POST_AO_STEPS 6
#endif

// Root constants; the layout of PostAo::Constants (no implicit padding).
cbuffer post_ao_constants : register(b0) {
  // View position from W and the guest screen position: z = w * z_from_w,
  // x = w * (x * x_mul + x_add), y = w * (y * y_mul + y_add) - x_mul, x_add,
  // y_mul, y_add.
  float4 post_ao_xy_mul_add;
  // Host pixels of the depth, W and AO textures.
  uint2 post_ao_size;
  // Guest pixels per host pixel (1 / resolution scale), and the guest screen
  // position of the center of host pixel 0 is post_ao_guest_per_host * 0.5 +
  // post_ao_guest_origin (-0.5 with the Direct3D 9 pixel center).
  float2 post_ao_guest_per_host;
  float2 post_ao_guest_origin;
  // W from the stored depth d: w_numerator / (d * w_depth_mul + w_depth_add).
  float post_ao_w_numerator;
  float post_ao_w_depth_mul;
  float post_ao_w_depth_add;
  float post_ao_z_from_w;
  // W at the far plane: pixels beyond 99.9% of it are sky, without occlusion.
  float post_ao_w_far;
  // The radius in view units, and the host pixels it covers at W = 1.
  float post_ao_radius;
  float post_ao_radius_pixels;
  // Samples fade out over this fraction of the radius at its end.
  float post_ao_falloff;
  // Visibility exponent (> 1 darkens, as the cosine lobe misses inter-reflections
  // only partly), denoise center weight, and debug output.
  float post_ao_power;
  float post_ao_denoise_center;
  uint post_ao_debug;
  // Factor of the final occlusion.
  float post_ao_occlusion_scale;
};

// Bilinear within one mip, clamped.
SamplerState post_ao_linear_clamp : register(s0);

static const float kPi = 3.14159265;
static const float kHalfPi = 1.57079633;
static const uint kMips = 5u;
// The visibility of a few slices of one pixel can exceed 1 on unoccluded
// surfaces seen at an angle (it is 1 only on average over all directions):
// the working textures keep it divided by this, so the denoise averages it
// before it is clamped.
static const float kWorkingScale = 1.5;

float2 GuestPosition(float2 host_pixel_center) {
  return host_pixel_center * post_ao_guest_per_host + post_ao_guest_origin;
}

float WFromDepth(float stored_depth) {
  // Numerator and denominator have the sign of the projection (both negative
  // for D3D style ones); the cleared depth and the far plane come out at or
  // beyond the far W, or as infinity or NaN: sky.
  float w = post_ao_w_numerator / (stored_depth * post_ao_w_depth_mul + post_ao_w_depth_add);
  return (w > 0.0 && w < post_ao_w_far) ? w : post_ao_w_far;
}

float3 ViewPosition(float2 host_pixel_center, float w) {
  float2 guest = GuestPosition(host_pixel_center);
  return float3(w * (guest * post_ao_xy_mul_add.xz + post_ao_xy_mul_add.yw), w * post_ao_z_from_w);
}

bool IsSky(float w) { return w >= post_ao_w_far * 0.999; }

// acos with a maximum error of about 0.0009 radians (Lagarde).
float FastAcos(float x) {
  float a = abs(x);
  float r = (-0.156583 * a + kHalfPi) * sqrt(1.0 - a);
  return x >= 0.0 ? r : kPi - r;
}

// ---------------------------------------------------------------------------
// post_ao_prefilter: 8x8 threads, each 2x2 pixels; 1 / W mips 0-4 of a 16x16
// tile. Coarser mips keep the farther W of their pixels unless they are
// similar, so thin foreground edges do not grow into halos at a distance.

Texture2D<float> post_ao_depth : register(t0);
RWTexture2D<float> post_ao_w_mip0 : register(u0);
RWTexture2D<float> post_ao_w_mip1 : register(u1);
RWTexture2D<float> post_ao_w_mip2 : register(u2);
RWTexture2D<float> post_ao_w_mip3 : register(u3);
RWTexture2D<float> post_ao_w_mip4 : register(u4);

groupshared float post_ao_shared_w[8][8];

float FilterW(float w0, float w1, float w2, float w3) {
  float far_w = max(max(w0, w1), max(w2, w3));
  // Within 3/4 of the radius of the farthest: averaged; nearer: left out.
  float range = max(post_ao_radius * 0.75, far_w * 1.0e-4);
  float4 w = float4(w0, w1, w2, w3);
  float4 weight = saturate(1.0 - (far_w - w) / range);
  return dot(w, weight) / max(dot(weight, 1.0), 1.0e-4);
}

[numthreads(8, 8, 1)]
void post_ao_prefilter(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
  uint2 base = group.xy * 16u + thread.xy * 2u;
  float w[4];
  [unroll] for (uint i = 0u; i < 4u; ++i) {
    uint2 pixel = min(base + uint2(i & 1u, i >> 1u), post_ao_size - 1u);
    w[i] = WFromDepth(post_ao_depth.Load(int3(pixel, 0)));
    if (all(base + uint2(i & 1u, i >> 1u) < post_ao_size)) {
      post_ao_w_mip0[base + uint2(i & 1u, i >> 1u)] = 1.0 / w[i];
    }
  }
  float mip = FilterW(w[0], w[1], w[2], w[3]);
  uint2 size = max(post_ao_size >> 1u, 1u);
  uint2 position = group.xy * 8u + thread.xy;
  if (all(position < size)) post_ao_w_mip1[position] = 1.0 / mip;
  post_ao_shared_w[thread.y][thread.x] = mip;
  GroupMemoryBarrierWithGroupSync();

  // Level L reads the level L - 1 values kept by the threads at multiples of
  // 2^(L - 2) and keeps its own at the multiples of 2^(L - 1).
  [unroll] for (uint level = 2u; level < kMips; ++level) {
    uint step = 1u << (level - 2u);
    bool active = all((thread.xy & (step * 2u - 1u)) == 0u);
    float value = 0.0;
    if (active) {
      value = FilterW(post_ao_shared_w[thread.y][thread.x],
                      post_ao_shared_w[thread.y][thread.x + step],
                      post_ao_shared_w[thread.y + step][thread.x],
                      post_ao_shared_w[thread.y + step][thread.x + step]);
      size = max(post_ao_size >> level, 1u);
      position = (group.xy * 16u >> level) + (thread.xy >> (level - 1u));
      if (all(position < size)) {
        if (level == 2u) post_ao_w_mip2[position] = 1.0 / value;
        else if (level == 3u) post_ao_w_mip3[position] = 1.0 / value;
        else post_ao_w_mip4[position] = 1.0 / value;
      }
    }
    GroupMemoryBarrierWithGroupSync();
    if (active) post_ao_shared_w[thread.y][thread.x] = value;
    GroupMemoryBarrierWithGroupSync();
  }
}

// ---------------------------------------------------------------------------
// post_ao_main: visibility and edges per pixel.

Texture2D<float> post_ao_inverse_w : register(t0);
RWTexture2D<unorm float> post_ao_visibility_out : register(u0);
RWTexture2D<unorm float> post_ao_edges_out : register(u1);

float LoadW(int2 pixel) {
  return 1.0 / post_ao_inverse_w.Load(int3(clamp(pixel, int2(0, 0), int2(post_ao_size) - 1), 0));
}

// Interleaved gradient noise (Jimenez 2014), a value in [0, 1) per pixel.
float Noise(float2 pixel) { return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715)))); }

// 1 where the neighbor continues the surface, 0 across a depth discontinuity
// (left, right, top, bottom), from how far each neighbor is off the plane
// through the center and the opposite neighbor.
float4 Edges(float center, float left, float right, float top, float bottom) {
  float4 delta = float4(left, right, top, bottom) - center;
  float4 slope = delta + delta.yxwz;
  float4 difference = min(abs(delta), abs(slope));
  return saturate(1.25 - difference / (center * 0.011));
}

float PackEdges(float4 edges) {
  uint4 bits = uint4(round(saturate(edges) * 2.9));
  return float(bits.x << 6u | bits.y << 4u | bits.z << 2u | bits.w) / 255.0;
}

[numthreads(8, 8, 1)]
void post_ao_main(uint3 id : SV_DispatchThreadID) {
  int2 pixel = int2(id.xy);
  if (any(id.xy >= post_ao_size)) return;
  float w = LoadW(pixel);
  float left = LoadW(pixel + int2(-1, 0)), right = LoadW(pixel + int2(1, 0));
  float top = LoadW(pixel + int2(0, -1)), bottom = LoadW(pixel + int2(0, 1));
  float4 edges = Edges(w, left, right, top, bottom);
  post_ao_edges_out[id.xy] = PackEdges(edges);
  float2 center = float2(pixel) + 0.5;
  float radius_pixels = post_ao_radius_pixels / w;
  if (IsSky(w) || radius_pixels < 1.3) {
    post_ao_visibility_out[id.xy] = 1.0 / kWorkingScale;
    return;
  }

  float3 position = ViewPosition(center, w);
  float3 view = normalize(-position);
  // Normal from the neighbors on the side that continues the surface.
  float3 position_left = ViewPosition(center + float2(-1.0, 0.0), left);
  float3 position_right = ViewPosition(center + float2(1.0, 0.0), right);
  float3 position_top = ViewPosition(center + float2(0.0, -1.0), top);
  float3 position_bottom = ViewPosition(center + float2(0.0, 1.0), bottom);
  float3 dx = abs(left - w) < abs(right - w) ? position - position_left : position_right - position;
  float3 dy = abs(top - w) < abs(bottom - w) ? position - position_top : position_bottom - position;
  float3 normal = normalize(cross(dx, dy));
  if (dot(normal, view) < 0.0) normal = -normal;

  float noise_slice = Noise(center);
  float noise_step = Noise(center + float2(19.0, 47.0));
  float falloff_range = max(post_ao_radius * post_ao_falloff, 1.0e-3);
  float falloff_mul = -1.0 / falloff_range;
  float falloff_add = post_ao_radius / falloff_range;
  float2 inv_size = 1.0 / float2(post_ao_size);

  // Slice directions evenly around the view vector (only then the slices
  // average to the full hemisphere, 1 on an unoccluded plane), each sampled
  // along its projection onto the screen.
  float3 basis_x = normalize(cross(view, abs(view.y) < 0.9 ? float3(0.0, 1.0, 0.0)
                                                           : float3(1.0, 0.0, 0.0)));
  float3 basis_y = cross(view, basis_x);
  float visibility = 0.0;
  [unroll] for (uint slice = 0u; slice < POST_AO_SLICES; ++slice) {
    float phi = (float(slice) + noise_slice) * (kPi / float(POST_AO_SLICES));
    float3 ortho_direction = cos(phi) * basis_x + sin(phi) * basis_y;
    // The screen direction of the slice: the derivative of the projection
    // (guest x = (x * z_from_w / z - x_add) / x_mul, likewise y) along it, in
    // host pixels.
    float2 omega = float2(ortho_direction.x * position.z - position.x * ortho_direction.z,
                          ortho_direction.y * position.z - position.y * ortho_direction.z) /
                   (post_ao_xy_mul_add.xz * post_ao_guest_per_host) * post_ao_z_from_w;
    omega /= max(length(omega), 1.0e-20);
    float3 axis = normalize(cross(ortho_direction, view));
    float3 projected_normal = normal - axis * dot(normal, axis);
    float projected_length = length(projected_normal);
    float sign_normal = sign(dot(ortho_direction, projected_normal));
    float cos_normal = saturate(dot(projected_normal, view) / max(projected_length, 1.0e-4));
    float n = sign_normal * FastAcos(cos_normal);
    // Lowest horizons: the tangent plane on both sides.
    float low_cos0 = cos(n + kHalfPi);
    float low_cos1 = cos(n - kHalfPi);
    float horizon_cos0 = low_cos0, horizon_cos1 = low_cos1;

    [unroll] for (uint step = 0u; step < POST_AO_STEPS; ++step) {
      float s = (float(step) + noise_step) / float(POST_AO_STEPS);
      s *= s;  // Denser near the center.
      float2 offset = omega * max(s * radius_pixels, float(step) + 1.0);
      float mip = floor(clamp(log2(length(offset)) - 3.3, 0.0, float(kMips - 1u)));
      [unroll] for (uint side = 0u; side < 2u; ++side) {
        float2 sample_center = side == 0u ? center + offset : center - offset;
        float sample_w =
            1.0 / post_ao_inverse_w.SampleLevel(post_ao_linear_clamp, sample_center * inv_size, mip);
        float3 delta = ViewPosition(sample_center, sample_w) - position;
        float distance = length(delta);
        float sample_cos = dot(delta, view) / max(distance, 1.0e-4);
        float weight = saturate(distance * falloff_mul + falloff_add);
        if (side == 0u) {
          horizon_cos0 = max(horizon_cos0, lerp(low_cos0, sample_cos, weight));
        } else {
          horizon_cos1 = max(horizon_cos1, lerp(low_cos1, sample_cos, weight));
        }
      }
    }

    // Horizon angles relative to the view vector, within the hemisphere of the
    // normal, and the visibility arcs on both sides (the paper's equation 10).
    float h0 = n + clamp(-FastAcos(horizon_cos1) - n, -kHalfPi, kHalfPi);
    float h1 = n + clamp(FastAcos(horizon_cos0) - n, -kHalfPi, kHalfPi);
    float arc0 = (cos_normal + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) * 0.25;
    float arc1 = (cos_normal + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) * 0.25;
    visibility += projected_length * (arc0 + arc1);
  }
  visibility = max(visibility / float(POST_AO_SLICES), 0.0);
  post_ao_visibility_out[id.xy] = pow(visibility, post_ao_power) / kWorkingScale;
}

// ---------------------------------------------------------------------------
// post_ao_denoise: 3x3 blur weighted by the edges of the center.

Texture2D<float> post_ao_visibility_in : register(t0);
Texture2D<float> post_ao_edges_in : register(t1);
Texture2D<float> post_ao_inverse_w_debug : register(t2);
RWTexture2D<unorm float> post_ao_denoised_out : register(u0);

float4 UnpackEdges(float packed) {
  uint bits = uint(round(packed * 255.0));
  return float4(bits >> 6u, (bits >> 4u) & 3u, (bits >> 2u) & 3u, bits & 3u) / 3.0;
}

float LoadVisibility(int2 pixel) {
  return post_ao_visibility_in.Load(int3(clamp(pixel, int2(0, 0), int2(post_ao_size) - 1), 0));
}

[numthreads(8, 8, 1)]
void post_ao_denoise(uint3 id : SV_DispatchThreadID) {
  int2 pixel = int2(id.xy);
  if (any(id.xy >= post_ao_size)) return;
  float4 edges = UnpackEdges(post_ao_edges_in.Load(int3(pixel, 0)));
  float sum = LoadVisibility(pixel) * post_ao_denoise_center;
  float weight = post_ao_denoise_center;
  // Left, right, top, bottom, then the diagonals through both of their sides.
  const int2 sides[4] = {int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)};
  [unroll] for (uint i = 0u; i < 4u; ++i) {
    sum += LoadVisibility(pixel + sides[i]) * edges[i];
    weight += edges[i];
  }
  float4 diagonal = float4(edges.x * edges.z, edges.y * edges.z, edges.x * edges.w, edges.y * edges.w) * 0.5;
  const int2 corners[4] = {int2(-1, -1), int2(1, -1), int2(-1, 1), int2(1, 1)};
  [unroll] for (uint j = 0u; j < 4u; ++j) {
    sum += LoadVisibility(pixel + corners[j]) * diagonal[j];
    weight += diagonal[j];
  }
  float visibility = sum / weight;
#ifdef POST_AO_FINAL
  float occlusion = (1.0 - saturate(visibility * kWorkingScale)) * post_ao_occlusion_scale;
  if (post_ao_debug == 1u) {
    // View W over 0-100 units, for checking the depth and its alignment.
    occlusion = saturate(0.01 / post_ao_inverse_w_debug.Load(int3(pixel, 0)));
  } else if (post_ao_debug == 2u) {
    occlusion = 1.0 - saturate(LoadVisibility(pixel) * kWorkingScale);  // Before denoising.
  } else if (post_ao_debug == 3u) {
    // 1 across a depth discontinuity on any side (the edges of the denoise).
    occlusion = 1.0 - min(min(edges.x, edges.y), min(edges.z, edges.w));
  } else if (post_ao_debug == 4u) {
    // View W over 0-2 units, for close surfaces.
    occlusion = saturate(0.5 / post_ao_inverse_w_debug.Load(int3(pixel, 0)));
  }
  post_ao_denoised_out[id.xy] = occlusion;
#else
  post_ao_denoised_out[id.xy] = visibility;
#endif
}

// ---------------------------------------------------------------------------
// post_ao_clear: no occlusion until the next frame computes it.

RWTexture2D<unorm float> post_ao_clear_out : register(u0);

[numthreads(8, 8, 1)]
void post_ao_clear(uint3 id : SV_DispatchThreadID) {
  if (all(id.xy < post_ao_size)) post_ao_clear_out[id.xy] = 0.0;
}
