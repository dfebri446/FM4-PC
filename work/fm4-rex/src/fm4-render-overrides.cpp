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
// FM4_TILING_SCENARIO=<digit> replaces the TilingScenario value 0 or 1 of the
// race scenarios (the title uses 1, two predicated tiles with 2x MSAA, for
// 8-16 car races and 0, more tiles, for pre-race, replays and 1-car races).
// 2 is the title's UI value: one 720p tile without MSAA. Split screen (4) and
// photo mode keep their own values.
void FM4MountRenderOverrides(rex::Runtime* runtime) {
  const char* enabled = std::getenv("FM4_SHADOW_WORKAROUND");
  const bool shadow_workaround = enabled && std::string(enabled) == "1";
  const char* tiling = std::getenv("FM4_TILING_SCENARIO");
  const bool tiling_override = tiling && tiling[0] >= '0' && tiling[0] <= '9' && !tiling[1];
  if (!shadow_workaround && !tiling_override) return;
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
    if (race && tiling_override) {
      constexpr char kTiling[] = "<TilingScenario value=\"";
      const auto at = xml.find(kTiling);
      if (at != std::string::npos && at + sizeof(kTiling) < xml.size() &&
          (xml[at + sizeof(kTiling) - 1] == '0' || xml[at + sizeof(kTiling) - 1] == '1')) {
        xml[at + sizeof(kTiling) - 1] = tiling[0];
        ++changed;
      }
    }
    if (shadow_workaround && race && xml.find("SkipShadowMapUnlessCockpit") == std::string::npos) {
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
  std::fprintf(stderr,
               "FM4 render scenario overrides (shadow workaround %d, tiling %s): %u changes; "
               "UI/global unchanged; source=%s\n",
               int(shadow_workaround), tiling_override ? tiling : "-", changed,
               source_device_path.c_str());
}
