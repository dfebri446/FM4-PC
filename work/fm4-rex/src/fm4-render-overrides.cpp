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

// Optional visual workaround documented by xenia-canary/game-patches#398
// (FM4_SHADOW_WORKAROUND=1, Build.ps1 -ShadowWorkaround; off by default).
// This suppresses the shadow-map pass outside the cockpit, as the title's
// split screen scenario does; Xenia rendered that pass as square shadows,
// this runtime renders it correctly. Keep UI and global scenarios intact
// (the global variant has been reported to hang career prize-car colour
// selection).
// FM4_TILING_SCENARIO=<digit> replaces the TilingScenario value 0 or 1 of the
// race scenarios (the title uses 1, two predicated tiles with 2x MSAA, for
// 8-16 car races and 0, more tiles, for pre-race, replays and 1-car races).
// 2 is the title's UI value: one 720p tile without MSAA. Split screen (4) and
// photo mode keep their own values.
// FM4_FRAME_RATE=<frames per second> replaces the FrameRate value 60 of the race
// scenarios; countdown, post-race, replays and photo mode keep 30
// (fm4-vblank-rate.cpp raises the guest vblank rate only for races).
// FM4_HALF_RATE_MIRROR=0 or 1 sets HalfRateMirror in the race scenarios: the
// title renders the rear-view mirror every other frame with 1, which it uses
// for 12 and 16 car races only (1 and 8 car races render it every frame).
// FM4_RACE_SETTINGS="Name=value;Name=attribute:value,attribute:value;Name=-"
// sets controls of the N-Car, Autocross and Bowling race scenarios (pre-race,
// countdown and post-race, race, replay; not split screen): each replaces the
// scenario's own element of that name or is added, and "-" removes it
// (pipeline.py -Quality). Applied after the variables above.

namespace {
// Applies FM4_RACE_SETTINGS to one scenario; returns the number of changes.
unsigned ApplyRaceSettings(std::string& xml, const std::string& settings) {
  unsigned changed = 0;
  for (size_t start = 0; start < settings.size();) {
    size_t end = settings.find(';', start);
    if (end == std::string::npos) end = settings.size();
    const std::string item = settings.substr(start, end - start);
    start = end + 1;
    const size_t equals = item.find('=');
    if (item.empty()) continue;
    if (equals == std::string::npos || equals == 0)
      throw std::runtime_error("FM4_RACE_SETTINGS items are Name=value");
    const std::string name = item.substr(0, equals), spec = item.substr(equals + 1);
    std::string element;
    if (spec != "-") {
      element = "<" + name;
      if (spec.find(':') == std::string::npos) {
        element += " value=\"" + spec + "\"";
      } else {
        for (size_t at = 0; at < spec.size();) {
          size_t next = spec.find(',', at);
          if (next == std::string::npos) next = spec.size();
          const std::string pair = spec.substr(at, next - at);
          const size_t colon = pair.find(':');
          if (colon == std::string::npos)
            throw std::runtime_error("FM4_RACE_SETTINGS attributes are name:value");
          element += " " + pair.substr(0, colon) + "=\"" + pair.substr(colon + 1) + "\"";
          at = next + 1;
        }
      }
      element += "/>";
    }
    const size_t at = xml.find("<" + name + " ");
    if (at != std::string::npos) {
      const size_t close = xml.find("/>", at);
      if (close == std::string::npos) throw std::runtime_error("Unexpected render scenario format");
      xml.replace(at, close + 2 - at, element);
      ++changed;
    } else if (!element.empty()) {
      const size_t root_end = xml.find("</DynamicRenderSettings>");
      if (root_end == std::string::npos)
        throw std::runtime_error("Unexpected render scenario format");
      xml.insert(root_end, "  " + element + "\r\n");
      ++changed;
    }
  }
  return changed;
}
}  // namespace

void FM4MountRenderOverrides(rex::Runtime* runtime) {
  const char* enabled = std::getenv("FM4_SHADOW_WORKAROUND");
  const bool shadow_workaround = enabled && std::string(enabled) == "1";
  const char* tiling = std::getenv("FM4_TILING_SCENARIO");
  const bool tiling_override = tiling && tiling[0] >= '0' && tiling[0] <= '9' && !tiling[1];
  const char* frame_rate = std::getenv("FM4_FRAME_RATE");
  const bool frame_rate_override =
      frame_rate && *frame_rate &&
      std::string(frame_rate).find_first_not_of("0123456789") == std::string::npos;
  const char* half_rate_mirror = std::getenv("FM4_HALF_RATE_MIRROR");
  const bool mirror_override = half_rate_mirror &&
                               (half_rate_mirror[0] == '0' || half_rate_mirror[0] == '1') &&
                               !half_rate_mirror[1];
  const char* race_settings_env = std::getenv("FM4_RACE_SETTINGS");
  const std::string race_settings = race_settings_env ? race_settings_env : "";
  if (!shadow_workaround && !tiling_override && !frame_rate_override && !mirror_override &&
      race_settings.empty())
    return;
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
    if (frame_rate_override) {
      constexpr char kFrameRate[] = "<FrameRate value=\"";
      for (size_t at = xml.find(kFrameRate); at != std::string::npos;
           at = xml.find(kFrameRate, at + 1)) {
        const size_t value = at + sizeof(kFrameRate) - 1;
        const size_t end = xml.find('"', value);
        if (end == std::string::npos) break;
        if (xml.compare(value, end - value, "60") != 0) continue;
        xml.replace(value, end - value, frame_rate);
        ++changed;
      }
    }
    if (race && mirror_override) {
      constexpr char kMirror[] = "<HalfRateMirror value=\"";
      const auto at = xml.find(kMirror);
      if (at != std::string::npos) {
        const size_t value = at + sizeof(kMirror) - 1;
        const size_t end = xml.find('"', value);
        if (end == std::string::npos) throw std::runtime_error("Unexpected render scenario format");
        xml.replace(value, end - value, half_rate_mirror);
      } else {
        const auto end = xml.find("</DynamicRenderSettings>");
        if (end == std::string::npos) throw std::runtime_error("Unexpected render scenario format");
        xml.insert(end, std::string("  <HalfRateMirror value=\"") + half_rate_mirror + "\"/>\r\n");
      }
      ++changed;
    }
    if (!race_settings.empty() &&
        (name.find("-Car_") != std::string::npos || name.starts_with("Autocross_") ||
         name.starts_with("Bowling_"))) {
      changed += ApplyRaceSettings(xml, race_settings);
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
               "FM4 render scenario overrides (shadow workaround %d, tiling %s, frame rate %s, "
               "half rate mirror %s, race settings %s): %u changes; UI/global unchanged; "
               "source=%s\n",
               int(shadow_workaround), tiling_override ? tiling : "-",
               frame_rate_override ? frame_rate : "-", mirror_override ? half_rate_mirror : "-",
               race_settings.empty() ? "-" : race_settings.c_str(), changed,
               source_device_path.c_str());
}
