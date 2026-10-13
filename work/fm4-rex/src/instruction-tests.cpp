#include "fm4-instruction-fixes.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>

static unsigned checks = 0, failures = 0;
static void check(bool ok) { ++checks; failures += !ok; }
#include "vector-instruction-tests.h"
static uint16_t reference(float value) {
  const uint32_t bits = std::bit_cast<uint32_t>(value);
  const uint16_t sign = std::signbit(value) ? 0x8000 : 0;
  if (std::isinf(value)) return sign | 0x7c00;
  if (std::isnan(value)) return sign | 0x7e00 | ((bits & 0x7fffff) >> 13);
  const double mag = std::fabs(double(value));
  if (mag >= 65504) return sign | 0x7bff;
  if (mag < std::ldexp(1.0, -14)) return sign | uint16_t(std::floor(std::ldexp(mag, 24)));
  int exp;
  const double mantissa = std::frexp(mag, &exp);
  return sign | uint16_t((exp + 14) * 1024 + int(std::floor(mantissa * 2048)) - 1024);
}
int main() {
  for (unsigned half = 0; half < 65536; ++half) {
    PPCVRegister value{};
    for (unsigned i = 0; i < 4; ++i) value.u16[i] = uint16_t(half);
    FM4UnpackHalf(value, value, 4);
    const unsigned exp = (half >> 10) & 31, fraction = half & 1023;
    double expected = exp ? std::ldexp(1.0 + double(fraction) / 1024, int(exp) - 15)
                          : std::ldexp(double(fraction), -24);
    if (half & 0x8000) expected = -expected;
    bool ok = true;
    for (unsigned i = 0; i < 4; ++i) {
      if (exp == 31) ok &= fraction ? std::isnan(value.f32[i]) : std::isinf(value.f32[i]);
      else ok &= value.f32[i] == expected && std::signbit(value.f32[i]) == bool(half & 0x8000);
    }
    check(ok);
  }
  for (unsigned exp = 0; exp < 256; ++exp) for (uint32_t fraction : {0u, 1u, 0x1fffu, 0x2000u, 0x7fffffu})
    for (uint32_t sign : {0u, 0x80000000u}) {
      uint32_t bits = sign | (exp << 23) | fraction;
      check(FM4HalfRTZ(bits) == reference(std::bit_cast<float>(bits)));
    }
  for (unsigned count : {2u, 4u}) for (unsigned pack : {1u, 2u, 3u})
    for (unsigned shift = 0; shift < 4; ++shift) {
      PPCVRegister value{};
      value.u32[0] = 0x7fc12345; value.u32[1] = 0x477fffff;
      value.u32[2] = 0xb3800000; value.u32[3] = 0x33800000;
      PPCVRegister expected = value;
      const uint32_t xy = (uint32_t(reference(value.f32[3])) << 16) | reference(value.f32[2]);
      const uint32_t zw = count == 4 ? (uint32_t(reference(value.f32[1])) << 16) | reference(value.f32[0]) : 0;
      if (pack == 3 && shift == 3) expected.u32[0] = count == 4 ? xy : 0;
      else {
        expected.u32[shift] = count == 4 ? zw : xy;
        if (pack != 1 && shift < 3) expected.u32[shift + 1] = count == 4 ? xy : 0;
      }
      FM4PackHalf(value, value, count, pack, shift);
      check(std::memcmp(&value, &expected, 16) == 0);
    }
  PPCVRegister pair{}; pair.u16[1] = 0x3c00; pair.u16[0] = 0xc000;
  FM4UnpackHalf(pair, pair, 2);
  check(pair.f32[3] == 1 && pair.f32[2] == -2 && pair.f32[1] == 0 && pair.f32[0] == 1);
  const unsigned half_checks = checks;
  vector_tests();
  std::printf("{\"checks\":%u,\"failures\":%u,\"half_checks\":%u,\"vector_checks\":%u,\"old_fma_differences\":%u,\"old_dot_differences\":%u}\n", checks, failures, half_checks, checks - half_checks, old_fma_differences, old_dot_differences);
  return failures ? 1 : 0;
}
