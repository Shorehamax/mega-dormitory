#include "mega/world.hpp"
#include <algorithm>
#include <cmath>
namespace mega {
void World::generateLifts() {
  for (auto &f : facilities)
    if (f.kind == FacilityKind::Elevator && !f.stops.empty() &&
        std::none_of(lifts.begin(), lifts.end(),
                     [&](const LiftCar &l) { return l.facility == f.id; })) {
      LiftCar l;
      l.id = Id(lifts.size());
      l.facility = f.id;
      l.y = l.startY = l.targetY = f.stops.front() * 3.f;
      l.doorsUntil = time;
      lifts.push_back(l);
      for (int stop : f.stops)
        invalidateGeometry(stop);
    }
}
Vec3 World::liftDoor(int floor) const {
  return {layout(floor).shaftWidth() + 2.2f, floor * 3.f, 0};
}
float World::liftHeight(Id id) const {
  const auto &l = lifts.at(id);
  if (!l.moving || l.arriveAt <= l.departAt)
    return l.y;
  float t = std::clamp(float((time - l.departAt) / (l.arriveAt - l.departAt)), 0.f, 1.f);
  t = t * t * (3 - 2 * t);
  return l.startY + (l.targetY - l.startY) * t;
}
Vec3 World::liftPassengerPosition(Id id) const {
  const auto &l = lifts.at(residents.at(id).lift);
  for (auto &p : l.passengers)
    if (p.resident == id) {
      Vec3 seat = p.seat + Vec3{0, liftHeight(l.id), 0};
      float t = std::clamp(float((time - p.start) / std::max(.01, p.until - p.start)), 0.f, 1.f);
      return p.exiting ? seat + (p.from - seat) * t : p.from + (seat - p.from) * t;
    }
  return residents.at(id).pos;
}
void World::cancelLift(Id id) {
  if (residents.at(id).lift != none)
    throw std::runtime_error("Wait for the lift to reach a landing");
  for (auto &l : lifts) {
    std::erase_if(l.calls, [&](const LiftCall &c) { return c.resident == id; });
    std::erase_if(l.passengers, [&](const LiftPassenger &p) { return p.resident == id; });
  }
  liftJourneys.erase(id);
}
bool World::planLift(Id id, Vec3 destination, TaskKind task, Id target) {
  auto &r = residents.at(id);
  Vec3 from = position(id);
  if (config.layoutVersion == 1 || r.medicalDuty != none || r.bodyCarrying != none ||
      task == TaskKind::Escort || task == TaskKind::Investigate ||
      std::abs(from.y - destination.y) < 24)
    return false;
  int floor = std::clamp(int(std::round(from.y / 3)), 0, config.floors - 1),
      goal = std::clamp(int(std::round(destination.y / 3)), 0, config.floors - 1);
  Id best = none;
  int start = 0, end = 0;
  float bestGain = 8;
  for (const auto &l : lifts) {
    const auto &f = facilities[l.facility];
    if (!operational(f.id) || !accessAllowed(id, f.id) || f.stops.size() < 2 ||
        l.calls.size() >= 64 ||
        (!f.whitelist.contains(id) && r.account != f.owner &&
         !(f.subscribers.contains(id) && f.subscribers.at(id) > time) &&
         accounts[r.account].balance < f.price))
      continue;
    int a = *std::min_element(f.stops.begin(), f.stops.end(), [&](int x, int y) {
      return std::abs(x - floor) < std::abs(y - floor);
    });
    int b = *std::min_element(f.stops.begin(), f.stops.end(), [&](int x, int y) {
      return std::abs(x - goal) < std::abs(y - goal);
    });
    float gain =
        std::abs(floor - goal) - std::abs(floor - a) - std::abs(goal - b) - std::abs(a - b) * .25f;
    gain -= float(l.calls.size()) * .4f;
    if (a != b && std::abs(floor - a) <= 12 && gain > bestGain) {
      best = l.id;
      start = a;
      end = b;
      bestGain = gain;
    }
  }
  if (best == none)
    return false;
  travel(id, liftDoor(start), TaskKind::LiftWait, best);
  liftJourneys[id] = {best, end, destination, task, target};
  return true;
}
void World::goByLift(Id id, Id cabin, int destination) {
  const auto &l = lifts.at(cabin);
  const auto &f = facilities[l.facility];
  int floor = int(std::round(position(id).y / 3));
  if (f.stops.empty() || std::find(f.stops.begin(), f.stops.end(), destination) == f.stops.end())
    throw std::runtime_error("Unserved lift destination");
  int start = *std::min_element(f.stops.begin(), f.stops.end(), [&](int a, int b) {
    return std::abs(a - floor) < std::abs(b - floor);
  });
  if (start == destination)
    throw std::runtime_error("Choose a different lift stop");
  travel(id, liftDoor(start), TaskKind::LiftWait, cabin);
  liftJourneys[id] = {cabin, destination, liftDoor(destination), TaskKind::None, none};
}
void World::requestLift(Id id, Id cabin, int destination) {
  auto &r = residents.at(id);
  auto &l = lifts.at(cabin);
  auto &f = facilities[l.facility];
  int floor = int(std::round(position(id).y / 3));
  if (!r.alive || !r.present || r.lift != none || r.ambulance != none || r.carriedBy != none ||
      r.vehicle != none || r.surrendered || r.activity == Activity::Jailed ||
      r.activity == Activity::Escorted || !operational(f.id) || !accessAllowed(id, f.id) ||
      !near(id, liftDoor(floor), 3) || floor == destination || l.calls.size() >= 64 ||
      std::find(f.stops.begin(), f.stops.end(), floor) == f.stops.end() ||
      std::find(f.stops.begin(), f.stops.end(), destination) == f.stops.end())
    throw std::runtime_error("Reach a served lift landing and choose another stop");
  if (std::any_of(l.calls.begin(), l.calls.end(),
                  [&](const LiftCall &c) { return c.resident == id; }))
    return;
  if (!liftJourneys.contains(id))
    liftJourneys[id] = {cabin, destination, liftDoor(destination), TaskKind::None, none};
  clockOut(id);
  leaveQueue(id);
  r.pos = position(id);
  r.route.clear();
  ++r.routeVersion;
  r.activity = Activity::Queuing;
  r.task = TaskKind::None;
  l.calls.push_back({id, floor, destination, time});
  log("lift.called", id, l.facility);
}
void World::liftTick() {
  for (auto &l : lifts) {
    auto &f = facilities[l.facility];
    for (auto &b : remains)
      if (b.lift == l.id) {
        Vec3 previous = b.pos;
        b.pos.y = liftHeight(l.id);
        if (!l.moving || time >= l.arriveAt) {
          b.pos = accessible(liftDoor(int(std::round(liftHeight(l.id) / 3))) + Vec3{2, 0, 0});
          b.lift = none;
        }
        for (Id item : residents[b.resident].items)
          if (items[item].carrier == none && !items[item].consumed &&
              length(items[item].pos - previous) < 2)
            items[item].pos = items[item].pos + b.pos - previous;
        residents[b.resident].pos = b.pos;
      }
    if (l.moving) {
      if (time < l.arriveAt)
        continue;
      l.y = l.targetY;
      l.moving = false;
      l.doorsUntil = time + 4 * (1440 / config.dayMinutes);
    }
    int floor = int(std::round(l.y / 3));
    std::vector<std::pair<Id, LiftJourney>> completed;
    for (auto it = l.passengers.begin(); it != l.passengers.end();) {
      auto &p = *it;
      auto &r = residents[p.resident];
      if (!r.alive || !r.present) {
        r.lift = none;
        liftJourneys.erase(r.id);
        it = l.passengers.erase(it);
        continue;
      }
      if (p.destination == floor && !p.exiting && time >= p.until) {
        p.exiting = true;
        p.from = liftDoor(floor);
        p.start = time;
        p.until =
            time + length(p.from - (p.seat + Vec3{0, l.y, 0})) / 1.6 * (1440 / config.dayMinutes);
        l.doorsUntil = std::max(l.doorsUntil, p.until + 30);
      }
      if (p.exiting && time >= p.until) {
        r.pos = p.from;
        r.lift = none;
        r.activity = Activity::Idle;
        crowd.erase(r.id);
        if (liftJourneys.contains(r.id))
          completed.push_back({r.id, liftJourneys.at(r.id)});
        liftJourneys.erase(r.id);
        it = l.passengers.erase(it);
        continue;
      }
      ++it;
    }
    for (auto &[id, j] : completed)
      try {
        travel(id, j.goal, j.task, j.target);
      } catch (const std::runtime_error &) {
        residents[id].activity = Activity::Idle;
      }
    // Advance each landing's physical line. The cabin waits for people to walk aboard.
    std::map<int, int> rows;
    for (auto it = l.calls.begin(); it != l.calls.end();) {
      auto call = *it;
      auto &r = residents[call.resident];
      if (!r.alive || !r.present || !liftJourneys.contains(r.id)) {
        it = l.calls.erase(it);
        continue;
      }
      if (!operational(f.id) || time - call.called > 3 * 3600) {
        auto j = liftJourneys.at(r.id);
        liftJourneys.erase(r.id);
        r.pos = position(r.id);
        r.route.clear();
        r.activity = Activity::Idle;
        it = l.calls.erase(it);
        try {
          routeTo(r.id, j.goal);
          r.task = j.task;
          r.target = j.target;
          r.activity = Activity::Walking;
          schedule(std::max(time + .01, r.route.back().time), EventKind::Arrive, r.id, none,
                   r.routeVersion);
        } catch (const std::runtime_error &) {
        }
        continue;
      }
      int row = rows[call.from]++;
      if (call.from == floor && l.passengers.size() < std::size_t(f.stations) &&
          near(r.id, liftDoor(floor), 2.8f)) {
        try {
          payLift(r.id, f.id, call.to);
        } catch (const std::runtime_error &) {
          liftJourneys.erase(r.id);
          r.activity = Activity::Idle;
          it = l.calls.erase(it);
          continue;
        }
        Vec3 here = position(r.id);
        r.route.clear();
        ++r.routeVersion;
        r.lift = l.id;
        r.activity = Activity::Using;
        crowd.erase(r.id);
        int seat = 0;
        for (; seat < f.stations; ++seat) {
          Vec3 proposed{-1.5f + (seat % 4) * .8f, 0, -.99f + (seat / 4) * .66f};
          if (std::none_of(l.passengers.begin(), l.passengers.end(),
                           [&](const LiftPassenger &p) { return length(p.seat - proposed) < .2f; }))
            break;
        }
        LiftPassenger p;
        p.resident = r.id;
        p.destination = call.to;
        p.from = here;
        p.seat = {-1.5f + (seat % 4) * .8f, 0, -.99f + (seat / 4) * .66f};
        p.start = time;
        p.until =
            time + length(here - (p.seat + Vec3{0, l.y, 0})) / 1.6 * (1440 / config.dayMinutes);
        l.doorsUntil = std::max(l.doorsUntil, p.until + 30);
        l.passengers.push_back(p);
        it = l.calls.erase(it);
        continue;
      }
      Vec3 slot = accessible(liftDoor(call.from) + Vec3{.4f, 0, float(row) * .8f});
      if (r.route.empty() || length(r.route.back().p - slot) > .1f)
        try {
          routeTo(r.id, slot);
        } catch (const std::runtime_error &) {
        }
      ++it;
    }
    if (time < l.doorsUntil || !operational(f.id))
      continue;
    int destination = floor;
    for (auto &p : l.passengers)
      if (!p.exiting) {
        destination = p.destination;
        break;
      }
    if (destination == floor && !l.calls.empty())
      destination = l.calls.front().from;
    if (destination == floor)
      continue;
    l.startY = l.y;
    l.targetY = destination * 3.f;
    l.departAt = time;
    l.arriveAt = time + (std::abs(l.targetY - l.y) / 6 + 2) * (1440 / config.dayMinutes);
    l.moving = true;
  }
}
} // namespace mega
