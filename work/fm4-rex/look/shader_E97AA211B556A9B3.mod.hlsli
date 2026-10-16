// FinalCombine of races: scene (fetch 0) and bloom (fetch 2), exposure,
// filmic tonemap (c5, c6), color grading LUT (fetch 7 at t * c0.x + c1.x),
// gamma 2. See fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) \
  fm4_combine_fetch(fetch, value, coord, XE_C(5), XE_C(6), XE_C(0).x, XE_C(1).x)
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_combine_input(p)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_grade(p)
