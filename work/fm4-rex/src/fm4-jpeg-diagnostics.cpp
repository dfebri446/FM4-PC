#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <rex/hook.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <vector>

REX_EXTERN(__imp__sub_82714208);
REX_EXTERN(__imp__sub_82714388);
REX_EXTERN(__imp__sub_82BB96E0);
REX_EXTERN(__imp__sub_82859218);
REX_EXTERN(__imp__sub_826DEF78);
REX_EXTERN(__imp__sub_8273B290);
REX_EXTERN(__imp__sub_822B0788);

namespace {
// Match generated REX_PHYS_HOST_OFFSET on Windows, including GPU writeback
// pointers in the E0000000-FFFFFFFF physical-memory alias.
uint8_t* PhotoGuestAddress(uint8_t* base, uint32_t address) {
  return base + address + (address >= 0xE0000000u ? 0x1000u : 0u);
}
thread_local int photo_trace_call = -1;
void DumpPhotoRange(uint8_t* base, const char* stage, uint32_t address, uint64_t length) {
  const char* directory = std::getenv("FM4_PHOTO_ENCODER_TRACE");
  if (!directory || !*directory || photo_trace_call < 0 || !address || !length ||
      length > 16 * 1024 * 1024 || uint64_t(address) + length > 0x100000000ull) return;
  try {
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    SIZE_T got = 0;
    const bool complete = ReadProcessMemory(GetCurrentProcess(), PhotoGuestAddress(base, address), bytes.data(),
                                            bytes.size(), &got) && got == bytes.size();
    size_t nonzero = 0;
    if (complete) for (auto byte : bytes) nonzero += byte != 0;
    std::filesystem::path dir(directory);
    std::filesystem::create_directories(dir);
    const auto stem = "surface-" + std::to_string(photo_trace_call) + "-" + stage;
    if (complete) {
      std::ofstream raw(dir / (stem + ".bin"), std::ios::binary);
      raw.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    std::ofstream info(dir / (stem + ".json"));
    info << "{\"address\":" << address << ",\"length\":" << length
         << ",\"complete\":" << (complete ? "true" : "false")
         << ",\"nonzero_bytes\":" << nonzero << "}\n";
  } catch (...) {
    std::fprintf(stderr, "PHOTO_SURFACE diagnostic snapshot failed\n");
  }
}
uint32_t ReadGuest32(uint8_t* base, uint32_t address) {
  uint32_t result = 0;
  SIZE_T bytes = 0;
  if (!ReadProcessMemory(GetCurrentProcess(), PhotoGuestAddress(base, address), &result, 4, &bytes) || bytes != 4)
    return 0;
  return _byteswap_ulong(result);
}
void TraceJPEG(PPCContext& ctx, uint8_t* base, PPCFunc* original, uint32_t address) {
  if (!std::getenv("FM4_CAPTURE_DIR")) { original(ctx, base); return; }
  const auto sp = ctx.r1.u32;
  const uint64_t saved[] = {ctx.r27.u64, ctx.r28.u64, ctx.r29.u64, ctx.r30.u64, ctx.r31.u64};
  const auto object = ctx.r3.u32;
  const auto source = ReadGuest32(base, object + 24);
  const auto callback = ReadGuest32(base, source + 12);
  static thread_local unsigned calls = 0;
  const auto call = ++calls;
  if (call <= 12)
    std::fprintf(stderr, "JPEG enter fn=%08x tid=%u n=%u object=%08x sp=%08x r31=%llx source=%08x fill=%08x lr=%llx\n",
                 address, unsigned(GetCurrentThreadId()), call, object, sp, saved[4], source, callback, ctx.lr);
  original(ctx, base);
  const bool changed = sp != ctx.r1.u32 || saved[0] != ctx.r27.u64 || saved[1] != ctx.r28.u64 ||
                       saved[2] != ctx.r29.u64 || saved[3] != ctx.r30.u64 || saved[4] != ctx.r31.u64;
  if (call <= 12 || changed) {
    std::fprintf(stderr, "JPEG return fn=%08x tid=%u n=%u abi_changed=%u result=%llx sp=%08x->%08x r27=%llx->%llx r28=%llx->%llx r29=%llx->%llx r30=%llx->%llx r31=%llx->%llx saved_r31=%08x%08x lr=%llx\n",
                 address, unsigned(GetCurrentThreadId()), call, unsigned(changed), ctx.r3.u64, sp, ctx.r1.u32,
                 saved[0], ctx.r27.u64, saved[1], ctx.r28.u64, saved[2], ctx.r29.u64,
                 saved[3], ctx.r30.u64, saved[4], ctx.r31.u64,
                 ReadGuest32(base, sp - 16), ReadGuest32(base, sp - 12), ctx.lr);
    std::fflush(stderr);
  }
}
}

REX_HOOK_RAW(sub_82714208) { TraceJPEG(ctx, base, __imp__sub_82714208, 0x82714208); }
REX_HOOK_RAW(sub_82714388) { TraceJPEG(ctx, base, __imp__sub_82714388, 0x82714388); }

REX_HOOK_RAW(sub_82859218) {
  const char* directory = std::getenv("FM4_PHOTO_ENCODER_TRACE");
  static std::atomic<unsigned> calls{0};
  const int previous = photo_trace_call;
  const unsigned call = directory && *directory ? calls.fetch_add(1) : 8;
  photo_trace_call = call < 8 ? static_cast<int>(call) : -1;
  struct Restore { int previous; ~Restore() { photo_trace_call = previous; } } restore{previous};
  __imp__sub_82859218(ctx, base);
}

REX_HOOK_RAW(sub_826DEF78) {
  const uint32_t texture = ctx.r3.u32, output = ctx.r5.u32;
  if (photo_trace_call >= 0) DumpPhotoRange(base, "lock-texture-before", texture, 64);
  __imp__sub_826DEF78(ctx, base);
  if (photo_trace_call >= 0) {
    DumpPhotoRange(base, "lock-result", output, 8);
    DumpPhotoRange(base, "lock-texture-after", texture, 64);
    std::fprintf(stderr, "PHOTO_LOCK n=%d texture=%08x result=%08x pitch=%u pixels=%08x\n",
                 photo_trace_call, texture, ctx.r3.u32, ReadGuest32(base, output), ReadGuest32(base, output + 4));
    std::fflush(stderr);
  }
}

// Observe the game's resource-fence wait without changing its timeout or result.
REX_HOOK_RAW(sub_822B0788) {
  if (photo_trace_call < 0) { __imp__sub_822B0788(ctx, base); return; }
  const uint32_t device = ctx.r3.u32, wanted = ctx.r4.u32;
  const uint32_t pointer = ReadGuest32(base, device + 11024);
  const uint32_t before = ReadGuest32(base, pointer);
  const uint32_t issued_before = ReadGuest32(base, device + 11036);
  LARGE_INTEGER begin{}, end{}, frequency{};
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&begin);
  __imp__sub_822B0788(ctx, base);
  QueryPerformanceCounter(&end);
  const uint32_t after = ReadGuest32(base, pointer);
  const uint32_t issued_after = ReadGuest32(base, device + 11036);
  std::fprintf(stderr,
      "PHOTO_FENCE n=%d device=%08x pointer=%08x wanted=%u before=%u after=%u issued_before=%u issued_after=%u pending_after=%u ticks_begin=%lld ticks_end=%lld frequency=%lld\n",
      photo_trace_call, device, pointer, wanted, before, after, issued_before, issued_after,
      unsigned(wanted && uint32_t(issued_after - wanted) < uint32_t(issued_after - after)),
      begin.QuadPart, end.QuadPart, frequency.QuadPart);
  std::fflush(stderr);
}

REX_HOOK_RAW(sub_8273B290) {
  const uint32_t destination = ctx.r3.u32, pitch = ctx.r4.u32, source = ctx.r6.u32;
  const uint32_t width = ctx.r7.u32, height = ctx.r8.u32;
  const bool bounded = width <= 4096 && height <= 2160 && pitch <= 65536;
  if (photo_trace_call >= 0 && bounded) {
    std::fprintf(stderr, "PHOTO_COPY n=%d dst=%08x dst_pitch=%u r5=%08x src=%08x width=%u height=%u r9=%08x\n",
                 photo_trace_call, destination, pitch, ctx.r5.u32, source, width, height, ctx.r9.u32);
    DumpPhotoRange(base, "copy-source", source, uint64_t((width + 31) & ~31u) * ((height + 31) & ~31u) * 4);
  }
  __imp__sub_8273B290(ctx, base);
  if (photo_trace_call >= 0 && bounded) {
    DumpPhotoRange(base, "copy-output", destination, uint64_t(pitch) * height);
    std::fflush(stderr);
  }
}

// Opt-in observation of the photo encoder's actual input. Never changes guest
// registers or memory, and caps both the number and size of snapshots.
REX_HOOK_RAW(sub_82BB96E0) {
  const char* directory = std::getenv("FM4_PHOTO_ENCODER_TRACE");
  static std::atomic<unsigned> calls{0};
  if (directory && *directory) {
    const unsigned call = calls.fetch_add(1);
    if (call < 8) {
      try {
        const uint32_t object = 0;
        const uint32_t source = ctx.r3.u32;
        const uint32_t width = ctx.r4.u32;
        const uint32_t height = ctx.r5.u32;
        const uint32_t left = 0;
        const uint32_t top = 0;
        const uint32_t stride = ctx.r6.u32;
        // JPEG v8 wrapper passes RGB rows directly to write_scanlines.
        const uint64_t start = source;
        const bool valid = source && width && width <= 4096 && height && height <= 2160 &&
            stride >= width * 3 && stride <= 65536 &&
            start + uint64_t(height - 1) * stride + width * 3 <= 0x100000000ull;
        std::vector<uint8_t> rgb;
        bool complete = valid;
        if (valid) {
          rgb.resize(size_t(width) * height * 3);
          std::vector<uint8_t> row(size_t(width) * 3);
          for (uint32_t y = 0; y < height; ++y) {
            SIZE_T bytes = 0;
            const uint32_t row_address = uint32_t(start + uint64_t(y) * stride);
            if (!ReadProcessMemory(GetCurrentProcess(), PhotoGuestAddress(base, row_address),
                                   row.data(), row.size(), &bytes) || bytes != row.size()) {
              complete = false; break;
            }
            for (uint32_t x = 0; x < width; ++x) {
              for (unsigned c = 0; c < 3; ++c)
                rgb[(size_t(y) * width + x) * 3 + c] = row[size_t(x) * 3 + c];
            }
          }
        }
        std::filesystem::path dir(directory);
        std::filesystem::create_directories(dir);
        const std::string stem = "encoder-" + std::to_string(call);
        size_t nonzero = 0;
        if (complete) {
          for (auto byte : rgb) nonzero += byte != 0;
          std::ofstream pixels(dir / (stem + ".ppm"), std::ios::binary);
          pixels << "P6\n" << width << ' ' << height << "\n255\n";
          pixels.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
        }
        std::ofstream info(dir / (stem + ".json"));
        info << "{\"call\":" << call << ",\"object\":" << object << ",\"source\":" << source
             << ",\"width\":" << width << ",\"height\":" << height << ",\"left\":" << left
             << ",\"top\":" << top << ",\"stride\":" << stride << ",\"complete\":"
             << (complete ? "true" : "false") << ",\"nonzero_rgb_bytes\":" << nonzero << "}\n";
        std::fprintf(stderr, "PHOTO_ENCODER n=%u source=%08x size=%ux%u stride=%u complete=%u nonzero=%zu\n",
                     call, source, width, height, stride, unsigned(complete), nonzero);
        std::fflush(stderr);
      } catch (...) {
        std::fprintf(stderr, "PHOTO_ENCODER diagnostic snapshot failed\n");
      }
    }
  }
  __imp__sub_82BB96E0(ctx, base);
}
