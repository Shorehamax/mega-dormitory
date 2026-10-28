#include "mega/world.hpp"
#include <algorithm>
#include <limits>
#include <queue>
namespace mega {
namespace {
bool operationalDriver(const Resident &r) {
  return r.alive && r.present && !r.surrendered && r.activity != Activity::Fighting &&
         r.activity != Activity::Jailed && r.activity != Activity::Restrained &&
         r.activity != Activity::Escorted;
}
float width(VehicleKind k) {
  return k <= VehicleKind::Scooter ? .65f : k == VehicleKind::CompactCar ? 1.7f : 2.05f;
}
float lengthOf(VehicleKind k) {
  return k <= VehicleKind::Scooter ? 1.7f : k == VehicleKind::CompactCar ? 4.2f : 5.1f;
}
bool boxContact(Vec3 pos, float yaw, float hw, float hl, const Obstacle &o) {
  float c = std::cos(yaw), s = std::sin(yaw);
  Vec3 middle = (o.min + o.max) * .5f, ext = (o.max - o.min) * .5f, d = middle - pos;
  return std::abs(d.x) <= ext.x + hw * std::abs(c) + hl * std::abs(s) &&
         std::abs(d.z) <= ext.z + hw * std::abs(s) + hl * std::abs(c) &&
         std::abs(d.x * c + d.z * s) <= hw + ext.x * std::abs(c) + ext.z * std::abs(s) &&
         std::abs(-d.x * s + d.z * c) <= hl + ext.x * std::abs(s) + ext.z * std::abs(c);
}
DeckCut footprint(const RoadLink &r) {
  return {r.pos.x - r.width / 2 - .15f, r.pos.x + r.width / 2 + .15f,
          r.kind == RoadKind::Ramp ? r.pos.z - .15f : r.pos.z - r.length / 2,
          r.kind == RoadKind::Ramp ? r.pos.z + r.length : r.pos.z + r.length / 2};
}
} // namespace
Layout World::layout(int floor) const {
  Layout l{config.layoutVersion, float(floor == 0 ? config.lobbyWidth : config.floorWidth),
           float(floor == 0 ? config.lobbyDepth : config.floorDepth)};
  if (floor >= 0 && std::size_t(floor) < floorPlans.size()) {
    l.width = float(floorPlans[floor].width);
    l.depth = float(floorPlans[floor].depth);
    l.deck = floorPlans[floor].deck;
  }
  for (const auto &r : roads)
    if (r.active && floor >= r.from && floor <= r.to &&
        (r.kind == RoadKind::FreightLift || floor == r.to))
      l.cuts.push_back(footprint(r));
  return l;
}
float World::vehicleWidth(Id id) const { return width(vehicles.at(id).kind); }
float World::vehicleLength(Id id) const { return lengthOf(vehicles.at(id).kind); }
float World::vehicleMass(Id id) const {
  auto k = vehicles.at(id).kind;
  return k == VehicleKind::Bicycle      ? 100
         : k == VehicleKind::Scooter    ? 200
         : k == VehicleKind::CompactCar ? 1500
                                        : 3000;
}
float World::freightHeight(Id id) const {
  const auto &r = roads.at(id);
  if (!r.moving)
    return r.height;
  float t =
      std::clamp(float((time - r.departAt) / std::max(.01, r.arriveAt - r.departAt)), 0.f, 1.f);
  t = t * t * (3 - 2 * t);
  return r.startHeight + (r.targetHeight - r.startHeight) * t;
}
float World::supportHeight(Vec3 p, float ceiling) const {
  float best = -10000;
  for (int floor = std::min(config.floors - 1, int(std::floor((ceiling + .001f) / 3)));
       floor >= std::max(0, int(std::floor((p.y - 6) / 3))); --floor) {
    if (layout(floor).supported({p.x, floor * 3.f, p.z})) {
      best = floor * 3.f;
      break;
    }
  }
  for (const auto &r : roads)
    if (r.active) {
      float h = -10000;
      if (r.kind == RoadKind::Ramp && std::abs(p.x - r.pos.x) <= r.width / 2 && p.z >= r.pos.z &&
          p.z <= r.pos.z + r.length)
        h = r.from * 3.f + (p.z - r.pos.z) / r.length * (r.to - r.from) * 3;
      if (r.kind == RoadKind::FreightLift && std::abs(p.x - r.pos.x) <= r.width / 2 &&
          std::abs(p.z - r.pos.z) <= r.length / 2)
        h = freightHeight(r.id);
      if (h <= ceiling + .001f)
        best = std::max(best, h);
    }
  return best;
}
bool World::vehicleContact(Id id, Vec3 p, float yaw, Id other) const {
  const auto &b = vehicles.at(other);
  if (std::abs(p.y - b.pos.y) >= 1.6f)
    return false;
  Vec3 right{std::cos(yaw), 0, std::sin(yaw)}, front{-std::sin(yaw), 0, std::cos(yaw)},
      br{std::cos(b.yaw), 0, std::sin(b.yaw)}, bf{-std::sin(b.yaw), 0, std::cos(b.yaw)},
      d = b.pos - p;
  for (auto axis : {right, front, br, bf}) {
    float a = std::abs(dot(axis, right)) * vehicleWidth(id) / 2 +
              std::abs(dot(axis, front)) * vehicleLength(id) / 2;
    float extent = std::abs(dot(axis, br)) * vehicleWidth(other) / 2 +
                   std::abs(dot(axis, bf)) * vehicleLength(other) / 2;
    if (std::abs(dot(axis, d)) > a + extent + .02f)
      return false;
  }
  return true;
}
bool World::vehicleFits(Id id, Vec3 pos, float yaw, bool people, Id ignore) const {
  const auto &v = vehicles.at(id);
  float hw = width(v.kind) / 2, hl = lengthOf(v.kind) / 2;
  if (config.layoutVersion == 1 &&
      (std::abs(pos.x) + hw > 3.6f || std::abs(pos.z) + std::max(hl, 2.1f) > 16.7f ||
       std::abs(pos.y) > .1f))
    return false;
  int first = std::max(0, int(std::floor(pos.y / 3))),
      last = std::min(config.floors - 1, int(std::floor((pos.y + 1.6f) / 3)));
  for (int f = first; f <= last; ++f)
    for (const auto &o : floorObstacles(f))
      if (pos.y + 1.55f > o.min.y && pos.y < o.max.y && boxContact(pos, yaw, hw, hl, o))
        return false;
  for (const auto &other : vehicles)
    if (other.id != id && other.id != ignore && other.id != v.towing && other.id != v.towedBy &&
        std::abs(other.pos.y - pos.y) < 1.5f) {
      if (vehicleContact(id, pos, yaw, other.id))
        return false;
    }
  if (people)
    for (const auto &r : residents)
      if (r.id != v.driver && r.alive && r.present && r.vehicle == none && r.lift == none &&
          r.ambulance == none && r.carriedBy == none) {
        auto p = position(r.id);
        if (std::abs(p.y - pos.y) < 1.6f &&
            boxContact(pos, yaw, hw, hl, {p - Vec3{.29f, 0, .29f}, p + Vec3{.29f, 1.7f, .29f}}))
          return false;
      }
  return true;
}
void World::checkRoadSite(Id id) const {
  const auto &r = roads.at(id);
  auto b = footprint(r);
  for (int f = r.from; f <= r.to; ++f) {
    if (!floorPlans[f].deck || structuralFloorLocked(f))
      throw std::runtime_error("structure.error.floor");
    for (float z = b.z0; z <= b.z1; z += .5f)
      for (float x = b.x0; x <= b.x1; x += .5f)
        if (!clearPosition({x, f * 3.f, z}))
          throw std::runtime_error("structure.error.plot");
    for (const auto &p : residents)
      if (p.alive && p.present && p.vehicle == none) {
        auto at = position(p.id);
        if (at.x > b.x0 && at.x < b.x1 && at.z > b.z0 && at.z < b.z1 &&
            std::abs(at.y - f * 3.f) < 2.5f)
          throw std::runtime_error("structure.error.occupied");
      }
  }
}
void World::activateRoad(Id id, bool active) {
  auto &r = roads.at(id);
  if (!active && (r.occupant != none || !r.calls.empty() ||
                  std::any_of(vehicles.begin(), vehicles.end(), [&](const Vehicle &v) {
                    return v.road == id || v.freightLift == id;
                  })))
    throw std::runtime_error("traffic.error.busy");
  if (!active) {
    auto b = footprint(r);
    for (const auto &v : vehicles)
      if (v.pos.x > b.x0 && v.pos.x < b.x1 && v.pos.z > b.z0 && v.pos.z < b.z1 &&
          v.pos.y >= r.from * 3.f - .1f && v.pos.y <= r.to * 3.f + .1f)
        throw std::runtime_error("traffic.error.busy");
    for (const auto &p : residents)
      if (p.alive && p.present) {
        auto at = position(p.id);
        if (at.x > b.x0 && at.x < b.x1 && at.z > b.z0 && at.z < b.z1 &&
            at.y >= r.from * 3.f - .1f && at.y <= r.to * 3.f + .1f)
          throw std::runtime_error("traffic.error.busy");
      }
  }
  r.active = active;
  facilities[r.facility].open = active;
  facilities[r.facility].demolished = !active;
  for (int f = r.from; f <= r.to; ++f)
    invalidateGeometry(f);
}
Id World::proposeRoad(Id actor, RoadKind kind, int from, int to, Vec3 pos, float length) {
  if (!residents.at(actor).alive || !residents[actor].present ||
      (actor != dormOwner && config.mode != Mode::Cheat))
    throw std::runtime_error("structure.error.permission");
  if (config.layoutVersion < 2 || from < 0 || to <= from || to >= config.floors ||
      !std::isfinite(pos.x) || !std::isfinite(pos.z) || !std::isfinite(length) ||
      (kind != RoadKind::Ramp && kind != RoadKind::FreightLift) ||
      (kind == RoadKind::Ramp && (to != from + 1 || length < 24 || length > 64)))
    throw std::runtime_error("traffic.error.plan");
  RoadLink road;
  road.id = Id(roads.size());
  road.kind = kind;
  road.from = from;
  road.to = to;
  road.pos = {pos.x, from * 3.f, pos.z};
  road.length = kind == RoadKind::Ramp ? length : 18;
  road.width = kind == RoadKind::Ramp ? 4.2f : 3.8f;
  road.height = road.startHeight = road.targetHeight = from * 3.f;
  auto cut = footprint(road);
  // The entire shaft/ramp plot is reserved, including service clearances on every affected level.
  for (int f = from; f <= to; ++f) {
    if (!floorPlans[f].deck || structuralFloorLocked(f))
      throw std::runtime_error("structure.error.floor");
    for (float z = cut.z0 - 3; z <= cut.z1 + 3; z += .5f)
      for (float x = cut.x0 - .5f; x <= cut.x1 + .5f; x += .5f)
        if (!clearPosition({x, f * 3.f, z}))
          throw std::runtime_error("structure.error.plot");
    for (Id fid : floorFacilities[f]) {
      auto p = facilities[fid].pos;
      if (!facilities[fid].demolished && p.x > cut.x0 - 6 && p.x < cut.x1 + 6 &&
          p.z > cut.z0 - 12 && p.z < cut.z1 + 4)
        throw std::runtime_error("structure.error.plot");
    }
    for (const auto &r : roads)
      if (f >= r.from && f <= r.to &&
          (r.active || std::any_of(structuralWorks.begin(), structuralWorks.end(),
                                   [&](const StructuralWork &p) {
                                     return p.transport == r.id &&
                                            p.state != StructuralState::Cancelled &&
                                            p.state != StructuralState::Completed;
                                   }))) {
        auto b = footprint(r);
        if (cut.x0 < b.x1 + 2 && cut.x1 > b.x0 - 2 && cut.z0 < b.z1 + 6 && cut.z1 > b.z0 - 6)
          throw std::runtime_error("structure.error.plot");
      }
  }
  StructuralWork p;
  p.id = Id(structuralWorks.size());
  p.actor = actor;
  p.payer = actor == dormOwner ? dormAccount : residents[actor].account;
  p.floor = from;
  p.kind = StructuralKind::Transport;
  p.transport = road.id;
  p.workshop = nearestAt(pos, FacilityKind::Workshop);
  if (p.workshop == none)
    throw std::runtime_error("structure.error.workshop");
  p.site = accessible(road.pos + Vec3{road.width / 2 + 2, 0, 0});
  p.budget = kind == RoadKind::Ramp ? 900000 : 3000000 + Money(to - from) * 200000;
  p.total = p.remaining = kind == RoadKind::Ramp ? 14400 : 21600 + 3600 * (to - from);
  Facility f;
  f.id = Id(facilities.size());
  f.kind = kind == RoadKind::Ramp ? FacilityKind::Ramp : FacilityKind::FreightLift;
  f.floor = from;
  f.pos = road.pos;
  f.owner = p.payer;
  f.manager = actor;
  f.open = false;
  f.price = kind == RoadKind::Ramp ? 200 : 1200;
  f.stops = {from, to};
  f.stock = 10000;
  road.facility = f.id;
  facilities.push_back(f);
  floorFacilities[from].push_back(f.id);
  roads.push_back(road);
  structuralWorks.push_back(p);
  return p.id;
}
Id World::proposeRoadRemoval(Id actor, Id id) {
  const auto &r = roads.at(id);
  if (!r.active || !residents.at(actor).alive || (actor != dormOwner && config.mode != Mode::Cheat))
    throw std::runtime_error("structure.error.permission");
  for (const auto &p : structuralWorks)
    if (p.transport == id && p.state != StructuralState::Completed &&
        p.state != StructuralState::Cancelled)
      throw std::runtime_error("structure.error.busy");
  StructuralWork p;
  p.id = Id(structuralWorks.size());
  p.actor = actor;
  p.payer = actor == dormOwner ? dormAccount : residents[actor].account;
  p.floor = r.from;
  p.transport = id;
  p.kind = StructuralKind::RemoveTransport;
  if (facilities[r.facility].owner != p.payer)
    throw std::runtime_error("structure.error.permission");
  p.workshop = nearestAt(r.pos, FacilityKind::Workshop);
  if (p.workshop == none)
    throw std::runtime_error("structure.error.workshop");
  p.site = accessible(r.pos + Vec3{r.width / 2 + 2, 0, 0});
  p.budget = 500000;
  p.total = p.remaining = 7200;
  structuralWorks.push_back(p);
  return p.id;
}
void World::generateRoads() {
  if (config.layoutVersion < 2)
    return;
  for (auto &f : facilities)
    if (f.kind == FacilityKind::Workshop)
      f.fuelStock = 400;
  for (const auto &f : facilities)
    if (f.kind == FacilityKind::Workshop) {
      Vehicle v;
      v.id = Id(vehicles.size());
      v.owner = operatingAccount(f.id);
      v.kind = VehicleKind::DeliveryVan;
      v.fuel = 40;
      v.towTruck = true;
      v.originalPrice = 8000000;
      vehicles.push_back(v);
      bool found = false;
      for (float radius : {6.f, 12.f, 18.f}) {
        for (int direction = 0; direction < 16; ++direction) {
          Vec3 p = f.pos + Vec3{std::cos(direction * .392699f) * radius, 0,
                                std::sin(direction * .392699f) * radius};
          bool supported = true;
          for (int a : {-1, 1})
            for (int b : {-1, 1})
              supported &= layout(f.floor).supported(p + Vec3{a * 1.1f, 0, b * 2.6f});
          if (supported && vehicleFits(v.id, p, 0, false)) {
            vehicles.back().pos = vehicles.back().garage = p;
            found = true;
            break;
          }
        }
        if (found)
          break;
      }
      if (!found)
        vehicles.pop_back();
    }
  for (auto kind : {RoadKind::Ramp, RoadKind::FreightLift}) {
    if (config.floors < 2)
      break;
    try {
      Id p = proposeRoad(dormOwner, kind, 0,
                         kind == RoadKind::Ramp ? 1 : std::min(10, config.floors - 1),
                         {kind == RoadKind::Ramp ? 80.f : 110.f, 0, 80}, 24);
      auto &job = structuralWorks[p];
      job.state = StructuralState::Completed;
      job.remaining = 0;
      job.supplied = true;
      job.escrow = account("Initial infrastructure endowment");
      activateRoad(job.transport, true);
      // Initial infrastructure is part of the generated world's assets, not a completed labor
      // contract.
    } catch (const std::runtime_error &) {
    }
  }
}
void World::requestVehicleLift(Id driver, Id id, int destination) {
  auto &r = roads.at(id);
  auto &person = residents.at(driver);
  if (!operationalDriver(person) || person.vehicle == none || !r.active ||
      r.kind != RoadKind::FreightLift || !operational(r.facility) ||
      !accessAllowed(driver, r.facility) || (destination != r.from && destination != r.to))
    throw std::runtime_error("traffic.error.lift");
  auto &v = vehicles[person.vehicle];
  int from = int(std::round(v.pos.y / 3));
  float mass = vehicleMass(v.id) + (v.towing == none ? 0 : vehicleMass(v.towing));
  if ((from != r.from && from != r.to) || from == destination ||
      std::hypot(v.pos.x - r.pos.x, v.pos.z - r.pos.z) > 80 || std::abs(v.speed) > .2f ||
      vehicleWidth(v.id) > r.width - .3f || mass > r.loadLimit || v.towedBy != none ||
      r.calls.size() >= 32)
    throw std::runtime_error("traffic.error.lift");
  if (std::any_of(r.calls.begin(), r.calls.end(), [&](auto c) { return c.vehicle == v.id; }))
    return;
  for (const auto &other : roads)
    if (other.id != id && std::any_of(other.calls.begin(), other.calls.end(),
                                      [&](auto c) { return c.vehicle == v.id; }))
      throw std::runtime_error("traffic.error.busy");
  r.calls.push_back({v.id, driver, from, destination, time});
  ensureVehicleTick();
}
void World::freightVehicleTick() {
  for (auto &r : roads)
    if (r.active && r.kind == RoadKind::FreightLift) {
      auto &f = facilities[r.facility];
      if (r.moving && time >= r.arriveAt) {
        r.height = r.targetHeight;
        r.moving = false;
        r.doorsUntil = time + 4 * (1440 / config.dayMinutes);
        for (int f = r.from; f <= r.to; ++f)
          invalidateGeometry(f);
      }
      if (r.occupant != none) {
        auto &v = vehicles[r.occupant];
        v.pos.y = freightHeight(r.id);
        if (r.moving)
          v.speed = 0;
        if (v.driver != none)
          residents[v.driver].pos = v.pos;
        if (v.towing != none)
          vehicles[v.towing].pos.y = v.pos.y;
      }
      if (r.moving || !operational(f.id))
        continue;
      std::erase_if(r.calls, [&](auto c) {
        return !residents[c.driver].alive || !residents[c.driver].present ||
               vehicles[c.vehicle].driver != c.driver || time - c.called > 6 * 3600 ||
               (!r.moving && r.occupant != c.vehicle &&
                std::hypot(vehicles[c.vehicle].pos.x - r.pos.x,
                           vehicles[c.vehicle].pos.z - r.pos.z) > 90);
      });
      if (r.occupant != none) {
        auto &v = vehicles[r.occupant];
        auto call =
            std::find_if(r.calls.begin(), r.calls.end(), [&](auto c) { return c.vehicle == v.id; });
        if (call != r.calls.end() && std::abs(v.pos.y - call->to * 3.f) > .1f &&
            time >= r.doorsUntil) {
          try {
            payLift(call->driver, f.id, call->to);
          } catch (const std::runtime_error &) {
            r.calls.erase(call);
            continue;
          }
          r.startHeight = r.height;
          r.targetHeight = call->to * 3.f;
          r.departAt = time;
          r.arriveAt =
              time + (std::abs(r.targetHeight - r.height) / 3 + 3) * (1440 / config.dayMinutes);
          r.moving = true;
          for (int f = r.from; f <= r.to; ++f)
            invalidateGeometry(f);
          r.calls.erase(call);
        }
        continue;
      }
      if (r.calls.empty())
        continue;
      auto c = r.calls.front();
      if (std::abs(r.height - c.from * 3.f) > .1f && time >= r.doorsUntil) {
        r.startHeight = r.height;
        r.targetHeight = c.from * 3.f;
        r.departAt = time;
        r.arriveAt =
            time + (std::abs(r.targetHeight - r.height) / 3 + 3) * (1440 / config.dayMinutes);
        r.moving = true;
        for (int f = r.from; f <= r.to; ++f)
          invalidateGeometry(f);
        continue;
      }
      auto &v = vehicles[c.vehicle];
      // Boarding is driving, not a position assignment. Only a fully centred stopped vehicle can
      // latch.
      auto inside = [&](const Vehicle &load) {
        float x = std::abs(std::cos(load.yaw)) * vehicleWidth(load.id) / 2 +
                  std::abs(std::sin(load.yaw)) * vehicleLength(load.id) / 2,
              z = std::abs(std::sin(load.yaw)) * vehicleWidth(load.id) / 2 +
                  std::abs(std::cos(load.yaw)) * vehicleLength(load.id) / 2;
        return std::abs(load.pos.x - r.pos.x) + x < r.width / 2 &&
               std::abs(load.pos.z - r.pos.z) + z < r.length / 2;
      };
      if (inside(v) && (v.towing == none || inside(vehicles[v.towing])) &&
          std::abs(v.pos.x - r.pos.x) < .25f && std::abs(v.pos.z - r.pos.z) < .35f &&
          std::abs(v.pos.y - r.height) < .15f && std::abs(v.speed) < .2f) {
        r.occupant = v.id;
        v.freightLift = r.id;
        r.doorsUntil = time + 2 * (1440 / config.dayMinutes);
      }
    }
}
std::vector<DrivePoint> World::planVehicleRoute(Id driver, Id id, Vec3 goal) const {
  const auto &v = vehicles.at(id);
  int from = int(std::round(v.pos.y / 3)), to = int(std::round(goal.y / 3));
  if (to < 0 || to >= config.floors || !floorPlans[to].deck || !std::isfinite(goal.x) ||
      !std::isfinite(goal.z))
    throw std::runtime_error("traffic.error.route");
  std::map<int, std::pair<int, Id>> previous;
  std::queue<int> open;
  open.push(from);
  previous[from] = {from, none};
  while (!open.empty() && !previous.contains(to)) {
    int floor = open.front();
    open.pop();
    for (const auto &r : roads)
      if (r.active && operational(r.facility) && accessAllowed(driver, r.facility) &&
          vehicleWidth(id) < r.width - .3f && (floor == r.from || floor == r.to)) {
        int next = floor == r.from ? r.to : r.from;
        if (!previous.contains(next)) {
          previous[next] = {floor, r.id};
          open.push(next);
        }
      }
  }
  if (!previous.contains(to))
    throw std::runtime_error("traffic.error.route");
  Id payer =
      (v.towTruck && residents[driver].employer == v.owner) ? v.owner : residents[driver].account;
  Money fares = 0;
  for (int f = to; f != from; f = previous[f].first) {
    const auto &business = facilities[roads[previous[f].second].facility];
    if (payer != business.owner && !business.whitelist.contains(driver) &&
        !(business.subscribers.contains(driver) && business.subscribers.at(driver) > time))
      fares += business.price;
  }
  if (accounts[payer].balance < fares)
    throw std::runtime_error("structure.error.funds");
  std::vector<Id> links;
  for (int f = to; f != from; f = previous[f].first)
    links.push_back(previous[f].second);
  std::reverse(links.begin(), links.end());
  std::vector<DrivePoint> out;
  Vec3 here = v.pos;
  auto floorRoute = [&](Vec3 target) {
    // Walking navigation supplies the topology; a separate footprint check rejects narrow passages.
    auto path = floorPath(here, target, false, id);
    for (std::size_t i = 1; i < path.size(); ++i) {
      auto d = path[i] - path[i - 1];
      float yaw = std::atan2(d.x, -d.z);
      int steps = std::max(1, int(length(d) / .4f));
      for (int n = 1; n <= steps; ++n)
        if (!vehicleFits(id, path[i - 1] + d * (float(n) / steps), yaw, false))
          throw std::runtime_error("traffic.error.route");
      out.push_back({path[i], none, -1});
    }
    here = target;
  };
  int floor = from;
  for (Id link : links) {
    const auto &r = roads[link];
    int next = floor == r.from ? r.to : r.from;
    if (r.kind == RoadKind::Ramp) {
      Vec3 a = r.pos + Vec3{0, 0, -4}, b = r.pos + Vec3{0, (r.to - r.from) * 3.f, r.length + 4};
      if (floor == r.to)
        std::swap(a, b);
      floorRoute(a);
      Vec3 first = r.pos, last = r.pos + Vec3{0, (r.to - r.from) * 3.f, r.length};
      if (floor == r.to)
        std::swap(first, last);
      out.push_back({first, link, -1});
      out.push_back({last, link, -1});
      out.push_back({b, link, -1});
      here = b;
    } else {
      int waiting = 0;
      for (auto c : r.calls)
        if (c.from == floor)
          ++waiting;
      for (const auto &[vid, trip] : vehicleJourneys)
        if (vid != id && std::any_of(trip.path.begin() + std::min(trip.next, trip.path.size()),
                                     trip.path.end(), [&](const DrivePoint &p) {
                                       return p.link == link && p.destination == next;
                                     }))
          ++waiting;
      Vec3 staging = r.pos + Vec3{0, (floor - r.from) * 3.f,
                                  -r.length / 2 - 5 - waiting * (vehicleLength(id) + 2)};
      // Arrive on a straight centre-line approach, leaving room to steer the whole
      // vehicle into alignment before its nose reaches the narrow cargo gate.
      if (std::hypot(here.x - staging.x, here.z - staging.z) > 10)
        floorRoute(staging - Vec3{0, 0, 12});
      floorRoute(staging);
      out.push_back({staging, link, next});
      out.push_back({r.pos + Vec3{0, (next - r.from) * 3.f, -r.length / 2 - 5}, link, -1});
      here = out.back().pos;
    }
    floor = next;
  }
  floorRoute(goal);
  return out;
}
void World::driveTo(Id driver, Vec3 goal, bool autoExit) {
  auto &r = residents.at(driver);
  if (!operationalDriver(r) || r.vehicle == none)
    throw std::runtime_error("traffic.error.driver");
  auto &v = vehicles[r.vehicle];
  if (v.condition < .08f || v.airborne || v.towedBy != none)
    throw std::runtime_error("traffic.error.disabled");
  auto path = planVehicleRoute(driver, v.id, goal);
  if (path.empty())
    return;
  vehicleJourneys[v.id] = {v.id, driver, std::move(path), 0, goal, -1, autoExit};
  ensureVehicleTick();
}
void World::cancelDrive(Id driver) {
  auto &r = residents.at(driver);
  if (r.vehicle == none)
    throw std::runtime_error("traffic.error.driver");
  vehicleJourneys.erase(r.vehicle);
  vehicles[r.vehicle].speed = 0;
  for (auto &link : roads)
    std::erase_if(link.calls, [&](auto c) { return c.driver == driver; });
}
void World::ensureVehicleTick() {
  bool work = !vehicleJourneys.empty();
  for (const auto &v : vehicles)
    work |= v.airborne || std::abs(v.speed) > .02f;
  for (const auto &r : roads)
    work |= r.moving || !r.calls.empty();
  if (work && nextVehicleAt < 0) {
    nextVehicleAt = time + .02 * (1440 / config.dayMinutes);
    schedule(nextVehicleAt, EventKind::VehicleTick, 0);
  }
}
void World::vehicleTick() {
  nextVehicleAt = -1;
  freightVehicleTick();
  std::vector<Id> done;
  for (auto &[id, j] : vehicleJourneys) {
    auto &v = vehicles[id];
    auto &r = residents[j.driver];
    if (!operationalDriver(r) || v.driver != j.driver) {
      done.push_back(id);
      continue;
    }
    if (id == vehicleController) {
      done.push_back(id);
      continue;
    }
    if (j.parkingDestination != none && v.bay != j.parkingDestination) {
      try {
        if (v.bay != none && length(v.pos - parking[v.bay].pos) > 2.5f)
          releaseParking(j.driver, v.id);
        if (v.bay == none)
          reserveParking(j.driver, v.id, j.parkingDestination);
      } catch (const std::runtime_error &) {
        v.speed = 0;
        done.push_back(id);
        continue;
      }
    }
    if (j.next >= j.path.size()) {
      v.speed = 0;
      if (j.autoExit && v.freightLift == none)
        try {
          if (v.bay != none && length(v.pos - parking[v.bay].pos) < 1.3f)
            parkVehicle(j.driver, v.id, v.bay);
          else
            exitVehicle(j.driver);
        } catch (const std::runtime_error &) {
          continue;
        }
      done.push_back(id);
      continue;
    }
    auto point = j.path[j.next];
    Vec3 target = point.pos;
    if (point.link != none && point.destination >= 0) {
      auto &road = roads[point.link];
      int origin = point.destination == road.to ? road.from : road.to;
      if (std::abs(v.pos.y - point.destination * 3.f) < .1f && v.freightLift == road.id) {
        ++j.next;
        continue;
      }
      if (v.freightLift == road.id) {
        v.speed = 0;
        continue;
      }
      bool queued = std::any_of(road.calls.begin(), road.calls.end(),
                                [&](auto c) { return c.vehicle == id; });
      if (!queued) {
        v.speed = 0;
        try {
          requestVehicleLift(j.driver, road.id, point.destination);
        } catch (const std::runtime_error &) {
          done.push_back(id);
        }
        continue;
      }
      if (road.moving || std::abs(freightHeight(road.id) - origin * 3.f) > .1f ||
          road.occupant != none || road.calls.front().vehicle != id) {
        v.speed = 0;
        continue;
      }
      target = road.pos + Vec3{0, (origin - road.from) * 3.f, 0};
      if (std::hypot(v.pos.x - target.x, v.pos.z - target.z) < .2f) {
        v.speed = 0;
        continue;
      }
    }
    Vec3 d = target - v.pos;
    float distance = std::hypot(d.x, d.z);
    if (distance < .35f && std::abs(d.y) < .3f) {
      v.speed = 0;
      ++j.next;
      continue;
    }
    float desired = std::atan2(d.x, -d.z), angle = std::remainder(desired - v.yaw, 6.2831853f);
    float steer = std::clamp(angle * 2, -1.f, 1.f), throttle = std::abs(angle) > 1.1f ? .15f
                                                               : distance < 2         ? .3f
                                                                                      : 1;
    if (std::abs(angle) > 2.1f && (distance < 20 || j.reversing)) {
      float reverse = std::remainder(angle + 3.14159265f, 6.2831853f);
      steer = -std::clamp(reverse * 2, -1.f, 1.f);
      throttle = -.45f;
      j.reversing = true;
    }
    if (std::abs(angle) < 1.f)
      j.reversing = false;
    auto before = v.pos;
    try {
      driveVehicle(j.driver, throttle, steer, .02f);
    } catch (const std::runtime_error &) {
      done.push_back(id);
      continue;
    }
    if (length(v.pos - before) < .0001f) {
      if (j.blockedAt < 0)
        j.blockedAt = time;
      else if (time - j.blockedAt > 3600) {
        v.speed = 0;
        done.push_back(id);
      }
    } else
      j.blockedAt = -1;
  }
  for (Id id : done)
    vehicleJourneys.erase(id);
  for (auto &v : vehicles)
    if ((v.airborne || std::abs(v.speed) > .02f) && v.id != vehicleController &&
        !vehicleJourneys.contains(v.id) && v.towedBy == none) {
      float dt = std::clamp(float((time - v.updatedAt) / (1440 / config.dayMinutes)), 0.f, .1f);
      if (dt <= 0)
        continue;
      if (v.driver != none && operationalDriver(residents[v.driver]))
        driveVehicle(v.driver, 0, 0, dt);
      else if (v.airborne) {
        v.verticalSpeed -= 9.81f * dt;
        auto next = v.pos + Vec3{std::sin(v.yaw) * v.speed * dt, v.verticalSpeed * dt,
                                 -std::cos(v.yaw) * v.speed * dt};
        float h = supportHeight(next, v.pos.y);
        if (h > -9000 && next.y <= h) {
          v.condition = std::max(0.f, v.condition - v.verticalSpeed * v.verticalSpeed * .003f);
          next.y = h;
          v.verticalSpeed = 0;
          v.speed = 0;
          v.airborne = false;
        }
        v.pos = next;
        v.updatedAt = time;
        if (v.driver != none && residents[v.driver].alive)
          residents[v.driver].pos = v.pos;
        if (v.pos.y < -100) {
          v.airborne = false;
          v.condition = 0;
          v.speed = 0;
        }
      } else
        v.speed = 0;
    }
  ensureVehicleTick();
}
} // namespace mega
