#include "mega/world.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
namespace mega {
Id World::report(CrimeKind kind, Id offender, Id victim, bool threat) {
  (void)residents.at(offender);
  (void)residents.at(victim);
  Incident c;
  c.id = static_cast<Id>(incidents.size());
  c.kind = kind;
  c.offender = offender;
  c.victim = victim;
  c.pos = position(victim);
  c.time = time;
  c.threat = threat;
  incidents.push_back(c);
  log(kind == CrimeKind::Murder         ? "crime.murder"
      : kind == CrimeKind::QueueCutting ? "crime.queue_cut"
      : kind == CrimeKind::Theft        ? "crime.theft"
                                        : "crime.violence",
      offender, victim);
  return c.id;
}
void World::cutQueue(Id id, Id fid, std::size_t place) {
  auto &r = residents.at(id);
  auto &f = facilities.at(fid);
  auto it = std::find(f.queue.begin(), f.queue.end(), id);
  if (it == f.queue.end() || r.activity != Activity::Queuing ||
      place >= std::size_t(it - f.queue.begin()))
    throw std::runtime_error("You must be behind someone in this queue");
  Id victim = f.queue[place];
  f.queue.erase(it);
  f.queue.insert(f.queue.begin() + static_cast<std::ptrdiff_t>(place), id);
  layoutQueue(fid);
  report(CrimeKind::QueueCutting, id, victim);
  residents[victim].happiness = std::max(0.f, residents[victim].happiness - .12f);
  if (residents[victim].aggression > .7f && random() < residents[victim].aggression) {
    leaveQueue(victim);
    if (near(victim, position(id), 2.5f))
      attack(victim, id, false);
  }
}
void World::attack(Id attacker, Id victim, bool lethal) {
  combatHit(attacker, victim, lethal, 2.5f);
}
void World::combatHit(Id attacker, Id victim, bool lethal, float reach) {
  auto &a = residents.at(attacker);
  auto &v = residents.at(victim);
  if (attacker == victim || !a.alive || !v.alive || !a.present || !v.present || a.surrendered ||
      a.activity == Activity::Jailed || a.activity == Activity::Restrained ||
      a.activity == Activity::Escorted || a.ambulance != none || a.carriedBy != none ||
      v.ambulance != none || v.carriedBy != none || a.lift != none || v.lift != none ||
      time < a.nextHit || !near(attacker, position(victim), reach))
    throw std::runtime_error("Attack unavailable");
  bool helpless = v.surrendered || v.activity == Activity::Jailed ||
                  v.activity == Activity::Restrained || v.activity == Activity::Escorted;
  bool defense = v.opponent == attacker && v.activity == Activity::Fighting && !helpless;
  bool enforcement =
      a.role == Role::Warden && std::any_of(incidents.begin(), incidents.end(), [&](auto &c) {
        return !c.resolved && c.offender == victim && c.kind != CrimeKind::QueueCutting;
      });
  if (helpless)
    report(CrimeKind::ExcessiveForce, attacker, victim);
  else if (!defense && !enforcement)
    report(CrimeKind::Assault, attacker, victim, true);
  bool justified =
      !helpless && (defense || enforcement) && v.lethalThreat && v.activity == Activity::Fighting;
  if (v.opponent != none && v.opponent != attacker && residents[v.opponent].opponent == victim) {
    auto &former = residents[v.opponent];
    former.opponent = none;
    if (former.activity == Activity::Fighting)
      former.activity = Activity::Idle;
  }
  clockOut(attacker);
  clockOut(victim);
  a.pos = position(attacker);
  v.pos = position(victim);
  leaveQueue(attacker);
  leaveQueue(victim);
  a.route.clear();
  v.route.clear();
  ++a.routeVersion;
  ++v.routeVersion;
  a.task = TaskKind::None;
  v.task = TaskKind::None;
  a.activity = Activity::Fighting;
  a.opponent = victim;
  a.nextHit = time + 20;
  a.lethalThreat = lethal;
  if (!helpless) {
    v.activity = Activity::Fighting;
    v.opponent = attacker;
  }
  float damage = lethal ? (helpless ? 110 : 38) : std::clamp(a.strength * .23f, 5.f, 24.f);
  v.health -= damage;
  a.energy = std::max(0.f, a.energy - .025f);
  if (v.health <= 0) {
    kill(victim, attacker, justified);
    return;
  }
  if (!helpless && (v.health < 30 || a.strength > v.strength * 1.6f || v.energy < .1f))
    surrender(victim);
}
void World::surrender(Id id) {
  auto &r = residents.at(id);
  if (!r.alive || !r.present)
    throw std::runtime_error("Resident unavailable");
  r.pos = position(id);
  r.route.clear();
  ++r.routeVersion;
  leaveQueue(id);
  clockOut(id);
  r.surrendered = true;
  r.activity = Activity::Surrendered;
  r.task = TaskKind::None;
  if (r.opponent != none) {
    auto &o = residents[r.opponent];
    if (o.activity == Activity::Fighting) {
      o.activity = Activity::Idle;
      o.opponent = none;
    }
  }
  r.opponent = none;
  r.lethalThreat = false;
  log("combat.surrender", id);
}
void World::wardenRespond(Id id) {
  auto &w = residents.at(id);
  if (w.role != Role::Warden || !w.alive || !w.present || w.activity == Activity::Jailed ||
      w.activity == Activity::Escorted || w.task == TaskKind::Escort ||
      w.task == TaskKind::Investigate)
    return;
  for (auto &c : incidents) {
    if (c.resolved || !residents[c.offender].alive || !residents[c.offender].present ||
        (c.warden != none && c.warden != id))
      continue;
    if (c.kind == CrimeKind::QueueCutting) {
      if (near(id, position(c.offender), 5)) {
        c.resolved = true;
        log("warden.warning", id, c.offender);
      }
      continue;
    }
    c.warden = id;
    if (near(id, position(c.offender), 2.5f)) {
      if (residents[c.offender].surrendered)
        apprehend(id, c.offender);
      else if (time >= w.nextHit)
        attack(id, c.offender, false);
    } else if (residents[c.offender].lethalThreat && hasGun(id) &&
               near(id, position(c.offender), 30)) {
      try {
        shoot(id, c.offender);
      } catch (const std::runtime_error &) {
        travel(id, position(c.offender), TaskKind::Investigate, c.id);
      }
    } else
      travel(id, position(c.offender), TaskKind::Investigate, c.id);
    return;
  }
}
void World::apprehend(Id warden, Id suspect) {
  auto &w = residents.at(warden);
  auto &s = residents.at(suspect);
  if (w.role != Role::Warden || !w.alive || !s.alive || !w.present || !s.present ||
      !s.surrendered || !near(warden, position(suspect), 2.5f) ||
      !std::any_of(incidents.begin(), incidents.end(), [&](auto &c) {
        return !c.resolved && c.offender == suspect && c.kind != CrimeKind::QueueCutting;
      }))
    throw std::runtime_error("Cannot apprehend this resident");
  Id jail = nearest(warden, FacilityKind::Jail);
  if (jail == none)
    throw std::runtime_error("No operational jail");
  int occupied = 0;
  for (auto &r : residents)
    if ((r.activity == Activity::Jailed || r.activity == Activity::Escorted) && r.target == jail)
      ++occupied;
  if (occupied >= std::max(4, facilities[jail].stations))
    throw std::runtime_error("Jail is full");
  w.escort = suspect;
  travel(warden, facilities[jail].pos, TaskKind::Escort, jail);
  Vec3 original = position(suspect), guardStart = position(warden);
  for (auto &point : w.route)
    point.time += 40;
  w.route.insert(w.route.begin(), {guardStart, time});
  ++w.routeVersion;
  schedule(w.route.back().time, EventKind::Arrive, warden, none, w.routeVersion);
  s.route = w.route;
  for (auto &point : s.route)
    point.p.x += .8f;
  s.route.front().p = original;
  s.pos = original;
  ++s.routeVersion;
  s.activity = Activity::Escorted;
  s.task = TaskKind::None;
  s.target = jail;
  s.escort = warden;
  for (auto &c : incidents)
    if (c.offender == suspect && !c.resolved) {
      c.resolved = true;
      c.warden = warden;
    }
  log("custody.escorting", warden, suspect);
}
void World::kill(Id id, Id killer, bool justified) {
  auto &r = residents.at(id);
  if (!r.alive)
    return;
  r.pos = position(id);
  Id deathLift = r.lift;
  r.lift = none;
  cancelLift(id);
  r.route.clear();
  ++r.routeVersion;
  clockOut(id);
  Remains body;
  body.resident = id;
  body.pos = r.pos;
  body.diedAt = time;
  body.ambulance = r.ambulance;
  body.lift = deathLift;
  remains.push_back(body);
  r.ambulance = none;
  r.carriedBy = none;
  crowd.erase(id);
  for (auto &b : remains)
    if (b.worker == id || b.carrier == id) {
      if (b.carrier == id)
        b.pos = r.pos;
      b.worker = none;
      b.carrier = none;
      b.destination = none;
    }
  r.bodyCarrying = none;
  for (auto &shipment : freight)
    if (shipment.worker == id && shipment.state != FreightState::Delivered) {
      shipment.worker = none;
      shipment.state = FreightState::Waiting;
    }
  for (auto &patient : residents)
    if (patient.carriedBy == id) {
      patient.pos = r.pos + Vec3{0, 0, .8f};
      patient.carriedBy = none;
      patient.activity = Activity::Idle;
    }
  stopExtinguish(id);
  r.alive = false;
  r.health = 0;
  r.activity = Activity::Dead;
  r.ladder = -1;
  for (auto &p : projects)
    if (p.worker == id && p.state == ProjectState::Working) {
      p.worker = none;
      p.state = ProjectState::Funded;
      p.workAt = -1;
    }
  for (auto &p : expansions)
    if (p.worker == id && !p.complete) {
      p.worker = none;
      p.workAt = -1;
    }
  r.task = TaskKind::None;
  r.surrendered = false;
  leaveQueue(id);
  if (r.vehicle != none) {
    vehicles[r.vehicle].driver = none;
    vehicles[r.vehicle].speed = 0;
    r.vehicle = none;
  }
  for (Id item : r.items)
    if (items[item].carrier == id) {
      items[item].carrier = none;
      items[item].pos = r.pos;
    }
  if (r.carrying != none) {
    items[r.carrying].carrier = none;
    items[r.carrying].pos = r.pos;
    for (auto &o : orders)
      if (o.item == r.carrying && o.state == OrderState::ToCustomer)
        o.state = OrderState::Lost;
    r.carrying = none;
  }
  if (r.opponent != none && residents[r.opponent].alive) {
    residents[r.opponent].activity = Activity::Idle;
    residents[r.opponent].opponent = none;
  }
  if (r.escort != none) {
    auto &other = residents[r.escort];
    other.escort = none;
    if (other.alive && other.activity == Activity::Escorted) {
      other.pos = position(other.id);
      other.route.clear();
      other.activity = Activity::Surrendered;
    }
  }
  if (killer != none && !justified)
    report(CrimeKind::Murder, killer, id);
  else if (killer != none)
    log("combat.justified", killer, id);
  log("resident.died", id, killer);
  if ((id == player && config.mode != Mode::Cheat) || living() == 0) {
    gameOver = true;
    endReason = id == player ? "player-died" : "population-empty";
  }
}
void World::applyFall(Id id, float speed, Id pusher) {
  if (!std::isfinite(speed) || speed < 0 || speed > 2000)
    throw std::runtime_error("Invalid impact speed");
  auto &r = residents.at(id);
  if (!r.alive)
    return;
  float damage = fallDamage(speed);
  r.health -= damage;
  if (r.health <= 0)
    kill(id, pusher);
  else if (pusher != none && damage > 0)
    report(CrimeKind::Assault, pusher, id);
}
void World::depart(Id id) {
  auto &r = residents.at(id);
  if (!r.alive || !r.present || r.vehicle != none || r.lift != none || r.ambulance != none ||
      r.carriedBy != none || r.medicalDuty != none || r.activity == Activity::Jailed ||
      r.activity == Activity::Escorted || !near(id, layout(0).entrance(), 4))
    throw std::runtime_error("Reach the lobby exit before moving out");
  r.pos = position(id);
  r.route.clear();
  ++r.routeVersion;
  leaveQueue(id);
  clockOut(id);
  r.present = false;
  r.activity = Activity::Departed;
  pay(r.account, 0, accounts[r.account].balance, "emigration-outflow");
  if (residentHomes[id] != none) {
    housing[residentHomes[id]].tenant = none;
    residentHomes[id] = none;
  }
  if ((id == player && config.mode != Mode::Cheat) || living() == 0) {
    gameOver = true;
    endReason = id == player ? "player-moved-out" : "population-empty";
  }
  log("resident.departed", id);
}
void World::validate() const {
  config.validate();
  validateCompanies();
  validateEmergencies();
  if (!std::isfinite(time) || time < 0 || accounts.size() < 2 || residents.size() < 2 ||
      residents.size() > 500000 || names.size() != residents.size() ||
      residentHomes.size() != residents.size() || player < 0 ||
      std::size_t(player) >= residents.size())
    throw std::runtime_error("Invalid world header");
  if (floorPlans.size() != std::size_t(config.floors))
    throw std::runtime_error("Missing floor plans");
  for (auto &f : floorPlans)
    if (f.width < 96 || f.width > 2048 || f.depth < 72 || f.depth > 2048)
      throw std::runtime_error("Invalid floor dimensions");
  for (const auto &r : residents)
    if (!std::isfinite(r.fitness) || r.fitness < 0 || r.fitness > 100)
      throw std::runtime_error("Invalid resident fitness");
  Money total = 0;
  for (std::size_t i = 0; i < accounts.size(); ++i) {
    if (i && accounts[i].balance < 0)
      throw std::runtime_error("Negative internal balance");
    if ((accounts[i].balance > 0 &&
         total > std::numeric_limits<Money>::max() - accounts[i].balance) ||
        (accounts[i].balance < 0 &&
         total < std::numeric_limits<Money>::min() - accounts[i].balance))
      throw std::runtime_error("Invalid money total");
    total += accounts[i].balance;
  }
  if (total != 0)
    throw std::runtime_error("Money conservation failed");
  auto accountValid = [&](Id a) { return a >= 0 && std::size_t(a) < accounts.size(); };
  auto residentValid = [&](Id a) { return a >= 0 && std::size_t(a) < residents.size(); };
  for (std::size_t i = 0; i < expansions.size(); ++i) {
    const auto &p = expansions[i];
    if (p.id != Id(i) || !accountValid(p.owner) || !accountValid(p.contractor) ||
        !accountValid(p.escrow) || (p.worker != none && !residentValid(p.worker)) || p.floor < 0 ||
        p.floor >= config.floors || p.width < 96 || p.width > 2048 || p.depth < 72 ||
        p.depth > 2048 || p.budget <= 0 || p.paid < 0 || p.paid > p.budget ||
        !std::isfinite(p.remaining) || !std::isfinite(p.totalWork) || p.totalWork <= 0 ||
        p.remaining < 0 || p.remaining > p.totalWork ||
        accounts[p.escrow].balance != p.budget - p.paid || !std::isfinite(p.site.x) ||
        !std::isfinite(p.site.y) || !std::isfinite(p.site.z))
      throw std::runtime_error("Invalid expansion contract");
  }
  for (std::size_t i = 0; i < residents.size(); ++i) {
    auto &r = residents[i];
    if (r.id != Id(i) || !accountValid(r.account) || r.home < 0 || r.home >= config.floors ||
        !std::isfinite(r.health) || !std::isfinite(r.pos.x) || !std::isfinite(r.pos.y) ||
        !std::isfinite(r.pos.z) || int(r.activity) < 0 ||
        int(r.activity) > int(Activity::Departed) || int(r.role) < 0 ||
        int(r.role) > int(Role::Firefighter))
      throw std::runtime_error("Invalid resident");
    if (r.ladder < -1 || r.ladder >= layout().ladderCount() || r.ladderTarget < -1 ||
        r.ladderTarget >= config.floors || std::abs(r.ladderDirection) > 1)
      throw std::runtime_error("Invalid climbing state");
    const auto &q = r.routine;
    if (!std::isfinite(q.shiftStart) || q.shiftStart < 0 || q.shiftStart >= 24 ||
        !std::isfinite(q.sleepStart) || q.sleepStart < 0 || q.sleepStart >= 24 ||
        !std::isfinite(q.shiftHours) || q.shiftHours < 0 || q.shiftHours > 24 ||
        !std::isfinite(q.sleepHours) || q.sleepHours < 0 || q.sleepHours > 24 || q.daysOff < 0 ||
        q.daysOff > 127 || !std::isfinite(q.workSince) || q.workSince < -1 || q.workSince > time ||
        !std::isfinite(q.unpaidWages) || q.unpaidWages < 0 || q.unpaidWages > 1e12)
      throw std::runtime_error("Invalid daily routine");
    for (const auto &[employer, claim] : q.wageClaims)
      if (!accountValid(employer) || employer == 0 || !std::isfinite(claim) || claim < 0 ||
          claim > 1e12)
        throw std::runtime_error("Invalid wage claim");
    if (!r.alive && r.activity != Activity::Dead)
      throw std::runtime_error("Dead resident performing activity");
    double prior = -1;
    for (auto &p : r.route) {
      if (!std::isfinite(p.time) || p.time < prior || !std::isfinite(p.p.x) ||
          !std::isfinite(p.p.y) || !std::isfinite(p.p.z))
        throw std::runtime_error("Invalid route");
      prior = p.time;
    }
  }
  std::set<Id> queued;
  for (std::size_t i = 0; i < facilities.size(); ++i) {
    auto &f = facilities[i];
    if (f.id != Id(i) || !accountValid(f.owner) || f.floor < 0 || f.floor >= config.floors ||
        f.stations < 1 || f.stock < 0 || f.price < 0 || !std::isfinite(f.duration) ||
        f.duration <= 0 || int(f.kind) < 0 || int(f.kind) > int(FacilityKind::FireStation) ||
        f.serving.size() > std::size_t(f.stations))
      throw std::runtime_error("Invalid facility");
    if (!std::isfinite(f.condition) || f.condition < 0 || f.condition > 1 || f.wageOffer < 0 ||
        f.waste < 0)
      throw std::runtime_error("Invalid business operations");
    if (!std::isfinite(f.opensAt) || f.opensAt < 0 || f.opensAt >= 24 ||
        !std::isfinite(f.openHours) || f.openHours < 0 || f.openHours > 24 || f.staffNeeded < 0 ||
        f.staffNeeded > 64 || int(f.staffRole) < 0 || int(f.staffRole) > int(Role::Firefighter))
      throw std::runtime_error("Invalid facility staffing/hours");
    std::set<Id> staff;
    for (Id id : f.employees)
      if (!residentValid(id) || !staff.insert(id).second)
        throw std::runtime_error("Invalid staff roster");
    for (Id id : f.whitelist)
      if (!residentValid(id))
        throw std::runtime_error("Invalid access list");
    std::set<int> stations;
    for (const auto &s : f.serving)
      if (s.station >= f.stations || (s.station >= 0 && !stations.insert(s.station).second) ||
          !std::isfinite(s.until) || s.until < -1 || !std::isfinite(s.remaining) ||
          s.remaining < 0 || s.price < 0)
        throw std::runtime_error("Invalid service progress");
    for (Id id : f.queue)
      if (!residentValid(id) || !residents[id].alive || !residents[id].present ||
          !queued.insert(id).second || residents[id].queuedAt != f.id)
        throw std::runtime_error("Invalid queue membership");
    for (auto &s : f.serving)
      if (!residentValid(s.resident) || !residents[s.resident].alive ||
          !residents[s.resident].present || !queued.insert(s.resident).second)
        throw std::runtime_error("Invalid service reservation");
  }
  for (auto &h : housing)
    if (!accountValid(h.owner) || h.floor < 0 || h.floor >= config.floors || h.price < 0 ||
        h.rent < 0 || !std::isfinite(h.location.x) || !std::isfinite(h.location.y) ||
        !std::isfinite(h.location.z) || (h.tenant != none && !residentValid(h.tenant)))
      throw std::runtime_error("Invalid housing");
  auto lookValid = [](const Appearance &a) {
    return a.primary >= 0 && a.primary < 8 && a.secondary >= 0 && a.secondary < 8 && a.style >= 0 &&
           a.style < 3 && a.accessory >= 0 && a.accessory < 3;
  };
  for (const auto &i : items)
    if (!lookValid(i.look) || (i.stolenFrom != none && !accountValid(i.stolenFrom)))
      throw std::runtime_error("Invalid item appearance/provenance");
  for (const auto &v : vehicles)
    if (!lookValid(v.look))
      throw std::runtime_error("Invalid vehicle appearance");
  for (const auto &h : housing) {
    const auto &d = h.decor;
    if (d.frame < 0 || d.frame > 7 || d.linen < 0 || d.linen > 7 || d.blanket < 0 ||
        d.blanket > 7 || d.closet < 0 || d.closet > 7)
      throw std::runtime_error("Invalid home appearance");
  }
  auto facilityValid = [&](Id id) { return id >= 0 && std::size_t(id) < facilities.size(); };
  auto itemValid = [&](Id id) { return id >= 0 && std::size_t(id) < items.size(); };
  auto vehicleValid = [&](Id id) { return id >= 0 && std::size_t(id) < vehicles.size(); };
  auto ambulanceValid = [&](Id id) { return id >= 0 && std::size_t(id) < ambulances.size(); };
  auto bayValid = [&](Id id) { return id >= 0 && std::size_t(id) < parking.size(); };
  if (!residentValid(dormOwner) || !accountValid(dormAccount))
    throw std::runtime_error("Invalid owner");
  for (const auto &r : residents) {
    if (int(r.task) < 0 || int(r.task) > int(TaskKind::Evacuate) ||
        ((r.task == TaskKind::Service || r.task == TaskKind::Work || r.task == TaskKind::Escort) &&
         !facilityValid(r.target)) ||
        ((r.task == TaskKind::DeliveryPickup || r.task == TaskKind::DeliveryDrop) &&
         (r.target < 0 || std::size_t(r.target) >= orders.size())) ||
        ((r.task == TaskKind::MedicalReach || r.task == TaskKind::MedicalBoard ||
          r.task == TaskKind::MedicalClinic) &&
         (r.target < 0 || std::size_t(r.target) >= ambulances.size())) ||
        ((r.task == TaskKind::FreightPickup || r.task == TaskKind::FreightDrop) &&
         (r.target < 0 || std::size_t(r.target) >= freight.size())) ||
        (r.task == TaskKind::BodyPickup &&
         (r.target < 0 || std::size_t(r.target) >= remains.size())) ||
        (r.task == TaskKind::BodyDrop && !facilityValid(r.target)) ||
        ((r.task == TaskKind::ConstructionPickup || r.task == TaskKind::ConstructionDrop) &&
         (r.target < 0 || std::size_t(r.target) >= projects.size())) ||
        (r.task == TaskKind::FireResponse &&
         (r.target < 0 || std::size_t(r.target) >= fires.size())) ||
        (r.task == TaskKind::Investigate &&
         (r.target < 0 || std::size_t(r.target) >= incidents.size())))
      throw std::runtime_error("Invalid resident task");
    if ((r.jobSite != none && !facilityValid(r.jobSite)) ||
        (r.employer != none && !accountValid(r.employer)) ||
        (r.queuedAt != none && !facilityValid(r.queuedAt)) ||
        (r.terminal != none && !facilityValid(r.terminal)) ||
        (r.carrying != none && !itemValid(r.carrying)) ||
        (r.opponent != none && !residentValid(r.opponent)) ||
        (r.escort != none && !residentValid(r.escort)) ||
        (r.outfit != none && (!itemValid(r.outfit) || items[r.outfit].owner != r.account)) ||
        (r.vehicle != none && (!vehicleValid(r.vehicle) || vehicles[r.vehicle].driver != r.id)))
      throw std::runtime_error("Invalid resident reference");
    if ((r.ambulance != none && !ambulanceValid(r.ambulance)) ||
        (r.medicalDuty != none && !ambulanceValid(r.medicalDuty)) ||
        (r.carriedBy != none && (!residentValid(r.carriedBy) || r.carriedBy == r.id ||
                                 residents[r.carriedBy].carriedBy != none)) ||
        (r.bodyCarrying != none &&
         (r.bodyCarrying < 0 || std::size_t(r.bodyCarrying) >= remains.size() ||
          remains[r.bodyCarrying].carrier != r.id)) ||
        (r.medicalProvider != none &&
         (!facilityValid(r.medicalProvider) ||
          facilities[r.medicalProvider].kind != FacilityKind::Infirmary)) ||
        r.medicalTier < -1 || r.medicalTier > 2 || !std::isfinite(r.medicalUntil) ||
        !std::isfinite(r.infectedAt) || !std::isfinite(r.immuneUntil))
      throw std::runtime_error("Invalid medical record");
    for (Id id : r.items)
      if (!itemValid(id))
        throw std::runtime_error("Invalid inventory reference");
    Id home = residentHomes[r.id];
    if (home != none &&
        (home < 0 || std::size_t(home) >= housing.size() || housing[home].tenant != r.id))
      throw std::runtime_error("Invalid resident home");
  }
  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto &item = items[i];
    if (item.id != Id(i) || item.quantity < 0 || item.quantity > 10000 ||
        !accountValid(item.owner) || (item.carrier != none && !residentValid(item.carrier)) ||
        (item.reservedAt != none && !facilityValid(item.reservedAt)) ||
        !std::isfinite(item.cleanliness) || item.cleanliness < 0 || item.cleanliness > 1 ||
        !std::isfinite(item.durability) || item.durability < 0 || item.durability > 1 ||
        int(item.kind) < 0 || int(item.kind) > int(ItemKind::Extinguisher) ||
        item.askingPrice < 0 || (item.seller != none && !residentValid(item.seller)))
      throw std::runtime_error("Invalid item");
  }
  for (std::size_t i = 0; i < officeOrders.size(); ++i) {
    auto &o = officeOrders[i];
    if (o.id != Id(i) || !facilityValid(o.facility) || !residentValid(o.worker) ||
        !accountValid(o.escrow) || !accountValid(o.client) || !accountValid(o.employer) ||
        o.budget < 0 || !std::isfinite(o.remaining) || !std::isfinite(o.total) || o.remaining < 0 ||
        o.remaining > o.total || o.total <= 0 || !std::isfinite(o.due) ||
        !std::isfinite(o.workAt) || !std::isfinite(o.quality) || o.quality < 0 || o.quality > 100 ||
        (o.role != Role::Programmer && o.role != Role::VideoEditor &&
         o.role != Role::Administrator) ||
        o.brief < 0 || o.brief > 2 || accounts[o.escrow].balance != (o.submitted ? 0 : o.budget))
      throw std::runtime_error("Invalid office contract");
    if (o.liveRequests)
      for (int slot = 0; slot < 4; ++slot) {
        int type = (slot + o.brief) % 4;
        Id target = o.tickets[slot], result = o.dispatched[slot];
        if (o.role != Role::Administrator || o.outcomes[slot] < 0 || o.outcomes[slot] > 4 ||
            (target != none &&
             ((type == 0 || type == 3) ? !facilityValid(target)
              : type == 2              ? !residentValid(target)
                                       : (target < 0 || std::size_t(target) >= incidents.size()))))
          throw std::runtime_error("Invalid administration request");
        if (result != none && (result < 0 || (type == 0   ? std::size_t(result) >= freight.size()
                                              : type == 3 ? std::size_t(result) >= repairs.size()
                                              : type == 2 ? std::size_t(result) >= ambulances.size()
                                                          : !residentValid(result))))
          throw std::runtime_error("Invalid administration dispatch");
      }
  }
  std::set<Id> passengers;
  for (std::size_t i = 0; i < lifts.size(); ++i) {
    const auto &l = lifts[i];
    if (l.id != Id(i) || !facilityValid(l.facility) ||
        facilities[l.facility].kind != FacilityKind::Elevator || !std::isfinite(l.y) ||
        !std::isfinite(l.startY) || !std::isfinite(l.targetY) || !std::isfinite(l.departAt) ||
        !std::isfinite(l.arriveAt) || !std::isfinite(l.doorsUntil) ||
        l.passengers.size() > std::size_t(facilities[l.facility].stations))
      throw std::runtime_error("Invalid lift cabin");
    for (auto &p : l.passengers)
      if (!residentValid(p.resident) || residents[p.resident].lift != l.id ||
          !passengers.insert(p.resident).second || !std::isfinite(p.start) ||
          !std::isfinite(p.until) || p.until < p.start)
        throw std::runtime_error("Invalid lift passenger");
    for (auto &call : l.calls)
      if (!residentValid(call.resident) || !std::isfinite(call.called))
        throw std::runtime_error("Invalid lift call");
  }
  for (auto &[id, j] : liftJourneys)
    if (!residentValid(id) || j.lift < 0 || std::size_t(j.lift) >= lifts.size() ||
        !std::isfinite(j.goal.x) || !std::isfinite(j.goal.y) || !std::isfinite(j.goal.z) ||
        int(j.task) < 0 || int(j.task) > int(TaskKind::Evacuate) || j.task == TaskKind::LiftWait)
      throw std::runtime_error("Invalid lift journey");
  for (auto &r : residents)
    if (r.lift != none &&
        (r.lift < 0 || std::size_t(r.lift) >= lifts.size() || !passengers.contains(r.id)))
      throw std::runtime_error("Missing lift passenger");
  if (governance.askingPrice < 0 ||
      (governance.successor != none && !residentValid(governance.successor)))
    throw std::runtime_error("Invalid succession record");
  for (auto &[key, b] : relationships)
    if (!residentValid(b.a) || !residentValid(b.b) || b.a >= b.b ||
        key != ((std::uint64_t(b.a) << 32) | std::uint32_t(b.b)) || !std::isfinite(b.trust) ||
        b.trust < -1 || b.trust > 1 || !std::isfinite(b.lastTalk) || b.sharedInterest < 0 ||
        b.sharedInterest > 7)
      throw std::runtime_error("Invalid relationship");
  for (std::size_t i = 0; i < housingWorks.size(); ++i) {
    const auto &p = housingWorks[i];
    if (p.id != Id(i) || !residentValid(p.actor) || !accountValid(p.payer) ||
        !accountValid(p.contractor) || !facilityValid(p.workshop) ||
        (p.worker != none && !residentValid(p.worker)) ||
        (p.material != none && !itemValid(p.material)) ||
        (p.kind != HousingWorkKind::AddFloor &&
         (p.home < 0 || std::size_t(p.home) >= housing.size())) ||
        (p.replacement != none &&
         (p.replacement < 0 || std::size_t(p.replacement) >= housing.size())) ||
        (p.state != ProjectState::AwaitingConsent && !accountValid(p.escrow)) || p.budget <= 0 ||
        p.offer < 0 || int(p.kind) < 0 || int(p.kind) > int(HousingWorkKind::AddFloor) ||
        p.floor < 0 || p.floor >= config.floors || !std::isfinite(p.remaining) ||
        !std::isfinite(p.total) || !std::isfinite(p.workedAt) || p.remaining < 0 || p.total <= 0 ||
        p.remaining > p.total || !std::isfinite(p.site.x) || !std::isfinite(p.site.y) ||
        !std::isfinite(p.site.z))
      throw std::runtime_error("Invalid housing construction contract");
    for (Id who : p.consented)
      if (!residentValid(who))
        throw std::runtime_error("Invalid relocation consent");
  }
  for (std::size_t i = 0; i < repairs.size(); ++i) {
    const auto &p = repairs[i];
    if (p.id != Id(i) || !facilityValid(p.facility) || !facilityValid(p.workshop) ||
        !accountValid(p.escrow) || (p.worker != none && !residentValid(p.worker)) ||
        (p.partItem != none && !itemValid(p.partItem)) || p.budget != 24000 ||
        !std::isfinite(p.remaining) || p.remaining < 0 || p.remaining > 3600 ||
        !std::isfinite(p.workedAt) || (p.parts && p.partItem == none) ||
        accounts[p.escrow].balance != (p.complete ? 0
                                       : p.parts  ? 18000
                                                  : 24000))
      throw std::runtime_error("Invalid repair contract");
  }
  for (std::size_t i = 0; i < freight.size(); ++i) {
    auto &s = freight[i];
    if (s.id != Id(i) || !facilityValid(s.facility) || !itemValid(s.item) ||
        !accountValid(s.escrow) || (s.worker != none && !residentValid(s.worker)) ||
        s.quantity < 1 || s.quantity > 10000 || s.cost < 0 || s.fee < 0 || int(s.state) < 0 ||
        int(s.state) > int(FreightState::Delivered) || !std::isfinite(s.placed) ||
        !std::isfinite(s.readyAt) || s.readyAt < 0 || !std::isfinite(s.origin.x) ||
        !std::isfinite(s.origin.y) || !std::isfinite(s.origin.z) || !std::isfinite(s.landing.x) ||
        !std::isfinite(s.landing.y) || !std::isfinite(s.landing.z) ||
        accounts[s.escrow].balance !=
            (s.state == FreightState::Delivered ? 0 : s.fee + (s.waste ? s.cost : 0)) ||
        (s.source != none && !facilityValid(s.source)))
      throw std::runtime_error("Invalid supply shipment");
  }
  std::set<Id> bodies;
  for (auto &b : remains) {
    if (!residentValid(b.resident) || residents[b.resident].alive ||
        !bodies.insert(b.resident).second || (b.worker != none && !residentValid(b.worker)) ||
        (b.carrier != none && !residentValid(b.carrier)) ||
        (b.storage != none && !facilityValid(b.storage)) ||
        (b.destination != none && !facilityValid(b.destination)) ||
        (b.lift != none && (b.lift < 0 || std::size_t(b.lift) >= lifts.size())) ||
        (b.ambulance != none && !ambulanceValid(b.ambulance)) || !std::isfinite(b.pos.x) ||
        !std::isfinite(b.pos.y) || !std::isfinite(b.pos.z) || !std::isfinite(b.diedAt) ||
        !std::isfinite(b.burnUntil))
      throw std::runtime_error("Invalid remains");
  }
  for (std::size_t i = 0; i < ambulances.size(); ++i) {
    const auto &a = ambulances[i];
    if (a.id != Id(i) || !facilityValid(a.provider) ||
        facilities[a.provider].kind != FacilityKind::Infirmary ||
        (a.patient != none && !residentValid(a.patient)) ||
        (a.medic != none && !residentValid(a.medic)) ||
        (a.pilot != none && !residentValid(a.pilot)) ||
        (a.guard != none && !residentValid(a.guard)) || a.tier < 0 || a.tier > 2 ||
        int(a.state) < 0 || int(a.state) > int(RescueState::Returning) || !std::isfinite(a.pos.x) ||
        !std::isfinite(a.pos.y) || !std::isfinite(a.pos.z) || !std::isfinite(a.readyAt))
      throw std::runtime_error("Invalid ambulance");
    double prior = -1;
    for (auto &p : a.route) {
      if (!std::isfinite(p.time) || p.time < prior || !std::isfinite(p.p.x) ||
          !std::isfinite(p.p.y) || !std::isfinite(p.p.z))
        throw std::runtime_error("Invalid ambulance route");
      prior = p.time;
    }
  }
  for (std::size_t i = 0; i < vehicles.size(); ++i) {
    const auto &v = vehicles[i];
    if (v.id != Id(i) || !accountValid(v.owner) || int(v.kind) < 0 ||
        int(v.kind) > int(VehicleKind::DeliveryVan) || !std::isfinite(v.pos.x) ||
        !std::isfinite(v.pos.y) || !std::isfinite(v.pos.z) || !std::isfinite(v.speed) ||
        !std::isfinite(v.yaw) || !std::isfinite(v.condition) || v.condition < 0 ||
        v.condition > 1 || v.askingPrice < 0 || v.originalPrice < 0 ||
        (v.driver != none && (!residentValid(v.driver) || residents[v.driver].vehicle != v.id ||
                              residents[v.driver].account != v.owner)) ||
        (v.bay != none && (!bayValid(v.bay) || parking[v.bay].vehicle != v.id)) ||
        (v.dealer != none &&
         (!facilityValid(v.dealer) || facilities[v.dealer].kind != FacilityKind::Dealership)))
      throw std::runtime_error("Invalid vehicle");
  }
  for (std::size_t i = 0; i < parking.size(); ++i) {
    const auto &bay = parking[i];
    if (bay.id != Id(i) || !facilityValid(bay.facility) ||
        facilities[bay.facility].kind != FacilityKind::Parking || !std::isfinite(bay.paidUntil) ||
        !std::isfinite(bay.width) || bay.width <= 0 || bay.dailyPrice < 0 ||
        (bay.vehicle != none &&
         (!vehicleValid(bay.vehicle) || vehicles[bay.vehicle].bay != bay.id)))
      throw std::runtime_error("Invalid parking allocation");
  }
  for (const auto &c : incidents)
    if (!residentValid(c.offender) || !residentValid(c.victim) ||
        (c.warden != none && !residentValid(c.warden)))
      throw std::runtime_error("Invalid incident");
  for (const auto &p : projects) {
    if (!residentValid(p.owner) || !facilityValid(p.facility) || !accountValid(p.escrow) ||
        (p.worker != none && !residentValid(p.worker)))
      throw std::runtime_error("Invalid construction project");
    if (p.newConstruction &&
        (!facilityValid(p.source) || !accountValid(p.contractor) || p.materials < 1 ||
         p.materials > 64 || (p.materialItem != none && !itemValid(p.materialItem)) ||
         !std::isfinite(p.workAt) || p.workAt > time))
      throw std::runtime_error("Invalid new facility construction");
    for (Id id : p.affected)
      if (!residentValid(id))
        throw std::runtime_error("Invalid compensation recipient");
  }
  for (auto &o : orders)
    if (!residentValid(o.buyer) || o.shop < 0 || std::size_t(o.shop) >= facilities.size() ||
        o.item < 0 || std::size_t(o.item) >= items.size() || !accountValid(o.escrow) ||
        o.price < 0 || o.deliveryFee < 0)
      throw std::runtime_error("Invalid order");
}
} // namespace mega
