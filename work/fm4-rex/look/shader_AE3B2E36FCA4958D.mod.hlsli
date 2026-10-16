// FinalCombine with depth of field and vignette (vignetteColor in c4, the
// strength in W). See fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) fm4_combine_fetch(fetch, value, coord)
#undef XE_HOOK_CONSTANT
#define XE_HOOK_CONSTANT(n, value) fm4_vignette(n, value, 4u)
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_exposure(p)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_grade(p)
