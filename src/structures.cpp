#include "mega/world.hpp"
#include <algorithm>
#include <numeric>

namespace mega {
namespace {
bool live(const StructuralWork &p) {
  return p.state != StructuralState::Completed && p.state != StructuralState::Cancelled;
}
bool adult(const World &w, Id id) {
  const auto &r = w.residents.at(id);
  return r.alive && r.present && r.age >= 18;
}
bool controls(const World &w, Id actor, Id shop) {
  const auto &f = w.facilities.at(shop);
  return w.residents.at(actor).account == f.owner ||
         w.directsCompany(actor, w.companyForAccount(f.owner), true);
}
bool freeWorker(const World &w, Id id, Id parcel = none) {
  const auto &r = w.residents.at(id);
  return adult(w, id) && r.role == Role::Builder && !r.surrendered &&
         (r.carrying == none || (parcel != none && r.carrying == parcel)) &&
         r.bodyCarrying == none && r.lift == none && r.ambulance == none && r.vehicle == none &&
         r.carriedBy == none && r.medicalDuty == none && !w.looseBodies.contains(id) &&
         r.activity != Activity::Jailed && r.activity != Activity::Restrained &&
         r.activity != Activity::Escorted && r.activity != Activity::Fighting;
}
void manager(const World &w, Id actor) {
  if (!adult(w, actor) || (actor != w.dormOwner && w.config.mode != Mode::Cheat))
    throw std::runtime_error("structure.error.permission");
}
bool intersects(const Obstacle &a, const Obstacle &b, float margin = 0) {
  return a.min.x < b.max.x + margin && a.max.x > b.min.x - margin && a.min.z < b.max.z + margin &&
         a.max.z > b.min.z - margin;
}
bool crossesWall(const World &w, const Resident &r, const InteriorWall &wall) {
  auto parts = w.wallSegments(wall);
  for (std::size_t i = 1; i < r.route.size(); ++i) {
    const auto &from = r.route[i - 1];
    const auto &to = r.route[i];
    if (to.time < w.time || std::abs(from.p.y - wall.floor * 3.f) > .1f ||
        std::abs(to.p.y - from.p.y) > .1f)
      continue;
    for (const auto &part : parts) {
      if (part.min.y > from.p.y + 1.75f)
        continue;
      float lo = 0, hi = 1;
      for (int axis = 0; axis < 2; ++axis) {
        float a = axis == 0 ? from.p.x : from.p.z;
        float d = axis == 0 ? to.p.x - from.p.x : to.p.z - from.p.z;
        float minimum = (axis == 0 ? part.min.x : part.min.z) - .3f;
        float maximum = (axis == 0 ? part.max.x : part.max.z) + .3f;
        if (std::abs(d) < .00001f) {
          if (a < minimum || a > maximum)
            hi = -1;
        } else {
          float near = (minimum - a) / d, far = (maximum - a) / d;
          if (near > far)
            std::swap(near, far);
          lo = std::max(lo, near);
          hi = std::min(hi, far);
        }
      }
      if (lo <= hi)
        return true;
    }
  }
  return false;
}
StructuralWork plan(World &w, Id actor, int floor, StructuralKind kind) {
  manager(w, actor);
  if (w.config.layoutVersion < 2 || floor < 0 || floor >= w.config.floors)
    throw std::runtime_error("structure.error.floor");
  StructuralWork p;
  p.id = Id(w.structuralWorks.size());
  p.actor = actor;
  p.payer = actor == w.dormOwner ? w.dormAccount : w.residents[actor].account;
  p.floor = floor;
  p.kind = kind;
  p.workshop = w.nearestAt({0, floor * 3.f, 0}, FacilityKind::Workshop);
  if (p.workshop == none)
    throw std::runtime_error("structure.error.workshop");
  return p;
}
} // namespace

bool World::structuralFloorLocked(int floor) const {
  return std::any_of(structuralWorks.begin(), structuralWorks.end(), [&](const StructuralWork &p) {
    return p.floor == floor && live(p) && p.state != StructuralState::Proposed &&
           (p.kind == StructuralKind::RemoveFloor || p.kind == StructuralKind::RestoreFloor);
  });
}
std::vector<Obstacle> World::wallSegments(const InteriorWall &w) const {
  Vec3 middle = (w.a + w.b) * .5f;
  bool alongX = std::abs(w.b.x - w.a.x) > .01f;
  float span = length(w.b - w.a);
  std::vector<Obstacle> out;
  auto add = [&](float from, float to, float bottom, float height) {
    Vec3 center = middle + (alongX ? Vec3{(from + to) / 2, bottom + height / 2, 0}
                                   : Vec3{0, bottom + height / 2, (from + to) / 2});
    Vec3 size = alongX ? Vec3{to - from, height, .18f} : Vec3{.18f, height, to - from};
    out.push_back({center - size * .5f, center + size * .5f});
  };
  if (w.door) {
    add(-span / 2, -.8f, 0, 2.7f);
    add(.8f, span / 2, 0, 2.7f);
    add(-.8f, .8f, 2.15f, .55f);
  } else
    add(-span / 2, span / 2, 0, 2.7f);
  return out;
}
void World::checkPartition(Id id) {
  auto &wall = interiorWalls.at(id);
  bool prior = wall.active;
  wall.active = false;
  invalidateGeometry(wall.floor);
  try {
    for (const auto &segment : wallSegments(wall)) {
      for (const auto &existing : floorObstacles(wall.floor))
        if (intersects(segment, existing, .08f)) {
          bool joint = false;
          float overlapX =
              std::min(segment.max.x, existing.max.x) - std::max(segment.min.x, existing.min.x);
          float overlapZ =
              std::min(segment.max.z, existing.max.z) - std::max(segment.min.z, existing.min.z);
          if (overlapX <= .35f && overlapZ <= .35f)
            for (const auto &other : interiorWalls)
              if (other.id != wall.id && other.active && other.floor == wall.floor)
                for (const auto &part : wallSegments(other))
                  if (length(part.min - existing.min) < .001f &&
                      length(part.max - existing.max) < .001f)
                    joint = true;
          if (!joint)
            throw std::runtime_error("structure.error.plot");
        }
      for (Id fid : floorFacilities[wall.floor]) {
        const auto &f = facilities[fid];
        Obstacle access{f.pos + Vec3{-5.5f, 0, -2.5f}, f.pos + Vec3{5.5f, 2.7f, 11.5f}};
        if (!f.demolished && intersects(segment, access))
          throw std::runtime_error("structure.error.plot");
      }
      // The lintel is overhead; its doorway is intentionally walkable.
      if (segment.min.y > wall.floor * 3.f + 2)
        continue;
      int steps = std::max(1, int(length(segment.max - segment.min) * 3));
      for (int i = 0; i <= steps; ++i) {
        Vec3 at = segment.min + (segment.max - segment.min) * (float(i) / steps);
        at.y = wall.floor * 3.f;
        if (!layout(wall.floor).supported(at))
          throw std::runtime_error("structure.error.plot");
        for (int ladder = 0; ladder < layout().ladderCount(); ++ladder)
          if (length(at - layout().landing(ladder, at.y)) < 3)
            throw std::runtime_error("structure.error.egress");
      }
    }
    wall.active = true;
    invalidateGeometry(wall.floor);
    auto escape = [&](Vec3 from) {
      from = accessible(from);
      for (int ladder : {0, 2})
        try {
          floorPath(from, layout(wall.floor).landing(ladder, wall.floor * 3.f), false);
          return;
        } catch (const std::runtime_error &) {
        }
      throw std::runtime_error("structure.error.egress");
    };
    for (Id home : floorHomes[wall.floor])
      if (housing[home].active)
        escape(homePosition(home) + Vec3{1.25f, 0, 0});
    for (Id fid : floorFacilities[wall.floor])
      if (!facilities[fid].demolished)
        escape(facilities[fid].pos + Vec3{1.6f, 0, 0});
    for (const auto &r : residents)
      if (r.alive && r.present && std::abs(position(r.id).y - wall.floor * 3.f) < .1f &&
          r.ladder < 0 && r.lift == none && r.ambulance == none && r.carriedBy == none)
        escape(position(r.id));
    wall.active = prior;
    invalidateGeometry(wall.floor);
  } catch (...) {
    wall.active = prior;
    invalidateGeometry(wall.floor);
    throw;
  }
}
Id World::proposePartition(Id actor, int floor, Vec3 a, Vec3 b, bool door) {
  auto p = plan(*this, actor, floor, StructuralKind::Partition);
  a.y = b.y = floor * 3.f;
  if (!std::isfinite(a.x) || !std::isfinite(a.z) || !std::isfinite(b.x) || !std::isfinite(b.z) ||
      (std::abs(a.x - b.x) > .01f && std::abs(a.z - b.z) > .01f) || length(b - a) < 2.5f ||
      length(b - a) > 64 || !floorPlans[floor].deck || structuralFloorLocked(floor))
    throw std::runtime_error("structure.error.plot");
  InteriorWall wall{Id(interiorWalls.size()), p.payer, floor, a, b, door, false};
  interiorWalls.push_back(wall);
  try {
    checkPartition(wall.id);
    Vec3 normal = std::abs(a.x - b.x) > .01f ? Vec3{0, 0, 1.5f} : Vec3{1.5f, 0, 0};
    p.site = accessible((a + b) * .5f + normal);
  } catch (...) {
    interiorWalls.pop_back();
    throw;
  }
  p.workshop = nearestAt(p.site, FacilityKind::Workshop);
  p.wall = wall.id;
  p.budget = 200000 + Money(std::ceil(length(b - a) * 25000));
  p.total = p.remaining = 1800 + length(b - a) * 300;
  structuralWorks.push_back(p);
  log("structure.proposed", actor, p.id);
  return p.id;
}
Id World::proposePartitionRemoval(Id actor, Id id) {
  const auto &wall = interiorWalls.at(id);
  auto p = plan(*this, actor, wall.floor, StructuralKind::RemovePartition);
  if (!wall.active || structuralFloorLocked(wall.floor))
    throw std::runtime_error("structure.error.plot");
  for (const auto &q : structuralWorks)
    if (q.wall == id && live(q))
      throw std::runtime_error("structure.error.busy");
  p.wall = id;
  p.site = accessible((wall.a + wall.b) * .5f + Vec3{1.5f, 0, 1.5f});
  p.workshop = nearestAt(p.site, FacilityKind::Workshop);
  p.budget = 80000 + Money(length(wall.b - wall.a) * 5000);
  p.total = p.remaining = 900 + length(wall.b - wall.a) * 120;
  structuralWorks.push_back(p);
  return p.id;
}
bool World::floorCleared(int floor, Id except) const {
  for (const auto &h : housing)
    if (h.floor == floor && h.active)
      return false;
  for (const auto &f : facilities)
    if (f.floor == floor && !f.demolished)
      return false;
  for (const auto &w : interiorWalls)
    if (w.floor == floor && w.active)
      return false;
  for (const auto &p : projects)
    if (facilities[p.facility].floor == floor && p.state != ProjectState::Completed)
      return false;
  for (const auto &p : housingWorks)
    if (p.floor == floor && p.state != ProjectState::Completed)
      return false;
  for (const auto &p : expansions)
    if (p.floor == floor && !p.complete)
      return false;
  for (const auto &p : structuralWorks)
    if (p.floor == floor && p.id != except && live(p))
      return false;
  for (const auto &r : residents)
    if (r.alive && r.present && !r.route.empty() &&
        std::abs(r.route.back().p.y - floor * 3.f) < .1f)
      return false;
  for (const auto &r : residents)
    if (r.alive && r.present && std::abs(position(r.id).y - floor * 3.f) < 2.5f && r.ladder < 0 &&
        !climbing(r.id) && r.lift == none && r.ambulance == none)
      return false;
  for (const auto &i : items)
    if (!i.consumed && i.carrier == none && std::abs(i.pos.y - floor * 3.f) < .2f)
      return false;
  for (const auto &b : remains)
    if (!b.lost && !b.cremated && b.carrier == none && std::abs(b.pos.y - floor * 3.f) < 2.5f)
      return false;
  for (const auto &[id, journey] : liftJourneys) {
    if (journey.destination == floor || std::abs(position(id).y - floor * 3.f) < .2f)
      return false;
  }
  for (const auto &car : lifts) {
    for (const auto &call : car.calls)
      if (call.from == floor || call.to == floor)
        return false;
    for (const auto &passenger : car.passengers)
      if (passenger.destination == floor)
        return false;
  }
  return true;
}
Id World::proposeFloorChange(Id actor, int floor, bool restore) {
  auto p = plan(*this, actor, floor,
                restore ? StructuralKind::RestoreFloor : StructuralKind::RemoveFloor);
  if (floor == 0 || floorPlans[floor].deck == restore)
    throw std::runtime_error("structure.error.floor");
  for (const auto &q : structuralWorks)
    if (q.floor == floor && live(q) &&
        (q.kind == StructuralKind::RemoveFloor || q.kind == StructuralKind::RestoreFloor))
      throw std::runtime_error("structure.error.busy");
  int support = floor - 1;
  while (support > 0 && !floorPlans[support].deck)
    --support;
  p.site = layout(support).landing(0, support * 3.f);
  p.workshop = nearestAt(p.site, FacilityKind::Workshop);
  p.budget = Money(floorPlans[floor].width) * floorPlans[floor].depth * (restore ? 3500 : 600);
  p.total = p.remaining = std::max(3600., double(floorPlans[floor].width) *
                                              floorPlans[floor].depth * (restore ? .36 : .06));
  structuralWorks.push_back(p);
  log("structure.proposed", actor, p.id);
  return p.id;
}
void World::planFloorClearance(Id actor, Id id) {
  const auto &p = structuralWorks.at(id);
  manager(*this, actor);
  if (p.actor != actor || p.kind != StructuralKind::RemoveFloor ||
      p.state != StructuralState::Proposed)
    throw std::runtime_error("structure.error.permission");
  // Preflight the whole batch before creating any claims or contracts.
  for (const auto &f : facilities)
    if (f.floor == p.floor && !f.demolished &&
        (leasedWorkspaces.contains(f.id) || activeFires.contains(f.id)))
      throw std::runtime_error("structure.error.clearance");
  for (Id home : floorHomes[p.floor]) {
    const auto &h = housing[home];
    if (!h.active ||
        std::any_of(housingWorks.begin(), housingWorks.end(), [&](const HousingWork &job) {
          return job.home == home && job.state != ProjectState::Completed;
        }))
      continue;
    proposeHomeRemoval(actor, home, std::max(h.arrears, h.rent * 7 + 30000));
  }
  for (Id fid : floorFacilities[p.floor]) {
    const auto &f = facilities[fid];
    if (f.demolished || std::any_of(projects.begin(), projects.end(), [&](const Project &job) {
          return job.facility == fid && job.state != ProjectState::Completed;
        }))
      continue;
    proposeProject(actor, fid, true, 150000);
  }
}
void World::collectStructuralSalvage(Id worker, Id id) {
  auto &p = structuralWorks.at(id);
  auto &r = residents.at(worker);
  if (p.state != StructuralState::Cancelled || p.material == none || !freeWorker(*this, worker) ||
      (worker != p.actor && r.account != p.payer && r.jobSite != p.workshop))
    throw std::runtime_error("structure.error.salvage");
  auto &item = items[p.material];
  if (item.consumed || item.reservedAt != none || item.carrier != none || item.owner != p.payer ||
      !near(worker, item.pos))
    throw std::runtime_error("structure.error.salvage");
  item.carrier = worker;
  r.carrying = item.id;
}
void World::returnStructuralSalvage(Id worker, Id id) {
  auto &p = structuralWorks.at(id);
  auto &r = residents.at(worker);
  if (p.state != StructuralState::Cancelled || p.material == none || !adult(*this, worker))
    throw std::runtime_error("structure.error.salvage");
  auto &item = items[p.material];
  auto &shop = facilities[p.workshop];
  if (item.consumed || item.reservedAt != none || item.carrier != worker || r.carrying != item.id ||
      item.owner != p.payer || !operational(shop.id) || !near(worker, shop.pos) ||
      accounts[operatingAccount(shop.id)].balance < 80000)
    throw std::runtime_error("structure.error.salvage");
  pay(operatingAccount(shop.id), item.owner, 80000, "construction-material-buyback");
  shop.stock += 4;
  item.consumed = true;
  item.carrier = none;
  item.pos = shop.pos;
  r.carrying = none;
}
void World::fundStructuralWork(Id actor, Id id) {
  auto &p = structuralWorks.at(id);
  if (!adult(*this, actor) || actor != p.actor || p.state != StructuralState::Proposed)
    throw std::runtime_error("structure.error.permission");
  manager(*this, actor);
  if (p.kind == StructuralKind::Partition)
    checkPartition(p.wall);
  if ((p.kind == StructuralKind::RemoveFloor || p.kind == StructuralKind::RestoreFloor) &&
      !floorCleared(p.floor, p.id))
    throw std::runtime_error("structure.error.clearance");
  if (accounts[p.payer].balance < p.budget)
    throw std::runtime_error("structure.error.funds");
  p.escrow = account("Structural construction escrow");
  pay(p.payer, p.escrow, p.budget, "structural-work-funding");
  p.state = StructuralState::Funded;
  p.blocked.clear();
}
bool World::builderCommitted(Id id) const {
  for (const auto &t : constructionTeams)
    if (t.members.contains(id) && t.work != none && live(structuralWorks[t.work]))
      return true;
  for (const auto &p : projects)
    if (p.worker == id && p.state == ProjectState::Working)
      return true;
  for (const auto &p : housingWorks)
    if (p.worker == id && p.state == ProjectState::Working)
      return true;
  for (const auto &p : expansions)
    if (p.worker == id && !p.complete)
      return true;
  for (const auto &p : repairs)
    if (p.worker == id && !p.complete)
      return true;
  return false;
}
Id World::createConstructionTeam(Id actor, Id workshop, Id foreman) {
  const auto &shop = facilities.at(workshop);
  if (!adult(*this, actor) || shop.kind != FacilityKind::Workshop || shop.demolished ||
      (!controls(*this, actor, workshop) && actor != foreman) || !freeWorker(*this, foreman) ||
      (foreman != actor && !residents[foreman].automatic) ||
      residents[foreman].jobSite != workshop || builderCommitted(foreman))
    throw std::runtime_error("structure.error.crew");
  for (const auto &t : constructionTeams)
    if (t.members.contains(foreman))
      throw std::runtime_error("structure.error.crew");
  ConstructionTeam t;
  t.id = Id(constructionTeams.size());
  t.workshop = workshop;
  t.foreman = foreman;
  t.members.insert(foreman);
  constructionTeams.push_back(t);
  return t.id;
}
void World::recruitConstructionWorker(Id actor, Id team, Id worker) {
  auto &t = constructionTeams.at(team);
  if (!adult(*this, actor) || t.foreman == none ||
      (actor != t.foreman && !controls(*this, actor, t.workshop) &&
       !(actor == worker && residents[t.foreman].automatic)) ||
      t.members.size() >= 8 || !freeWorker(*this, worker) || builderCommitted(worker) ||
      residents[worker].jobSite != t.workshop || (worker != actor && !residents[worker].automatic))
    throw std::runtime_error("structure.error.crew");
  for (const auto &other : constructionTeams)
    if (other.members.contains(worker))
      throw std::runtime_error("structure.error.crew");
  t.members.insert(worker);
}
void World::removeConstructionWorker(Id actor, Id team, Id worker) {
  auto &t = constructionTeams.at(team);
  if (!adult(*this, actor) ||
      (actor != t.foreman && actor != worker && !controls(*this, actor, t.workshop)) ||
      !t.members.contains(worker) || residents[worker].carrying != none)
    throw std::runtime_error("structure.error.crew");
  if (t.work != none) {
    auto &p = structuralWorks[t.work];
    p.attendance.erase(worker);
    if (p.hauler == worker)
      p.hauler = none;
  }
  auto &r = residents[worker];
  if (r.task == TaskKind::None &&
      (r.activity == Activity::Walking || r.activity == Activity::Working)) {
    r.pos = position(worker);
    r.route.clear();
    ++r.routeVersion;
    r.activity = Activity::Idle;
  }
  t.members.erase(worker);
  if (t.foreman == worker)
    t.foreman = t.members.empty() ? none : *t.members.begin();
  if (t.members.empty() && t.work != none) {
    auto &p = structuralWorks[t.work];
    p.team = none;
    p.hauler = none;
    p.state = StructuralState::Funded;
    p.attendance.clear();
    p.blocked = "structure.error.crew";
    t.work = none;
  }
}
void World::appointForeman(Id actor, Id team, Id worker) {
  auto &t = constructionTeams.at(team);
  if (!adult(*this, actor) || (actor != t.foreman && !controls(*this, actor, t.workshop)) ||
      !t.members.contains(worker) || !freeWorker(*this, worker))
    throw std::runtime_error("structure.error.crew");
  t.foreman = worker;
}
void World::assignStructuralTeam(Id actor, Id id, Id team) {
  auto &p = structuralWorks.at(id);
  auto &t = constructionTeams.at(team);
  if (!adult(*this, actor) ||
      (actor != t.foreman && !controls(*this, actor, t.workshop) && actor != p.actor) ||
      (p.state != StructuralState::Funded && p.state != StructuralState::Working) ||
      p.team != none || t.work != none || t.foreman == none || t.workshop != p.workshop ||
      (actor != t.foreman && !controls(*this, actor, t.workshop) &&
       !residents[t.foreman].automatic))
    throw std::runtime_error("structure.error.crew");
  for (Id id : t.members)
    if (builderCommitted(id))
      throw std::runtime_error("structure.error.crew");
  t.work = p.id;
  p.team = team;
  if (!p.supplied)
    p.hauler = t.foreman;
  p.state = StructuralState::Working;
  p.workedAt = time;
}
Vec3 World::structuralDestination(Id worker, Id id) const {
  const auto &p = structuralWorks.at(id);
  if (!p.supplied && p.hauler == worker) {
    if (p.material == none)
      return facilities[p.workshop].pos + Vec3{1.6f, 0, 0};
    if (items[p.material].carrier != worker)
      return items[p.material].pos;
    return p.site;
  }
  int index = 0;
  if (p.team != none)
    for (Id id : constructionTeams[p.team].members) {
      if (id == worker)
        break;
      ++index;
    }
  return accessible(p.site + Vec3{float(index % 4) * .8f, 0, float(index / 4) * .8f});
}
bool World::structuralDecision(Id worker) {
  for (auto &t : constructionTeams)
    if (t.members.contains(worker) && t.work != none) {
      auto &p = structuralWorks[t.work];
      if (!live(p))
        continue;
      if (!freeWorker(*this, worker, p.material))
        return true;
      try {
        auto goal = structuralDestination(worker, p.id);
        if (near(worker, goal, .7f))
          beginStructuralWork(worker, p.id);
        else
          travel(worker, goal, TaskKind::None, none);
        p.blocked.clear();
      } catch (const std::runtime_error &) {
        p.blocked = "structure.error.route";
      }
      return true;
    }
  return false;
}
void World::beginStructuralWork(Id worker, Id id) {
  auto &p = structuralWorks.at(id);
  auto &r = residents.at(worker);
  if (p.state != StructuralState::Working || p.team == none ||
      !constructionTeams[p.team].members.contains(worker) || !r.alive || !r.present ||
      !freeWorker(*this, worker, p.material) || !onShift(worker) ||
      !near(worker, structuralDestination(worker, id), .8f))
    throw std::runtime_error("structure.error.attend");
  clockOut(worker);
  r.activity = Activity::Working;
}
void World::cancelStructuralWork(Id actor, Id id) {
  auto &p = structuralWorks.at(id);
  if (actor != p.actor || !adult(*this, actor) || !live(p))
    throw std::runtime_error("structure.error.permission");
  p.state = StructuralState::Cancelled;
  if (p.material != none) {
    auto &item = items[p.material];
    if (item.carrier != none) {
      auto &r = residents[item.carrier];
      item.pos = position(r.id);
      if (r.carrying == item.id)
        r.carrying = none;
    }
    item.carrier = item.reservedAt = none;
  }
  if (p.escrow != none)
    pay(p.escrow, p.payer, accounts[p.escrow].balance, "structural-work-refund");
  if (p.team != none) {
    auto &t = constructionTeams[p.team];
    for (Id worker : t.members) {
      auto &r = residents[worker];
      if ((r.activity == Activity::Working || r.activity == Activity::Walking) &&
          r.task == TaskKind::None) {
        r.activity = Activity::Idle;
        r.pos = position(worker);
        r.route.clear();
        ++r.routeVersion;
      }
    }
    t.work = none;
  }
  p.attendance.clear();
}

void World::structuralTick() {
  for (auto &t : constructionTeams) {
    std::vector<Id> gone;
    for (Id id : t.members)
      if (!adult(*this, id) || residents[id].role != Role::Builder ||
          residents[id].jobSite != t.workshop)
        gone.push_back(id);
    for (Id id : gone) {
      t.members.erase(id);
      if (t.work != none) {
        structuralWorks[t.work].attendance.erase(id);
        if (structuralWorks[t.work].hauler == id)
          structuralWorks[t.work].hauler = none;
      }
    }
    if (!t.members.contains(t.foreman))
      t.foreman = t.members.empty() ? none : *t.members.begin();
    if (t.members.empty() && t.work != none) {
      auto &p = structuralWorks[t.work];
      p.team = none;
      p.state = StructuralState::Funded;
      p.workedAt = -1;
      p.blocked = "structure.error.crew";
      t.work = none;
    }
  }
  for (auto &p : structuralWorks) {
    if (!live(p) || p.state == StructuralState::Proposed)
      continue;
    if (p.team == none) {
      Id candidate = none;
      for (const auto &t : constructionTeams)
        if (t.workshop == p.workshop && t.work == none && t.foreman != none &&
            residents[t.foreman].automatic &&
            std::none_of(t.members.begin(), t.members.end(),
                         [&](Id id) { return builderCommitted(id); })) {
          candidate = t.id;
          break;
        }
      if (candidate == none)
        for (Id worker : facilities[p.workshop].employees)
          if (freeWorker(*this, worker) && residents[worker].automatic &&
              !builderCommitted(worker) &&
              std::none_of(constructionTeams.begin(), constructionTeams.end(),
                           [&](const ConstructionTeam &t) { return t.members.contains(worker); })) {
            candidate = createConstructionTeam(worker, p.workshop, worker);
            for (Id helper : facilities[p.workshop].employees)
              if (helper != worker && residents[helper].automatic && freeWorker(*this, helper) &&
                  !builderCommitted(helper) && constructionTeams[candidate].members.size() < 8)
                try {
                  recruitConstructionWorker(worker, candidate, helper);
                } catch (const std::runtime_error &) {
                }
            break;
          }
      if (candidate != none)
        try {
          assignStructuralTeam(constructionTeams[candidate].foreman, p.id, candidate);
        } catch (const std::runtime_error &) {
        }
      if (p.team == none) {
        p.blocked = "structure.error.crew";
        continue;
      }
    }
    auto &t = constructionTeams[p.team];
    if (!p.supplied) {
      if (p.hauler == none || !t.members.contains(p.hauler))
        p.hauler = t.foreman;
      if (p.hauler == none)
        continue;
      auto &r = residents[p.hauler];
      auto &shop = facilities[p.workshop];
      if (!freeWorker(*this, r.id, p.material) || r.activity != Activity::Working)
        continue;
      if (p.material == none) {
        if (!onShift(r.id) || !near(r.id, shop.pos) || !operational(shop.id) || shop.stock < 4 ||
            r.carrying != none) {
          p.blocked = "structure.error.materials";
          continue;
        }
        Item item;
        item.id = Id(items.size());
        item.kind = ItemKind::Materials;
        item.owner = p.payer;
        item.carrier = r.id;
        item.pos = position(r.id);
        item.reservedAt = p.workshop;
        if (accounts[p.escrow].balance < 80000) {
          p.blocked = "structure.error.funds";
          continue;
        }
        pay(p.escrow, operatingAccount(shop.id), 80000, "structural-materials");
        p.spent += 80000;
        shop.stock -= 4;
        p.material = item.id;
        items.push_back(item);
        r.carrying = item.id;
      } else {
        auto &item = items[p.material];
        if (item.carrier != r.id) {
          if (item.carrier != none || !onShift(r.id) || r.carrying != none || !near(r.id, item.pos))
            continue;
          item.carrier = r.id;
          r.carrying = item.id;
        }
        if (!onShift(r.id) || !near(r.id, p.site, .8f))
          continue;
        item.pos = p.site;
        item.carrier = none;
        r.carrying = none;
        p.supplied = true;
      }
      p.attendance.clear();
      p.blocked.clear();
      continue;
    }
    std::map<Id, double> work;
    double effort = 0;
    bool supervised = t.foreman != none && onShift(t.foreman) && freeWorker(*this, t.foreman) &&
                      residents[t.foreman].activity == Activity::Working &&
                      near(t.foreman, p.site, 5);
    for (Id worker : t.members) {
      const auto &r = residents[worker];
      if (freeWorker(*this, worker) && onShift(worker) && r.activity == Activity::Working &&
          near(worker, structuralDestination(worker, p.id), .8f)) {
        double dt =
            p.attendance.contains(worker) ? std::clamp(time - p.attendance[worker], 0., 60.) : 0;
        p.attendance[worker] = time;
        work[worker] = dt;
        effort += dt;
      } else
        p.attendance.erase(worker);
    }
    if (!effort) {
      p.blocked = "structure.error.attend";
      continue;
    }
    double productivity = supervised ? 1.15 : 1;
    double scale = std::min(1., p.remaining / (effort * productivity));
    Money wages = 0;
    for (auto [worker, dt] : work)
      wages += Money(std::ceil(dt * scale / 3600 * residents[worker].hourlyWage));
    if (accounts[p.escrow].balance < wages) {
      p.blocked = "structure.error.funds";
      continue;
    }
    for (auto [worker, dt] : work) {
      Money wage = Money(std::ceil(dt * scale / 3600 * residents[worker].hourlyWage));
      pay(p.escrow, residents[worker].account, wage, "structural-crew-wage");
      p.spent += wage;
    }
    p.remaining = std::max(0., p.remaining - effort * scale * productivity);
    p.blocked.clear();
    if (p.remaining > .001)
      continue;
    try {
      if (p.kind == StructuralKind::Partition) {
        checkPartition(p.wall);
        for (const auto &r : residents)
          if (r.alive && r.present)
            for (const auto &o : wallSegments(interiorWalls[p.wall]))
              if (overlaps(position(r.id), o))
                throw std::runtime_error("structure.error.occupied");
        for (const auto &r : residents)
          if (r.alive && r.present && climbing(r.id) &&
              crossesWall(*this, r, interiorWalls[p.wall]))
            throw std::runtime_error("structure.error.occupied");
        interiorWalls[p.wall].active = true;
      } else if (p.kind == StructuralKind::RemovePartition)
        interiorWalls[p.wall].active = false;
      else {
        if (!floorCleared(p.floor, p.id))
          throw std::runtime_error("structure.error.clearance");
        floorPlans[p.floor].deck = p.kind == StructuralKind::RestoreFloor;
        if (!floorPlans[p.floor].deck)
          for (auto &f : facilities)
            if (f.kind == FacilityKind::Elevator)
              std::erase(f.stops, p.floor);
      }
    } catch (const std::runtime_error &e) {
      p.remaining = 1;
      p.blocked = e.what();
      continue;
    }
    if (p.material != none) {
      items[p.material].consumed = true;
      items[p.material].reservedAt = none;
    }
    pay(p.escrow, operatingAccount(p.workshop), accounts[p.escrow].balance,
        "structural-contractor-profit");
    p.state = StructuralState::Completed;
    p.attendance.clear();
    t.work = none;
    for (Id worker : t.members)
      if (residents[worker].activity == Activity::Working)
        residents[worker].activity = Activity::Idle;
    invalidateGeometry(p.floor);
    // Existing routes must not pass through a newly completed partition.
    for (auto &r : residents)
      if (r.alive && r.present && !r.route.empty() && p.kind == StructuralKind::Partition &&
          crossesWall(*this, r, interiorWalls[p.wall]) && !climbing(r.id) && r.ladder < 0 &&
          r.lift == none && r.ambulance == none && r.carriedBy == none &&
          r.activity != Activity::Jailed && r.activity != Activity::Restrained &&
          r.activity != Activity::Escorted) {
        r.pos = position(r.id);
        r.route.clear();
        ++r.routeVersion;
        r.task = TaskKind::None;
        r.target = none;
        if (r.activity == Activity::Walking || r.activity == Activity::Working)
          r.activity = Activity::Idle;
        schedule(time + 1, EventKind::Decide, r.id);
      }
    repriceHousing();
    log("structure.completed", p.actor, p.id);
  }
}

void World::validateStructures() const {
  auto ref = [](Id id, std::size_t size) { return id >= 0 && std::size_t(id) < size; };
  for (std::size_t i = 0; i < interiorWalls.size(); ++i) {
    const auto &w = interiorWalls[i];
    if (w.id != Id(i) || !ref(w.owner, accounts.size()) || w.floor < 0 ||
        w.floor >= config.floors || !std::isfinite(w.a.x) || !std::isfinite(w.a.z) ||
        !std::isfinite(w.b.x) || !std::isfinite(w.b.z) || w.a.y != w.floor * 3.f ||
        w.b.y != w.a.y || length(w.b - w.a) < 2.5f || length(w.b - w.a) > 64 ||
        (std::abs(w.a.x - w.b.x) > .01f && std::abs(w.a.z - w.b.z) > .01f) ||
        (w.active && !floorPlans[w.floor].deck) || std::abs(w.a.x) >= layout(w.floor).halfWidth() ||
        std::abs(w.b.x) >= layout(w.floor).halfWidth() ||
        std::abs(w.a.z) >= layout(w.floor).halfDepth() ||
        std::abs(w.b.z) >= layout(w.floor).halfDepth())
      throw std::runtime_error("Invalid interior wall");
  }
  std::set<Id> workers;
  for (std::size_t i = 0; i < constructionTeams.size(); ++i) {
    const auto &t = constructionTeams[i];
    if (t.id != Id(i) || !ref(t.workshop, facilities.size()) ||
        facilities[t.workshop].kind != FacilityKind::Workshop || t.members.size() > 8 ||
        (t.members.empty() != (t.foreman == none)) ||
        (t.foreman != none && !t.members.contains(t.foreman)) ||
        (t.work != none &&
         (!ref(t.work, structuralWorks.size()) || structuralWorks[t.work].team != t.id ||
          !live(structuralWorks[t.work]))))
      throw std::runtime_error("Invalid construction team");
    for (Id worker : t.members)
      if (!ref(worker, residents.size()) || !workers.insert(worker).second)
        throw std::runtime_error("Invalid construction member");
  }
  for (std::size_t i = 0; i < structuralWorks.size(); ++i) {
    const auto &p = structuralWorks[i];
    if (p.id != Id(i) || !ref(p.actor, residents.size()) || !ref(p.payer, accounts.size()) ||
        !ref(p.workshop, facilities.size()) ||
        facilities[p.workshop].kind != FacilityKind::Workshop || int(p.kind) < 0 ||
        int(p.kind) > int(StructuralKind::RestoreFloor) || int(p.state) < 0 ||
        int(p.state) > int(StructuralState::Cancelled) || p.floor < 0 || p.floor >= config.floors ||
        !std::isfinite(p.total) || p.total <= 0 || !std::isfinite(p.remaining) || p.workedAt < -1 ||
        p.workedAt > time || p.remaining < 0 || p.remaining > p.total ||
        !std::isfinite(p.workedAt) || !std::isfinite(p.site.x) || !std::isfinite(p.site.y) ||
        !std::isfinite(p.site.z) || p.budget <= 0 || p.spent < 0 || p.spent > p.budget ||
        (p.escrow != none && !ref(p.escrow, accounts.size())) ||
        (p.state != StructuralState::Proposed && p.state != StructuralState::Cancelled &&
         p.escrow == none) ||
        (p.team != none && !ref(p.team, constructionTeams.size())) ||
        (p.state == StructuralState::Working &&
         (p.team == none || constructionTeams[p.team].work != p.id)) ||
        ((p.state == StructuralState::Proposed || p.state == StructuralState::Funded) &&
         p.team != none) ||
        (p.wall != none && !ref(p.wall, interiorWalls.size())) ||
        ((p.kind == StructuralKind::Partition || p.kind == StructuralKind::RemovePartition) &&
         p.wall == none) ||
        (p.material != none && !ref(p.material, items.size())) ||
        (p.hauler != none && !ref(p.hauler, residents.size())) || p.blocked.size() > 128)
      throw std::runtime_error("Invalid structural contract");
    for (auto [worker, at] : p.attendance)
      if (!ref(worker, residents.size()) || p.team == none ||
          !constructionTeams[p.team].members.contains(worker) || !std::isfinite(at) || at < 0 ||
          at > time)
        throw std::runtime_error("Invalid construction attendance");
  }
  if (!floorPlans.empty() && !floorPlans[0].deck)
    throw std::runtime_error("Missing entrance slab");
  for (const auto &h : housing)
    if (h.active && h.floor >= 0 && std::size_t(h.floor) < floorPlans.size() &&
        !floorPlans[h.floor].deck)
      throw std::runtime_error("Home on a removed floor");
  for (const auto &f : facilities)
    if (!f.demolished && f.floor >= 0 && std::size_t(f.floor) < floorPlans.size() &&
        !floorPlans[f.floor].deck)
      throw std::runtime_error("Facility on a removed floor");
}
} // namespace mega
