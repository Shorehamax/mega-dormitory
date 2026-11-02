#pragma once
#include "math.hpp"
#include <vector>
namespace mega {
// Metres. One source of truth for meshes, navigation and physical collision.
struct DeckCut {
  float x0 = 0, x1 = 0, z0 = 0, z1 = 0;
};
struct Slab {
  Vec3 center{}, size{};
};
struct Layout {
  Layout(int v = 2, float w = 512, float d = 512, bool slab = true)
      : version(v), width(w), depth(d), deck(slab) {}
  int version = 2;
  float width = 512, depth = 512;
  bool deck = true;
  std::vector<DeckCut> cuts;
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
    if (!deck)
      return false;
    if (std::abs(p.x) >= halfWidth() || std::abs(p.z) >= halfDepth())
      return false;
    for (const auto &cut : cuts)
      if (p.x > cut.x0 && p.x < cut.x1 && p.z > cut.z0 && p.z < cut.z1)
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
  std::vector<Slab> slabs(int floor) const {
    if (!deck)
      return {};
    std::vector<DeckCut> pieces;
    float hw = halfWidth(), hd = halfDepth(), sw = shaftWidth(), sd = shaftDepth();
    pieces.push_back({-hw, -sw, -hd, hd});
    pieces.push_back({sw, hw, -hd, hd});
    if (floor == 0)
      pieces.push_back({-sw, sw, -hd, hd});
    else if (version == 1) {
      pieces.push_back({-sw, sw, -hd, -sd});
      pieces.push_back({-sw, sw, sd, hd});
    } else
      for (int sign : {-1, 1}) {
        auto add = [&](float x0, float x1, float z0, float z1) {
          pieces.push_back({x0, x1, sign > 0 ? z0 : -z1, sign > 0 ? z1 : -z0});
        };
        add(-sw, sw, sd, 30.3f);
        add(-sw, sw, 31.7f, hd);
        add(-sw, -1.9f, 30.3f, 31.7f);
        add(1.9f, sw, 30.3f, 31.7f);
        add(-.5f, .5f, 30.3f, 31.7f);
      }
    for (const auto &cut : cuts) {
      std::vector<DeckCut> next;
      for (auto r : pieces) {
        float a = std::max(r.x0, cut.x0), b = std::min(r.x1, cut.x1), c = std::max(r.z0, cut.z0),
              d = std::min(r.z1, cut.z1);
        if (a >= b || c >= d) {
          next.push_back(r);
          continue;
        }
        for (auto q : {DeckCut{r.x0, a, r.z0, r.z1}, DeckCut{b, r.x1, r.z0, r.z1},
                       DeckCut{a, b, r.z0, c}, DeckCut{a, b, d, r.z1}})
          if (q.x1 - q.x0 > .0001f && q.z1 - q.z0 > .0001f)
            next.push_back(q);
      }
      pieces = std::move(next);
    }
    std::vector<Slab> out;
    for (auto r : pieces)
      out.push_back({{(r.x0 + r.x1) / 2, floor * 3.f - .09f, (r.z0 + r.z1) / 2},
                     {r.x1 - r.x0, .18f, r.z1 - r.z0}});
    return out;
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
  Vec3 min, max; // world-space broad-phase bounds
  Vec3 half{};   // optional oriented half-extents; zero keeps legacy AABB geometry
  float yaw = 0;
};
inline Vec3 obstacleHalf(const Obstacle &o) {
  return o.half.x > 0 ? o.half : (o.max - o.min) * .5f;
}
inline Vec3 obstacleLocal(Vec3 p, const Obstacle &o) {
  p = p - (o.min + o.max) * .5f;
  float c = std::cos(o.yaw), s = std::sin(o.yaw);
  return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}
inline Vec3 obstacleWorld(Vec3 p, const Obstacle &o) {
  float c = std::cos(o.yaw), s = std::sin(o.yaw);
  return (o.min + o.max) * .5f + Vec3{p.x * c - p.z * s, p.y, p.x * s + p.z * c};
}
inline Obstacle orientedObstacle(Vec3 center, Vec3 size, float yaw) {
  Vec3 h = size * .5f;
  float c = std::abs(std::cos(yaw)), s = std::abs(std::sin(yaw));
  Vec3 bounds{h.x * c + h.z * s, h.y, h.x * s + h.z * c};
  return {center - bounds, center + bounds, h, yaw};
}
inline float obstacleClearance(Vec3 p, const Obstacle &o) {
  p = obstacleLocal(p, o);
  Vec3 h = obstacleHalf(o);
  float dx = std::abs(p.x) - h.x, dz = std::abs(p.z) - h.z;
  return std::hypot(std::max(0.f, dx), std::max(0.f, dz)) + std::min(0.f, std::max(dx, dz));
}
inline bool overlaps(Vec3 p, const Obstacle &b, float radius = .29f, float height = 1.75f) {
  if (p.y + height <= b.min.y || p.y >= b.max.y)
    return false;
  return obstacleClearance(p, b) < radius;
}
inline float obstacleSeparation(const Obstacle &a, const Obstacle &b) {
  Vec3 d = (b.min + b.max - a.min - a.max) * .5f;
  Vec3 ha = obstacleHalf(a), hb = obstacleHalf(b);
  Vec3 axes[] = {{std::cos(a.yaw), 0, std::sin(a.yaw)},
                 {-std::sin(a.yaw), 0, std::cos(a.yaw)},
                 {std::cos(b.yaw), 0, std::sin(b.yaw)},
                 {-std::sin(b.yaw), 0, std::cos(b.yaw)}};
  float gap = -1e30f;
  for (auto axis : axes) {
    float da = std::abs(axis.x * axes[0].x + axis.z * axes[0].z) * ha.x +
               std::abs(axis.x * axes[1].x + axis.z * axes[1].z) * ha.z;
    float db = std::abs(axis.x * axes[2].x + axis.z * axes[2].z) * hb.x +
               std::abs(axis.x * axes[3].x + axis.z * axes[3].z) * hb.z;
    gap = std::max(gap, std::abs(d.x * axis.x + d.z * axis.z) - da - db);
  }
  return gap;
}
inline bool segmentHitsObstacle(Vec3 from, Vec3 to, const Obstacle &box, float margin = 0) {
  from = obstacleLocal(from, box);
  to = obstacleLocal(to, box);
  Vec3 delta = to - from, h = obstacleHalf(box) + Vec3{margin, margin, margin};
  float lo = 0, hi = 1;
  for (int axis = 0; axis < 3; ++axis) {
    float a = axis == 0 ? from.x : axis == 1 ? from.y : from.z;
    float d = axis == 0 ? delta.x : axis == 1 ? delta.y : delta.z;
    float ext = axis == 0 ? h.x : axis == 1 ? h.y : h.z;
    if (std::abs(d) < .00001f) {
      if (a < -ext || a > ext)
        return false;
    } else {
      float near = (-ext - a) / d, far = (ext - a) / d;
      if (near > far)
        std::swap(near, far);
      lo = std::max(lo, near);
      hi = std::min(hi, far);
      if (lo > hi)
        return false;
    }
  }
  return hi > 0 && lo < 1;
}
} // namespace mega
