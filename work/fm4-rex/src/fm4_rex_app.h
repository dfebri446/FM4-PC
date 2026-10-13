// fm4_rex - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <cstdlib>
#include "fm4-frame-capture.h"
#include "fm4-test-input.h"
#include <rex/input/input_system.h>
#include <rex/cvar.h>

void FM4InstallCrashDiagnostics();
void FM4InstallDisc2(rex::Runtime*);
void FM4MountCache(rex::Runtime*);
void FM4MountRenderOverrides(rex::Runtime*);

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
    FM4InstallDisc2(runtime());
    const char* input_file = std::getenv("FM4_TEST_INPUT_FILE");
    if (input_file && *input_file && runtime()->input_system()) {
      auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system());
      input->AddDriver(std::make_unique<FM4TestInput>(input_file));
    }
  }

  void OnShutdown() override {
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
};
