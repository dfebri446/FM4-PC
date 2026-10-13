#pragma once
#include <rex/ui/presenter.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <renderdoc/renderdoc_app.h>
#endif

namespace rex { class Runtime; }
void FM4QueueGpuProbe(rex::Runtime*, std::filesystem::path, unsigned);

// A host Present may only copy an already rendered guest image. Opt-in bounded
// interval captures also include the guest command lists between host Presents.
inline void FM4CaptureGpuCommands(std::stop_token stop, const std::filesystem::path& directory) {
#if defined(_WIN32)
  const auto request = directory / "renderdoc-request.txt";
  if (!std::filesystem::exists(request)) return;
  unsigned duration_ms = 0;
  { std::ifstream input(request); input >> duration_ms; }
  std::filesystem::remove(request);
  std::ofstream result(directory / "renderdoc-result.txt");
  if (duration_ms < 50 || duration_ms > 1000) {
    result << "error=duration must be 50..1000 milliseconds\n";
    return;
  }
  auto module = GetModuleHandleW(L"renderdoc.dll");
  auto get_api = module ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI")) : nullptr;
  RENDERDOC_API_1_0_0* api = nullptr;
  if (!get_api || !get_api(eRENDERDOC_API_Version_1_0_0, reinterpret_cast<void**>(&api)) || !api) {
    result << "error=RenderDoc is not attached\n";
    return;
  }
  if (api->IsFrameCapturing()) {
    result << "error=another capture is active\n";
    return;
  }
  const auto before = api->GetNumCaptures();
  api->StartFrameCapture(nullptr, nullptr);
  if (!api->IsFrameCapturing()) {
    result << "error=capture did not start\n";
    return;
  }
  for (unsigned elapsed = 0; elapsed < duration_ms && !stop.stop_requested(); elapsed += 10)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const auto success = api->EndFrameCapture(nullptr, nullptr);
  result << "success=" << success << "\nduration_ms=" << duration_ms << '\n';
  if (success && api->GetNumCaptures() > before) {
    uint32_t size = 0;
    api->GetCapture(before, nullptr, &size, nullptr);
    std::string path(size + 1, '\0');
    if (api->GetCapture(before, path.data(), &size, nullptr))
      result << "path=" << path.c_str() << '\n';
  }
#endif
}

// Diagnostic GPU readback only. No desktop capture or input injection.
inline void FM4CaptureFrames(std::stop_token stop, rex::ui::Presenter* presenter,
                             const std::filesystem::path& directory, rex::Runtime* runtime) {
  unsigned saved = 0;
  unsigned archive_limit = 450;
  if (const char* configured = std::getenv("FM4_CAPTURE_ARCHIVE_LIMIT")) {
    char* end = nullptr;
    const auto value = std::strtoul(configured, &end, 10);
    if (end != configured && *end == '\0' && value <= 450)
      archive_limit = static_cast<unsigned>(value);
  }
  try {
    std::filesystem::create_directories(directory);
    while (!stop.stop_requested()) {
      for (unsigned tick = 0; tick < (saved ? 20u : 5u); ++tick) {
        if (stop.stop_requested()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (stop.stop_requested()) return;
      FM4CaptureGpuCommands(stop, directory);
      if (stop.stop_requested()) return;
      rex::ui::RawImage frame;
      if (!presenter->CaptureGuestOutput(frame)) continue;
      if (!frame.width || !frame.height || frame.width > 8192 || frame.height > 8192 ||
          frame.stride < size_t(frame.width) * 4 ||
          frame.data.size() < size_t(frame.height - 1) * frame.stride + size_t(frame.width) * 4)
        continue;
      char name[32];
      ++saved;
      // Keep the original archival bound, but retain one fresh image for long
      // user-driven sessions. A full archive must not end live observation.
      if (saved <= archive_limit)
        std::snprintf(name, sizeof(name), "frame-%03u.ppm", saved);
      else
        std::snprintf(name, sizeof(name), "live-latest.ppm");
      const auto target = directory / name;
      const auto temporary = directory / "capture-in-progress.tmp";
      std::ofstream output(temporary, std::ios::binary);
      output << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
      for (uint32_t y = 0; y < frame.height; ++y)
        for (uint32_t x = 0; x < frame.width; ++x)
          output.write(reinterpret_cast<const char*>(frame.data.data() + size_t(y) * frame.stride + size_t(x) * 4), 3);
      output.close();
      if (!output) throw std::runtime_error("Cannot write GPU capture");
#if defined(_WIN32)
      bool published = false;
      for (unsigned retry = 0; retry < 25 && !stop.stop_requested(); ++retry) {
        if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
          published = true;
          break;
        }
        if (GetLastError() != ERROR_SHARING_VIOLATION && GetLastError() != ERROR_ACCESS_DENIED)
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (stop.stop_requested()) return;
      if (!published)
        throw std::runtime_error("Cannot publish GPU capture");
#else
      std::filesystem::rename(temporary, target);
#endif
      FM4QueueGpuProbe(runtime, directory, saved);
    }
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FM4 GPU capture failed: %s\n", error.what());
  }
}
