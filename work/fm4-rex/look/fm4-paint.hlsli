// FM4 car paint mods: a clear coat over the game's car body shading. Original
// code of this project, like fm4-look.hlsli: it reads the game's shader inputs
// (outputs/FM4-analysis/MATERIAL-ATLAS.txt) and adds to its output.
//   FM4_LOOK_CLEARCOAT            0...1  clear coat strength (0: the game's paint)
//   FM4_LOOK_CLEARCOAT_ROUGHNESS  > 0    clear coat roughness (GGX alpha is its square)
//   FM4_LOOK_CLEARCOAT_GLINT      > 0    soft limit of the sun highlight, in units of the
//                                        sun's diffuse light: a GGX highlight at grazing
//                                        angles reaches 20-40x and blooms into a blob
//   FM4_LOOK_FLAKES               0...1  metallic flakes under the clear coat (0: none)
//   FM4_LOOK_FLAKE_SCALE          > 0    flake cells per unit of the paint's texture UV
//
// The game lights car paint with a Phong highlight, pow(R.L), and a Fresnel
// weighted environment reflection. The clear coat adds what a lacquer layer
// does: a sharp, energy normalized GGX highlight of the sun and the reflection
// of the environment cube at grazing angles (Schlick Fresnel, F0 0.04), both
// in the paint's own shadow, before the game's fog.

#ifndef FM4_LOOK_CLEARCOAT
#define FM4_LOOK_CLEARCOAT 0.0
#endif
#ifndef FM4_LOOK_CLEARCOAT_ROUGHNESS
#define FM4_LOOK_CLEARCOAT_ROUGHNESS 0.3
#endif
#ifndef FM4_LOOK_CLEARCOAT_GLINT
#define FM4_LOOK_CLEARCOAT_GLINT 4.0
#endif
#ifndef FM4_LOOK_FLAKES
#define FM4_LOOK_FLAKES 0.0
#endif
#ifndef FM4_LOOK_FLAKE_SCALE
#define FM4_LOOK_FLAKE_SCALE 1200.0
#endif

static const float FM4_PI = 3.14159265;

// Height correlated Smith visibility for GGX (divided by 4 N.L N.V).
float fm4_ggx_visibility(float n_dot_v, float n_dot_l, float a2) {
  float v = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - a2) + a2);
  float l = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - a2) + a2);
  return 0.5 / max(v + l, 1.0e-5);
}

float fm4_schlick(float f0, float cosine) {
  float m = 1.0 - saturate(cosine);
  return f0 + (1.0 - f0) * (m * m) * (m * m) * m;
}

// The cube fetch coordinate of a world direction d from xe_cube(d.zzxy), the
// way the game's shaders compute it: S and T over the major axis plus the
// offset, and the face.
float3 fm4_env_cube_coord(float4 cube, float offset) {
  float scale = 1.0 / abs(cube.z);
  return float3(cube.y * scale + offset, cube.x * scale + offset, cube.w);
}

// The car's own variance shadow map (3x3 taps around the shadow coordinate,
// Chebyshev bound with the minimum variance and darkening exponent the game
// uses) and the track's lightmap shadow, as their minimum: 1 lit, 0 shadowed.
// shadow: x, y shadow map coordinate, z depth; lightmap_uv: track lightmap.
#if defined(XE_TEXTURE13_2D_U) && defined(XE_TEXTURE10_2D_U)
float fm4_paint_shadow(float3 shadow, float2 lightmap_uv, float4 lightmap_params,
                       float min_variance, float darkening, bool self_shadow) {
  float lit = 1.0;
  if (self_shadow) {
    float2 texel = 1.0 / xe_texture_size_2d(13u);
    float2 moments = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) {
      [unroll] for (int x = -1; x <= 1; ++x) {
        moments += XE_TFETCH2D(13, shadow.xy + float2(x, y) * texel).xy;
      }
    }
    moments *= 1.0 / 9.0;
    float d = shadow.z - moments.x;
    float variance = moments.y - moments.x * moments.x + min_variance;
    float p = pow(saturate(variance / (variance + d * d)), darkening);
    lit = d > 0.0 ? p : 1.0;
  }
  float lightmap = XE_TFETCH2D(10, lightmap_uv).x;
  float track = saturate((lightmap - lightmap_params.x) / (lightmap_params.y - lightmap_params.x));
  return min(lit, track);
}
#endif

// Three random numbers in [0, 1) for an integer cell (PCG3D, Jarzynski and Olano 2020).
float3 fm4_random3(uint3 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y * v.z;
  v.y += v.z * v.x;
  v.z += v.x * v.y;
  v ^= v >> 16u;
  v.x += v.y * v.z;
  v.y += v.z * v.x;
  v.z += v.x * v.y;
  return float3(v) * (1.0 / 4294967296.0);
}

// Metallic flakes: one flake in a share of the cells of the paint's texture UV,
// each with its normal tilted at random, glinting where it mirrors the sun.
// Cells smaller than a pixel fade out, so distant paint does not sparkle in
// noise (flakes show from up close, as on a real car). The result is in units
// of the sun's diffuse light, like the clear coat highlight. In races they do
// not show: from the chase camera the cells are smaller than a pixel (faded
// out), and on the hood (cells of 3-5 pixels) no flake met the narrow lobe,
// which only the paint near the sun's mirror direction can.
float fm4_flakes(float3 n, float3 v, float3 l, float2 uv) {
  float2 cell = uv * FM4_LOOK_FLAKE_SCALE;
  float2 footprint = fwidth(cell);
  float fade = saturate(1.5 - 1.5 * max(footprint.x, footprint.y));
  float3 random = fm4_random3(uint3(int3(int2(floor(cell)), 0) + 0x8000));
  float3 tangent = normalize(cross(n, abs(n.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0)));
  float3 bitangent = cross(n, tangent);
  float2 tilt = (random.xy * 2.0 - 1.0) * 0.35;
  float3 flake = normalize(n + tangent * tilt.x + bitangent * tilt.y);
  float3 h = normalize(l + v);
  // A narrow normalized lobe (roughness 0.1) on the tilted flake, in a quarter of the cells.
  const float a2 = 0.0001;
  float n_dot_h = saturate(dot(flake, h));
  float d = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
  float glint = a2 / (FM4_PI * d * d) * 0.05 * saturate(dot(n, l));
  return random.z < 0.25 ? glint * fade : 0.0;
}

// Adds the clear coat to the paint's output (gamma 2, linear squared).
// normal, position: world space; eye: eye position; to_sun: direction to the
// sun; sun: its color; env: the environment (linear) in the reflection
// direction; lit: shadow; fog: the game's fog amount (its fog is applied last);
// uv: the paint's texture coordinate and paint: its color, for the flakes.
void fm4_clearcoat(inout XePixel p, float3 normal, float3 position, float3 eye,
                   float3 to_sun, float3 sun, float3 env, float lit, float fog, float2 uv,
                   float3 paint) {
  float3 n = normalize(normal);
  float3 v = normalize(eye - position);
  float3 l = normalize(to_sun);
  float3 h = normalize(l + v);
  float n_dot_v = max(dot(n, v), 1.0e-4);
  float n_dot_l = saturate(dot(n, l));
  float n_dot_h = saturate(dot(n, h));
  float roughness = max(FM4_LOOK_CLEARCOAT_ROUGHNESS, 0.02);
  float a2 = roughness * roughness * roughness * roughness;
  float denominator = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
  float distribution = a2 / (FM4_PI * denominator * denominator);
  float fresnel_sun = fm4_schlick(0.04, dot(l, h));
  // The game's lighting leaves out 1/pi (Lambert diffuse is albedo * sun * N.L),
  // so the punctual specular is scaled by pi to match.
  float glint = FM4_PI * distribution * fm4_ggx_visibility(n_dot_v, n_dot_l, a2) * fresnel_sun *
                n_dot_l;
  glint *= saturate(FM4_LOOK_CLEARCOAT);
  // Flakes under the coat, tinted towards the paint color (the light the coat lets through).
  float flakes = FM4_LOOK_FLAKES > 0.0 ? fm4_flakes(n, v, l, uv) * saturate(FM4_LOOK_FLAKES) : 0.0;
  float3 tint = paint / max(max(paint.r, max(paint.g, paint.b)), 1.0e-3);
  float3 flake_color = lerp(float3(1.0, 1.0, 1.0), saturate(tint), 0.6) * (1.0 - fresnel_sun);
  // Soft limit: the core stays sharp, the energy that bloom spreads does not run away.
  float glint_limit = max(FM4_LOOK_CLEARCOAT_GLINT, 1.0e-3);
  float3 sparkle = glint + flakes * flake_color;
  float3 highlight = sparkle / (1.0 + sparkle / glint_limit) * sun * lit;
  // Above the head-on 0.04, which the game's own reflection already covers.
  float fresnel_view = fm4_schlick(0.04, n_dot_v) - 0.04;
  float3 reflection = env * fresnel_view * lerp(0.35, 1.0, lit) * saturate(FM4_LOOK_CLEARCOAT);
  float3 color = p.color[0].rgb;
  float3 linear_color = color * color;
  linear_color += (highlight + reflection) * (1.0 - fog);
  p.color[0].rgb = sqrt(max(linear_color, 0.0));
}
