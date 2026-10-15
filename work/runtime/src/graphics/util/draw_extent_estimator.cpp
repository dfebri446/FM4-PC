/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstring>

#include <rex/assert.h>
#include <rex/cvar.h>
#include <rex/dbg.h>
#include <rex/graphics/flags.h>
#include <rex/graphics/format/ucode.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw_extent_estimator.h>
#include <rex/graphics/xenos.h>
#include <rex/math.h>
#include <rex/memory.h>
#include <rex/ui/graphics_util.h>

// On as in Xenia (the ReXGlue SDK had it off): FM4 clears its render targets with
// screen-space rectangles and an 8192 scissor, and without the vertex positions
// each clear claims the EDRAM up to its end, so the host render target path
// transfers up to all 2048 tiles around every clear. Quick Race on RTV: GPU 10.8
// -> 7.8 ms a race frame, command processor time unchanged.
REXCVAR_DEFINE_BOOL(execute_unclipped_draw_vs_on_cpu, true, "GPU",
                    "Execute unclipped draw vertex shader on CPU");

REXCVAR_DEFINE_BOOL(execute_unclipped_draw_vs_on_cpu_with_scissor, false, "GPU",
                    "Execute unclipped draw VS on CPU with scissor");

// DEFINE_bool(
//     execute_unclipped_draw_vs_on_cpu, true,
//     "Execute the vertex shader for draws with clipping disabled, primarily "
//     "screen-space draws (such as clears), on the CPU when possible to estimate "
//     "the extent of the EDRAM involved in the draw.\n"
//     "Enabling this may significantly improve GPU performance as otherwise up "
//     "to the entire EDRAM may be considered used in draws without clipping, "
//     "potentially resulting in spurious EDRAM range ownership transfer round "
//     "trips between host render targets.\n"
//     "Also, on hosts where certain render target formats have to be emulated in "
//     "a lossy way (for instance, 16-bit fixed-point via 16-bit floating-point), "
//     "this prevents corruption of other render targets located after the "
//     "current ones in the EDRAM by lossy range ownership transfers done for "
//     "those draws.",
//     "GPU");
// DEFINE_bool(
//     execute_unclipped_draw_vs_on_cpu_with_scissor, false,
//     "Don't restrict the usage of execute_unclipped_draw_vs_on_cpu to only "
//     "non-scissored draws (with the right and the bottom sides of the scissor "
//     "rectangle at 8192 or beyond) even though if the scissor rectangle is "
//     "present, it's usually sufficient for esimating the height of the render "
//     "target.\n"
//     "Enabling this may cause excessive processing of vertices on the CPU, as "
//     "some games draw rectangles (for their UI, for instance) without clipping, "
//     "but with a proper scissor rectangle.",
//     "GPU");

namespace rex::graphics {

void DrawExtentEstimator::PositionYExportSink::Export(ucode::ExportRegister export_register,
                                                      const float* value, uint32_t value_mask) {
  if (export_register == ucode::ExportRegister::kVSPosition) {
    if (value_mask & 0b0001) {
      position_x_ = value[0];
    }
    if (value_mask & 0b0010) {
      position_y_ = value[1];
    }
    if (value_mask & 0b0100) {
      position_z_ = value[2];
    }
    if (value_mask & 0b1000) {
      position_w_ = value[3];
    }
  } else if (uint32_t(export_register) <= uint32_t(ucode::ExportRegister::kVSInterpolator15)) {
    // Partial writes are not tracked: an interpolator counts as written once
    // all its components are.
    uint32_t index = uint32_t(export_register);
    for (uint32_t i = 0; i < 4; ++i) {
      if (value_mask & (uint32_t(1) << i)) {
        interpolators_[index][i] = value[i];
      }
    }
    if ((value_mask & 0b1111) == 0b1111) {
      interpolators_written_ |= uint32_t(1) << index;
    }
  } else if (export_register == ucode::ExportRegister::kVSPointSizeEdgeFlagKillVertex) {
    if (value_mask & 0b0001) {
      point_size_ = value[0];
    }
    if (value_mask & 0b0100) {
      vertex_kill_ = rex::memory::Reinterpret<uint32_t>(value[2]);
    }
  }
}

uint32_t DrawExtentEstimator::EstimateVertexMaxY(const Shader& vertex_shader) {
  SCOPE_profile_cpu_f("gpu");

  const RegisterFile& regs = register_file_;

  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  if (!vgt_draw_initiator.num_indices) {
    return 0;
  }
  if (vgt_draw_initiator.source_select != xenos::SourceSelect::kDMA &&
      vgt_draw_initiator.source_select != xenos::SourceSelect::kAutoIndex) {
    // TODO(Triang3l): Support immediate indices.
    return xenos::kTexture2DCubeMaxWidthHeight;
  }

  // Not reproducing tessellation.
  if (xenos::IsMajorModeExplicit(vgt_draw_initiator.major_mode, vgt_draw_initiator.prim_type) &&
      regs.Get<reg::VGT_OUTPUT_PATH_CNTL>().path_select ==
          xenos::VGTOutputPath::kTessellationEnable) {
    return xenos::kTexture2DCubeMaxWidthHeight;
  }

  assert_true(vertex_shader.type() == xenos::ShaderType::kVertex);
  assert_true(vertex_shader.is_ucode_analyzed());
  if (!ShaderInterpreter::CanInterpretShader(vertex_shader)) {
    return xenos::kTexture2DCubeMaxWidthHeight;
  }

  auto vgt_dma_size = regs.Get<reg::VGT_DMA_SIZE>();
  union {
    const void* index_buffer;
    const uint16_t* index_buffer_16;
    const uint32_t* index_buffer_32;
  };
  xenos::Endian index_endian = vgt_dma_size.swap_mode;
  if (vgt_draw_initiator.source_select == xenos::SourceSelect::kDMA) {
    xenos::IndexFormat index_format = vgt_draw_initiator.index_size;
    uint32_t index_buffer_base = regs[XE_GPU_REG_VGT_DMA_BASE];
    if (vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16) {
      // Handle the index endianness to same way as the PrimitiveProcessor.
      if (index_endian == xenos::Endian::k8in32) {
        index_endian = xenos::Endian::k8in16;
      } else if (index_endian == xenos::Endian::k16in32) {
        index_endian = xenos::Endian::kNone;
      }
      index_buffer_base &= ~uint32_t(sizeof(uint16_t) - 1);
    } else {
      assert_true(vgt_draw_initiator.index_size == xenos::IndexFormat::kInt32);
      index_buffer_base &= ~uint32_t(sizeof(uint32_t) - 1);
    }
    index_buffer = memory_.TranslatePhysical(index_buffer_base);
  }
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  uint32_t reset_index = regs.Get<reg::VGT_MULTI_PRIM_IB_RESET_INDX>().reset_indx;
  uint32_t index_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;

  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  float viewport_y_scale =
      pa_cl_vte_cntl.vport_y_scale_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YSCALE) : 1.0f;
  float viewport_y_offset =
      pa_cl_vte_cntl.vport_y_offset_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YOFFSET) : 0.0f;

  int32_t point_vertex_min_diameter_float = 0;
  int32_t point_vertex_max_diameter_float = 0;
  float point_constant_radius_y = 0.0f;
  if (vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList) {
    auto pa_su_point_minmax = regs.Get<reg::PA_SU_POINT_MINMAX>();
    point_vertex_min_diameter_float =
        rex::memory::Reinterpret<int32_t>(float(pa_su_point_minmax.min_size) * (2.0f / 16.0f));
    point_vertex_max_diameter_float =
        rex::memory::Reinterpret<int32_t>(float(pa_su_point_minmax.max_size) * (2.0f / 16.0f));
    point_constant_radius_y = float(regs.Get<reg::PA_SU_POINT_SIZE>().height) * (1.0f / 16.0f);
  }

  float max_y = -FLT_MAX;

  shader_interpreter_.SetShader(vertex_shader);

  PositionYExportSink position_y_export_sink;
  shader_interpreter_.SetExportSink(&position_y_export_sink);
  for (uint32_t i = 0; i < vgt_draw_initiator.num_indices; ++i) {
    uint32_t vertex_index;
    if (vgt_draw_initiator.source_select == xenos::SourceSelect::kDMA) {
      if (i < vgt_dma_size.num_words) {
        if (vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16) {
          vertex_index = index_buffer_16[i];
        } else {
          vertex_index = index_buffer_32[i];
        }
        // The Xenos only uses 24 bits of the index (reset_indx is 24-bit).
        vertex_index = xenos::GpuSwap(vertex_index, index_endian) & 0xFFFFFF;
      } else {
        vertex_index = 0;
      }
      if (pa_su_sc_mode_cntl.multi_prim_ib_ena && vertex_index == reset_index) {
        continue;
      }
    } else {
      assert_true(vgt_draw_initiator.source_select == xenos::SourceSelect::kAutoIndex);
      vertex_index = i;
    }
    vertex_index =
        std::min(max_index, std::max(min_index, (vertex_index + index_offset) & 0xFFFFFF));

    position_y_export_sink.Reset();

    shader_interpreter_.temp_registers()[0] = float(vertex_index);
    shader_interpreter_.Execute();

    if (position_y_export_sink.vertex_kill().has_value() &&
        (position_y_export_sink.vertex_kill().value() & ~(UINT32_C(1) << 31))) {
      continue;
    }
    if (!position_y_export_sink.position_y().has_value()) {
      continue;
    }
    float vertex_y = position_y_export_sink.position_y().value();
    if (!pa_cl_vte_cntl.vtx_xy_fmt) {
      if (!position_y_export_sink.position_w().has_value()) {
        continue;
      }
      vertex_y /= position_y_export_sink.position_w().value();
    }

    vertex_y = vertex_y * viewport_y_scale + viewport_y_offset;

    if (vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList) {
      float point_radius_y;
      if (position_y_export_sink.point_size().has_value()) {
        // Vertex-specified diameter. Clamped effectively as a signed integer in
        // the hardware, -NaN, -Infinity ... -0 to the minimum, +Infinity, +NaN
        // to the maximum.
        point_radius_y = 0.5f * rex::memory::Reinterpret<float>(std::min(
                                    point_vertex_max_diameter_float,
                                    std::max(point_vertex_min_diameter_float,
                                             rex::memory::Reinterpret<int32_t>(
                                                 position_y_export_sink.point_size().value()))));
      } else {
        // Constant radius.
        point_radius_y = point_constant_radius_y;
      }
      vertex_y += point_radius_y;
    }

    // std::max is `a < b ? b : a`, thus in case of NaN, the first argument is
    // always returned - max_y, which is initialized to a normalized value.
    max_y = std::max(max_y, vertex_y);
  }
  shader_interpreter_.SetExportSink(nullptr);

  int32_t max_y_24p8 = ui::FloatToD3D11Fixed16p8(max_y);
  // 16p8 range is -32768 to 32767+255/256, but it's stored as uint32_t here,
  // as 24p8, so overflowing up to -8388608 to 8388608+255/256 is safe. The
  // range of the window offset plus the half-pixel offset is -16384 to 16384.5,
  // so it's safe to add both - adding it will neither move the 16p8 clamping
  // bounds -32768 and 32767+255/256 into the 0...8192 screen space range, nor
  // cause 24p8 overflow.
  if (regs.Get<reg::PA_SU_VTX_CNTL>().pix_center == xenos::PixelCenter::kD3DZero) {
    max_y_24p8 += 128;
  }
  if (pa_su_sc_mode_cntl.vtx_window_offset_enable) {
    max_y_24p8 += regs.Get<reg::PA_SC_WINDOW_OFFSET>().window_y_offset * 256;
  }
  // Top-left rule - .5 exclusive without MSAA, 1. exclusive with MSAA.
  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  return (uint32_t(std::max(int32_t(0), max_y_24p8)) +
          ((rb_surface_info.msaa_samples == xenos::MsaaSamples::k1X) ? 127 : 255)) >>
         8;
}

bool DrawExtentEstimator::EstimateRectangleBounds(const Shader& vertex_shader,
                                                  float bounds_out[4], bool* flat_z_out,
                                                  uint32_t interpolator_mask,
                                                  bool* flat_interpolators_out) {
  const RegisterFile& regs = register_file_;
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  if (vgt_draw_initiator.prim_type != xenos::PrimitiveType::kRectangleList ||
      vgt_draw_initiator.num_indices != 3 ||
      (vgt_draw_initiator.source_select != xenos::SourceSelect::kDMA &&
       vgt_draw_initiator.source_select != xenos::SourceSelect::kAutoIndex) ||
      !ShaderInterpreter::CanInterpretShader(vertex_shader)) {
    return false;
  }
  uint32_t vertex_indices[3];
  if (vgt_draw_initiator.source_select == xenos::SourceSelect::kDMA) {
    auto vgt_dma_size = regs.Get<reg::VGT_DMA_SIZE>();
    if (vgt_dma_size.num_words < 3) {
      return false;
    }
    xenos::Endian index_endian = vgt_dma_size.swap_mode;
    uint32_t index_buffer_base = regs[XE_GPU_REG_VGT_DMA_BASE];
    const void* index_buffer;
    if (vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16) {
      if (index_endian == xenos::Endian::k8in32) {
        index_endian = xenos::Endian::k8in16;
      } else if (index_endian == xenos::Endian::k16in32) {
        index_endian = xenos::Endian::kNone;
      }
      index_buffer = memory_.TranslatePhysical(index_buffer_base & ~uint32_t(1));
    } else {
      index_buffer = memory_.TranslatePhysical(index_buffer_base & ~uint32_t(3));
    }
    for (uint32_t i = 0; i < 3; ++i) {
      uint32_t vertex_index = vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16
                                  ? static_cast<const uint16_t*>(index_buffer)[i]
                                  : static_cast<const uint32_t*>(index_buffer)[i];
      vertex_indices[i] = xenos::GpuSwap(vertex_index, index_endian) & 0xFFFFFF;
    }
  } else {
    for (uint32_t i = 0; i < 3; ++i) {
      vertex_indices[i] = i;
    }
  }
  uint32_t index_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;

  // As in draw_util::GetHostViewportInfo with clipping disabled.
  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  float scale[] = {
      pa_cl_vte_cntl.vport_x_scale_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XSCALE) : 1.0f,
      pa_cl_vte_cntl.vport_y_scale_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YSCALE) : 1.0f,
  };
  float offset[] = {
      pa_cl_vte_cntl.vport_x_offset_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XOFFSET) : 0.0f,
      pa_cl_vte_cntl.vport_y_offset_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YOFFSET) : 0.0f,
  };
  if (regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable) {
    auto pa_sc_window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();
    offset[0] += float(pa_sc_window_offset.window_x_offset);
    offset[1] += float(pa_sc_window_offset.window_y_offset);
  }
  if (REXCVAR_GET(half_pixel_offset) &&
      regs.Get<reg::PA_SU_VTX_CNTL>().pix_center == xenos::PixelCenter::kD3DZero) {
    offset[0] += 0.5f;
    offset[1] += 0.5f;
  }

  bounds_out[0] = bounds_out[1] = FLT_MAX;
  bounds_out[2] = bounds_out[3] = -FLT_MAX;
  shader_interpreter_.SetShader(vertex_shader);
  PositionYExportSink export_sink;
  shader_interpreter_.SetExportSink(&export_sink);
  bool valid = true;
  bool flat_z = true;
  bool flat_interpolators = true;
  std::optional<float> first_z, first_w;
  float first_interpolators[16][4];
  float corner_positions[3][2] = {};
  for (uint32_t i = 0; i < 3 && valid; ++i) {
    uint32_t vertex_index =
        std::min(max_index, std::max(min_index, (vertex_indices[i] + index_offset) & 0xFFFFFF));
    export_sink.Reset();
    shader_interpreter_.temp_registers()[0] = float(vertex_index);
    shader_interpreter_.Execute();
    if (export_sink.vertex_kill().has_value() &&
        (export_sink.vertex_kill().value() & ~(UINT32_C(1) << 31))) {
      valid = false;
      break;
    }
    if (!export_sink.position_x().has_value() || !export_sink.position_y().has_value()) {
      valid = false;
      break;
    }
    float position[] = {export_sink.position_x().value(), export_sink.position_y().value()};
    if ((export_sink.interpolators_written() & interpolator_mask) != interpolator_mask) {
      flat_interpolators = false;
    }
    if (i == 0) {
      first_z = export_sink.position_z();
      first_w = export_sink.position_w();
      for (uint32_t j = 0; j < 16; ++j) {
        std::memcpy(first_interpolators[j], export_sink.interpolator(j), sizeof(float) * 4);
      }
    } else {
      flat_z &= export_sink.position_z() == first_z && export_sink.position_w() == first_w;
      uint32_t interpolators_remaining = interpolator_mask;
      uint32_t j;
      while (rex::bit_scan_forward(interpolators_remaining, &j)) {
        interpolators_remaining &= ~(uint32_t(1) << j);
        flat_interpolators &=
            std::memcmp(first_interpolators[j], export_sink.interpolator(j), sizeof(float) * 4) == 0;
      }
    }
    if (!pa_cl_vte_cntl.vtx_xy_fmt) {
      if (!export_sink.position_w().has_value()) {
        valid = false;
        break;
      }
      position[0] /= export_sink.position_w().value();
      position[1] /= export_sink.position_w().value();
    }
    for (uint32_t j = 0; j < 2; ++j) {
      float pixel = position[j] * scale[j] + offset[j];
      // NaN fails both comparisons.
      if (!(pixel >= -16384.0f && pixel <= 16384.0f)) {
        valid = false;
        break;
      }
      corner_positions[i][j] = pixel;
      bounds_out[j] = std::min(bounds_out[j], pixel);
      bounds_out[2 + j] = std::max(bounds_out[2 + j], pixel);
    }
  }
  shader_interpreter_.SetExportSink(nullptr);
  // An axis-aligned rectangle has its 3 vertices at 3 different corners of their
  // bounding box (a skewed one would cover less than the box).
  if (valid) {
    uint32_t corners = 0;
    for (uint32_t i = 0; i < 3; ++i) {
      bool right = corner_positions[i][0] == bounds_out[2];
      bool bottom = corner_positions[i][1] == bounds_out[3];
      if ((!right && corner_positions[i][0] != bounds_out[0]) ||
          (!bottom && corner_positions[i][1] != bounds_out[1])) {
        valid = false;
        break;
      }
      corners |= uint32_t(1) << (uint32_t(right) | (uint32_t(bottom) << 1));
    }
    valid = valid && rex::bit_count(corners) == 3;
  }
  if (flat_z_out) {
    *flat_z_out = valid && flat_z;
  }
  if (flat_interpolators_out) {
    *flat_interpolators_out = valid && flat_interpolators;
  }
  return valid;
}

uint32_t DrawExtentEstimator::EstimateMaxY(bool try_to_estimate_vertex_max_y,
                                           const Shader& vertex_shader) {
  SCOPE_profile_cpu_f("gpu");

  const RegisterFile& regs = register_file_;

  auto pa_sc_window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();
  int32_t window_y_offset = pa_sc_window_offset.window_y_offset;

  // Scissor.
  auto pa_sc_window_scissor_br = regs.Get<reg::PA_SC_WINDOW_SCISSOR_BR>();
  int32_t scissor_bottom = int32_t(pa_sc_window_scissor_br.br_y);
  bool scissor_window_offset = !regs.Get<reg::PA_SC_WINDOW_SCISSOR_TL>().window_offset_disable;
  if (scissor_window_offset) {
    scissor_bottom += window_y_offset;
  }
  auto pa_sc_screen_scissor_br = regs.Get<reg::PA_SC_SCREEN_SCISSOR_BR>();
  scissor_bottom = std::min(scissor_bottom, int32_t(pa_sc_screen_scissor_br.br_y));
  uint32_t max_y = uint32_t(std::max(scissor_bottom, int32_t(0)));

  if (regs.Get<reg::PA_CL_CLIP_CNTL>().clip_disable) {
    // Actual extent from the vertices.
    if (try_to_estimate_vertex_max_y && REXCVAR_GET(execute_unclipped_draw_vs_on_cpu)) {
      bool estimate_vertex_max_y;
      if (REXCVAR_GET(execute_unclipped_draw_vs_on_cpu_with_scissor)) {
        estimate_vertex_max_y = true;
      } else {
        estimate_vertex_max_y = false;
        if (scissor_bottom >= xenos::kTexture2DCubeMaxWidthHeight) {
          // Handle just the usual special 8192x8192 case in Direct3D 9 - 8192
          // may be a normal render target height (80x8192 is well within the
          // EDRAM size, for instance), no need to process the vertices on the
          // CPU in this case.
          int32_t scissor_right = int32_t(pa_sc_window_scissor_br.br_x);
          if (scissor_window_offset) {
            scissor_right += pa_sc_window_offset.window_x_offset;
          }
          scissor_right = std::min(scissor_right, int32_t(pa_sc_screen_scissor_br.br_x));
          if (scissor_right >= xenos::kTexture2DCubeMaxWidthHeight) {
            estimate_vertex_max_y = true;
          }
        }
      }
      if (estimate_vertex_max_y) {
        max_y = std::min(max_y, EstimateVertexMaxY(vertex_shader));
      }
    }
  } else {
    // Viewport. Though the Xenos itself doesn't have an implicit viewport
    // scissor (it's set by Direct3D 9 when a viewport is used), on hosts, it
    // usually exists and can't be disabled.
    auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
    float viewport_bottom = 0.0f;
    // First calculate all the integer.0 or integer.5 offsetting exactly at full
    // precision.
    if (regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable) {
      viewport_bottom += float(window_y_offset);
    }
    if (regs.Get<reg::PA_SU_VTX_CNTL>().pix_center == xenos::PixelCenter::kD3DZero) {
      viewport_bottom += 0.5f;
    }
    // Then apply the floating-point viewport offset.
    if (pa_cl_vte_cntl.vport_y_offset_ena) {
      viewport_bottom += regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YOFFSET);
    }
    viewport_bottom += pa_cl_vte_cntl.vport_y_scale_ena
                           ? std::abs(regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YSCALE))
                           : 1.0f;
    // Using floor, or, rather, truncation (because maxing with zero anyway)
    // similar to how viewport scissoring behaves on real AMD, Intel and Nvidia
    // GPUs on Direct3D 12 (but not WARP), also like in
    // draw_util::GetHostViewportInfo.
    // max(0.0f, viewport_bottom) to drop NaN and < 0 - max picks the first
    // argument in the !(a < b) case (always for NaN), min as float (max_y is
    // well below 2^24) to safely drop very large values.
    max_y = uint32_t(std::min(float(max_y), std::max(0.0f, viewport_bottom)));
  }

  return max_y;
}

}  // namespace rex::graphics
