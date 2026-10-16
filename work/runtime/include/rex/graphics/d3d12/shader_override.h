/**
 * @file        graphics/d3d12/shader_override.h
 * @brief       Hand-written HLSL replacing the DXBC translation of guest shaders
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <rex/graphics/d3d12/shader.h>
#include <rex/hash.h>

namespace rex::graphics::d3d12 {

// <shader_override_path>/shader_<ucode hash>[_<modification>].ps.hlsl replaces
// the DXBC translation of a guest pixel shader. The file defines
//   void xe_pixel_shader(inout XePixel p)
// and is compiled with FXC (ps_5_1, the translator's bindless root signature)
// after a contract describing the translation it replaces: its constants,
// descriptor slots, interpolators and outputs (see shader_override.hlsli). On
// any failure, or if its signatures don't match the translation's, the DXBC
// translation stays. Only the bindless host render target path is supported.
class ShaderOverrides {
 public:
  struct HostConfig {
    bool bindless_resources_used;
    bool edram_rov_used;
    bool gamma_render_target_as_unorm8;
    bool msaa_2x_supported;
    uint32_t draw_resolution_scale_x;
    uint32_t draw_resolution_scale_y;
  };

  // nullptr if shader_override_path is empty or the host path is unsupported.
  static std::unique_ptr<ShaderOverrides> Create(const HostConfig& config);
  ~ShaderOverrides();

  // Whether the guest pixel shader is overridden: by a file, by generated HLSL
  // (shader_override_generated), or by generated HLSL with a mod.
  bool HasPixelShader(uint64_t ucode_hash) const {
    return use_generated_ || pixel_shaders_.find(ucode_hash) != pixel_shaders_.end() ||
           mod_shaders_.find(ucode_hash) != mod_shaders_.end();
  }
  // Whether shader_override_ab_ms currently selects the DXBC translations.
  static bool IsAbOffPhase();

  // After a successful DXBC translation: if there is an override for it,
  // compiles it and replaces the translated binary. Pixel shaders without an
  // override file get HLSL from HlslShaderTranslator, written to
  // shader_override_generate_path and, with shader_override_generated, used. With
  // dump_shaders, writes the contract of every translated pixel shader there.
  void Apply(D3D12Shader::D3D12Translation& translation);

 private:
  ShaderOverrides(const HostConfig& config, std::filesystem::path directory);

  std::string BuildContract(const D3D12Shader::D3D12Translation& translation) const;
  bool Compile(const D3D12Shader::D3D12Translation& translation, const std::string& contract,
               const std::string& text, const std::string& name, const std::string& mod,
               std::vector<uint8_t>& binary_out) const;

  HostConfig config_;
  std::filesystem::path directory_;
  std::string generate_path_;
  bool use_generated_ = false;
  std::unordered_set<uint64_t, rex::IdentityHasher<uint64_t>> pixel_shaders_;
  // Shader mods (shader_mod_path): shader_<hash>.mod.hlsli.
  std::filesystem::path mod_directory_;
  std::unordered_set<uint64_t, rex::IdentityHasher<uint64_t>> mod_shaders_;
  std::vector<std::pair<std::string, std::string>> mod_defines_;
  void* d3dcompiler_ = nullptr;
  void* d3d_compile_ = nullptr;
};

}  // namespace rex::graphics::d3d12
