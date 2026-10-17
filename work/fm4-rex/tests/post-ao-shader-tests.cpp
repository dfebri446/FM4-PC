// Tests of the post_ao compute shaders (work/runtime/src/graphics/d3d12/
// post_ao.hlsl) on WARP, the CPU Direct3D 12 device: no GPU is used. A
// synthetic scene is traced into a stored depth texture the way FM4 stores it
// (reversed Z through the viewport), the passes run with the root signature and
// constants of post_ao.cpp, and the occlusion is checked where the answer is
// known: sky and open ground without occlusion, the corner of a wall and the
// ground, and the contact of a box, darker.
//
// usage: fm4_post_ao_shader_tests <post_ao.hlsl> [occlusion.pgm]

#include <windows.h>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <rex/graphics/d3d12/post_ao_camera.h>

using Microsoft::WRL::ComPtr;
namespace post_ao = rex::graphics::d3d12::post_ao;

namespace {

constexpr uint32_t kWidth = 640, kHeight = 360;
constexpr uint32_t kMips = 5;
int failures = 0;

void Check(bool condition, const char* what, double value) {
  std::printf("%s %s (%.3f)\n", condition ? "ok  " : "FAIL", what, value);
  if (!condition) ++failures;
}

bool Succeeded(HRESULT result, const char* what) {
  if (FAILED(result)) {
    std::printf("FAIL %s: 0x%08X\n", what, unsigned(result));
    ++failures;
    return false;
  }
  return true;
}

// post_ao.cpp Constants.
struct Constants {
  float xy_mul_add[4];
  uint32_t size[2];
  float guest_per_host[2];
  float guest_origin[2];
  float w_numerator, w_depth_mul, w_depth_add, z_from_w, w_far;
  float radius, radius_pixels, falloff, power, denoise_center;
  uint32_t debug;
  float occlusion_scale;
};

// The scene, in view space (left-handed, +Z forward, +Y up): ground at y =
// -1.5, a wall at z = 14 for x > 1, a 1 m box on the ground at x -3..-2,
// z 7..8. Returns the view distance along the ray, or 0 for the sky.
double Trace(double dx, double dy, double dz, double& hit_y_out, int& surface_out) {
  double best = 0.0;
  surface_out = 0;
  auto consider = [&](double t, int surface) {
    if (t > 0.0 && (best == 0.0 || t < best)) {
      best = t;
      surface_out = surface;
    }
  };
  if (dy < 0.0) {
    double t = -1.5 / dy;
    if (t * dz < 400.0) consider(t, 1);  // Ground (to 400 m).
  }
  if (dz > 0.0) {
    double t = 14.0 / dz;
    if (t * dx > 1.0 && t * dy > -1.5 && t * dy < 4.0) consider(t, 2);  // Wall.
  }
  // Box faces: front (z = 7), top (y = -0.5), side (x = -2).
  if (dz > 0.0) {
    double t = 7.0 / dz, x = t * dx, y = t * dy;
    if (x > -3.0 && x < -2.0 && y > -1.5 && y < -0.5) consider(t, 3);
  }
  if (dy < 0.0) {
    double t = -0.5 / dy, x = t * dx, z = t * dz;
    if (x > -3.0 && x < -2.0 && z > 7.0 && z < 8.0) consider(t, 3);
  }
  if (dx < 0.0) {
    double t = -2.0 / dx, y = t * dy, z = t * dz;
    if (y > -1.5 && y < -0.5 && z > 7.0 && z < 8.0) consider(t, 3);
  }
  hit_y_out = best * dy;
  return best;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: fm4_post_ao_shader_tests <post_ao.hlsl> [occlusion.pgm]\n");
    return 2;
  }
  std::ifstream source_file(argv[1], std::ios::binary);
  std::string source((std::istreambuf_iterator<char>(source_file)),
                     std::istreambuf_iterator<char>());
  if (source.empty()) {
    std::printf("FAIL cannot read %s\n", argv[1]);
    return 1;
  }

  // Camera: 60 degrees vertical field of view, near 0.25, far 1000, the guest
  // viewport of 1280x720 with Z reversed (FM4), rendered at half resolution
  // (guest pixels per host pixel 2, like the inverse of a 0.5x scale).
  const double fov_y = 60.0 * 3.14159265358979 / 180.0, aspect = 16.0 / 9.0;
  const double near_z = 0.25, far_z = 1000.0, q = far_z / (far_z - near_z);
  post_ao::Matrix projection = {};
  projection.m[0][0] = float(1.0 / std::tan(fov_y * 0.5) / aspect);
  projection.m[1][1] = float(1.0 / std::tan(fov_y * 0.5));
  projection.m[2][2] = float(q);
  projection.m[2][3] = float(-q * near_z);
  projection.m[3][2] = 1.0f;
  const post_ao::Viewport viewport = {640.0f, 640.0f, -360.0f, 360.0f, -1.0f, 1.0f};
  post_ao::Unpack unpack;
  if (!post_ao::UnpackFromProjection(projection, viewport, unpack)) {
    std::printf("FAIL unpack constants\n");
    return 1;
  }
  const float guest_per_host = 1280.0f / float(kWidth);

  // Stored depth per host pixel center (Direct3D 9 pixel centers: guest
  // screen = (host + 0.5) * 2 - 0.5).
  std::vector<float> depth(kWidth * kHeight);
  std::vector<int> surface(kWidth * kHeight);
  std::vector<double> hit_y(kWidth * kHeight), view_w(kWidth * kHeight);
  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      double gx = (x + 0.5) * guest_per_host - 0.5, gy = (y + 0.5) * guest_per_host - 0.5;
      double ndc_x = (gx - viewport.x_offset) / viewport.x_scale;
      double ndc_y = (gy - viewport.y_offset) / viewport.y_scale;
      // View direction with z = 1 (P has no off-center terms here).
      double dx = ndc_x / projection.m[0][0], dy = ndc_y / projection.m[1][1];
      double t = Trace(dx, dy, 1.0, hit_y[y * kWidth + x], surface[y * kWidth + x]);
      double ndc_z = t > 0.0 ? q - q * near_z / t : 1.0;  // Sky at the far plane.
      depth[y * kWidth + x] = float(ndc_z * viewport.z_scale + viewport.z_offset);
      view_w[y * kWidth + x] = t > 0.0 ? t : far_z;
    }
  }

  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter> warp;
  ComPtr<ID3D12Device> device;
  if (!Succeeded(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "DXGI factory") ||
      !Succeeded(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter") ||
      !Succeeded(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)),
                 "WARP device")) {
    return 1;
  }

  // Root signature of post_ao.cpp.
  D3D12_ROOT_PARAMETER parameters[9];
  D3D12_DESCRIPTOR_RANGE ranges[8];
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants = {0, 0, sizeof(Constants) / 4};
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  for (UINT i = 0; i < 8; ++i) {
    bool srv = i < 3;
    ranges[i] = {srv ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1,
                 srv ? i : i - 3, 0, 0};
    parameters[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[i + 1].DescriptorTable = {1, &ranges[i]};
    parameters[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {9, parameters, 1, &sampler,
                                         D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ComPtr<ID3DBlob> root_blob, root_error;
  ComPtr<ID3D12RootSignature> root_signature;
  if (!Succeeded(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &root_blob, &root_error),
                 "root signature") ||
      !Succeeded(device->CreateRootSignature(0, root_blob->GetBufferPointer(),
                                             root_blob->GetBufferSize(),
                                             IID_PPV_ARGS(&root_signature)),
                 "root signature object")) {
    return 1;
  }

  // Pipelines, as post_ao.cpp compiles them (quality 2: 3 slices x 6 steps).
  struct Entry {
    const char* name;
    bool final;
  } entries[] = {{"post_ao_prefilter", false},
                 {"post_ao_main", false},
                 {"post_ao_denoise", false},
                 {"post_ao_denoise", true},
                 {"post_ao_clear", false}};
  ComPtr<ID3D12PipelineState> pipelines[5];
  for (int i = 0; i < 5; ++i) {
    D3D_SHADER_MACRO defines[] = {{"POST_AO_SLICES", "3"},
                                  {"POST_AO_STEPS", "6"},
                                  {entries[i].final ? "POST_AO_FINAL" : nullptr, "1"},
                                  {nullptr, nullptr}};
    ComPtr<ID3DBlob> code, messages;
    HRESULT result = D3DCompile(source.data(), source.size(), "post_ao.hlsl", defines, nullptr,
                                entries[i].name, "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                &code, &messages);
    if (messages) std::printf("%s\n", static_cast<const char*>(messages->GetBufferPointer()));
    if (!Succeeded(result, entries[i].name)) return 1;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root_signature.Get();
    desc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    if (!Succeeded(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelines[i])),
                   "pipeline")) {
      return 1;
    }
  }

  // Textures: depth, W mips, visibility x2, edges, occlusion.
  D3D12_HEAP_PROPERTIES default_heap = {D3D12_HEAP_TYPE_DEFAULT};
  auto texture = [&](DXGI_FORMAT format, uint32_t mips, bool uav, D3D12_RESOURCE_STATES state) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = UINT16(mips);
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> resource;
    Succeeded(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &desc, state,
                                              nullptr, IID_PPV_ARGS(&resource)),
              "texture");
    return resource;
  };
  ComPtr<ID3D12Resource> depth_texture =
      texture(DXGI_FORMAT_R32_FLOAT, 1, false, D3D12_RESOURCE_STATE_COPY_DEST);
  ComPtr<ID3D12Resource> w_texture =
      texture(DXGI_FORMAT_R32_FLOAT, kMips, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ComPtr<ID3D12Resource> r8[4];  // visibility 0, visibility 1, edges, occlusion
  for (auto& resource : r8) {
    resource = texture(DXGI_FORMAT_R8_UNORM, 1, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  if (failures) return 1;

  ComPtr<ID3D12DescriptorHeap> heap;
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 32,
                                          D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  if (!Succeeded(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)), "heap")) return 1;
  UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto cpu = [&](UINT i) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(i) * increment;
    return h;
  };
  auto gpu = [&](UINT i) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += UINT64(i) * increment;
    return h;
  };
  auto srv = [&](ID3D12Resource* resource, DXGI_FORMAT format, UINT mips, UINT index) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2D.MipLevels = mips;
    device->CreateShaderResourceView(resource, &desc, cpu(index));
  };
  auto uav = [&](ID3D12Resource* resource, DXGI_FORMAT format, UINT mip, UINT index) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = mip;
    device->CreateUnorderedAccessView(resource, nullptr, &desc, cpu(index));
  };
  // 0 depth, 1 W, 2-6 W mip UAVs, 7-10 R8 SRVs, 11-14 R8 UAVs.
  srv(depth_texture.Get(), DXGI_FORMAT_R32_FLOAT, 1, 0);
  srv(w_texture.Get(), DXGI_FORMAT_R32_FLOAT, kMips, 1);
  for (UINT mip = 0; mip < kMips; ++mip) uav(w_texture.Get(), DXGI_FORMAT_R32_FLOAT, mip, 2 + mip);
  for (UINT i = 0; i < 4; ++i) {
    srv(r8[i].Get(), DXGI_FORMAT_R8_UNORM, 1, 7 + i);
    uav(r8[i].Get(), DXGI_FORMAT_R8_UNORM, 0, 11 + i);
  }

  // Upload and readback buffers (rows aligned to 256 bytes).
  const UINT depth_pitch = (kWidth * 4 + 255) & ~255u, r8_pitch = (kWidth + 255) & ~255u;
  auto buffer = [&](D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES properties = {type};
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    Succeeded(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state,
                                              nullptr, IID_PPV_ARGS(&resource)),
              "buffer");
    return resource;
  };
  ComPtr<ID3D12Resource> upload = buffer(D3D12_HEAP_TYPE_UPLOAD, UINT64(depth_pitch) * kHeight,
                                         D3D12_RESOURCE_STATE_GENERIC_READ);
  ComPtr<ID3D12Resource> readback = buffer(D3D12_HEAP_TYPE_READBACK, UINT64(r8_pitch) * kHeight * 2,
                                           D3D12_RESOURCE_STATE_COPY_DEST);
  if (failures) return 1;
  {
    uint8_t* mapped = nullptr;
    upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    for (uint32_t y = 0; y < kHeight; ++y) {
      std::memcpy(mapped + y * depth_pitch, &depth[y * kWidth], kWidth * 4);
    }
    upload->Unmap(0, nullptr);
  }

  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  D3D12_COMMAND_QUEUE_DESC queue_desc = {D3D12_COMMAND_LIST_TYPE_DIRECT};
  device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue));
  device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
  device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                            IID_PPV_ARGS(&list));

  D3D12_TEXTURE_COPY_LOCATION depth_dst = {depth_texture.Get(),
                                           D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
  D3D12_TEXTURE_COPY_LOCATION depth_src = {upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
  depth_src.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_FLOAT, kWidth, kHeight, 1, depth_pitch};
  list->CopyTextureRegion(&depth_dst, 0, 0, 0, &depth_src, nullptr);
  auto transition = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                        D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &barrier);
  };
  constexpr D3D12_RESOURCE_STATES kSrv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  transition(depth_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, kSrv);

  // Constants as PostAo::Run fills them (radius 1 m, falloff 0.6, power 1.5).
  Constants constants = {};
  constants.xy_mul_add[0] = unpack.x_mul;
  constants.xy_mul_add[1] = unpack.x_add;
  constants.xy_mul_add[2] = unpack.y_mul;
  constants.xy_mul_add[3] = unpack.y_add;
  constants.size[0] = kWidth;
  constants.size[1] = kHeight;
  constants.guest_per_host[0] = constants.guest_per_host[1] = guest_per_host;
  constants.guest_origin[0] = constants.guest_origin[1] = -0.5f;
  constants.w_numerator = unpack.w_numerator;
  constants.w_depth_mul = unpack.w_depth_mul;
  constants.w_depth_add = unpack.w_depth_add;
  constants.z_from_w = unpack.z_from_w;
  constants.w_far = unpack.w_far;
  constants.radius = 1.0f;
  constants.radius_pixels = constants.radius * unpack.pixels_per_unit_x / guest_per_host;
  constants.falloff = 0.6f;
  constants.power = 1.5f;
  constants.denoise_center = 2.0f;
  constants.occlusion_scale = 1.0f;

  ID3D12DescriptorHeap* heaps[] = {heap.Get()};
  list->SetDescriptorHeaps(1, heaps);
  auto dispatch = [&](int pipeline, UINT s0, UINT s1, UINT s2, UINT u0, UINT u1, UINT u2, UINT u3,
                      UINT u4, UINT gx, UINT gy, uint32_t debug) {
    constants.debug = debug;
    list->SetPipelineState(pipelines[pipeline].Get());
    list->SetComputeRootSignature(root_signature.Get());
    list->SetComputeRoot32BitConstants(0, sizeof(Constants) / 4, &constants, 0);
    const UINT tables[] = {s0, s1, s2, u0, u1, u2, u3, u4};
    for (UINT i = 0; i < 8; ++i) list->SetComputeRootDescriptorTable(1 + i, gpu(tables[i]));
    list->Dispatch(gx, gy, 1);
  };
  const UINT gx8 = (kWidth + 7) / 8, gy8 = (kHeight + 7) / 8;
  // Same sequence as PostAo::Run with two denoise passes, then the W debug
  // output into visibility 1 (free by then).
  dispatch(0, 0, 0, 0, 2, 3, 4, 5, 6, (kWidth + 15) / 16, (kHeight + 15) / 16, 0);
  transition(w_texture.Get(), kUav, kSrv);
  dispatch(1, 1, 1, 1, 11, 13, 13, 13, 13, gx8, gy8, 0);
  transition(r8[0].Get(), kUav, kSrv);
  transition(r8[2].Get(), kUav, kSrv);
  dispatch(2, 7, 9, 1, 12, 12, 12, 12, 12, gx8, gy8, 0);
  transition(r8[1].Get(), kUav, kSrv);
  dispatch(3, 8, 9, 1, 14, 14, 14, 14, 14, gx8, gy8, 0);
  transition(r8[1].Get(), kSrv, kUav);
  dispatch(3, 7, 9, 1, 12, 12, 12, 12, 12, gx8, gy8, 1);
  transition(r8[3].Get(), kUav, D3D12_RESOURCE_STATE_COPY_SOURCE);
  transition(r8[1].Get(), kUav, D3D12_RESOURCE_STATE_COPY_SOURCE);
  for (int i = 0; i < 2; ++i) {
    D3D12_TEXTURE_COPY_LOCATION src = {(i ? r8[1] : r8[3]).Get(),
                                       D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION dst = {readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint.Offset = UINT64(i) * r8_pitch * kHeight;
    dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8_UNORM, kWidth, kHeight, 1, r8_pitch};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  }
  if (!Succeeded(list->Close(), "command list")) return 1;
  ID3D12CommandList* lists[] = {list.Get()};
  queue->ExecuteCommandLists(1, lists);
  ComPtr<ID3D12Fence> fence;
  device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  queue->Signal(fence.Get(), 1);
  fence->SetEventOnCompletion(1, event);
  WaitForSingleObject(event, INFINITE);
  if (!Succeeded(device->GetDeviceRemovedReason(), "device")) return 1;

  std::vector<uint8_t> occlusion(kWidth * kHeight), w_debug(kWidth * kHeight);
  {
    uint8_t* mapped = nullptr;
    D3D12_RANGE range = {0, SIZE_T(r8_pitch) * kHeight * 2};
    readback->Map(0, &range, reinterpret_cast<void**>(&mapped));
    for (uint32_t y = 0; y < kHeight; ++y) {
      std::memcpy(&occlusion[y * kWidth], mapped + y * r8_pitch, kWidth);
      std::memcpy(&w_debug[y * kWidth], mapped + (kHeight + y) * r8_pitch, kWidth);
    }
    readback->Unmap(0, nullptr);
  }
  if (argc > 2) {
    std::ofstream image(argv[2], std::ios::binary);
    image << "P5\n" << kWidth << ' ' << kHeight << "\n255\n";
    image.write(reinterpret_cast<const char*>(occlusion.data()), occlusion.size());
  }

  // Averages of the occlusion (0..1) over pixels of a region.
  auto average = [&](auto&& select) {
    double sum = 0.0;
    int count = 0;
    for (uint32_t y = 2; y + 2 < kHeight; ++y) {
      for (uint32_t x = 2; x + 2 < kWidth; ++x) {
        uint32_t i = y * kWidth + x;
        if (select(x, y, i)) {
          sum += occlusion[i] / 255.0;
          ++count;
        }
      }
    }
    return count ? sum / count : -1.0;
  };
  // Pixel positions on the ground: view x and z.
  auto ground_xz = [&](uint32_t x, uint32_t y, double& gx, double& gz) {
    double w = view_w[y * kWidth + x];
    double sx = (x + 0.5) * guest_per_host - 0.5;
    gx = w * ((sx - viewport.x_offset) / viewport.x_scale) / projection.m[0][0];
    gz = w;
  };
  double sky = average([&](uint32_t, uint32_t, uint32_t i) { return surface[i] == 0; });
  double open_ground = average([&](uint32_t x, uint32_t y, uint32_t i) {
    double gx, gz;
    ground_xz(x, y, gx, gz);
    return surface[i] == 1 && gz > 4.0 && gz < 11.0 && gx > -1.0 && gx < 0.5;
  });
  double wall_corner = average([&](uint32_t x, uint32_t y, uint32_t i) {
    double gx, gz;
    ground_xz(x, y, gx, gz);
    return surface[i] == 1 && gz > 13.6 && gz < 14.0 && gx > 2.0 && gx < 6.0;
  });
  double wall_high = average([&](uint32_t, uint32_t, uint32_t i) {
    return surface[i] == 2 && hit_y[i] > 1.0;
  });
  double box_contact = average([&](uint32_t x, uint32_t y, uint32_t i) {
    double gx, gz;
    ground_xz(x, y, gx, gz);
    return surface[i] == 1 && gz > 6.5 && gz < 7.0 && gx > -3.0 && gx < -2.0;
  });
  // W debug output: saturate(W / 100), against the traced W.
  double worst_w = 0.0;
  for (uint32_t i = 0; i < kWidth * kHeight; ++i) {
    if (surface[i] && view_w[i] < 90.0) {
      worst_w = std::fmax(worst_w, std::fabs(w_debug[i] / 255.0 - view_w[i] * 0.01));
    }
  }
  std::printf("occlusion: sky %.3f, open ground %.3f, wall corner %.3f, upper wall %.3f, "
              "box contact %.3f\n",
              sky, open_ground, wall_corner, wall_high, box_contact);
  Check(sky == 0.0, "sky has no occlusion", sky);
  Check(open_ground >= 0.0 && open_ground < 0.06, "open ground nearly unoccluded", open_ground);
  Check(wall_high >= 0.0 && wall_high < 0.06, "upper wall nearly unoccluded", wall_high);
  Check(wall_corner > 0.15, "wall and ground corner occluded", wall_corner);
  Check(box_contact > 0.1, "ground at the box occluded", box_contact);
  Check(wall_corner > 4.0 * std::fmax(open_ground, 0.01), "corner darker than open ground",
        wall_corner);
  Check(worst_w < 1.5 / 255.0 + 1.0e-3, "W debug output matches the traced W", worst_w * 255.0);
  std::printf(failures ? "post_ao shader tests: %d failed\n" : "post_ao shader tests: passed\n",
              failures);
  return failures ? 1 : 0;
}
