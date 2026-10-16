// fm4_rex - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <cstdlib>
#include "fm4-frame-capture.h"
#include "fm4-info-overlay.h"
#include "fm4-test-input.h"
#include <rex/input/input_system.h>
#include <rex/cvar.h>
#include <rex/ui/keybinds.h>

void FM4InstallCrashDiagnostics();
void FM4InstallDisc2(rex::Runtime*);
void FM4MountCache(rex::Runtime*);
void FM4MountRenderOverrides(rex::Runtime*);
void FM4InstallVblankRate(rex::Runtime*);
void FM4InstallCommandLine();

class Fm4RexApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Fm4RexApp>(new Fm4RexApp(ctx, "fm4_rex",
        PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.gpu_plugin = "xenos";
  }

  void OnPostLaunchModule(rex::system::XThread*) override {
    const char* directory = std::getenv("FM4_CAPTURE_DIR");
    if (!directory || !runtime()->graphics_system()) return;
    auto* presenter = runtime()->graphics_system()->presenter();
    if (!presenter) return;
    capture_thread_ = std::jthread(FM4CaptureFrames, presenter, std::filesystem::path(directory), runtime());
  }

  void OnPostSetup() override {
    // The GPU plugin has registered its flags by this point, and guest
    // execution has not started. FM4's one-shot photo draws must wait for
    // shaders rather than being skipped while compilation is pending.
    // Preserve explicit config, environment and command-line overrides.
    constexpr auto shader_flag = "async_shader_compilation";
    if (rex::cvar::GetFlagInfo(shader_flag) &&
        rex::cvar::GetFlagSource(shader_flag) == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName(shader_flag, "false");
    }
    std::fprintf(stderr, "FM4 shader policy: async_shader_compilation=%s\n",
                 rex::cvar::GetFlagByName(shader_flag).c_str());
    FM4InstallCrashDiagnostics();
    FM4MountCache(runtime());
    FM4MountRenderOverrides(runtime());
    FM4InstallVblankRate(runtime());
    FM4InstallCommandLine();
    FM4InstallDisc2(runtime());
    const char* input_file = std::getenv("FM4_TEST_INPUT_FILE");
    if (input_file && *input_file && runtime()->input_system()) {
      auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system());
      input->AddDriver(std::make_unique<FM4TestInput>(input_file));
    }
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    // The settings overlay (fm4-info-overlay.h): F2, shown at start with FM4_INFO_SHOW=1.
    info_lines_ = FM4InfoLines();
    if (info_lines_.empty()) return;
    const char* show = std::getenv("FM4_INFO_SHOW");
    if (show && *show == '1') {
      info_overlay_ =
          std::make_unique<FM4InfoOverlay>(drawer, window(), guest_frames(), info_lines_);
    }
    rex::ui::RegisterBind("bind_fm4_info", "F2", "Toggle the FM4 settings overlay",
                          [this, drawer] {
                            if (info_overlay_) {
                              info_overlay_.reset();
                            } else {
                              info_overlay_ =
                                  std::make_unique<FM4InfoOverlay>(drawer, window(), guest_frames(),
                                                                 info_lines_);
                            }
                          });
  }

  // The guest frames so far, for the settings overlay's frame rate (0 before the presenter).
  std::function<uint64_t()> guest_frames() const {
    return [this]() -> uint64_t {
      auto* graphics = runtime() ? runtime()->graphics_system() : nullptr;
      auto* presenter = graphics ? graphics->presenter() : nullptr;
      return presenter ? presenter->GetGuestOutputRefreshCount() : 0;
    };
  }

  void OnShutdown() override {
    // Before the runtime destroys the ImGui drawer.
    if (!info_lines_.empty()) rex::ui::UnregisterBind("bind_fm4_info");
    info_overlay_.reset();
    capture_thread_.request_stop();
    if (capture_thread_.joinable()) capture_thread_.join();
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
 private:
  std::jthread capture_thread_;
  std::vector<std::string> info_lines_;
  std::unique_ptr<FM4InfoOverlay> info_overlay_;
};
