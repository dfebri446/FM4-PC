// FinalCombine with depth of field and vignette (vignetteColor in c4, the
// strength in W); filmic tonemap in c22 and c23, LUT scale and offset in c5.x
// and c6.x. See fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) \
  fm4_combine_fetch(fetch, value, coord, XE_C(22), XE_C(23), XE_C(5).x, XE_C(6).x)
#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_vignette(n, value, 4u)
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_combine_input(p)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_grade(p)
