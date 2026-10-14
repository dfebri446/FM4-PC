// Track streaming fix: the GPU data of a track section (for example 1.5 MB for
// the start straight of Bernese Alps) is allocated from the title's "sGPU" heap
// (about 120 MB) when the section loads. Load order varies here with host
// thread scheduling, and at the race start the heap can be fragmented: about
// 18 MB free, but no block for a 1.8 MB section. The loader then drops the
// whole section and only streams it again much later, so the road is missing
// (STATUS.txt, RTV missing road). The title's other GPU heaps ("nGPU" and the
// front-end "pGPU") fall back to the "fGPU" heap when full, and fGPU had about
// 33 MB free at such a failure; sGPU has no fallback. When a loader section
// allocation fails, give sGPU the same fallback and allocate again.
// FM4_STREAMING_FALLBACK=0 disables it.
#include <rex/hook.h>
#include <rex/memory.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
// The pool table of the title's memory manager: six 556-byte pools, each with
// a CPU heap at +16 and a GPU heap at +284; a heap's fallback pool id is at
// +236 (6: none). Pool 0 is the streaming pool, pool id 0x22 the fGPU heap.
constexpr uint32_t kStreamingGpuFallback = 0x82D9B7C0 + 284 + 236;
constexpr uint32_t kNoFallback = 6;
constexpr uint32_t kFrontEndGpuPool = 0x22;

bool FallbackEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("FM4_STREAMING_FALLBACK");
    return !value || std::strcmp(value, "0") != 0;
  }();
  return enabled;
}
}  // namespace

// The section allocation of a loader request (request): its failure aborts the
// request. Only GPU allocations made from here use the fallback.
thread_local bool fm4_streaming_section_allocation = false;
void FM4StreamTraceSectionAllocation(const uint8_t* base, uint32_t request, uint32_t result);

REX_EXTERN(__imp__sub_82935F80);
REX_HOOK_RAW(sub_82935F80) {
  const uint32_t request = ctx.r3.u32;
  fm4_streaming_section_allocation = true;
  __imp__sub_82935F80(ctx, base);
  fm4_streaming_section_allocation = false;
  FM4StreamTraceSectionAllocation(base, request, ctx.r3.u32);
}

// GPU allocator of loader sections (allocator, bytes).
REX_EXTERN(__imp__sub_82882C78);
REX_HOOK_RAW(sub_82882C78) {
  const PPCRegister allocator = ctx.r3, bytes = ctx.r4;
  __imp__sub_82882C78(ctx, base);
  if (ctx.r3.u32 || !fm4_streaming_section_allocation || !FallbackEnabled())
    return;
  uint8_t* fallback = base + kStreamingGpuFallback;
  if (rex::memory::load_and_swap<uint32_t>(fallback) != kNoFallback)
    return;
  rex::memory::store_and_swap<uint32_t>(fallback, kFrontEndGpuPool);
  ctx.r3 = allocator;
  ctx.r4 = bytes;
  __imp__sub_82882C78(ctx, base);
  std::fprintf(stderr,
               "FM4 streaming: sGPU allocation of 0x%X bytes failed; with the fGPU fallback %s\n",
               bytes.u32, ctx.r3.u32 ? "it succeeded" : "it failed too");
}
