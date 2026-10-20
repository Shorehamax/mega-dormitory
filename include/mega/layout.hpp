#pragma once
#include "math.hpp"
#include <vector>
namespace mega {
// Metres. One source of truth for meshes, navigation and physical collision.
struct Layout {
  int version = 2;
  float width = 512, depth = 512;
  float halfWidth() const { return version == 1 ? 21.f : width / 2; }
  float halfDepth() const { return version == 1 ? 17.f : depth / 2; }
  float shaftWidth() const { return version == 1 ? 3.8f : 6.f; }
  float shaftDepth() const { return version == 1 ? 12.f : 20.f; }
  float bridge() const { return version == 1 ? 14.f : 31.f; }
  int banks() const { return version == 1 ? 4 : 8; }
  int cells() const { return version == 1 ? 8 : 16; }
  int tierSize() const { return banks() * cells(); }
  bool residential(int floor, int floors) const {
    return (version == 1 || floor != 0 || floors == 1) && floor % 10 != 5 && floor % 10 != 9;
  }
  Vec3 bed(int floor, int bed) const {
    int bank = bed % banks(), cell = (bed / banks()) % cells();
    float x = version == 1 ? 7.f + (bank % 2) * 7.f : 10.f + (bank % 4) * 6.f;
    return {(bank < banks() / 2 ? -1.f : 1.f) * x, floor * 3.f,
            version == 1 ? -10.5f + cell * 3.f : -25.5f + cell * 3.4f};
  }
  Vec3 entrance() const { return version == 1 ? Vec3{-12, 0, 14} : Vec3{0, 0, halfDepth() - 8}; }
  int ladderCount() const { return version == 1 ? 1 : 4; }
  Vec3 ladder(int index, float y) const {
    return version == 1 ? Vec3{0, y, 14}
                        : Vec3{index % 2 ? 1.2f : -1.2f, y, index < 2 ? 31.f : -31.f};
  }
  Vec3 landing(int index, float y) const {
    auto p = ladder(index, y);
    p.z += p.z > 0 ? -1.5f : 1.5f;
    return p;
  }
  bool supported(Vec3 p) const {
    if (std::abs(p.x) >= halfWidth() || std::abs(p.z) >= halfDepth())
      return false;
    if (p.y < .1f)
      return true;
    if (std::abs(p.x) < shaftWidth() && std::abs(p.z) < shaftDepth())
      return false;
    if (version > 1)
      for (int i = 0; i < ladderCount(); ++i) {
        auto l = ladder(i, p.y);
        if (std::abs(p.x - l.x) < .7f && std::abs(p.z - l.z) < .7f)
          return false;
      }
    return true;
  }
  Vec3 facility(int floor, int slot) const {
    if (version == 1)
      return {slot % 2 ? 17.f : -17.f, floor * 3.f, -12.f + (slot / 2 % 5) * 5.f};
    // Independent 12 x 14 m service plots; the ground floor has no bed banks.
    float x = floor == 0 ? 12.f + (slot / 8 % 3) * 14.f : 42.f - (slot / 10) * 12.f;
    return {slot % 2 ? x : -x, floor * 3.f,
            floor == 0 ? halfDepth() - 89.f + (slot / 2 % 4) * 14.f
                       : -28.f + (slot / 2 % 5) * 13.f};
  }
};
struct Obstacle {
  Vec3 min, max;
};
inline bool overlaps(Vec3 p, const Obstacle &b, float radius = .29f, float height = 1.75f) {
  if (p.y + height <= b.min.y || p.y >= b.max.y)
    return false;
  float x = std::clamp(p.x, b.min.x, b.max.x), z = std::clamp(p.z, b.min.z, b.max.z);
  return (p.x - x) * (p.x - x) + (p.z - z) * (p.z - z) < radius * radius;
}
} // namespace mega
