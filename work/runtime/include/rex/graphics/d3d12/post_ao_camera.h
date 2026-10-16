/**
 * @file        graphics/d3d12/post_ao_camera.h
 * @brief       Camera math of the post_ao pass: the projection of a guest
 *              vertex shader's matrices and the depth unpacking constants
 *
 * Header only and free of runtime dependencies, so CPU tests can include it.
 * Matrices are 4x4, m[row][column], applied to column vectors (clip = M * p).
 */

#pragma once

#include <cmath>
#include <cstdint>

namespace rex::graphics::d3d12::post_ao {

struct Matrix {
  float m[4][4];
};

inline Matrix Identity() {
  Matrix r = {};
  for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0f;
  return r;
}

inline Matrix Multiply(const Matrix& a, const Matrix& b) {
  Matrix r = {};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      double sum = 0.0;
      for (int k = 0; k < 4; ++k) sum += double(a.m[i][k]) * b.m[k][j];
      r.m[i][j] = float(sum);
    }
  }
  return r;
}

// General 4x4 inverse (Gauss-Jordan with partial pivoting, in double).
inline bool Invert(const Matrix& a, Matrix& out) {
  double w[4][8];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      w[i][j] = a.m[i][j];
      w[i][4 + j] = i == j ? 1.0 : 0.0;
    }
  }
  for (int c = 0; c < 4; ++c) {
    int pivot = c;
    for (int r = c + 1; r < 4; ++r) {
      if (std::fabs(w[r][c]) > std::fabs(w[pivot][c])) pivot = r;
    }
    if (std::fabs(w[pivot][c]) < 1.0e-12) return false;
    if (pivot != c) {
      for (int j = 0; j < 8; ++j) {
        double t = w[c][j];
        w[c][j] = w[pivot][j];
        w[pivot][j] = t;
      }
    }
    double scale = 1.0 / w[c][c];
    for (int j = 0; j < 8; ++j) w[c][j] *= scale;
    for (int r = 0; r < 4; ++r) {
      if (r == c) continue;
      double f = w[r][c];
      if (f == 0.0) continue;
      for (int j = 0; j < 8; ++j) w[r][j] -= f * w[c][j];
    }
  }
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) out.m[i][j] = float(w[i][4 + j]);
  }
  return true;
}

// How a guest vertex shader keeps its matrices in float constants c<n>, as read
// from its ucode.
enum class CameraLayout : uint32_t {
  // Row vectors: clip = x * c[wvp] + y * c[wvp + 1] + z * c[wvp + 2] + c[wvp + 3],
  // and the view position the same way with c[wv]..c[wv + 3] (xyz used).
  kWvpWvRows,
  // Column vectors: clip.i = dot(c[wvp + i], p) (4 constants), world.i =
  // dot(c[world + i], p) and world = view inverse: world.i = dot(c[view_inverse
  // + i].xyz, v) + c[view_inverse + i].w (3 constants each).
  kWvpWorldViewInverse,
};

// constants: the guest float constants c0..c255 as 256 float4.
// Returns the matrices clip = wvp * p and view = wv * p as column-vector 4x4.
inline bool CameraMatrices(CameraLayout layout, const float (*constants)[4], uint32_t wvp,
                           uint32_t second, uint32_t third, Matrix& wvp_out, Matrix& wv_out) {
  if (wvp + 4 > 256) return false;
  switch (layout) {
    case CameraLayout::kWvpWvRows: {
      if (second + 4 > 256) return false;
      // Constant i is row i of the row-vector matrix, so column i here.
      for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
          wvp_out.m[j][i] = constants[wvp + i][j];
          wv_out.m[j][i] = j < 3 ? constants[second + i][j] : (i == 3 ? 1.0f : 0.0f);
        }
      }
      return true;
    }
    case CameraLayout::kWvpWorldViewInverse: {
      if (second + 3 > 256 || third + 3 > 256) return false;
      Matrix world = Identity(), view_inverse = Identity();
      for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
          wvp_out.m[i][j] = constants[wvp + i][j];
          if (i < 3) {
            world.m[i][j] = constants[second + i][j];
            view_inverse.m[i][j] = constants[third + i][j];
          }
        }
      }
      Matrix view;
      if (!Invert(view_inverse, view)) return false;
      wv_out = Multiply(view, world);
      return true;
    }
  }
  return false;
}

// The projection, clip = P * view position, of the matrices of one draw.
inline bool ProjectionFromCamera(const Matrix& wvp, const Matrix& wv, Matrix& projection_out) {
  Matrix wv_inverse;
  if (!Invert(wv, wv_inverse)) return false;
  projection_out = Multiply(wvp, wv_inverse);
  return true;
}

// The guest viewport of the draw: screen = ndc * scale + offset (pixels for X
// and Y, the stored depth for Z), PA_CL_VPORT_*.
struct Viewport {
  float x_scale, x_offset, y_scale, y_offset, z_scale, z_offset;
};

// Constants of the post_ao shaders for turning a stored depth d at a guest
// pixel center (x, y) into a view position:
//   w = w_numerator / (d * w_depth_mul + w_depth_add),
//   view.z = w * z_from_w,
//   view.x = w * (x * x_mul + x_add), view.y = w * (y * y_mul + y_add).
// w is the clip-space W, positive in front of the camera; the view space is the
// game's own (its handedness does not matter for occlusion angles). The depth
// terms are folded on the CPU in double: with reversed Z, ndc_z - c / b would
// cancel in float far from the camera.
struct Unpack {
  float w_numerator, w_depth_mul, w_depth_add;
  float z_from_w;
  float x_mul, x_add, y_mul, y_add;
  // Clip W at the far plane (stored depth of the far plane), to skip the sky.
  float w_far;
  // Pixels per unit of view X and Y at W = 1 (focal length in pixels), to turn
  // a view-space radius into a screen-space one.
  float pixels_per_unit_x, pixels_per_unit_y;
};

// A perspective projection without shear along Z: P[3] = (0, 0, b, 0) and
// P[2] = (0, 0, c, e); P[0] and P[1] may have an off-center term in column 2.
inline bool UnpackFromProjection(const Matrix& p, const Viewport& viewport, Unpack& out) {
  const float b = p.m[3][2];
  if (std::fabs(b) < 1.0e-6f || std::fabs(p.m[3][0]) > 1.0e-4f * std::fabs(b) ||
      std::fabs(p.m[3][1]) > 1.0e-4f * std::fabs(b) || std::fabs(p.m[3][3]) > 1.0e-4f) {
    return false;  // Not a perspective projection (orthographic or skewed).
  }
  if (std::fabs(p.m[0][0]) < 1.0e-6f || std::fabs(p.m[1][1]) < 1.0e-6f ||
      std::fabs(p.m[2][3]) < 1.0e-12f || std::fabs(viewport.z_scale) < 1.0e-12f ||
      std::fabs(viewport.x_scale) < 1.0e-6f || std::fabs(viewport.y_scale) < 1.0e-6f) {
    return false;
  }
  // ndc_z = (c * vz + e) / (b * vz) = c / b + e / w, with w = b * vz, and
  // ndc_z = (d - z_offset) / z_scale.
  const double c_over_b = double(p.m[2][2]) / b;
  out.w_numerator = p.m[2][3];
  out.w_depth_mul = float(1.0 / viewport.z_scale);
  out.w_depth_add = float(-double(viewport.z_offset) / viewport.z_scale - c_over_b);
  out.z_from_w = float(1.0 / b);
  // ndc_x = (p00 * vx + p02 * vz) / w, ndc_x = (x - x_offset) / x_scale.
  //   vx = w * (ndc_x - p02 / b) / p00.
  out.x_mul = float(1.0 / (double(viewport.x_scale) * p.m[0][0]));
  out.x_add = float((-double(viewport.x_offset) / viewport.x_scale - double(p.m[0][2]) / b) /
                    p.m[0][0]);
  out.y_mul = float(1.0 / (double(viewport.y_scale) * p.m[1][1]));
  out.y_add = float((-double(viewport.y_offset) / viewport.y_scale - double(p.m[1][2]) / b) /
                    p.m[1][1]);
  // The far plane is at ndc_z = 1.
  const double far_denominator = 1.0 - c_over_b;
  out.w_far = std::fabs(far_denominator) > 1.0e-12 ? float(p.m[2][3] / far_denominator) : 0.0f;
  if (!(out.w_far > 0.0f)) out.w_far = 1.0e30f;
  out.pixels_per_unit_x = std::fabs(viewport.x_scale * p.m[0][0]);
  out.pixels_per_unit_y = std::fabs(viewport.y_scale * p.m[1][1]);
  return true;
}

// The view position the post_ao shaders reconstruct (for tests).
inline void ViewFromDepth(const Unpack& u, float x, float y, float stored_depth, float view_out[3]) {
  const float w = u.w_numerator / (stored_depth * u.w_depth_mul + u.w_depth_add);
  view_out[0] = w * (x * u.x_mul + u.x_add);
  view_out[1] = w * (y * u.y_mul + u.y_add);
  view_out[2] = w * u.z_from_w;
}

}  // namespace rex::graphics::d3d12::post_ao
