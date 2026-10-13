#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <rex/runtime.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/util/draw.h>
#include <fstream>
#include <filesystem>
#include <array>

void FM4QueueGpuProbe(rex::Runtime* runtime, std::filesystem::path directory, unsigned index) {
  auto* gpu = static_cast<rex::graphics::GraphicsSystem*>(runtime->graphics_system());
  auto* memory = runtime->memory();
  // The SDK DLL does not export CommandProcessor::CallInThread. Use an OS
  // memory snapshot for diagnostics only; this is not an atomic draw capture.
  {
    using namespace rex::graphics;
    std::array<uint32_t, RegisterFile::kRegisterCount> values{};
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), gpu->register_file()->values,
                            values.data(), sizeof(values), &bytes_read) || bytes_read != sizeof(values)) return;
    auto get = [&]<class T>() { T result; std::memcpy(&result, &values[T::register_index], sizeof(T)); return result; };
    xenos::xe_gpu_vertex_fetch_t fetch;
    std::memcpy(&fetch, &values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], sizeof(fetch));
    auto surface = get.operator()<reg::RB_SURFACE_INFO>();
    auto offset = get.operator()<reg::PA_SC_WINDOW_OFFSET>();
    auto mode = get.operator()<reg::PA_SU_SC_MODE_CNTL>();
    auto center = get.operator()<reg::PA_SU_VTX_CNTL>();
    auto tl = get.operator()<reg::PA_SC_WINDOW_SCISSOR_TL>();
    auto br = get.operator()<reg::PA_SC_WINDOW_SCISSOR_BR>();
    auto stl = get.operator()<reg::PA_SC_SCREEN_SCISSOR_TL>();
    auto sbr = get.operator()<reg::PA_SC_SCREEN_SCISSOR_BR>();
    std::ofstream out(directory / "gpu-state.txt", std::ios::app);
    out << "frame=" << index << " edram=" << unsigned(get.operator()<reg::RB_MODECONTROL>().edram_mode)
        << " pitch=" << surface.surface_pitch << " msaa=" << unsigned(surface.msaa_samples)
        << " window_scissor=" << tl.tl_x << ',' << tl.tl_y << ',' << br.br_x << ',' << br.br_y
        << " screen_scissor=" << stl.tl_x << ',' << stl.tl_y << ',' << sbr.br_x << ',' << sbr.br_y
        << " scissor_offset_disable=" << tl.window_offset_disable
        << " offset=" << offset.window_x_offset << ',' << offset.window_y_offset
        << " offset_enable=" << mode.vtx_window_offset_enable
        << " pixel_center=" << unsigned(center.pix_center)
        << " fetch_type=" << unsigned(fetch.type) << " fetch_size=" << fetch.size
        << " fetch_address=" << std::hex << uint32_t(fetch.address * 4) << std::dec;
    if (fetch.type == xenos::FetchConstantType::kVertex && fetch.size == 6) {
      float vertices[6]{};
      SIZE_T read = 0;
      if (ReadProcessMemory(GetCurrentProcess(), memory->TranslatePhysical(fetch.address * 4),
                            vertices, sizeof(vertices), &read) && read == sizeof(vertices)) {
        out << " vertices=";
        for (float vertex : vertices) out << xenos::GpuSwap(vertex, fetch.endian) << ',';
      }
    }
    out << '\n';
  }
}
