// Guest vblank rate per render scenario. FM4 assumes a 60 Hz display: menus
// render one frame per vblank and their idle timers (the career and GO RACE
// menus hide, the post-race menu starts the replay) run faster at a higher
// refresh rate, and FM4_FRAME_RATE is meant for races only. With FM4_FRAME_RATE
// above 60 the launcher starts the guest vblank at 60 Hz, and this module
// raises it to FM4_FRAME_RATE while a race scenario decides the frame rate.
//
// The title keeps a stack of render scenarios (media\RenderScenarios) and
// mirrors every change to its render thread through CRenderThreadLink: Push
// with the scenario's CDynamicRenderSettings, Pop without arguments (LIFO; the
// race phase code pops the old phase before it pushes the next one, photo mode
// is pushed on top of the race). The frame rate of the stack is that of the
// topmost scenario that sets one: races 60 (FM4_FRAME_RATE replaces it),
// countdown, post-race, replays and photo mode 30; menus and pre-race set none
// (the control's default, 60).
// FM4_VBLANK_SWITCH=0 keeps the vblank rate fixed; FM4_VBLANK_TRACE=1 prints
// every push and pop.
//
// Scenarios below 60 (30: two vblanks per frame) also hold vblank_vrr to the
// fixed grid: their render thread waits for the vblanks itself, which the
// vsync thread cannot see, and after one long frame (a 200 ms hitch in the
// career replay at draw_resolution_scale 2) every vblank came forced or late
// and the replay ran at 23 fps instead of 30. FM4_VRR_HALF_RATE=1 keeps
// vblank_vrr there for comparisons.
#include <rex/graphics/graphics_system.h>
#include <rex/hook.h>
#include <rex/memory.h>
#include <rex/runtime.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {
// CDynamicRenderSettings: vtable, name (MSVC std::string) at +8, the FrameRate
// control at +44 with its value at +48.
constexpr uint32_t kSettingsVtable = 0x8214EB48;
constexpr uint32_t kSettingsName = 8;
constexpr uint32_t kSettingsFrameRate = 48;
constexpr uint32_t kDefaultFrameRate = 60;

rex::Runtime* runtime = nullptr;
uint32_t race_rate = 0;  // 0: switching off
bool vrr_half_rate = false;
bool trace = false;
std::mutex mutex;
std::vector<uint32_t> frame_rates;  // per pushed scenario, bottom first
double current_rate = 0.0;

uint32_t Load(const uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}

std::string Name(const uint8_t* base, uint32_t settings) {
  const uint32_t string = settings + kSettingsName;
  const uint32_t size = Load(base, string + 16), capacity = Load(base, string + 20);
  if (size > 64 || capacity < size) return "?";
  const uint32_t data = capacity >= 16 ? Load(base, string) : string;
  return std::string(reinterpret_cast<const char*>(base + data), size);
}

// With the mutex held.
void Apply(const char* event, const std::string& name) {
  uint32_t frame_rate = kDefaultFrameRate;
  for (auto it = frame_rates.rbegin(); it != frame_rates.rend(); ++it) {
    if (*it != kDefaultFrameRate) {
      frame_rate = *it;
      break;
    }
  }
  const double rate = frame_rate > kDefaultFrameRate ? race_rate : kDefaultFrameRate;
  auto* gpu = static_cast<rex::graphics::GraphicsSystem*>(runtime->graphics_system());
  if (gpu) gpu->SetVblankVrrPaused(!vrr_half_rate && frame_rate < kDefaultFrameRate);
  if (race_rate && gpu && rate != current_rate) {
    gpu->SetVblankRate(rate);
    current_rate = rate;
  }
  if (trace)
    std::fprintf(stderr, "FM4 render scenario %s %s: depth %zu, frame rate %u, vblank %g Hz\n",
                 event, name.c_str(), frame_rates.size(), frame_rate, current_rate);
}
}  // namespace

void FM4InstallVblankRate(rex::Runtime* rt) {
  const char* frame_rate = std::getenv("FM4_FRAME_RATE");
  const char* enabled = std::getenv("FM4_VBLANK_SWITCH");
  const char* traced = std::getenv("FM4_VBLANK_TRACE");
  const char* half_rate = std::getenv("FM4_VRR_HALF_RATE");
  trace = traced && std::strcmp(traced, "1") == 0;
  vrr_half_rate = half_rate && std::strcmp(half_rate, "1") == 0;
  runtime = rt;
  const unsigned long rate = frame_rate ? std::strtoul(frame_rate, nullptr, 10) : 0;
  if (rate <= kDefaultFrameRate || (enabled && std::strcmp(enabled, "0") == 0)) return;
  auto* gpu = static_cast<rex::graphics::GraphicsSystem*>(rt->graphics_system());
  if (!gpu) return;
  race_rate = static_cast<uint32_t>(rate);
  current_rate = kDefaultFrameRate;
  gpu->SetVblankRate(current_rate);
  std::fprintf(stderr, "FM4 vblank: 60 Hz, %u Hz while a race scenario sets the frame rate\n",
               race_rate);
}

// CRenderThreadLink::PushDynamicRenderSettings(this + 8,
// TReference<CDynamicRenderSettings>*).
REX_EXTERN(__imp__sub_82442600);
REX_HOOK_RAW(sub_82442600) {
  {
    const uint32_t settings = Load(base, ctx.r4.u32);
    const bool valid = settings && Load(base, settings) == kSettingsVtable;
    std::lock_guard lock(mutex);
    frame_rates.push_back(valid ? Load(base, settings + kSettingsFrameRate) : kDefaultFrameRate);
    Apply("push", valid ? Name(base, settings) : "?");
  }
  __imp__sub_82442600(ctx, base);
}

// CRenderThreadLink::PopDynamicRenderSettings(this + 8).
REX_EXTERN(__imp__sub_82438A38);
REX_HOOK_RAW(sub_82438A38) {
  {
    std::lock_guard lock(mutex);
    if (!frame_rates.empty()) frame_rates.pop_back();
    Apply("pop", "");
  }
  __imp__sub_82438A38(ctx, base);
}
