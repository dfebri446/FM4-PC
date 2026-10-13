#pragma once
#include <rex/ppc/context.h>
#include <simde/simde-f16.h>

// Port of the float16 semantics already checked in the XenonRecomp regression suite.
inline uint16_t FM4HalfRTZ(uint32_t bits) noexcept {
  const uint16_t sign = uint16_t((bits >> 16) & 0x8000);
  const uint32_t exponent = (bits >> 23) & 255;
  const uint32_t fraction = bits & 0x7fffff;
  if (exponent == 255)
    return uint16_t(sign | 0x7c00 | (fraction ? ((fraction >> 13) | 0x200) : 0));
  if (exponent > 142) return uint16_t(sign | 0x7bff);
  if (exponent < 103) return sign;
  if (exponent < 113)
    return uint16_t(sign | ((fraction | 0x800000) >> (126 - exponent)));
  return uint16_t(sign | ((exponent - 112) << 10) | (fraction >> 13));
}

inline void FM4PackHalf(PPCVRegister& dest, const PPCVRegister& source,
                        unsigned count, unsigned pack, unsigned shift) {
  PPCVRegister packed{};
  for (unsigned i = 0; i < count; ++i)
    packed.u16[count - 1 - i] = FM4HalfRTZ(source.u32[3 - i]);
  if (pack == 3 && shift == 3) dest.u32[0] = packed.u32[1];
  else {
    dest.u32[shift] = packed.u32[0];
    if (pack != 1 && shift < 3) dest.u32[shift + 1] = packed.u32[1];
  }
}

inline void FM4UnpackHalf(PPCVRegister& dest, const PPCVRegister& source, unsigned count) {
  PPCVRegister unpacked{};
  for (unsigned i = 0; i < count; ++i)
    unpacked.f32[3 - i] = simde_float16_to_float32(
        simde_uint16_as_float16(source.u16[count - 1 - i]));
  if (count == 2) unpacked.f32[0] = 1.0f;
  dest = unpacked;
}
