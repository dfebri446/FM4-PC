/**
 * @file        graphics/d3d12/post_ao.h
 * @brief       post_ao: screen-space ambient occlusion (GTAO) of the guest
 *              scene, injected between the scene and its post-processing
 *
 * The guest resolves its scene depth tile by tile into one texture. When the
 * last tile is resolved, the passes of post_ao.hlsl turn that depth, with the
 * projection read from the constants of the scene's vertex shaders, into an
 * occlusion texture, bound in the bindless heap at
 * SystemBindlessView::kPostAoOcclusion for shader mods (xe_post_ao in
 * shader_override.hlsli). The swap clears it, so frames without a scene depth
 * get no occlusion. Off unless post_ao is set; see docs/analysis/gtao-design.md.
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <rex/graphics/d3d12/post_ao_camera.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/xenos.h>
#include <rex/ui/d3d12/d3d12_api.h>

namespace rex::graphics::d3d12 {

class D3D12CommandProcessor;

class PostAo {
 public:
  // nullptr unless post_ao is enabled and the host can run it (bindless
  // resources, host render targets, D3DCompiler_47).
  static std::unique_ptr<PostAo> Create(D3D12CommandProcessor& command_processor);
  ~PostAo();

  // Every draw: keeps the camera of the vertex shaders in post_ao_camera, and
  // logs the pixel shader constants in post_ao_log_constants.
  void OnDraw(const RegisterFile& regs, uint64_t vertex_shader_hash, uint64_t pixel_shader_hash) {
    for (const CameraSource& source : camera_sources_) {
      if (source.vertex_shader_hash == vertex_shader_hash) {
        CaptureCamera(regs, source);
        break;
      }
    }
    if (!logged_constants_.empty()) {
      LogConstants(regs, pixel_shader_hash);
    }
  }
  // After every resolve (address and length of the written range): tracks the
  // scene depth and runs the passes when it is complete.
  void OnResolve(const RegisterFile& regs, uint32_t address, uint32_t length);
  // In the swap, in an open submission: clears the occlusion of the frame.
  void OnSwap();

 private:
  struct CameraSource {
    uint64_t vertex_shader_hash;
    post_ao::CameraLayout layout;
    uint32_t constants[3];
  };
  struct Camera {
    bool valid = false;
    uint32_t depth_edram_base = 0;
    uint64_t frame = 0;
    post_ao::Unpack unpack = {};
    float guest_origin = 0.0f;
    // For the log: ndc_z = q - q_near / view z of a Direct3D style projection.
    float q = 0.0f, q_near = 0.0f;
  };
  // A pixel shader float constant to log (post_ao_log_constants).
  struct LoggedConstant {
    uint64_t pixel_shader_hash;
    uint32_t constant;
    uint64_t next_frame;
  };
  // One texture of the depth tiles of a frame.
  struct DepthGroup {
    uint64_t frame = UINT64_MAX;
    uint32_t base = 0, end = 0;
    uint32_t pitch = 0, height = 0;
    uint32_t depth_edram_base = 0;
    bool done = false;
    xenos::DepthRenderTargetFormat depth_format = xenos::DepthRenderTargetFormat::kD24FS8;
    xenos::Endian128 endianness = xenos::Endian128::k8in32;
  };
  struct Texture {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    uint32_t srv = UINT32_MAX;
    std::array<uint32_t, 5> uavs = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
  };
  enum class Pass : uint32_t { kPrefilter, kMain, kDenoise, kDenoiseFinal, kClear, kCount };

  explicit PostAo(D3D12CommandProcessor& command_processor);
  bool Initialize();
  void CaptureCamera(const RegisterFile& regs, const CameraSource& source);
  void LogConstants(const RegisterFile& regs, uint64_t pixel_shader_hash);
  void Run(const DepthGroup& group);
  bool EnsureTextures(uint32_t width, uint32_t height);
  bool CreateTexture(Texture& texture, DXGI_FORMAT format, uint32_t width, uint32_t height,
                     uint32_t mips);
  void ReleaseTexture(Texture& texture);
  void Transition(Texture& texture, D3D12_RESOURCE_STATES state);
  D3D12_GPU_DESCRIPTOR_HANDLE Gpu(uint32_t descriptor) const;
  void Dispatch(Pass pass, const D3D12_GPU_DESCRIPTOR_HANDLE (&srvs)[3],
                const D3D12_GPU_DESCRIPTOR_HANDLE (&uavs)[5], uint32_t groups_x,
                uint32_t groups_y);
  void ReleasePendingDescriptors();

  D3D12CommandProcessor& command_processor_;
  std::vector<CameraSource> camera_sources_;
  std::vector<LoggedConstant> logged_constants_;
  Camera camera_;
  DepthGroup group_;
  uint32_t min_width_ = 1024;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, size_t(Pass::kCount)> pipelines_;
  bool two_denoise_passes_ = false;

  uint32_t width_ = 0, height_ = 0;
  // W mips, visibility (ping-pong), edges, occlusion.
  Texture w_, visibility_[2], edges_, occlusion_;
  bool occlusion_written_ = false;
  // Descriptors of replaced textures, freed once the GPU is past them.
  std::vector<std::pair<uint32_t, uint64_t>> pending_descriptors_;

  // One run per frame: a second scene depth (split screen) is left out.
  uint64_t last_run_frame_ = UINT64_MAX;
  uint64_t runs_ = 0;
  bool warned_no_camera_ = false;
};

}  // namespace rex::graphics::d3d12
