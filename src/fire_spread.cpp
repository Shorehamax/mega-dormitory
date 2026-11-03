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
    return 0; // Active freight is handled separately; structural material batches retain their
              // contract model
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
    for (int level = source.floor; level <= std::min(config.floors - 1, source.floor + 3);
         ++level) {
      auto floor = floorFacilities.find(level);
      if (floor == floorFacilities.end())
        continue;
      for (Id other : floor->second) {
        const auto &target = facilities[other];
        if (other == fid || target.demolished || activeFires.contains(other) ||
            !combustible(target.kind) || length(target.pos - source.pos) >= 24)
          continue;
        float transfer = fireHeatBetween(source.pos, target.pos);
        if (transfer <= 0)
          continue;
        heated.insert(other);
        double &exposure = fire.spreadExposure[other];
        exposure += dt * fire.heat * transfer * (.6f + .4f * (1 - target.condition));
        if (exposure >= 300) {
          exposure = 300;
          igniteNext.insert(other);
        }
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
  std::map<Id, Id> shipments;
  for (const auto &s : freight)
    if (!s.finished())
      shipments[s.item] = s.id;
  // Each real object is exposed once. A shipment's original title and import payment remain real.
  for (auto &item : items) {
    if (item.carrier != none && !residents[item.carrier].present)
      continue;
    Id shipment = shipments.contains(item.id) ? shipments.at(item.id) : none;
    float vulnerability = itemVulnerability(item.kind);
    if (shipment != none) {
      const auto &s = freight[shipment];
      auto kind = facilities[s.facility].kind;
      vulnerability = s.waste                                                    ? .85f
                      : kind == FacilityKind::Food || kind == FacilityKind::Cafe ? 1.f
                      : kind == FacilityKind::Workshop                           ? .3f
                                                                                 : .6f;
    }
    if (item.consumed || item.durability <= 0 || vulnerability <= 0)
      continue;
    Vec3 at = item.carrier == none ? item.pos : position(item.carrier);
    if (shipment != none && freight[shipment].readyAt > 0)
      at = freightPosition(shipment);
    double exposure = 0;
    for (auto [fid, id] : activeFires) {
      const auto &fire = fires[id];
      const auto &source = facilities[fid];
      if (fire.contained)
        continue;
      float d = length(at - source.pos);
      double dt = elapsed.contains(id) ? elapsed.at(id) : 0;
      if (d < 12 && dt > 0) {
        float transfer = fireHeatBetween(source.pos, at);
        exposure += dt * fire.heat * transfer * std::max(0.f, 1 - d / 12);
      }
    }
    if (exposure <= 0)
      continue;
    item.durability = std::max(0.f, item.durability - float(exposure / 1200) * vulnerability);
    if (shipment != none) {
      auto &s = freight[shipment];
      int lost = std::clamp(int(std::floor(s.quantity * (1 - item.durability) + .0001f)),
                            s.lostQuantity, item.durability > 0 ? s.quantity - 1 : s.quantity);
      int newlyLost = lost - s.lostQuantity;
      if (s.waste && s.source != none)
        facilities[s.source].waste = std::max(0, facilities[s.source].waste - newlyLost);
      if (s.lostQuantity == 0 && lost > 0)
        log("fire.freight_damaged", s.worker, s.id);
      s.lostQuantity = lost;
      if (item.durability == 0)
        ruinFreight(s.id);
    }
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
