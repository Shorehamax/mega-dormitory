#include "mega/world.hpp"
#include <algorithm>

namespace mega {
namespace {
bool combustible(FacilityKind kind) {
  return kind != FacilityKind::Water && kind != FacilityKind::Toilet &&
         kind != FacilityKind::Shower && kind != FacilityKind::Washbasin &&
         kind != FacilityKind::Pool && kind != FacilityKind::Elevator &&
         kind != FacilityKind::Ramp && kind != FacilityKind::FreightLift;
}
float itemVulnerability(ItemKind kind) {
  if (kind == ItemKind::Waste || kind == ItemKind::Materials || kind == ItemKind::Extinguisher)
    return 0; // bulk construction/freight contents need their own damage model
  if (kind == ItemKind::Tools || kind == ItemKind::Tableware)
    return .1f;
  return kind == ItemKind::Food || kind == ItemKind::Clothes || kind == ItemKind::Medicine ? 1.f
                                                                                           : .55f;
}
} // namespace
void World::spreadFireTick(const std::map<Id, double> &elapsed) {
  std::set<Id> igniteNext;
  for (const auto &[fid, id] : activeFires) {
    auto &fire = fires[id];
    const auto &source = facilities[fid];
    if (fire.contained || fire.heat < .6f) {
      fire.spreadExposure.clear();
      continue;
    }
    double dt = elapsed.contains(id) ? elapsed.at(id) : 0;
    if (dt <= 0)
      continue;
    std::set<Id> heated;
    auto floor = floorFacilities.find(source.floor);
    if (floor == floorFacilities.end())
      continue;
    for (Id other : floor->second) {
      const auto &target = facilities[other];
      float d = length(target.pos - source.pos);
      if (other == fid || target.demolished || activeFires.contains(other) ||
          !combustible(target.kind) || d >= 18 ||
          !lineOfSight(source.pos + Vec3{0, 1.6f, 0}, target.pos + Vec3{0, 1.6f, 0}))
        continue;
      heated.insert(other);
      double &exposure = fire.spreadExposure[other];
      exposure += dt * fire.heat * (1 - d / 18) * (.6f + .4f * (1 - target.condition));
      if (exposure >= 300) {
        exposure = 300;
        igniteNext.insert(other);
      }
    }
    for (auto &[other, exposure] : fire.spreadExposure)
      if (!heated.contains(other))
        exposure = std::max(0., exposure - dt * 2);
    std::erase_if(fire.spreadExposure, [&](const auto &entry) {
      return igniteNext.contains(entry.first) || activeFires.contains(entry.first) ||
             entry.second <= 0;
    });
  }
  // Apply heat to each actual possession once, using its carrier's real position.
  // Smoke alone does not destroy belongings, and walls block direct heat.
  for (auto &item : items) {
    if (item.carrier != none && !residents[item.carrier].present)
      continue;
    float vulnerability = itemVulnerability(item.kind);
    if (item.consumed || item.durability <= 0 || vulnerability <= 0)
      continue;
    Vec3 at = item.carrier == none ? item.pos : position(item.carrier);
    double exposure = 0;
    for (auto [fid, id] : activeFires) {
      const auto &fire = fires[id];
      const auto &source = facilities[fid];
      if (fire.contained || std::abs(at.y - source.pos.y) >= 2)
        continue;
      float distance = std::hypot(at.x - source.pos.x, at.z - source.pos.z);
      double dt = elapsed.contains(id) ? elapsed.at(id) : 0;
      if (distance < 8 && dt > 0 &&
          lineOfSight(source.pos + Vec3{0, 1.6f, 0}, at + Vec3{0, 1.6f, 0}))
        exposure += dt * fire.heat * (1 - distance / 8);
    }
    if (exposure <= 0)
      continue;
    item.durability = std::max(0.f, item.durability - float(exposure / 1200) * vulnerability);
    if (item.durability == 0) {
      item.askingPrice = 0;
      item.seller = none;
      itemOffers.erase(item.id);
      Id owner = none;
      for (const auto &r : residents)
        if (r.account == item.owner) {
          owner = r.id;
          residents[owner].happiness = std::max(0.f, r.happiness - .05f);
          break;
        }
      log("fire.item_destroyed", owner, item.id);
    }
  }
  // New fires can grow the vector; no references from the traversal survive here.
  for (Id fid : igniteNext)
    if (!activeFires.contains(fid) && !facilities[fid].demolished) {
      ignite(fid, .25f);
      log("fire.spread", facilities[fid].manager, fid);
    }
}
} // namespace mega
