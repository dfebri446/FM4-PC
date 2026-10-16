// FM4 developer options. The title keeps its command line parameters
// (CForzaCommandLineParameters, a singleton built by sub_8237D468: launch,
// launchcarexperience, nohud, ...) and the code that parses them, but the
// retail build never fills them: the loader of GAME:\cmdline.txt is compiled
// out and the title does not import ExLoadedCommandLine, so every option keeps
// its default. FM4_GAME_CMDLINE="<option> <option>=<value> ..." feeds them:
// after the constructor and after the defaults (sub_823E7A10), each option goes
// through the title's own parse methods, as a cmdline.txt line would. A bare
// name means name=1 (the parser toggles a bare switch, and some switches are
// parsed by two methods). Which options the retail code still reads, and which
// fm4-dev-features.cpp rebuilds, is in STATUS.txt.
#include <rex/hook.h>
#include <rex/memory.h>
#include <rex/ppc/stack.h>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

// CForzaCommandLineParameters parse methods (vtable 0x8200ED8C slots 2-5), each
// (this, const char* option): the option names they know get the value.
REX_EXTERN(sub_823E2388);
REX_EXTERN(sub_823E2408);
REX_EXTERN(sub_82400E18);
REX_EXTERN(sub_823E80D0);

namespace {
std::vector<std::string> options;

void Apply(PPCContext& ctx, uint8_t* base, uint32_t self, const char* when) {
  static PPCFunc* const parsers[] = {sub_823E2388, sub_823E2408, sub_82400E18, sub_823E80D0};
  const PPCContext saved = ctx;
  for (const auto& option : options) {
    rex::ppc::stack_guard guard(ctx);
    const uint32_t text = rex::ppc::stack_push_string(ctx, base, option.c_str());
    ctx.r1.u32 = (ctx.r1.u32 - 128) & ~15u;  // a frame for the callee below the string
    for (PPCFunc* parse : parsers) {
      ctx.r3.u32 = self;
      ctx.r4.u32 = text;
      parse(ctx, base);
    }
  }
  ctx = saved;
  std::fprintf(stderr, "FM4 developer options applied after the %s of 0x%08X\n", when, self);
}
}  // namespace

void FM4InstallCommandLine() {
  const char* text = std::getenv("FM4_GAME_CMDLINE");
  if (!text) return;
  std::istringstream stream(text);
  for (std::string option; stream >> option;)
    options.push_back(option.find('=') == std::string::npos ? option + "=1" : option);
  if (options.empty()) return;
  std::string joined;
  for (const auto& option : options) joined += ' ' + option;
  std::fprintf(stderr, "FM4 developer options:%s\n", joined.c_str());
}

// CForzaCommandLineParameters constructor (this).
REX_EXTERN(__imp__sub_823E2338);
REX_HOOK_RAW(sub_823E2338) {
  const uint32_t self = ctx.r3.u32;
  __imp__sub_823E2338(ctx, base);
  if (!options.empty()) Apply(ctx, base, self, "constructor");
}

// CForzaCommandLineParameters defaults (this; vtable slot 0).
REX_EXTERN(__imp__sub_823E7A10);
REX_HOOK_RAW(sub_823E7A10) {
  const uint32_t self = ctx.r3.u32;
  __imp__sub_823E7A10(ctx, base);
  if (!options.empty()) Apply(ctx, base, self, "defaults");
}
