#include "mega/world.hpp"
#include <algorithm>
namespace mega {
bool World::hasGun(Id id) const {
  auto &r = residents.at(id);
  bool gun = false, ammo = false;
  for (Id item : r.items) {
    auto &i = items[item];
    if (i.owner != r.account || i.carrier != id || i.consumed || i.reservedAt != none)
      continue;
    if (i.kind == ItemKind::Handgun && i.durability > 0)
      gun = true;
    if (i.kind == ItemKind::Ammunition && i.quantity > 0)
      ammo = true;
  }
  return gun && ammo;
}
bool World::lineOfSight(Vec3 from, Vec3 to) const {
  if (std::abs(from.y - to.y) > 2.5f)
    return false;
  Vec3 delta = to - from;
  int floor = std::clamp(int(std::floor(from.y / 3)), 0, config.floors - 1);
  for (auto &box : floorObstacles(floor)) {
    float lo = 0, hi = 1;
    for (int axis = 0; axis < 3; ++axis) {
      float a = axis == 0   ? from.x
                : axis == 1 ? from.y
                            : from.z,
            d = axis == 0   ? delta.x
                : axis == 1 ? delta.y
                            : delta.z;
      float minimum = axis == 0   ? box.min.x
                      : axis == 1 ? box.min.y
                                  : box.min.z,
            maximum = axis == 0   ? box.max.x
                      : axis == 1 ? box.max.y
                                  : box.max.z;
      if (std::abs(d) < .00001f) {
        if (a < minimum || a > maximum) {
          lo = 2;
          break;
        }
      } else {
        float x = (minimum - a) / d, y = (maximum - a) / d;
        if (x > y)
          std::swap(x, y);
        lo = std::max(lo, x);
        hi = std::min(hi, y);
      }
    }
    if (lo <= hi && hi > 0 && lo < 1)
      return false;
  }
  return true;
}
void World::shoot(Id attacker, Id victim) {
  auto &r = residents.at(attacker);
  auto &target = residents.at(victim);
  Vec3 from = position(attacker) + Vec3{0, 1.3f, 0}, to = position(victim) + Vec3{0, 1.25f, 0};
  if (!hasGun(attacker) || !r.alive || !target.alive || !near(attacker, position(victim), 35) ||
      !lineOfSight(from, to))
    throw std::runtime_error("A loaded firearm and a clear line of sight are required");
  // An intervening person intercepts the shot. Violence toward bystanders is still a crime.
  Id hit = victim;
  float nearest = length(to - from);
  Vec3 direction = normalized(to - from);
  for (auto &other : residents)
    if (other.id != attacker && other.id != victim && other.alive && other.present &&
        other.ambulance == none && other.lift == none) {
      Vec3 center = position(other.id) + Vec3{0, 1.1f, 0};
      float projected = dot(center - from, direction);
      if (projected > .4f && projected < nearest &&
          length(center - (from + direction * projected)) < .38f) {
        hit = other.id;
        nearest = projected;
      }
    }
  combatHit(attacker, hit, true, 35);
  for (Id item : r.items) {
    auto &ammo = items[item];
    if (ammo.owner == r.account && ammo.carrier == attacker && !ammo.consumed &&
        ammo.kind == ItemKind::Ammunition && ammo.quantity > 0) {
      --ammo.quantity;
      if (ammo.quantity == 0)
        ammo.consumed = true;
      break;
    }
  }
  shots.push_back({attacker, hit, from, position(hit) + Vec3{0, 1.25f, 0}, time});
  if (shots.size() > 256)
    shots.erase(shots.begin(), shots.begin() + 128);
  log("combat.shot", attacker, hit);
}
} // namespace mega
