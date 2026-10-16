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
#include <string_view>

namespace {
// CForzaCommandLineParameters, the singleton of sub_8237D468 (static storage).
constexpr uint32_t kParameters = 0x82DC4420;
constexpr uint32_t kEngageAiOnStart = 1872;

bool Option(const uint8_t* base, uint32_t offset) { return base[kParameters + offset] != 0; }

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

// Starts the pending session of the app (app; the session type at app+536, 1 for
// a race), scheduled by the main loop (sub_822F7A00) as the task sub_826D0018.
// The race itself is described by the race settings (sub_822DEC50: +444 of the
// object at 0x82DC36FC). FM4_TRACE_RACE_SETUP=<file> also dumps them there.
REX_EXTERN(__imp__sub_826CFA68);
REX_HOOK_RAW(sub_826CFA68) {
  static const char* const dump = std::getenv("FM4_TRACE_RACE_SETUP");
  if (dump) {
    const uint32_t app = ctx.r3.u32;
    const uint32_t game = rex::memory::load_and_swap<uint32_t>(base + 0x82DC36FC) + 444;
    std::fprintf(stderr, "FM4 pending session: app 0x%08X type %u, race settings 0x%08X\n", app,
                 rex::memory::load_and_swap<uint32_t>(base + app + 536), game);
    if (game && std::string_view(dump) != "1") {
      if (FILE* f = std::fopen(dump, "ab")) {
        std::fwrite(base + game, 1, 0x1000, f);
        std::fclose(f);
      }
    }
  }
  __imp__sub_826CFA68(ctx, base);
}

// Creates the driver of a car (driver slot, car setup). A slot whose +104 is 0
// gets a CHumanDriver, and an AI driver only as a standby; any other slot gets
// the AI driver of sub_82A935B8 (CMetaAIDriver or CAIDrivatar) as its driver.
REX_EXTERN(__imp__sub_82A93880);
REX_HOOK_RAW(sub_82A93880) {
  const uint32_t slot = ctx.r3.u32;
  static const bool trace = std::getenv("FM4_TRACE_RACE_SETUP") != nullptr;
  if (trace && !rex::memory::load_and_swap<uint32_t>(base + slot + 104))
    TraceCaller(ctx, base, "player driver");
  if (trace) {
    const uint32_t setup = ctx.r4.u32;
    std::fprintf(stderr, "FM4 driver setup slot+104 %u, setup 0x%08X:",
                 rex::memory::load_and_swap<uint32_t>(base + slot + 104), setup);
    for (uint32_t i = 0; i < 12; ++i)
      std::fprintf(stderr, " %08X", rex::memory::load_and_swap<uint32_t>(base + setup + i * 4));
    std::fprintf(stderr, " +360 %02X %02X +1629 %02X\n", base[setup + 360], base[setup + 361],
                 base[setup + 1629]);
  }
  // +104: 0 for the player's car, 2 for AI opponents (setup type 2; the player's is 3).
  if (!Option(base, kEngageAiOnStart) || rex::memory::load_and_swap<uint32_t>(base + slot + 104)) {
    __imp__sub_82A93880(ctx, base);
    return;
  }
  // The player's setup (+0 type 3, +360 set) would make sub_82A935B8 configure the
  // Drivatar as the player's (sub_82A96900, sub_82A992E8 with 104), which neither
  // steers nor throttles: the car stops at the first wall. During the creation the
  // setup looks like an opponent's (+0 type 2, +360 clear), so the player's car
  // gets an opponent's AI driver; elsewhere it stays the player's.
  const uint32_t setup = ctx.r4.u32;
  const uint32_t type = rex::memory::load_and_swap<uint32_t>(base + setup);
  const uint8_t player = base[setup + 360];
  rex::memory::store_and_swap<uint32_t>(base + setup, 2);
  base[setup + 360] = 0;
  rex::memory::store_and_swap<uint32_t>(base + slot + 104, 2);
  __imp__sub_82A93880(ctx, base);
  rex::memory::store_and_swap<uint32_t>(base + setup, type);
  base[setup + 360] = player;
  std::fprintf(stderr, "FM4 engageaionstart: AI driver for the player's car (slot 0x%08X)\n", slot);
}
