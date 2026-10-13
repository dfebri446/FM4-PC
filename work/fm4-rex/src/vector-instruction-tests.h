#pragma once
#include "fm4-vector-fixes.h"
#include "../tests/vector-vectors.h"
#include <iterator>

static unsigned old_fma_differences = 0, old_dot_differences = 0;
static bool same_float(uint32_t a, uint32_t b) {
  auto nan = [](uint32_t v) { return (v & 0x7fffffff) > 0x7f800000; };
  return a == b || (nan(a) && nan(b));
}
static void vector_tests() {
  const auto initial = simde_mm_getcsr();
  for (unsigned mode : {0u, 0x2000u, 0x4000u, 0x6000u, 0x8040u, 0xa040u, 0xc040u, 0xe040u}) {
    simde_mm_setcsr((initial & ~0xe040u) | mode);
    for (unsigned index = 0; index < std::size(kFMAVectors); ++index) {
      for (bool negative : {false, true}) for (unsigned alias = 0; alias < 4; ++alias) {
        PPCVRegister a{}, b{}, c{}, out{}, expected{};
        for (unsigned lane = 0; lane < 4; ++lane) {
          const auto& test = kFMAVectors[(index + lane) % std::size(kFMAVectors)];
          a.u32[lane] = test.a; b.u32[lane] = test.b; c.u32[lane] = test.c;
          expected.u32[lane] = negative ? test.nsub : test.add;
        }
        PPCVRegister& dest = alias == 1 ? a : alias == 2 ? b : alias == 3 ? c : out;
        FM4VectorFMA(dest, a, b, c, negative);
        for (unsigned lane = 0; lane < 4; ++lane) {
          bool ok = same_float(dest.u32[lane], expected.u32[lane]);
          if (!ok && failures < 8) std::fprintf(stderr, "FMA index=%u lane=%u alias=%u neg=%u mode=%x got=%08x expected=%08x\n", index, lane, alias, negative, mode, dest.u32[lane], expected.u32[lane]);
          check(ok);
        }
        check((simde_mm_getcsr() & 0xe040u) == mode);
      }
    }
    for (const auto& test : kDotVectors) for (unsigned count : {3u, 4u})
      for (unsigned alias = 0; alias < 3; ++alias) {
        PPCVRegister a{}, b{}, out{};
        std::memcpy(a.u32, test.a, 16); std::memcpy(b.u32, test.b, 16);
        PPCVRegister& dest = alias == 1 ? a : alias == 2 ? b : out;
        FM4VectorDot(dest, a, b, count);
        const auto expected = count == 3 ? test.dot3 : test.dot4;
        for (unsigned lane = 0; lane < 4; ++lane) {
          bool ok = same_float(dest.u32[lane], expected);
          if (!ok && failures < 8) std::fprintf(stderr, "Dot count=%u alias=%u mode=%x got=%08x expected=%08x\n", count, alias, mode, dest.u32[lane], expected);
          check(ok);
        }
        check((simde_mm_getcsr() & 0xe040u) == mode);
      }
  }
  simde_mm_setcsr((initial & ~0x6000u) | 0x8040u);
  // Confirm that these vectors detect the original emitter's arithmetic errors.
  for (const auto& test : kFMAVectors) {
    auto av = simde_mm_set1_ps(std::bit_cast<float>(test.a));
    auto bv = simde_mm_set1_ps(std::bit_cast<float>(test.b));
    auto cv = simde_mm_set1_ps(std::bit_cast<float>(test.c));
    auto value = simde_mm_cvtss_f32(simde_mm_add_ps(simde_mm_mul_ps(av, bv), cv));
    old_fma_differences += !same_float(std::bit_cast<uint32_t>(value), test.add);
  }
  for (const auto& test : kDotVectors) {
    auto av = simde_mm_loadu_ps(reinterpret_cast<const float*>(test.a));
    auto bv = simde_mm_loadu_ps(reinterpret_cast<const float*>(test.b));
    auto value = simde_mm_cvtss_f32(simde_mm_dp_ps(av, bv, 0xEF));
    old_dot_differences += !same_float(std::bit_cast<uint32_t>(value), test.dot3);
  }
  check(old_fma_differences > 0);
  check(old_dot_differences > 0);
  // Non-finite input, ignored W, all-source aliases, and signed zero.
  PPCVRegister a{}, b{}, c{}, out{};
  for (auto& v : a.f32) v = 1;
  for (auto& v : b.f32) v = 1;
  a.u32[0] = 0x7f800000;
  FM4VectorDot(out, a, b, 3); check(out.f32[0] == 3);
  FM4VectorDot(out, a, b, 4); check(out.u32[0] == 0x7f800000);
  a.u32[0] = 0x7fc12345;
  FM4VectorDot(out, a, b, 3); check(out.f32[0] == 3);
  FM4VectorDot(out, a, b, 4); check(std::isnan(out.f32[0]));
  a = {}; b = {}; c = {};
  FM4VectorFMA(out, a, b, c, true); check(out.u32[0] == 0x80000000);
  for (auto& v : a.f32) v = 1.5f;
  FM4VectorFMA(a, a, a, a, false); check(a.f32[0] == 3.75f);
  FM4VectorDot(a, a, a, 4); check(a.f32[0] == 56.25f);
  simde_mm_setcsr(initial);
}
