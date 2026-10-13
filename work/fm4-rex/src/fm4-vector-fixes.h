#pragma once
#include <rex/ppc/context.h>
#include <bit>
#include <cmath>

// VMX arithmetic uses nearest-even independently of scalar FPSCR rounding.
// Preserve the scalar mode and leave the SDK's cached FPSCR control unchanged.
class FM4VectorRounding {
  using Platform = rex::platform::FPSCRPlatform;
  uint32_t saved_ = Platform::getcsr();
public:
  FM4VectorRounding() noexcept {
    if (saved_ & Platform::RoundMaskVal)
      Platform::setcsr(saved_ & ~uint32_t(Platform::RoundMaskVal));
  }
  ~FM4VectorRounding() {
    if (saved_ & Platform::RoundMaskVal)
      Platform::setcsr((Platform::getcsr() & ~uint32_t(Platform::RoundMaskVal)) |
                      (saved_ & Platform::RoundMaskVal));
  }
};

inline float FM4VectorFlush(float value) noexcept {
  uint32_t bits = std::bit_cast<uint32_t>(value);
  if ((bits & 0x7f800000u) == 0) bits &= 0x80000000u;
  return std::bit_cast<float>(bits);
}

// Explicit fma keeps one rounding even when the compiler's baseline lacks FMA3.
// A temporary vector preserves inputs for all destination/source aliases.
inline void FM4VectorFMA(PPCVRegister& dest, const PPCVRegister& a,
                         const PPCVRegister& b, const PPCVRegister& c,
                         bool negative_subtract) {
#pragma STDC FENV_ACCESS ON
#pragma clang fp contract(off)
  FM4VectorRounding rounding;
  PPCVRegister result{};
  for (unsigned lane = 0; lane < 4; ++lane) {
    const float av = FM4VectorFlush(a.f32[lane]);
    const float bv = FM4VectorFlush(b.f32[lane]);
    const float cv = FM4VectorFlush(c.f32[lane]);
    // VNMSUB negates after the fused subtraction, including the sign of zero.
    const float value = negative_subtract ? -std::fma(av, bv, -cv) : std::fma(av, bv, cv);
    result.f32[lane] = FM4VectorFlush(value);
  }
  dest = result;
}

// Matches the double-precision accumulation and overflow classification in
// Xenia Canary's DOT_PRODUCT_3/4. SDK host lanes are [w,z,y,x]. Products of two
// finite float32 values are exact in float64; the additions retain their order.
inline void FM4VectorDot(PPCVRegister& dest, const PPCVRegister& a,
                        const PPCVRegister& b, unsigned count) {
#pragma STDC FENV_ACCESS ON
#pragma clang fp contract(off)
  FM4VectorRounding rounding;
  double products[4]{};
  for (unsigned lane = count == 3 ? 1 : 0; lane < 4; ++lane)
    products[lane] = double(FM4VectorFlush(a.f32[lane])) *
                     double(FM4VectorFlush(b.f32[lane]));
  const double xz = products[3] + products[1];
  const double sum = count == 3 ? xz + products[2] : xz + (products[2] + products[0]);
  float value = float(sum);
  // Infinity from an input remains infinity. Only finite-to-float overflow
  // becomes quiet NaN; this is not equivalent to mapping every infinity to NaN.
  if (std::isfinite(sum) && std::isinf(value))
    value = std::bit_cast<float>(0x7fc00000u);
  value = FM4VectorFlush(value);
  for (auto& lane : dest.f32) lane = value;
}
