// Motion blur of the scene (fetch 0, at interpolator 0 XY). See fm4-look.hlsli.
#include "fm4-look.hlsli"
#undef XE_HOOK_OUTPUT
#define XE_HOOK_OUTPUT(p, input) fm4_motion_blur(p, input)
