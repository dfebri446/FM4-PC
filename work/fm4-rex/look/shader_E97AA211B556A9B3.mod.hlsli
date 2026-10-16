// FinalCombine of races: scene (fetch 0) and bloom (fetch 2), exposure,
// filmic tonemap, color grading LUT, gamma 2. See fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_combine_fetch(fetch, value, coord)
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_exposure(p)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_grade(p)
