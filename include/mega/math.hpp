#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace mega {
struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
  Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalized(Vec3 a) {
  float l = length(a);
  return l > 1e-6f ? a * (1 / l) : Vec3{};
}
inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
using Mat4 = std::array<float, 16>;
inline Mat4 identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
inline Mat4 mul(const Mat4 &a, const Mat4 &b) {
  Mat4 r{};
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row)
      for (int k = 0; k < 4; ++k)
        r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
  return r;
}
inline Mat4 perspective(float fov, float aspect, float nearZ, float farZ) {
  float f = 1 / std::tan(fov * .5f);
  return {f / aspect,
          0,
          0,
          0,
          0,
          -f,
          0,
          0,
          0,
          0,
          farZ / (nearZ - farZ),
          -1,
          0,
          0,
          nearZ * farZ / (nearZ - farZ),
          0};
}
// Right-handed view space, Vulkan zero-to-one depth and downward framebuffer Y.
inline Mat4 orthographic(float radius, float nearZ, float farZ) {
  return {1 / radius,
          0,
          0,
          0,
          0,
          -1 / radius,
          0,
          0,
          0,
          0,
          1 / (nearZ - farZ),
          0,
          0,
          0,
          nearZ / (nearZ - farZ),
          1};
}
inline Mat4 lookAt(Vec3 eye, Vec3 target) {
  Vec3 f = normalized(target - eye), s = normalized(cross(f, {0, 1, 0})), u = cross(s, f);
  return {s.x, u.x, -f.x, 0, s.y,          u.y,          -f.y,        0,
          s.z, u.z, -f.z, 0, -dot(s, eye), -dot(u, eye), dot(f, eye), 1};
}
inline bool walkable(Vec3 p) {
  return std::abs(p.x) < 21 && std::abs(p.z) < 17 && !(std::abs(p.x) < 3.8f && std::abs(p.z) < 12);
}
struct Body {
  Vec3 position{}, velocity{};
  bool grounded = false;
};
// The render frame never supplies an unbounded physics timestep.
inline float integrateBody(Body &b, Vec3 desired, float dt) {
  dt = std::clamp(dt, 0.f, 1.f / 30);
  float oldY = b.position.y;
  b.velocity.x = desired.x;
  b.velocity.z = desired.z;
  b.velocity.y -= 9.81f * dt;
  b.position = b.position + b.velocity * dt;
  float impact = 0;
  bool groundCorridor = b.position.y <= .1f && std::abs(b.position.x) < 21 &&
                        std::abs(b.position.z) < 17 && oldY >= 0;
  if ((walkable(b.position) || groundCorridor) && b.velocity.y <= 0) {
    float floor = std::floor(oldY / 3.f) * 3.f;
    if (b.position.y <= floor && oldY >= floor) {
      impact = -b.velocity.y;
      b.position.y = floor;
      b.velocity.y = 0;
      b.grounded = true;
    } else
      b.grounded = false;
  } else
    b.grounded = false;
  return impact;
}
inline float fallDamage(float impactSpeed) {
  return std::max(0.f, (impactSpeed * impactSpeed - 49.f) * .72f);
}
} // namespace mega
