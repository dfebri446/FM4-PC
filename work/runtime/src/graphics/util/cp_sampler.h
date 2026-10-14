#pragma once
// Optional sampling profiler for the GPU command-processor thread. When
// REX_CP_PROFILE=<path> is set, a background thread suspends the calling
// thread about every millisecond, unwinds its stack with the x64 unwind data
// of the loaded modules and counts each stack. Sampling starts after
// REX_CP_PROFILE_DELAY seconds (default 0) and lasts REX_CP_PROFILE_SECONDS
// (default 30); the stacks are then written as lines of
//   <count> <module>+0x<offset> ...   (innermost frame first)
// for offline symbolization. Diagnostics only; does nothing otherwise.

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rex::graphics::cp_sampler {

inline void Start() {
  const char* path = std::getenv("REX_CP_PROFILE");
  if (!path || !*path) return;
  const char* delay_env = std::getenv("REX_CP_PROFILE_DELAY");
  const char* seconds_env = std::getenv("REX_CP_PROFILE_SECONDS");
  const double delay = delay_env ? std::atof(delay_env) : 0.0;
  const double seconds = seconds_env ? std::atof(seconds_env) : 30.0;
  HANDLE target = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target,
                       THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                       FALSE, 0)) {
    return;
  }
  std::thread([target, output_path = std::string(path), delay, seconds]() {
    constexpr uint32_t kMaxFrames = 24;
    std::this_thread::sleep_for(std::chrono::duration<double>(delay));
    timeBeginPeriod(1);
    std::unordered_map<std::string, uint32_t> stacks;
    uint64_t pcs[kMaxFrames];
    uint32_t samples = 0, failed = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
      Sleep(1);
      if (SuspendThread(target) == DWORD(-1)) break;
      CONTEXT context = {};
      context.ContextFlags = CONTEXT_FULL;
      uint32_t frame_count = 0;
      if (GetThreadContext(target, &context)) {
        // No allocation while the target is suspended: it may hold the heap lock.
        while (frame_count < kMaxFrames && context.Rip) {
          pcs[frame_count++] = context.Rip;
          DWORD64 image_base = 0;
          PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
          if (!function) {
            // Leaf function without unwind data: the return address is on top.
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += 8;
          } else {
            PVOID handler_data = nullptr;
            DWORD64 establisher_frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context,
                             &handler_data, &establisher_frame, nullptr);
          }
        }
      }
      ResumeThread(target);
      if (!frame_count) {
        ++failed;
        continue;
      }
      ++samples;
      ++stacks[std::string(reinterpret_cast<const char*>(pcs), frame_count * sizeof(uint64_t))];
    }
    timeEndPeriod(1);
    // Resolve addresses to module + offset.
    std::map<uint64_t, std::string> module_names;  // Base -> file name.
    const auto describe = [&](uint64_t pc) {
      HMODULE module = nullptr;
      if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(pc), &module)) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "?+0x%llx", static_cast<unsigned long long>(pc));
        return std::string(buffer);
      }
      const uint64_t base = reinterpret_cast<uint64_t>(module);
      auto it = module_names.find(base);
      if (it == module_names.end()) {
        char name[MAX_PATH] = {};
        GetModuleFileNameA(module, name, MAX_PATH);
        std::string file(name);
        const size_t slash = file.find_last_of("\\/");
        it = module_names.emplace(base, slash == std::string::npos ? file : file.substr(slash + 1))
                 .first;
      }
      char buffer[64];
      snprintf(buffer, sizeof(buffer), "+0x%llx", static_cast<unsigned long long>(pc - base));
      return it->second + buffer;
    };
    std::ofstream output(output_path);
    output << "# samples " << samples << " failed " << failed << "\n";
    for (const auto& [key, count] : stacks) {
      output << count;
      const auto* frames = reinterpret_cast<const uint64_t*>(key.data());
      for (size_t i = 0; i < key.size() / sizeof(uint64_t); ++i) output << ' ' << describe(frames[i]);
      output << '\n';
    }
    CloseHandle(target);
  }).detach();
}

}  // namespace rex::graphics::cp_sampler

#else

namespace rex::graphics::cp_sampler {
inline void Start() {}
}  // namespace rex::graphics::cp_sampler

#endif
