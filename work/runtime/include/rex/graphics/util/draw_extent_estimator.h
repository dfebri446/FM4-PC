#pragma once
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

#include <cstdint>
#include <optional>

#include <rex/graphics/pipeline/shader/interpreter.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/register_file.h>
#include <rex/memory.h>

namespace rex::graphics {

class DrawExtentEstimator {
 public:
  DrawExtentEstimator(const RegisterFile& register_file, const memory::Memory& memory)
      : register_file_(register_file),
        memory_(memory),
        shader_interpreter_(register_file, memory) {}

  // The shader must have its ucode analyzed.
  uint32_t EstimateVertexMaxY(const Shader& vertex_shader);
  uint32_t EstimateMaxY(bool try_to_estimate_vertex_max_y, const Shader& vertex_shader);
  // For a single screen-space rectangle (kRectangleList, 3 vertices, clipping
  // disabled): its corners in render target pixels after the viewport, window and
  // half-pixel offsets (x0, y0, x1, y1), and whether all its vertices have the
  // same Z and W, and the same values of the interpolators in interpolator_mask.
  // False if the vertices can't be evaluated.
  bool EstimateRectangleBounds(const Shader& vertex_shader, float bounds_out[4],
                               bool* flat_z_out = nullptr, uint32_t interpolator_mask = 0,
                               bool* flat_interpolators_out = nullptr);

 private:
  class PositionYExportSink : public ShaderInterpreter::ExportSink {
   public:
    void Export(ucode::ExportRegister export_register, const float* value,
                uint32_t value_mask) override;

    void Reset() {
      position_x_.reset();
      position_y_.reset();
      position_z_.reset();
      interpolators_written_ = 0;
      position_w_.reset();
      point_size_.reset();
      vertex_kill_.reset();
    }

    const std::optional<float>& position_x() const { return position_x_; }
    const std::optional<float>& position_y() const { return position_y_; }
    const std::optional<float>& position_z() const { return position_z_; }
    // Interpolator i (4 components) if its bit is set in interpolators_written().
    const float* interpolator(uint32_t i) const { return interpolators_[i]; }
    uint32_t interpolators_written() const { return interpolators_written_; }
    const std::optional<float>& position_w() const { return position_w_; }
    const std::optional<float>& point_size() const { return point_size_; }
    const std::optional<uint32_t>& vertex_kill() const { return vertex_kill_; }

   private:
    std::optional<float> position_x_;
    std::optional<float> position_y_;
    std::optional<float> position_z_;
    float interpolators_[16][4] = {};
    uint32_t interpolators_written_ = 0;
    std::optional<float> position_w_;
    std::optional<float> point_size_;
    std::optional<uint32_t> vertex_kill_;
  };

  const RegisterFile& register_file_;
  const memory::Memory& memory_;

  ShaderInterpreter shader_interpreter_;
};

}  // namespace rex::graphics
