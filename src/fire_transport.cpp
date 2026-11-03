#include "mega/world.hpp"
#include <algorithm>
#include <cmath>
namespace mega {
namespace {
struct Opening {
  Vec3 pos;
  float flow;
};
std::vector<Opening> openings(const World &w, int floor, Vec3 origin) {
  std::vector<Opening> out;
  auto l = w.layout(floor);
  auto add = [&](float x, float z, float flow) {
    Vec3 p{x, floor * 3.f, z};
    if (w.fireOpening(floor, p))
      out.push_back({p, flow});
  };
  if (!l.deck)
    add(std::clamp(origin.x, -l.halfWidth() + .4f, l.halfWidth() - .4f),
        std::clamp(origin.z, -l.halfDepth() + .4f, l.halfDepth() - .4f), 1);
  if (floor > 0) {
    add(std::clamp(origin.x, -l.shaftWidth() + .4f, l.shaftWidth() - .4f),
        std::clamp(origin.z, -l.shaftDepth() + .4f, l.shaftDepth() - .4f), 1);
    add(0, 0, 1);
    if (l.version > 1)
      for (int i = 0; i < l.ladderCount(); ++i) {
        auto p = l.ladder(i, floor * 3.f);
        add(p.x, p.z, .45f);
      }
  }
  for (const auto &cut : l.cuts) {
    float flow = std::clamp(std::sqrt((cut.x1 - cut.x0) * (cut.z1 - cut.z0)) / 4, .2f, 1.f);
    add((cut.x0 + cut.x1) * .5f, (cut.z0 + cut.z1) * .5f, flow);
    add(std::clamp(origin.x, cut.x0 + .2f, cut.x1 - .2f),
        std::clamp(origin.z, cut.z0 + .2f, cut.z1 - .2f), flow);
  }
  return out;
}
float planar(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.z - b.z); }
} // namespace
bool World::gasLine(int floor, Vec3 a, Vec3 b) const {
  if (floor < 0 || floor >= config.floors)
    return false;
  // Smoke can pass under a doorway lintel or over low furniture; full partitions block both.
  for (float height : {2.65f, 1.6f}) {
    a.y = b.y = floor * 3.f + height;
    if (lineOfSight(a, b))
      return true;
  }
  return false;
}
bool World::fireOpening(int floor, Vec3 p) const {
  if (floor <= 0 || floor >= config.floors)
    return false;
  auto l = layout(floor);
  p.y = floor * 3.f;
  if (std::abs(p.x) >= l.halfWidth() - .1f || std::abs(p.z) >= l.halfDepth() - .1f ||
      l.supported(p))
    return false;
  // A freight platform or ramp surface at the landing can seal a nominal slab cut.
  return supportHeight(p, p.y + .02f) < p.y - .05f;
}
float World::fireHeatBetween(Vec3 from, Vec3 to) const {
  int lower = int(std::floor((from.y + .1f) / 3)), upper = int(std::floor((to.y + .1f) / 3));
  if (lower < 0 || upper < lower || upper >= config.floors || upper - lower > 3)
    return 0;
  if (upper == lower) {
    float d = planar(from, to);
    return d < 18 && lineOfSight(from + Vec3{0, 1.6f, 0}, to + Vec3{0, 1.6f, 0}) ? 1 - d / 18 : 0;
  }
  float best = 0;
  for (auto portal : openings(*this, lower + 1, from)) {
    bool open = true;
    for (int f = lower + 2; f <= upper; ++f)
      if (!fireOpening(f, portal.pos)) {
        open = false;
        break;
      }
    float distance = planar(from, portal.pos) + planar(to, portal.pos) + (upper - lower) * 3;
    if (!open || distance >= 24 || !gasLine(lower, from, portal.pos) ||
        !gasLine(upper, portal.pos, to))
      continue;
    best = std::max(best, portal.flow * std::pow(.65f, float(upper - lower)) * (1 - distance / 24));
  }
  return best;
}
void World::smokeTick(const std::map<Id, double> &elapsed) {
  for (const auto &[fid, id] : activeFires) {
    auto &fire = fires[id];
    double dt = elapsed.contains(id) ? elapsed.at(id) : 0;
    if (dt <= 0)
      continue;
    const auto &source = facilities[fid];
    auto previous = fire.smokeLayers;
    std::map<int, SmokeLayer> next;
    for (int floor = source.floor + 1; floor <= std::min(config.floors - 1, source.floor + 8);
         ++floor) {
      SmokeLayer lower;
      if (floor == source.floor + 1)
        lower = {source.pos, fire.smoke};
      else if (previous.contains(floor - 1))
        lower = previous.at(floor - 1);
      float incoming = 0;
      Vec3 outlet{};
      if (lower.density > .001f)
        for (auto portal : openings(*this, floor, lower.pos)) {
          float distance = planar(lower.pos, portal.pos);
          float flow = lower.density * portal.flow * .78f * std::exp(-distance / 18);
          if (distance <= 36 && flow > incoming && gasLine(floor - 1, lower.pos, portal.pos)) {
            incoming = flow;
            outlet = portal.pos;
          }
        }
      SmokeLayer layer = previous.contains(floor) ? previous.at(floor) : SmokeLayer{outlet, 0};
      // Changing outlet must not instantly move an established cloud across a sealed room.
      if (incoming > .001f && layer.density > .001f && planar(layer.pos, outlet) > 1 &&
          !gasLine(floor, layer.pos, outlet))
        incoming = 0;
      else if (incoming > .001f && layer.density > .001f)
        incoming *= std::exp(-planar(layer.pos, outlet) / 18);
      else if (incoming > .001f)
        layer.pos = outlet;
      float blend = float(1 - std::exp(-dt / (incoming > layer.density ? 120 : 180)));
      layer.density += (incoming - layer.density) * blend;
      layer.pos.y = floor * 3.f;
      if (layer.density > .001f)
        next[floor] = layer;
    }
    fire.smokeLayers = std::move(next);
  }
}
std::vector<FirePlume> World::firePlumes() const {
  std::vector<FirePlume> out;
  for (const auto &[fid, id] : activeFires) {
    const auto &fire = fires[id];
    const auto &f = facilities[fid];
    if (fire.smoke > .001f)
      out.push_back({id, f.floor, f.pos, fire.smoke});
    for (const auto &[floor, layer] : fire.smokeLayers)
      out.push_back({id, floor, layer.pos, layer.density});
  }
  return out;
}
float World::smokeAt(Vec3 p) const {
  int floor = int(std::floor((p.y + .1f) / 3));
  float density = 0;
  for (const auto &[fid, id] : activeFires) {
    const auto &fire = fires[id];
    const auto &f = facilities[fid];
    SmokeLayer layer;
    if (floor == f.floor)
      layer = {f.pos, fire.smoke};
    else if (fire.smokeLayers.contains(floor))
      layer = fire.smokeLayers.at(floor);
    else
      continue;
    float d = planar(p, layer.pos);
    if (d < 18 && layer.density > 0 && gasLine(floor, layer.pos, p))
      density += layer.density * (1 - d / 18);
  }
  return std::clamp(density, 0.f, 2.f);
}
std::array<std::uint32_t, 32> World::smokeVisibility(int floor, Vec3 origin) const {
  std::array<std::uint32_t, 32> mask{};
  for (int z = 0; z < 32; ++z)
    for (int x = 0; x < 32; ++x) {
      Vec3 at = origin + Vec3{(x + .5f) * 36 / 32 - 18, 0, (z + .5f) * 36 / 32 - 18};
      if (planar(at, origin) < 18 && gasLine(floor, origin, at))
        mask[z] |= 1u << x;
    }
  return mask;
}
} // namespace mega
