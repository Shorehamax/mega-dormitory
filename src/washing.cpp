#include "mega/world.hpp"
#include <algorithm>
namespace mega {
std::vector<Id> World::washableItems(Id id) const {
  const auto &r = residents.at(id);
  std::vector<Id> out;
  for (Id item : r.items) {
    const auto &i = items.at(item);
    if (i.owner == r.account && i.carrier == id && !i.consumed && i.reservedAt == none &&
        i.durability > 0 && i.cleanliness < .95f && !itemInUse(item) &&
        (i.kind == ItemKind::Tableware || i.kind == ItemKind::RiceCooker))
      out.push_back(item);
  }
  std::stable_sort(out.begin(), out.end(),
                   [&](Id a, Id b) { return items[a].cleanliness < items[b].cleanliness; });
  return out;
}
void World::seekWashbasin(Id id) {
  Id facility = serviceChoice(id, FacilityKind::Washbasin);
  if (facility == none)
    throw std::runtime_error("wash.error.unavailable");
  travel(id, facilities[facility].pos, TaskKind::Service, facility);
}
void World::reserveWashing(Id id, Id facility) {
  auto available = washableItems(id);
  if (available.size() > 4)
    available.resize(4);
  for (Id item : available) {
    auto &i = items[item];
    i.reservedAt = facility;
    i.carrier = none;
    i.pos = facilities[facility].pos;
    i.askingPrice = 0;
    i.seller = none;
  }
}
void World::finishWashing(Id id, Id facility) {
  auto &r = residents.at(id);
  for (Id item : r.items) {
    auto &i = items[item];
    if (i.reservedAt != facility)
      continue;
    if (i.owner == r.account && (i.kind == ItemKind::Tableware || i.kind == ItemKind::RiceCooker))
      i.cleanliness = 1;
    i.reservedAt = none;
    i.carrier = id;
    i.pos = position(id);
  }
  // A hand wash helps general hygiene, but does not replace a full shower.
  r.hygiene = std::max(r.hygiene, std::min(.8f, r.hygiene + .25f));
  ++facilities[facility].waste;
  log("wash.finished", id, facility);
}
} // namespace mega
