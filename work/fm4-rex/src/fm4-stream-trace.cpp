// Optional trace of the title's resource loader (FM4_STREAM_TRACE=<path>), to find
// why a track data item can stop streaming after its first chunks (STATUS.txt,
// RTV missing road). One line per call of the loader step and of the checks that
// set its failure flags: "<steady ms> <host thread> <kind> <request> ...", where
// kinds are "step" (entry and exit: the flag words at +84 and +88 of the
// request), "check1"/"check2" (the checks that set flag bits 1 and 4, with
// their result), "finish" (the path taken once a flag is set), "sread" (the
// buffered archive stream read), "readfile"/"overlapped" (its ReadFile and
// GetOverlappedResult calls, by OVERLAPPED address) and "inflate8"/"inflate21"
// (its decompressors).
#include <rex/hook.h>
#include <rex/memory.h>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace {
std::FILE* Output() {
  static std::FILE* output = [] {
    const char* path = std::getenv("FM4_STREAM_TRACE");
    return path && *path ? std::fopen(path, "w") : nullptr;
  }();
  return output;
}

uint32_t Load(const uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}

void Line(const char* kind, uint32_t request, const char* format, ...) {
  std::FILE* output = Output();
  if (!output)
    return;
  static std::mutex mutex;
  char details[2048];
  va_list args;
  va_start(args, format);
  std::vsnprintf(details, sizeof(details), format, args);
  va_end(args);
  std::lock_guard<std::mutex> lock(mutex);
  std::fprintf(output, "%lld %lu %s %08X %s\n",
               static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count()),
               GetCurrentThreadId(), kind, request, details);
  std::fflush(output);
}
}  // namespace

REX_EXTERN(__imp__sub_82936720);
REX_HOOK_RAW(sub_82936720) {
  const uint32_t request = ctx.r4.u32;
  if (Output()) {
    Line("step", request, "in cancel=%u f84=%08X f88=%08X lr=%08X", ctx.r5.u32,
         Load(base, request + 84), Load(base, request + 88), uint32_t(ctx.lr));
  }
  __imp__sub_82936720(ctx, base);
  if (Output()) {
    Line("step", request, "out f84=%08X f88=%08X", Load(base, request + 84),
         Load(base, request + 88));
  }
}

REX_EXTERN(__imp__sub_82935BF8);
REX_HOOK_RAW(sub_82935BF8) {
  const uint32_t request = ctx.r3.u32;
  __imp__sub_82935BF8(ctx, base);
  if (Output())
    Line("check1", request, "-> %08X", ctx.r3.u32);
}

// The section allocation of a request (header at +0: the section count at +77,
// 40-byte section entries from +80, the allocation at +20 of an entry), hooked
// in fm4-streaming-fixes.cpp; on failure also one "section" line per entry
// with its raw words.
// Set by the hook in fm4-streaming-fixes.cpp, which calls the function below.
extern thread_local bool fm4_streaming_section_allocation;

void FM4StreamTraceSectionAllocation(const uint8_t* base, uint32_t request, uint32_t result) {
  if (!Output())
    return;
  Line("check2", request, "-> %08X", result);
  if (!result)
    return;
  const uint32_t header = Load(base, request);
  const uint32_t sections = base[header + 77];
  for (uint32_t i = 0; i < sections && i < 16; ++i) {
    const uint32_t entry = header + (i + 2) * 40;
    Line("section", request, "%u: %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X", i,
         Load(base, entry), Load(base, entry + 4), Load(base, entry + 8), Load(base, entry + 12),
         Load(base, entry + 16), Load(base, entry + 20), Load(base, entry + 24),
         Load(base, entry + 28), Load(base, entry + 32), Load(base, entry + 36));
  }
}

// Looks up the allocator of a section (entry, &allocator): the allocator, a
// table of entry points whose Alloc is at +12. Only from the section
// allocation above, where the second argument is a stack slot.
REX_EXTERN(__imp__sub_82934F78);
REX_HOOK_RAW(sub_82934F78) {
  const uint32_t entry = ctx.r3.u32, out = ctx.r4.u32;
  __imp__sub_82934F78(ctx, base);
  if (!Output() || !fm4_streaming_section_allocation)
    return;
  const uint32_t allocator = Load(base, out);
  Line("allocator", entry, "-> %08X table=%08X %08X %08X %08X %08X %08X type=%02X%02X%02X%02X",
       allocator, allocator ? Load(base, allocator) : 0, allocator ? Load(base, allocator + 4) : 0,
       allocator ? Load(base, allocator + 8) : 0, allocator ? Load(base, allocator + 12) : 0,
       allocator ? Load(base, allocator + 16) : 0, allocator ? Load(base, allocator + 20) : 0,
       base[entry + 8], base[entry + 9], base[entry + 10], base[entry + 11]);
}

// The buffered archive stream read (this, destination, bytes): two prefetch
// slots of up to 32 KB, then a copy or a decompression into the destination.
// Logs the request, the compression method at +100, the stream position at +96
// and its end at +104 before and after, and the bytes read.
REX_EXTERN(__imp__sub_829708D0);
REX_HOOK_RAW(sub_829708D0) {
  const uint32_t stream = ctx.r3.u32;
  const uint32_t requested = ctx.r5.u32;
  uint32_t position = 0, end = 0, method = 0;
  if (Output()) {
    position = Load(base, stream + 96);
    end = Load(base, stream + 104);
    method = rex::memory::load_and_swap<uint16_t>(base + stream + 100);
  }
  __imp__sub_829708D0(ctx, base);
  if (Output()) {
    Line("sread", stream, "bytes=%X -> %X method=%u pos=%X->%X end=%X file=%X", requested,
         ctx.r3.u32, method, position, Load(base, stream + 96), end, Load(base, stream + 196));
  }
}

// ReadFile and GetOverlappedResult as the stream calls them.
REX_EXTERN(__imp__sub_8293AF48);
REX_HOOK_RAW(sub_8293AF48) {
  const uint32_t overlapped = ctx.r7.u32, length = ctx.r5.u32;
  __imp__sub_8293AF48(ctx, base);
  if (Output())
    Line("readfile", overlapped, "len=%X -> %u", length, ctx.r3.u32);
}

REX_EXTERN(__imp__sub_82938B08);
REX_HOOK_RAW(sub_82938B08) {
  const uint32_t overlapped = ctx.r4.u32, bytes_address = ctx.r5.u32;
  __imp__sub_82938B08(ctx, base);
  if (Output()) {
    Line("overlapped", overlapped, "-> %u bytes=%X internal=%08X", ctx.r3.u32,
         Load(base, bytes_address), Load(base, overlapped));
  }
}

// The decompressors of methods 8 and 21.
REX_EXTERN(__imp__sub_82C9E178);
REX_HOOK_RAW(sub_82C9E178) {
  const uint32_t context = ctx.r3.u32;
  __imp__sub_82C9E178(ctx, base);
  if (Output())
    Line("inflate8", context, "-> %d", int32_t(ctx.r3.u32));
}

REX_EXTERN(__imp__sub_82C79D60);
REX_HOOK_RAW(sub_82C79D60) {
  const uint32_t context = ctx.r3.u32;
  __imp__sub_82C79D60(ctx, base);
  if (Output())
    Line("inflate21", context, "-> %d", int32_t(ctx.r3.u32));
}

// The title's pooled allocator (manager, pool, bytes, alignment, flags), behind
// both section allocators: "poolfail" with the pool table for failures of a
// loader section allocation, the pool table on the first allocation of each
// pool, and every 4096th allocation per pool a "pool" line with the running
// totals.
REX_EXTERN(__imp__sub_822B1920);
REX_HOOK_RAW(sub_822B1920) {
  const uint32_t manager = ctx.r3.u32, pool = ctx.r4.u32, bytes = ctx.r5.u32,
                 alignment = ctx.r6.u32;
  const uint32_t lr = uint32_t(ctx.lr);
  __imp__sub_822B1920(ctx, base);
  if (!Output())
    return;
  static std::mutex mutex;
  static uint64_t requested[64] = {}, count[64] = {};
  uint64_t total = 0, calls = 0;
  {
    std::lock_guard<std::mutex> lock(mutex);
    const uint32_t slot = pool & 63;
    requested[slot] += bytes;
    calls = ++count[slot];
    total = requested[slot];
  }
  // The pool table: six 556-byte pools from 0x82D9B7C0 (index in the low four
  // bits of the pool id), dumped on failures and on the first allocation.
  const uint32_t index = pool & 15;
  auto dump = [&](const char* kind) {
    if (index >= 6)
      return;
    const uint32_t entry = 0x82D9B7C0 + index * 556;
    std::string words;
    char word[10];
    for (uint32_t offset = 0; offset < 556; offset += 4) {
      std::snprintf(word, sizeof(word), " %08X", Load(base, entry + offset));
      words += word;
    }
    Line(kind, entry, "pool=%u%s", pool, words.c_str());
  };
  // Failures are common (the title probes pools); only those of a loader
  // section allocation, which drop the request, are logged.
  if (!ctx.r3.u32 && fm4_streaming_section_allocation) {
    Line("poolfail", manager, "pool=%u bytes=%X align=%X lr=%08X", pool, bytes, alignment, lr);
    for (uint32_t other = 0; other < 6; ++other) {
      const uint32_t entry = 0x82D9B7C0 + other * 556;
      std::string words;
      char word[10];
      for (uint32_t offset = 0; offset < 556; offset += 4) {
        std::snprintf(word, sizeof(word), " %08X", Load(base, entry + offset));
        words += word;
      }
      Line("pooltable", entry, "pool=%u%s", other, words.c_str());
    }
  } else if (ctx.r3.u32 && calls == 1) {
    dump("pooltable");
  } else if (ctx.r3.u32 && calls % 4096 == 0) {
    Line("pool", manager, "pool=%u allocations=%llu requested=%llX", pool,
         static_cast<unsigned long long>(calls), static_cast<unsigned long long>(total));
  }
}

REX_EXTERN(__imp__sub_82936300);
REX_HOOK_RAW(sub_82936300) {
  if (Output()) {
    Line("finish", ctx.r4.u32, "arg=%08X f84=%08X lr=%08X", ctx.r5.u32, Load(base, ctx.r4.u32 + 84),
         uint32_t(ctx.lr));
  }
  __imp__sub_82936300(ctx, base);
}
