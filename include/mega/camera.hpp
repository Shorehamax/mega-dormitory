#pragma once
#include "math.hpp"
namespace mega {
// Observer motion is independent of resident bodies and their physics.
inline Vec3 observerEye(Vec3 target, float yaw, float pitch, float distance) {
  float horizontal = std::cos(pitch) * distance;
  return target +
         Vec3{std::sin(yaw) * horizontal, std::sin(pitch) * distance, std::cos(yaw) * horizontal};
}
// Shift the projected image into the uncovered viewport without rotating the camera.
inline Mat4 observerProjection(float width, float height, float coveredWidth) {
  width = std::max(1.f, width);
  height = std::max(1.f, height);
  auto projection = perspective(1.02f, width / height, .08f, 5000);
  projection[8] = std::clamp(coveredWidth, 0.f, width) / width;
  return projection;
}
inline void moveObserverHeight(int &floor, float &offset, float delta, int floors) {
  if (floors < 1) {
    floor = 0;
    offset = 0;
    return;
  }
  float altitude = std::clamp(floor * 3.f + offset + delta, 0.f, (floors - 1) * 3.f);
  floor = std::clamp(int(std::floor(altitude / 3)), 0, floors - 1);
  offset = altitude - floor * 3.f;
}
} // namespace mega
