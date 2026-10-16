// Developer options whose code the retail build no longer has, rebuilt on the
// title's own systems. fm4-command-line.cpp puts FM4_GAME_CMDLINE into the
// title's CForzaCommandLineParameters; these hooks read the options from there
// (their defaults, so off, without FM4_GAME_CMDLINE).
//   engageaionstart  an AI driver drives the player's car from the start: a race
//                    without input, for benchmarks and for finishing races. The
//                    pad still works in menus (pause, photo mode).
#include <rex/hook.h>
#include <rex/memory.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
// CForzaCommandLineParameters, the singleton of sub_8237D468 (static storage).
constexpr uint32_t kParameters = 0x82DC4420;
constexpr uint32_t kEngageAiOnStart = 1872;

bool Option(const uint8_t* base, uint32_t offset) { return base[kParameters + offset] != 0; }
}  // namespace

namespace {
// Diagnostic (FM4_TRACE_RACE_SETUP=1): the guest return addresses of the first
// driver creation of each race, from the saved LR of each frame.
void TraceCaller(PPCContext& ctx, uint8_t* base, const char* what) {
  std::fprintf(stderr, "FM4 %s r3 0x%08X r4 0x%08X r5 0x%08X, return addresses: %08X", what,
               ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, uint32_t(ctx.lr));
  uint32_t frame = ctx.r1.u32;
  for (int depth = 0; depth < 32; ++depth) {
    const uint32_t next = rex::memory::load_and_swap<uint32_t>(base + frame);
    if (next <= frame || next - frame > 0x100000) break;
    std::fprintf(stderr, " %08X", rex::memory::load_and_swap<uint32_t>(base + next - 8));
    frame = next;
  }
  std::fprintf(stderr, "\n");
}
}  // namespace

// Creates the driver of a car (driver slot, car setup). A slot whose +104 is 0
// gets a CHumanDriver, and an AI driver only as a standby; any other slot gets
// the AI driver of sub_82A935B8 (CMetaAIDriver or CAIDrivatar) as its driver.
REX_EXTERN(__imp__sub_82A93880);
REX_HOOK_RAW(sub_82A93880) {
  const uint32_t slot = ctx.r3.u32;
  static const bool trace = std::getenv("FM4_TRACE_RACE_SETUP") != nullptr;
  if (trace && !rex::memory::load_and_swap<uint32_t>(base + slot + 104))
    TraceCaller(ctx, base, "player driver");
  // +104: 0 for the player's car, 2 for AI opponents (setup type 2; the player's is 3).
  if (Option(base, kEngageAiOnStart) && !rex::memory::load_and_swap<uint32_t>(base + slot + 104)) {
    rex::memory::store_and_swap<uint32_t>(base + slot + 104, 1);
    std::fprintf(stderr, "FM4 engageaionstart: AI driver for the player's car (slot 0x%08X)\n", slot);
  }
  __imp__sub_82A93880(ctx, base);
}
