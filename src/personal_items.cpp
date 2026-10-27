#include "mega/world.hpp"
#include <algorithm>
namespace mega {
bool World::itemInUse(Id item) const {
  if (item < 0 || std::size_t(item) >= items.size())
    return false;
  auto found = personalUses.find(items[item].carrier);
  return found != personalUses.end() &&
         (found->second.item == item || found->second.ingredient == item);
}
void World::usePersonalItem(Id id, Id item) {
  auto &r = residents.at(id);
  auto &object = items.at(item);
  if (looseBodies.contains(id))
    throw std::runtime_error("physics.error.airborne");
  if (!r.alive || !r.present || r.surrendered || r.leaving || r.lift != none ||
      r.ambulance != none || r.carriedBy != none || r.vehicle != none || r.ladder >= 0 ||
      r.carrying != none || !r.route.empty() || personalUses.contains(id) ||
      (r.activity != Activity::Idle && r.activity != Activity::Sleeping) ||
      object.owner != r.account || object.carrier != id || object.consumed ||
      object.reservedAt != none || object.durability <= 0 || itemInUse(item))
    throw std::runtime_error("personal.error.busy");
  if (object.kind != ItemKind::Fan && object.kind != ItemKind::Tableware &&
      object.kind != ItemKind::RiceCooker)
    throw std::runtime_error("personal.error.kind");
  if (object.kind != ItemKind::Fan && object.cleanliness < .25f)
    throw std::runtime_error("wash.error.dirty");
  PersonalUse use;
  use.item = item;
  use.origin = position(id);
  use.workedAt = time;
  use.remaining = object.kind == ItemKind::Fan          ? 1200
                  : object.kind == ItemKind::RiceCooker ? 900
                                                        : 600;
  if (residentHomes[id] == none || !housing[residentHomes[id]].active ||
      !near(id, homePosition(residentHomes[id]), .75f))
    throw std::runtime_error("personal.error.home");
  if (object.kind != ItemKind::Fan) {
    for (Id ingredient : r.items)
      if (items[ingredient].kind == ItemKind::Food && items[ingredient].owner == r.account &&
          items[ingredient].carrier == id && !items[ingredient].consumed &&
          items[ingredient].reservedAt == none && !itemInUse(ingredient)) {
        use.ingredient = ingredient;
        break;
      }
    if (use.ingredient == none)
      throw std::runtime_error("personal.error.food");
  }
  needs(id);
  if (!r.alive)
    throw std::runtime_error("personal.error.busy");
  clockOut(id);
  object.askingPrice = 0;
  object.seller = none;
  if (use.ingredient != none) {
    items[use.ingredient].askingPrice = 0;
    items[use.ingredient].seller = none;
  }
  r.activity = Activity::Using;
  r.task = TaskKind::None;
  r.target = none;
  personalUses[id] = use;
  log("personal.started", id, item);
}
void World::stopPersonalUse(Id id) {
  if (!personalUses.erase(id))
    return;
  auto &r = residents.at(id);
  if (r.activity == Activity::Using && r.queuedAt == none)
    r.activity = Activity::Idle;
  log("personal.stopped", id);
}
void World::personalUseTick() {
  std::vector<Id> ids;
  for (const auto &[id, use] : personalUses) {
    (void)use;
    ids.push_back(id);
  }
  for (Id id : ids) {
    auto found = personalUses.find(id);
    if (found == personalUses.end())
      continue;
    auto use = found->second;
    auto &r = residents.at(id);
    auto valid = [&](Id item) {
      const auto &i = items.at(item);
      return i.owner == r.account && i.carrier == id && !i.consumed && i.reservedAt == none &&
             i.durability > 0;
    };
    if (!r.alive || !r.present || r.activity != Activity::Using || r.queuedAt != none ||
        r.surrendered || r.ladder >= 0 || r.vehicle != none || r.lift != none ||
        r.ambulance != none || r.carriedBy != none || !r.route.empty() ||
        length(position(id) - use.origin) > .8f || !valid(use.item) ||
        (use.ingredient != none && !valid(use.ingredient))) {
      stopPersonalUse(id);
      continue;
    }
    needs(id);
    if (!r.alive || !personalUses.contains(id))
      continue;
    auto &current = personalUses.at(id);
    current.remaining = std::max(0., use.remaining - std::max(0., time - use.workedAt));
    current.workedAt = time;
    if (current.remaining > 0)
      continue;
    auto kind = items[use.item].kind;
    personalUses.erase(id);
    if (kind == ItemKind::Fan) {
      r.energy = std::min(1.f, r.energy + .12f);
      r.happiness = std::min(1.f, r.happiness + .025f);
    } else {
      consumeItem(id, use.ingredient);
      r.hunger = std::max(0.f, r.hunger - (kind == ItemKind::RiceCooker ? .15f : .05f));
      r.happiness = std::min(1.f, r.happiness + .015f);
    }
    items[use.item].durability = std::max(0.f, items[use.item].durability - .03f);
    items[use.item].cleanliness = std::max(0.f, items[use.item].cleanliness - .04f);
    r.activity = Activity::Idle;
    log("personal.finished", id, use.item);
  }
}
void World::validatePersonalUses() const {
  std::set<Id> locked;
  for (const auto &[id, use] : personalUses) {
    if (id < 0 || std::size_t(id) >= residents.size() || use.item < 0 ||
        std::size_t(use.item) >= items.size() || !std::isfinite(use.remaining) ||
        use.remaining < 0 || use.remaining > 1200 || !std::isfinite(use.workedAt) ||
        use.workedAt < 0 || use.workedAt > time || !std::isfinite(use.origin.x) ||
        !std::isfinite(use.origin.y) || !std::isfinite(use.origin.z) ||
        !locked.insert(use.item).second)
      throw std::runtime_error("Invalid personal activity");
    auto kind = items[use.item].kind;
    if ((kind != ItemKind::Fan && kind != ItemKind::Tableware && kind != ItemKind::RiceCooker) ||
        ((kind == ItemKind::Fan) != (use.ingredient == none)) ||
        (use.ingredient != none &&
         (use.ingredient < 0 || std::size_t(use.ingredient) >= items.size() ||
          items[use.ingredient].kind != ItemKind::Food || !locked.insert(use.ingredient).second)))
      throw std::runtime_error("Invalid personal activity item");
  }
}
} // namespace mega
