// CPU tests of the post_ao camera math (rex/graphics/d3d12/post_ao_camera.h):
// the projection of the matrices in a guest vertex shader's constants, in both
// layouts FM4 uses, and the view position reconstructed from a stored depth.
// Synthetic cameras only; prints the worst errors and returns 1 on failure.

#include <cmath>
#include <cstdio>

#include <rex/graphics/d3d12/post_ao_camera.h>

namespace {

using namespace rex::graphics::d3d12::post_ao;

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

struct Vec3 {
  double x, y, z;
};
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Normalize(Vec3 a) {
  double l = std::sqrt(Dot(a, a));
  return {a.x / l, a.y / l, a.z / l};
}

// World to view, looking along +Z (left-handed) or -Z (right-handed).
Matrix LookAt(Vec3 eye, Vec3 target, Vec3 up, bool right_handed) {
  Vec3 z = Normalize(right_handed ? Sub(eye, target) : Sub(target, eye));
  Vec3 x = Normalize(Cross(up, z));
  Vec3 y = Cross(z, x);
  Matrix v = Identity();
  const Vec3 axes[3] = {x, y, z};
  for (int i = 0; i < 3; ++i) {
    v.m[i][0] = float(axes[i].x);
    v.m[i][1] = float(axes[i].y);
    v.m[i][2] = float(axes[i].z);
    v.m[i][3] = float(-Dot(axes[i], eye));
  }
  return v;
}

// D3D style perspective: ndc_z 0 at near, 1 at far; w = +vz (LH) or -vz (RH).
Matrix Perspective(double fov_y, double aspect, double near_z, double far_z, double off_center_x,
                   bool right_handed) {
  double y_scale = 1.0 / std::tan(fov_y * 0.5), x_scale = y_scale / aspect;
  double q = far_z / (far_z - near_z);
  double s = right_handed ? -1.0 : 1.0;
  Matrix p = {};
  p.m[0][0] = float(x_scale);
  p.m[0][2] = float(off_center_x * s);
  p.m[1][1] = float(y_scale);
  p.m[2][2] = float(q * s);
  p.m[2][3] = float(-q * near_z);
  p.m[3][2] = float(s);
  return p;
}

Matrix World(double yaw, Vec3 translation, double scale) {
  Matrix w = Identity();
  double c = std::cos(yaw) * scale, s = std::sin(yaw) * scale;
  w.m[0][0] = float(c);
  w.m[0][2] = float(s);
  w.m[1][1] = float(scale);
  w.m[2][0] = float(-s);
  w.m[2][2] = float(c);
  w.m[0][3] = float(translation.x);
  w.m[1][3] = float(translation.y);
  w.m[2][3] = float(translation.z);
  return w;
}

void Transform(const Matrix& m, const double p[4], double out[4]) {
  for (int i = 0; i < 4; ++i) {
    out[i] = 0.0;
    for (int j = 0; j < 4; ++j) out[i] += double(m.m[i][j]) * p[j];
  }
}

double MaxRelativeDifference(const Matrix& a, const Matrix& b) {
  double scale = 0.0, difference = 0.0;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      scale = std::fmax(scale, std::fabs(double(b.m[i][j])));
      difference = std::fmax(difference, std::fabs(double(a.m[i][j]) - double(b.m[i][j])));
    }
  }
  return difference / scale;
}

void TestCamera(const char* name, bool right_handed) {
  const double near_z = 0.25, far_z = 8000.0;
  Matrix view = LookAt({10.0, 2.0, -30.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, right_handed);
  Matrix projection =
      Perspective(50.0 * 3.14159265358979 / 180.0, 16.0 / 9.0, near_z, far_z, 0.01, right_handed);
  Matrix world = World(0.5, {5.0, 0.0, 3.0}, 1.0);
  Matrix wv = Multiply(view, world);
  Matrix wvp = Multiply(projection, wv);
  Matrix view_inverse;
  Check(Invert(view, view_inverse), "view inverse");

  // Both constant layouts, with garbage where the shaders do not read.
  static float constants[256][4];
  for (auto& c : constants) c[0] = c[1] = c[2] = c[3] = 0.5f;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      constants[0 + i][j] = wvp.m[j][i];  // Row vectors: constant i is column i.
      if (j < 3) constants[4 + i][j] = wv.m[j][i];
      constants[128 + i][j] = wvp.m[i][j];  // Column vectors: constant i is row i.
      if (i < 3) {
        constants[140 + i][j] = world.m[i][j];
        constants[243 + i][j] = view_inverse.m[i][j];
      }
    }
  }
  const Viewport viewport = {640.0f, 640.0f, -360.0f, 360.0f, -1.0f, 1.0f};  // Reversed Z.

  struct Case {
    const char* layout;
    CameraLayout kind;
    uint32_t a, b, c;
  } cases[] = {{"wvp+wv rows", CameraLayout::kWvpWvRows, 0, 4, 0},
               {"wvp+world+view inverse", CameraLayout::kWvpWorldViewInverse, 128, 140, 243}};
  for (const Case& test : cases) {
    Matrix wvp_read, wv_read, projection_read;
    bool ok = CameraMatrices(test.kind, constants, test.a, test.b, test.c, wvp_read, wv_read) &&
              ProjectionFromCamera(wvp_read, wv_read, projection_read);
    Check(ok, "projection extraction");
    double projection_error = ok ? MaxRelativeDifference(projection_read, projection) : 1.0;
    Unpack unpack;
    ok = ok && UnpackFromProjection(projection_read, viewport, unpack);
    Check(ok, "unpack constants");

    // Points of the model in front of the camera, from 0.5 m to 7 km.
    // Worst relative error near the camera (up to 500 m) and overall: the
    // projection is float, and with reversed Z its depth terms differ from 1
    // by n / (f - n), so far points lose precision (as in the game's own math).
    double worst = 0.0, worst_near = 0.0;
    const double points[][3] = {{0.0, 0.0, 0.0},       {1.0, 0.5, -1.0},   {-3.0, 2.0, 4.0},
                                {20.0, -1.0, 10.0},    {-40.0, 5.0, 80.0}, {150.0, 3.0, 400.0},
                                {-900.0, 20.0, 3000.0}, {2500.0, 50.0, 6000.0}};
    int visible = 0;
    for (const auto& point : points) {
      double model[4] = {point[0], point[1], point[2], 1.0}, clip[4], view_position[4];
      Transform(wvp, model, clip);
      Transform(wv, model, view_position);
      if (clip[3] <= near_z) continue;
      ++visible;
      double ndc_x = clip[0] / clip[3], ndc_y = clip[1] / clip[3], ndc_z = clip[2] / clip[3];
      float x = float(ndc_x * viewport.x_scale + viewport.x_offset);
      float y = float(ndc_y * viewport.y_scale + viewport.y_offset);
      float depth = float(ndc_z * viewport.z_scale + viewport.z_offset);
      float reconstructed[3];
      ViewFromDepth(unpack, x, y, depth, reconstructed);
      double distance = std::sqrt(view_position[0] * view_position[0] +
                                  view_position[1] * view_position[1] +
                                  view_position[2] * view_position[2]);
      for (int k = 0; k < 3; ++k) {
        double error = std::fabs(reconstructed[k] - view_position[k]) / distance;
        worst = std::fmax(worst, error);
        if (distance < 500.0) worst_near = std::fmax(worst_near, error);
      }
    }
    Check(visible >= 6, "visible test points");
    double far_error = std::fabs(unpack.w_far - far_z) / far_z;
    std::printf(
        "%s %s: projection %.2e, view position %.2e (%.2e within 500 m), far W %.2e, %d points\n",
        name, test.layout, projection_error, worst, worst_near, far_error, visible);
    Check(projection_error < 1.0e-4, "projection matches");
    // 5e-5: 2.5 cm at 500 m, below the 20e4 precision of the game's depth.
    Check(worst_near < 5.0e-5, "view position from depth near the camera");
    Check(worst < 1.0e-3, "view position from depth");
    Check(far_error < 1.0e-3, "far plane W");
  }
}

void TestRejectsOrthographic() {
  Matrix ortho = Identity();
  ortho.m[0][0] = 0.01f;
  ortho.m[1][1] = 0.01f;
  ortho.m[2][2] = 0.001f;
  Unpack unpack;
  Check(!UnpackFromProjection(ortho, {640.0f, 640.0f, -360.0f, 360.0f, 1.0f, 0.0f}, unpack),
        "orthographic rejected");
}

}  // namespace

int main() {
  TestCamera("left-handed", false);
  TestCamera("right-handed", true);
  TestRejectsOrthographic();
  std::printf(failures ? "post_ao camera tests: %d failed\n" : "post_ao camera tests: passed\n",
              failures);
  return failures ? 1 : 0;
}
