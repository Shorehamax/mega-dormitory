#pragma once
#include "math.hpp"
namespace mega {
// Presentation only: collision and interaction checks continue to use the real capsule.
// Keep the eye inside its body's footprint, without filtering mouse look or vertical physics.
struct FirstPersonFollow {
  Vec3 anchor{}, previous{};
  bool initialized = false;
  void reset() { initialized = false; }
  Vec3 update(Vec3 body, float seconds, bool grounded) {
    if (!initialized || !grounded || length(body - previous) > 1.25f) {
      anchor = body;
      initialized = true;
    } else {
      Vec3 error = body - anchor;
      error.y = 0;
      float distance = length(error);
      if (distance > .008f)
        anchor = anchor + error * ((1 - .008f / distance) *
                                   (1 - std::exp(-14.f * std::clamp(seconds, 0.f, .1f))));
      error = body - anchor;
      error.y = 0;
      if (length(error) > .10f)
        anchor = body - normalized(error) * .10f;
      anchor.y = body.y;
    }
    previous = body;
    return anchor;
  }
};
// Observer motion is independent of resident bodies and their physics.
inline Vec3 observerEye(Vec3 target, float yaw, float pitch, float distance) {
  float horizontal = std::cos(pitch) * distance;
  return target +
         Vec3{std::sin(yaw) * horizontal, std::sin(pitch) * distance, std::cos(yaw) * horizontal};
}
// Shift the projected image into the uncovered viewport without rotating the camera.
inline Mat4 observerProjection(float width, float height, float coveredWidth,
                               float verticalFov = 1.02f) {
  width = std::max(1.f, width);
  height = std::max(1.f, height);
  auto projection = perspective(std::clamp(verticalFov, .8f, 1.8f), width / height, .08f, 5000);
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
