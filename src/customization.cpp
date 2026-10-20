#include "mega/world.hpp"
#include <algorithm>
#include <cmath>
namespace mega {
namespace {
void appearance(const Appearance &a) {
  if (a.primary < 0 || a.primary > 7 || a.secondary < 0 || a.secondary > 7 || a.style < 0 ||
      a.style > 2 || a.accessory < 0 || a.accessory > 2)
    throw std::runtime_error("Unsupported customization option");
}
void canAct(const Resident &r) {
  if (!r.alive || !r.present || r.surrendered || r.activity == Activity::Jailed ||
      r.activity == Activity::Restrained || r.activity == Activity::Escorted ||
      r.activity == Activity::Fighting)
    throw std::runtime_error("Resident cannot do this now");
}
} // namespace
void World::customizeHome(Id actor, Id unit, const HomeStyle &style) {
  const auto &r = residents.at(actor);
  auto &h = housing.at(unit);
  canAct(r);
  if (!h.active || (h.owner != r.account && h.tenant != actor))
    throw std::runtime_error("This is someone else's living space");
  if (style.frame < 0 || style.frame > 7 || style.linen < 0 || style.linen > 7 ||
      style.blanket < 0 || style.blanket > 7 || style.closet < 0 || style.closet > 7)
    throw std::runtime_error("Unsupported furniture finish");
  // Tenants may dress their beds and operate storage, but cannot repaint landlord fixtures.
  if (h.owner != r.account && (style.frame != h.decor.frame || style.closet != h.decor.closet))
    throw std::runtime_error("Repainting rented furniture requires its owner's permission");
  h.decor = style;
}
void World::customizeClothes(Id actor, Id item, const Appearance &look) {
  const auto &r = residents.at(actor);
  auto &i = items.at(item);
  canAct(r);
  appearance(look);
  if (i.owner != r.account || i.carrier != actor || i.kind != ItemKind::Clothes ||
      i.reservedAt != none || i.consumed)
    throw std::runtime_error("Only clothing in your possession can be customized");
  i.look = look;
}
void World::customizeVehicle(Id actor, Id vehicle, const Appearance &look) {
  const auto &r = residents.at(actor);
  auto &v = vehicles.at(vehicle);
  canAct(r);
  appearance(look);
  if (v.owner != r.account || v.driver != none || std::abs(v.speed) > .05f)
    throw std::runtime_error("Only an owned, stopped, unoccupied vehicle can be customized");
  v.look = look;
}
bool World::stealItem(Id thief, Id victim, Id item, bool robbery) {
  auto &a = residents.at(thief);
  auto &v = residents.at(victim);
  auto &i = items.at(item);
  canAct(a);
  if (thief == victim || !v.alive || !v.present || !near(thief, position(victim), 1.5f) ||
      i.owner != v.account || i.carrier != victim || i.reservedAt != none || i.order != none ||
      i.consumed || v.outfit == item ||
      std::find(v.items.begin(), v.items.end(), item) == v.items.end())
    throw std::runtime_error(
        "The item must be within reach; worn, reserved or delivery items cannot be snatched");
  bool noticed = robbery || v.activity != Activity::Sleeping;
  if (noticed)
    report(CrimeKind::Theft, thief, victim, robbery);
  bool success =
      robbery ? v.surrendered || a.strength > v.strength * 1.6f : random() < (noticed ? .6f : .9f);
  if (success) {
    if (i.stolenFrom == none)
      i.stolenFrom = i.owner;
    i.owner = a.account;
    i.carrier = thief;
    i.pos = position(thief);
    std::erase(v.items, item);
    a.items.push_back(item);
    log("crime.stolen", thief, victim);
  } else
    log("crime.theft_failed", thief, victim);
  if (noticed) {
    v.happiness = std::max(0.f, v.happiness - .2f);
    if (!v.surrendered && (robbery || v.aggression > .55f)) {
      clockOut(thief);
      clockOut(victim);
      leaveQueue(thief);
      leaveQueue(victim);
      a.pos = position(thief);
      v.pos = position(victim);
      a.route.clear();
      v.route.clear();
      ++a.routeVersion;
      ++v.routeVersion;
      a.task = v.task = TaskKind::None;
      a.activity = v.activity = Activity::Fighting;
      a.opponent = victim;
      v.opponent = thief;
      if (robbery && success)
        surrender(victim);
    }
  }
  return success;
}
} // namespace mega
