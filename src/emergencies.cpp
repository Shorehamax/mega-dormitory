#include "mega/world.hpp"
#include <algorithm>
#include <cmath>

namespace mega {
static void releaseResponder(World &w, Fire &fire) {
  if (fire.worker == none)
    return;
  auto &r = w.residents[fire.worker];
  if (r.target == fire.id && r.task == TaskKind::FireResponse) {
    w.stopExtinguish(r.id);
    r.pos = w.position(r.id);
    r.route.clear();
    ++r.routeVersion;
    r.task = TaskKind::None;
    r.target = none;
    if (r.alive && r.present &&
        (r.activity == Activity::Walking || r.activity == Activity::Working))
      r.activity = Activity::Idle;
  }
  fire.worker = none;
}
Id World::ignite(Id fid, float intensity) {
  auto &f = facilities.at(fid);
  if (!f.open || f.demolished || activeFires.contains(fid) || !std::isfinite(intensity) ||
      intensity <= 0 || intensity > 1)
    throw std::runtime_error("fire.error.site");
  Fire fire;
  fire.id = Id(fires.size());
  fire.facility = fid;
  fire.heat = intensity;
  fire.started = fire.updated = time;
  fires.push_back(fire);
  activeFires[fid] = fire.id;
  auto waiting = f.queue;
  for (const auto &s : f.serving)
    waiting.push_back(s.resident);
  for (Id id : waiting)
    leaveQueue(id);
  log("fire.started", f.manager, fid);
  return fire.id;
}
void World::triggerFire(Id actor, Id fid) {
  if (config.mode != Mode::Cheat || actor != player)
    throw std::runtime_error("fire.error.cheat");
  ignite(fid);
}
float World::dangerAt(Vec3 p) const {
  float total = 0;
  for (const auto &[fid, id] : activeFires) {
    const auto &f = facilities[fid];
    const auto &fire = fires[id];
    int floor = int(std::floor((p.y + .1f) / 3));
    int above = floor - f.floor;
    if (above < 0 || above > 3)
      continue;
    float d = std::hypot(p.x - f.pos.x, p.z - f.pos.z);
    if (above == 0 && !fire.contained)
      total += fire.heat * std::max(0.f, 1 - d / 8);
    total += fire.smoke * .35f * std::max(0.f, 1 - d / 18) * (1 - above * .2f);
  }
  return std::clamp(total, 0.f, 2.f);
}
void World::requestFireResponse(Id actor, Id id) {
  auto &fire = fires.at(id);
  const auto &r = residents.at(actor);
  const auto &f = facilities[fire.facility];
  if (!r.alive || !r.present || fire.out || fire.contained || fire.escrow != none)
    throw std::runtime_error("fire.error.contract");
  bool business = actor == f.manager || r.account == operatingAccount(f.id) ||
                  directsCompany(actor, companyForAccount(operatingAccount(f.id)));
  Id payer = actor == dormOwner ? dormAccount : business ? operatingAccount(f.id) : r.account;
  if (accounts[payer].balance < fire.budget)
    throw std::runtime_error("fire.error.budget");
  fire.escrow = account("Emergency response escrow " + std::to_string(id));
  fire.payer = payer;
  pay(payer, fire.escrow, fire.budget, "fire-response-budget");
  log("fire.funded", actor, fire.facility);
}
Id World::extinguisher(Id actor) const {
  const auto &r = residents.at(actor);
  for (Id id : r.items) {
    const auto &i = items[id];
    if (i.kind == ItemKind::Extinguisher && !i.consumed && i.durability > .00001f &&
        i.carrier == actor &&
        (i.owner == r.account || (i.owner == r.employer && i.reservedAt == r.jobSite)))
      return id;
  }
  return none;
}
Id World::issueExtinguisher(Id actor, Id fid) {
  auto &r = residents.at(actor);
  auto &f = facilities.at(fid);
  if (!r.alive || !r.present || f.kind != FacilityKind::FireStation || !operational(fid) ||
      f.stock < 1 || !near(actor, f.pos, 3) || !accessAllowed(actor, fid))
    throw std::runtime_error("fire.error.equipment");
  bool duty = r.role == Role::Firefighter && r.jobSite == fid && onShift(actor);
  if (extinguisher(actor) != none)
    throw std::runtime_error("fire.error.carried");
  if (!duty)
    pay(r.account, operatingAccount(fid), 4000, "extinguisher-purchase");
  Item item;
  item.id = Id(items.size());
  item.kind = ItemKind::Extinguisher;
  item.owner = duty ? r.employer : r.account;
  item.carrier = actor;
  item.pos = position(actor);
  item.reservedAt = duty ? fid : none;
  --f.stock;
  items.push_back(item);
  r.items.push_back(item.id);
  log("fire.equipped", actor, fid);
  return item.id;
}
Vec3 World::fireApproach(Id id) const {
  const auto &f = facilities.at(fires.at(id).facility);
  // The nozzle remains outside the counter's collision box.
  return accessible(f.pos + Vec3{0, 0, -4});
}
void World::assignFirefighter(Id actor, Id id) {
  auto &fire = fires.at(id);
  auto &r = residents.at(actor);
  if (!r.alive || !r.present || r.surrendered || r.activity == Activity::Fighting ||
      r.role != Role::Firefighter || !onShift(actor) || r.jobSite == none ||
      facilities[r.jobSite].kind != FacilityKind::FireStation || !operational(r.jobSite) ||
      fire.out || fire.contained || fire.worker != none || fire.escrow == none ||
      r.queuedAt != none || r.carrying != none || r.medicalDuty != none || r.ambulance != none ||
      r.carriedBy != none || r.lift != none || !near(actor, facilities[r.jobSite].pos, 3))
    throw std::runtime_error("fire.error.dispatch");
  for (const auto &other : fires)
    if (!other.out && other.worker == actor)
      throw std::runtime_error("fire.error.busy");
  if (extinguisher(actor) == none && facilities[r.jobSite].stock < 1)
    throw std::runtime_error("fire.error.empty_rack");
  // Route replacement is transactional; do not consume stock until planning succeeds.
  routeTo(actor, fireApproach(id));
  if (extinguisher(actor) == none)
    issueExtinguisher(actor, r.jobSite);
  clockOut(actor);
  fire.worker = actor;
  fire.station = r.jobSite;
  r.task = TaskKind::FireResponse;
  r.target = id;
  r.activity = Activity::Walking;
  schedule(std::max(time + .01, r.route.back().time), EventKind::Arrive, actor, none,
           r.routeVersion);
  log("fire.dispatched", actor, fire.facility);
}
void World::beginExtinguish(Id actor, Id id) {
  auto &r = residents.at(actor);
  auto &fire = fires.at(id);
  Vec3 point = facilities[fire.facility].pos + Vec3{0, 1.6f, 0};
  if (!r.alive || !r.present || fire.out || fire.contained || r.surrendered ||
      r.activity == Activity::Fighting || r.activity == Activity::Jailed ||
      r.activity == Activity::Escorted || r.activity == Activity::Restrained || r.vehicle != none ||
      r.lift != none || r.carriedBy != none || r.ambulance != none ||
      !near(actor, point - Vec3{0, 1.6f, 0}, 7) ||
      !lineOfSight(position(actor) + Vec3{0, 1.6f, 0}, point) || extinguisher(actor) == none)
    throw std::runtime_error("fire.error.spray");
  clockOut(actor);
  leaveQueue(actor);
  r.pos = position(actor);
  r.route.clear();
  ++r.routeVersion;
  r.activity = Activity::Working;
  r.task = TaskKind::FireResponse;
  r.target = id;
  fireSprays[actor] = {id, extinguisher(actor), time};
  log("fire.spraying", actor, fire.facility);
}
void World::stopExtinguish(Id actor) {
  fireSprays.erase(actor);
  auto &r = residents.at(actor);
  if (r.task == TaskKind::FireResponse && r.activity == Activity::Working) {
    r.task = TaskKind::None;
    r.target = none;
    r.activity = Activity::Idle;
  }
}
void World::extinguishTick() {
  std::vector<Id> stop;
  for (auto &[actor, spray] : fireSprays) {
    auto &r = residents[actor];
    auto &fire = fires[spray.fire];
    auto &item = items[spray.item];
    Vec3 at = facilities[fire.facility].pos;
    if (!r.alive || !r.present || r.surrendered || r.activity != Activity::Working ||
        r.task != TaskKind::FireResponse || r.target != fire.id || fire.contained || fire.out ||
        extinguisher(actor) != spray.item || !near(actor, at, 7) ||
        !lineOfSight(position(actor) + Vec3{0, 1.6f, 0}, at + Vec3{0, 1.6f, 0})) {
      stop.push_back(actor);
      continue;
    }
    double seconds =
        std::min({60.0, std::max(0.0, time - spray.workedAt), double(item.durability) * 1200});
    spray.workedAt = time;
    if (seconds <= 0)
      continue;
    item.durability = std::max(0.f, item.durability - float(seconds / 1200));
    fire.heat = std::max(0.f, fire.heat - float(seconds / 600));
    fire.smoke = std::max(0.f, fire.smoke - float(seconds / 1800));
    r.energy = std::max(0.f, r.energy - float(seconds / 12000));
    if (fire.worker == actor && fire.escrow != none) {
      Money amount = std::min(fire.budget - fire.paid, Money(seconds * 15));
      Money wage = amount / 3;
      pay(fire.escrow, r.account, wage, "fire-response-labor");
      pay(fire.escrow, operatingAccount(fire.station), amount - wage, "fire-response-service");
      fire.paid += amount;
    }
    if (fire.heat < .001f)
      fire.contained = true;
    if (item.durability <= .00001f || fire.contained)
      stop.push_back(actor);
  }
  for (Id id : stop)
    stopExtinguish(id);
}
bool World::evacuate(Id actor) {
  auto &r = residents.at(actor);
  if (!r.alive || !r.present || r.vehicle != none || r.lift != none || r.ambulance != none ||
      r.carriedBy != none || r.medicalDuty != none || r.activity == Activity::Jailed ||
      r.activity == Activity::Restrained || r.activity == Activity::Escorted)
    return false;
  Vec3 start = position(actor);
  int floor = std::clamp(int(std::round(start.y / 3)), 0, config.floors - 1);
  if (std::abs(start.y - floor * 3.f) > .15f)
    return false;
  for (float distance : {20.f, 32.f, 48.f})
    for (int direction = 0; direction < 8; ++direction) {
      float angle = direction * .785398163f;
      Vec3 goal = start + Vec3{std::cos(angle) * distance, 0, std::sin(angle) * distance};
      if (!clearPosition(goal) || dangerAt(goal) > .02f)
        continue;
      try {
        stopExtinguish(actor);
        travel(actor, goal, TaskKind::Evacuate, none);
        log("fire.evacuating", actor);
        return true;
      } catch (const std::runtime_error &) {
      }
    }
  return false;
}
void World::emergencyTick() {
  if (nextAccidentAt <= 0)
    nextAccidentAt = time + 3600;
  if (time >= nextAccidentAt) {
    nextAccidentAt = time + 3600;
    // One building-wide hourly check: rare, seeded accidents, scaled to occupancy.
    if (config.accidentRate > 0 &&
        random() < .006f * config.accidentRate * std::min(2.f, float(residents.size()) / 100000)) {
      std::vector<Id> possible;
      for (const auto &f : facilities)
        if (f.open && !f.demolished && !activeFires.contains(f.id) && f.condition > .1f &&
            (f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe ||
             f.kind == FacilityKind::Workshop || f.kind == FacilityKind::Laundry ||
             f.kind == FacilityKind::Office))
          possible.push_back(f.id);
      if (!possible.empty())
        ignite(possible[std::size_t(random() * possible.size())]);
    }
  }
  if (activeFires.empty() && fireSprays.empty())
    return;
  extinguishTick();
  std::vector<Id> finished;
  double exposureSeconds = 0;
  for (const auto &[fid, id] : activeFires) {
    auto &fire = fires[id];
    auto &f = facilities[fid];
    double dt = std::min(60.0, std::max(0.0, time - fire.updated));
    fire.updated = time;
    exposureSeconds = std::max(exposureSeconds, dt);
    if (!fire.contained) {
      fire.fuel = std::max(0.f, fire.fuel - float(dt / 10800));
      fire.heat = fire.fuel > 0 ? std::min(1.f, fire.heat + float(dt / 7200))
                                : std::max(0.f, fire.heat - float(dt / 1200));
      fire.smoke = std::min(1.f, fire.smoke + float(dt / 3600) * fire.heat);
      f.condition = std::max(0.f, f.condition - float(dt / 10800) * fire.heat);
      if (f.stock > 0 && dt > 0)
        f.stock = std::max(0, f.stock - std::max(1, int(dt / 60 * fire.heat * 3)));
      if (fire.heat <= .001f)
        fire.contained = true;
    } else {
      fire.heat = 0;
      fire.smoke = std::max(0.f, fire.smoke - float(dt / 1200));
    }
    if (fire.worker != none) {
      auto &r = residents[fire.worker];
      if (!r.alive || !r.present || !onShift(r.id) || r.jobSite != fire.station ||
          (r.task != TaskKind::FireResponse || r.target != id)) {
        releaseResponder(*this, fire);
      }
    }
    if (fire.escrow == none && !fire.contained) {
      if (f.manager != none && residents[f.manager].alive && residents[f.manager].present) {
        try {
          requestFireResponse(f.manager, id);
        } catch (const std::runtime_error &) {
        }
      }
      if (fire.escrow == none && dormOwner != player && residents[dormOwner].alive) {
        try {
          requestFireResponse(dormOwner, id);
        } catch (const std::runtime_error &) {
        }
      }
    }
    if (fire.escrow != none && fire.worker == none && !fire.contained) {
      std::vector<std::pair<float, Id>> responders;
      for (const auto &r : residents)
        if (r.automatic && r.alive && r.present && r.role == Role::Firefighter && onShift(r.id) &&
            r.jobSite != none && std::abs(facilities[r.jobSite].floor - f.floor) <= 80 &&
            near(r.id, facilities[r.jobSite].pos, 3))
          responders.emplace_back(length(position(r.id) - f.pos), r.id);
      std::sort(responders.begin(), responders.end());
      for (const auto &[distance, actor] : responders) {
        try {
          assignFirefighter(actor, id);
          break;
        } catch (const std::runtime_error &) {
        }
      }
    }
    if (fire.contained && fire.smoke <= .001f) {
      fire.out = true;
      if (fire.escrow != none)
        pay(fire.escrow, fire.payer, accounts[fire.escrow].balance, "fire-response-unspent");
      releaseResponder(*this, fire);
      finished.push_back(fid);
      log("fire.out", f.manager, fid);
    }
  }
  if (exposureSeconds > 0)
    for (auto &r : residents) {
      if (!r.alive || !r.present)
        continue;
      float danger = dangerAt(position(r.id));
      if (danger <= .01f)
        continue;
      bool protectedCrew = r.role == Role::Firefighter && extinguisher(r.id) != none;
      r.health -= float(exposureSeconds / 60) * danger * (protectedCrew ? .5f : 5.f);
      r.happiness = std::max(0.f, r.happiness - float(exposureSeconds / 3600) * danger * .1f);
      if (r.health <= 0)
        kill(r.id);
      else if (r.automatic && !fireSprays.contains(r.id) && r.task != TaskKind::FireResponse &&
               r.task != TaskKind::Evacuate && danger > .02f)
        evacuate(r.id);
    }
  for (Id fid : finished)
    activeFires.erase(fid);
}
void World::validateEmergencies() const {
  auto resident = [&](Id id) { return id >= 0 && std::size_t(id) < residents.size(); };
  auto facility = [&](Id id) { return id >= 0 && std::size_t(id) < facilities.size(); };
  auto account = [&](Id id) { return id > 0 && std::size_t(id) < accounts.size(); };
  if (!std::isfinite(nextAccidentAt) || nextAccidentAt < 0)
    throw std::runtime_error("Invalid emergency clock");
  std::map<Id, Id> active;
  std::set<Id> assigned;
  for (std::size_t i = 0; i < fires.size(); ++i) {
    const auto &f = fires[i];
    if (f.id != Id(i) || !facility(f.facility) || !std::isfinite(f.heat) || f.heat < 0 ||
        f.heat > 1 || !std::isfinite(f.smoke) || f.smoke < 0 || f.smoke > 1 ||
        !std::isfinite(f.fuel) || f.fuel < 0 || f.fuel > 1 || !std::isfinite(f.started) ||
        !std::isfinite(f.updated) || f.started < 0 || f.updated < f.started || f.updated > time ||
        f.budget != 18000 || f.paid < 0 || f.paid > f.budget ||
        (f.out && (!f.contained || f.heat > .001f || f.smoke > .001f)) ||
        (f.worker != none && !resident(f.worker)) ||
        (f.station != none &&
         (!facility(f.station) || facilities[f.station].kind != FacilityKind::FireStation)) ||
        (f.worker != none && f.station == none) ||
        (f.escrow != none && (!account(f.escrow) || !account(f.payer) ||
                              accounts[f.escrow].balance != (f.out ? 0 : f.budget - f.paid))) ||
        (f.escrow == none && (f.payer != none || f.paid != 0)))
      throw std::runtime_error("Invalid fire emergency");
    if (!f.out && !active.emplace(f.facility, f.id).second)
      throw std::runtime_error("Duplicate active fire");
    if (!f.out && f.worker != none && !assigned.insert(f.worker).second)
      throw std::runtime_error("Responder assigned twice");
  }
  if (active != activeFires)
    throw std::runtime_error("Invalid active fire index");
  for (const auto &[actor, s] : fireSprays)
    if (!resident(actor) || s.fire < 0 || std::size_t(s.fire) >= fires.size() || s.item < 0 ||
        std::size_t(s.item) >= items.size() || items[s.item].kind != ItemKind::Extinguisher ||
        !std::isfinite(s.workedAt) || s.workedAt < 0 || s.workedAt > time)
      throw std::runtime_error("Invalid extinguisher activity");
}
} // namespace mega
