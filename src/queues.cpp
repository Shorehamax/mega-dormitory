#include "mega/world.hpp"
#include <algorithm>
#include <cmath>
namespace mega {
std::vector<Vec3> World::queueSlots(Id fid, std::size_t count) const {
  const auto &f = facilities.at(fid);
  if (count > 65)
    throw std::runtime_error("The waiting area is full");
  std::vector<Vec3> slots;
  for (std::size_t i = 0; i < count; ++i) {
    int row = int(i / 8), col = int(i % 8);
    if (row % 2)
      col = 7 - col;
    Vec3 dest =
        config.layoutVersion == 1
            ? Vec3{(f.pos.x < 0 ? -1.f : 1.f) * (19.f - col * .75f), f.floor * 3.f,
                   std::clamp(f.pos.z, -12.f, 6.f) + 1.8f + row * .8f}
            : f.pos + Vec3{-3.5f + col, 0,
                           std::max(3.2f, 2.4f + float((f.stations + 3) / 4) * .8f) + row * .85f};
    auto vacant = [&](Vec3 p) {
      return clearPosition(p) &&
             std::none_of(slots.begin(), slots.end(), [&](Vec3 q) { return length(p - q) < .7f; });
    };
    if (!vacant(dest)) {
      bool found = false;
      for (float radius = .4f; radius < 12 && !found; radius += .4f)
        for (int a = 0; a < 32; ++a) {
          Vec3 p = dest + Vec3{std::sin(a * .19635f) * radius, 0, std::cos(a * .19635f) * radius};
          if (vacant(p)) {
            dest = p;
            found = true;
            break;
          }
        }
      if (!found)
        throw std::runtime_error("No free queue standing space");
    }
    slots.push_back(dest);
  }
  return slots;
}
Vec3 World::serviceApproach(Id fid) const {
  const auto &f = facilities.at(fid);
  return queueSlots(fid, std::min<std::size_t>(64, f.queue.size()) + 1).back();
}
bool World::atServiceEntrance(Id person, Id fid) const {
  Vec3 entrance = serviceApproach(fid);
  return near(person, entrance, 1.5f) &&
         lineOfSight(position(person) + Vec3{0, 1.5f, 0}, entrance + Vec3{0, 1.5f, 0});
}
Id World::legacyServiceDeparture(Id person) const {
  const auto &r = residents.at(person);
  if (!r.automatic || r.task != TaskKind::None || r.activity != Activity::Walking ||
      r.target != none || r.route.empty())
    return none;
  int floor = int(std::round(r.route.back().p.y / 3));
  auto found = floorFacilities.find(floor);
  if (found != floorFacilities.end())
    for (Id fid : found->second)
      if (length(r.route.back().p - (facilities[fid].pos + Vec3{5, 0, 1.5f})) < .05f)
        return fid;
  return none;
}
void World::clearServiceArea(Id person, Id fid) {
  Vec3 start = position(person);
  const auto &f = facilities.at(fid);
  // People who have already cleared the service plot can pause where they are.
  bool clear = length(start - f.pos) > 6;
  auto onFloor = floorFacilities.find(f.floor);
  if (onFloor != floorFacilities.end())
    for (Id other : onFloor->second)
      if (!facilities[other].demolished && length(start - facilities[other].pos) < 4.5f)
        clear = false;
  if (clear)
    return;
  // Stable personal destinations avoid a common exit becoming another queue.
  std::uint32_t pattern = std::uint32_t(person + 1) * 2654435761U;
  for (int choice = 0; choice < 32; ++choice) {
    float angle = ((pattern + choice * 23U) % 64) * .09817477f;
    float radius = 7.f + ((pattern >> 6) + choice / 8) % 12;
    Vec3 goal = f.pos + Vec3{std::sin(angle) * radius, 0, std::cos(angle) * radius};
    if (!clearPosition(goal) || dangerAt(goal) > .02f)
      continue;
    bool blocked = false;
    if (onFloor != floorFacilities.end())
      for (Id other : onFloor->second)
        if (!facilities[other].demolished && length(goal - facilities[other].pos) < 4.5f)
          blocked = true;
    if (blocked)
      continue;
    try {
      travel(person, goal, TaskKind::LeaveService, fid);
      return;
    } catch (const std::runtime_error &) {
    }
  }
}
void World::resumeAfterService(Id person, Id fid) {
  auto &r = residents.at(person);
  if (!r.automatic || !r.alive || !r.present)
    return;
  r.pos = position(person);
  r.route.clear();
  ++r.routeVersion;
  r.task = TaskKind::None;
  r.target = none;
  r.activity = Activity::Idle;
  // Keep the resident's existing periodic decision chain; a meal must not spawn
  // another permanent chain of decision events.
  decide(person, false);
  if (r.activity == Activity::Idle && r.route.empty())
    clearServiceArea(person, fid);
}
} // namespace mega
