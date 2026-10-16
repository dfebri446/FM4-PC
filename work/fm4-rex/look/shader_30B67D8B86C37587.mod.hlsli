// FinalCombine with depth of field (photo mode, replays): the scene in focus
// (fetch 0), the blurred scene (fetch 4) and the bloom (fetch 2); filmic
// tonemap in c19 and c20, LUT scale and offset in c2.x and c3.x. See
// fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_TFETCH
#define XE_HOOK_TFETCH(fetch, value, coord) \
  fm4_combine_fetch(fetch, value, coord, XE_C(19), XE_C(20), XE_C(2).x, XE_C(3).x)
#undef XE_HOOK_INPUT
#define XE_HOOK_INPUT(p) fm4_combine_input(p)
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_grade(p)
