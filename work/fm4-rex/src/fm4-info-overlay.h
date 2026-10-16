// FM4 settings overlay: a small panel in the top left corner with what the launcher or
// e2e.py set up (display and render scale, race quality, look, shadows, frame rate, ...),
// from FM4_INFO ("line|line|..."), above the window size and the game's frame rate (guest
// output refreshes). The presenter draws it over the guest output, so captures
// (FM4_CAPTURE_DIR) never contain it. FM4_INFO_SHOW=1 shows it at start (e2e.py,
// Build.ps1 -Info); F2 toggles it.

#pragma once

#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window.h>

class FM4InfoOverlay : public rex::ui::ImGuiDialog {
 public:
  // frames: the guest frames so far (Presenter::GetGuestOutputRefreshCount).
  FM4InfoOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
                 std::function<uint64_t()> frames, std::vector<std::string> lines)
      : ImGuiDialog(drawer),
        window_(window),
        frames_(std::move(frames)),
        lines_(std::move(lines)),
        rate_time_(std::chrono::steady_clock::now()),
        rate_frames_(frames_()) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    // Game frames per second, over half a second or more.
    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now - rate_time_).count();
    if (seconds >= 0.5) {
      uint64_t frames = frames_();
      fps_ = double(frames - rate_frames_) / seconds;
      rate_frames_ = frames;
      rate_time_ = now;
    }
    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f));
    ImGui::SetNextWindowBgAlpha(0.6f);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##fm4_info", nullptr, flags)) {
      for (const std::string& line : lines_) ImGui::TextUnformatted(line.c_str());
      ImGui::Separator();
      ImGui::Text("Window %ux%u, game %.1f fps (F2 hides)",
                  window_ ? window_->GetActualPhysicalWidth() : 0u,
                  window_ ? window_->GetActualPhysicalHeight() : 0u, fps_);
    }
    ImGui::End();
  }

 private:
  rex::ui::Window* window_;
  std::function<uint64_t()> frames_;
  std::vector<std::string> lines_;
  std::chrono::steady_clock::time_point rate_time_;
  uint64_t rate_frames_;
  double fps_ = 0.0;
};

// The lines of FM4_INFO; none when it is not set.
inline std::vector<std::string> FM4InfoLines() {
  std::vector<std::string> lines;
  const char* info = std::getenv("FM4_INFO");
  if (!info) return lines;
  std::string text(info);
  for (size_t start = 0; start < text.size();) {
    size_t end = text.find('|', start);
    if (end == std::string::npos) end = text.size();
    if (end > start) lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}
