#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <rex/hook.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include "fm4-diagnostic-state.h"

REX_EXTERN(__imp__sub_825D8850);
REX_EXTERN(__imp__sub_825D5C18);
REX_EXTERN(__imp__sub_825D7738);
REX_EXTERN(__imp__sub_82516C38);
namespace {
uint32_t UIRead(uint8_t* base, uint32_t address) {
  uint32_t value = 0; SIZE_T size = 0;
  if (!ReadProcessMemory(GetCurrentProcess(), base + address, &value, 4, &size) || size != 4) return 0;
  return _byteswap_ulong(value);
}
void TraceUI(PPCContext& ctx, uint8_t* base, PPCFunc* original, uint32_t function) {
  if (std::getenv("FM4_CAPTURE_DIR")) {
    const auto object = ctx.r3.u32;
    const auto vtable = UIRead(base, object);
    static thread_local unsigned calls = 0;
    if ((function != 0x82516C38 || vtable == 0x82043E04) &&
        ((fm4_diagnostic_buttons.load(std::memory_order_relaxed) & 0x1003) || calls < 8) && calls < 600) {
      ++calls;
      std::fprintf(stderr, "FM4 UI fn=%08x n=%u object=%08x vt=%08x r4=%08x r5=%08x r6=%08x r7=%08x lr=%llx state832=%08x state836=%08x control=%08x\n",
        function, calls, object, vtable, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.lr,
        UIRead(base, object + 832), UIRead(base, object + 836), UIRead(base, object + 864));
      std::fflush(stderr);
    }
  }
  original(ctx, base);
}
}
REX_HOOK_RAW(sub_825D8850) { TraceUI(ctx, base, __imp__sub_825D8850, 0x825D8850); }
REX_HOOK_RAW(sub_825D5C18) { TraceUI(ctx, base, __imp__sub_825D5C18, 0x825D5C18); }
REX_HOOK_RAW(sub_825D7738) { TraceUI(ctx, base, __imp__sub_825D7738, 0x825D7738); }
REX_HOOK_RAW(sub_82516C38) { TraceUI(ctx, base, __imp__sub_82516C38, 0x82516C38); }

// Observe the game's actual controller reads and converted button state.
// Bounded and opt-in; neither hook changes guest registers or memory.
REX_EXTERN(__imp__sub_82C778C8);
REX_HOOK_RAW(sub_82C778C8) {
  const auto user = ctx.r3.u32, address = ctx.r4.u32;
  const auto caller = uint32_t(ctx.lr);
  __imp__sub_82C778C8(ctx, base);
  if (!std::getenv("FM4_TEST_INPUT_FILE") || ctx.r3.u32 || !address) return;
  const auto buttons = UIRead(base, address + 4) >> 16;
  static thread_local std::map<uint64_t, uint32_t> previous;
  static thread_local unsigned count = 0;
  const uint64_t key = (uint64_t(caller) << 32) | user;
  const auto found = previous.find(key);
  if ((found == previous.end() || found->second != buttons) && count++ < 256) {
    std::fprintf(stderr, "FM4 guest input: caller=%08x user=%u packet=%u buttons=%04x state=%08x\n",
                 caller, user, UIRead(base, address), buttons, address);
    std::fflush(stderr);
  }
  previous[key] = buttons;
}

REX_EXTERN(__imp__sub_822B4130);
REX_HOOK_RAW(sub_822B4130) {
  const auto object = ctx.r3.u32, input = ctx.r4.u32;
  const auto buttons = std::getenv("FM4_TEST_INPUT_FILE") ? UIRead(base, input + 4) >> 16 : 0;
  __imp__sub_822B4130(ctx, base);
  if (!std::getenv("FM4_TEST_INPUT_FILE")) return;
  static thread_local std::map<uint32_t, uint32_t> previous;
  static thread_local unsigned count = 0;
  auto found = previous.find(object);
  if ((found == previous.end() || found->second != buttons) && count++ < 256) {
    std::fprintf(stderr, "FM4 converted input: object=%08x buttons=%04x words=", object, buttons);
    for (unsigned offset=0; offset<32; offset+=4)
      std::fprintf(stderr, "%08x,", UIRead(base, object + offset));
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
  }
  previous[object] = buttons;
}

// Observe current, previous and release-history masks in the input owner.
// The generated caller copies owner+64 to owner+248 before converting the
// new controller state. This hook is diagnostic only and bounded per thread.
REX_EXTERN(__imp__sub_82379300);
REX_HOOK_RAW(sub_82379300) {
  const auto object = ctx.r3.u32;
  static const bool enabled = std::getenv("FM4_INPUT_EDGE_TRACE") != nullptr;
  static thread_local unsigned count = 0;
  static thread_local std::map<uint32_t, uint64_t> previous;
  const auto mask = enabled ? (uint64_t(UIRead(base, object + 72)) << 32 |
                               UIRead(base, object + 76)) : 0;
  __imp__sub_82379300(ctx, base);
  if (!enabled || count >= 128) return;
  const auto found = previous.find(object);
  if (found == previous.end() || found->second != mask) {
    ++count;
    std::fprintf(stderr, "FM4 input edge: thread=%lu owner=%08x current=%016llx previous=%08x%08x history=%08x%08x\n",
      GetCurrentThreadId(), object, static_cast<unsigned long long>(mask),
      UIRead(base, object + 256), UIRead(base, object + 260),
      UIRead(base, object + 440), UIRead(base, object + 444));
    std::fflush(stderr);
  }
  previous[object] = mask;
}
