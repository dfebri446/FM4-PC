#include <rex/runtime.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/entry.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

// Optional visual workaround documented by xenia-canary/game-patches#398.
// This suppresses the faulty shadow-map pass outside the cockpit; it is not
// a renderer fix. Keep UI and global scenarios intact (the global variant
// has been reported to hang career prize-car colour selection).
void FM4MountRenderOverrides(rex::Runtime* runtime) {
  const char* enabled = std::getenv("FM4_SHADOW_WORKAROUND");
  if (!enabled || std::string(enabled) != "1") return;
  const auto source = runtime->game_data_root() / "media" / "RenderScenarios";
  const auto destination = runtime->cache_root() / "fm4-render-overrides";
  std::filesystem::create_directories(destination);
  unsigned changed = 0;
  for (const auto& entry : std::filesystem::directory_iterator(source)) {
    if (!entry.is_regular_file()) continue;
    std::ifstream input(entry.path(), std::ios::binary);
    std::string xml{std::istreambuf_iterator<char>(input), {}};
    if (!input || xml.empty()) throw std::runtime_error("Unable to read render scenario");
    const auto name = entry.path().filename().string();
    const bool race = name.find("-Car_") != std::string::npos ||
                      name.starts_with("Autocross") || name.starts_with("Bowling") ||
                      name.starts_with("Splitscreen") || name == "Soccer.xml";
    if (race && xml.find("SkipShadowMapUnlessCockpit") == std::string::npos) {
      const auto end = xml.find("</DynamicRenderSettings>");
      if (end == std::string::npos) throw std::runtime_error("Unexpected render scenario format");
      xml.insert(end, "  <SkipShadowMapUnlessCockpit value=\"1\"/>\r\n");
      ++changed;
    }
    std::ofstream output(destination / entry.path().filename(), std::ios::binary | std::ios::trunc);
    output.write(xml.data(), xml.size());
    output.close();
    if (!output) throw std::runtime_error("Unable to write render scenario override");
  }
  auto* vfs = runtime->file_system();
  std::string source_device_path;
  if (!vfs->FindSymbolicLink("game:", source_device_path))
    throw std::runtime_error("Unable to resolve render scenario device path");
  if (!source_device_path.ends_with('\\')) source_device_path += '\\';
  source_device_path += "media\\RenderScenarios";
  constexpr auto target = "\\Device\\FM4RenderOverrides";
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(target, destination, true);
  if (!device->Initialize() || !vfs->RegisterDevice(std::move(device)) ||
      !vfs->RegisterSymbolicLink(source_device_path, target))
    throw std::runtime_error("Unable to mount render scenario overrides");
  // OpenFile resolves the parent directory before GetChild, so the alias
  // must also match the directory itself (without a trailing separator).
  auto* parent = vfs->ResolvePath("game:\\media\\RenderScenarios");
  auto* file = vfs->ResolvePath("game:\\media\\RenderScenarios\\1-Car_Race.xml");
  if (!parent || !file || parent->device()->mount_path() != target ||
      file->device()->mount_path() != target)
    throw std::runtime_error("Render scenario override path validation failed");
  std::fprintf(stderr, "FM4 shadow workaround: %u race scenarios overridden; UI/global unchanged; source=%s\n",
               changed, source_device_path.c_str());
}
