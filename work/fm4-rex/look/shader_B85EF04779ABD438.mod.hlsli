// Car body paint (docs/analysis/material-atlas.md): interpolator 1 the world normal, 2 the
// world position, 3 the shadow map coordinate, 4 the track lightmap UV;
// c42 the sun color, c43 the direction to the sun, c47 the fog color and
// amount, c59 the eye position, c60 the lightmap shadow range, c80 the shadow
// darkening, c254.w the minimum shadow variance, c255.x the cube coordinate
// offset; tf5 the dynamic environment cube, tf6 the static one (bool 130),
// tf4 a background map instead of either (bool 133), tf13 the variance shadow
// map (bool 128); interpolator 0 XY the paint texture UV and c83 the paint
// color, for the flakes. See fm4-paint.hlsli.
#include "fm4-paint.hlsli"

void fm4_body_paint(inout XePixel p, XePixel input) {
  if (FM4_LOOK_CLEARCOAT <= 0.0 && FM4_LOOK_FLAKES <= 0.0) {
    return;
  }
  float3 n = normalize(input.r[1].xyz);
  float3 v = normalize(XE_C(59).xyz - input.r[2].xyz);
  float3 r = reflect(-v, n);
  float3 env = 0.0;
  if (!xe_bool_constant(133u)) {
    float3 coord = fm4_env_cube_coord(xe_cube(r.zzxy), XE_C(255).x);
    env = xe_bool_constant(130u) ? XE_TFETCHCUBE(6, coord).rgb : XE_TFETCHCUBE(5, coord).rgb;
    env *= env;
  }
  float lit = fm4_paint_shadow(input.r[3].xyz, input.r[4].xy, XE_C(60), XE_C(254).w, XE_C(80).x,
                               xe_bool_constant(128u));
  fm4_clearcoat(p, n, input.r[2].xyz, XE_C(59).xyz, XE_C(43).xyz, XE_C(42).xyz, env, lit,
                saturate(XE_C(47).w), input.r[0].xy, XE_C(83).xyz);
}

#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_body_paint(p, input)
