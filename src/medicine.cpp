#include "mega/world.hpp"
#include <algorithm>
namespace mega {
Vec3 World::ambulancePosition(Id id) const {
  const auto &a = ambulances.at(id);
  if (a.route.empty())
    return a.pos;
  if (time <= a.route.front().time)
    return a.route.front().p;
  for (std::size_t n = 1; n < a.route.size(); ++n)
    if (time < a.route[n].time) {
      auto &x = a.route[n - 1];
      auto &y = a.route[n];
      return x.p + (y.p - x.p) * float((time - x.time) / (y.time - x.time));
    }
  return a.route.back().p;
}
Vec3 World::medicalLanding(Id provider) const {
  auto &f = facilities.at(provider);
  return {layout(f.floor).halfWidth() - 2, f.floor * 3.f, f.pos.z};
}
static void fly(World &w, Ambulance &a, Vec3 destination) {
  Vec3 p = w.ambulancePosition(a.id);
  a.pos = p;
  a.route = {{p, w.time}};
  double at = w.time;
  float outside = 0;
  for (auto &f : w.floorPlans)
    outside = std::max(outside, float(f.width) / 2 + 8);
  auto point = [&](Vec3 q) {
    at += length(q - p) / (8 + a.tier * 3) * (1440 / w.config.dayMinutes);
    a.route.push_back({q, at});
    p = q;
  };
  point({outside, p.y, p.z});
  point({outside, destination.y, destination.z});
  point(destination);
}
void World::generateMedical() {
  for (auto &f : facilities)
    if (f.kind == FacilityKind::Infirmary && f.open &&
        std::none_of(ambulances.begin(), ambulances.end(),
                     [&](const Ambulance &a) { return a.provider == f.id; })) {
      Ambulance a;
      a.id = Id(ambulances.size());
      a.provider = f.id;
      a.pos = medicalLanding(f.id) + Vec3{7, 1, 0};
      ambulances.push_back(a);
      // Service crews are real residents, with three shifts and no phantom workers.
      for (Role role : {Role::Pilot, Role::Warden})
        for (int shift = 0; shift < 3; ++shift) {
          Id chosen = none;
          for (auto &r : residents)
            if (r.id > 1 && r.alive && r.present && r.age >= 18 && r.role == Role::Unemployed &&
                std::abs(r.home - f.floor) < 20) {
              chosen = r.id;
              break;
            }
          if (chosen == none)
            continue;
          auto &r = residents[chosen];
          r.role = role;
          r.jobSite = f.id;
          r.employer = f.owner;
          r.hourlyWage = role == Role::Pilot ? 1900 : 1600;
          r.routine.shiftStart = 6 + shift * 8;
          r.routine.shiftHours = 8;
          r.routine.daysOff = 0;
          r.routine.sleepStart = shift == 2 ? 8 : 22;
          f.employees.push_back(chosen);
          if (onShift(chosen)) {
            r.pos = accessible(f.pos + Vec3{role == Role::Pilot ? 1.3f : -1.3f, 0, -1.6f});
            clockIn(chosen);
          }
        }
    }
}
void World::subscribeMedical(Id id, Id provider, int tier) {
  auto &r = residents.at(id);
  auto &f = facilities.at(provider);
  if (!r.alive || !r.present || tier < 0 || tier > 2 || f.kind != FacilityKind::Infirmary ||
      !f.open || (!hasDevice(id) && !near(id, f.pos, 3)))
    throw std::runtime_error("A device or an infirmary visit is required for medical coverage");
  Money prices[] = {40000, 150000, 350000};
  pay(r.account, f.owner, prices[tier], "medical-subscription");
  r.medicalUntil =
      (r.medicalProvider == provider && r.medicalTier == tier ? std::max(time, r.medicalUntil)
                                                              : time) +
      30 * 86400;
  r.medicalProvider = provider;
  r.medicalTier = tier;
  log("medical.subscribed", id, provider);
}
void World::callAmbulance(Id id) {
  auto &r = residents.at(id);
  if (!r.alive || !r.present || r.vehicle != none || r.ladder >= 0 ||
      r.activity == Activity::Jailed || r.activity == Activity::Escorted || !hasDevice(id) ||
      r.medicalUntil <= time || r.medicalProvider == none || r.medicalTier < 0)
    throw std::runtime_error(
        "An active subscription and a device are required to call an ambulance");
  if (r.ambulance != none || r.carriedBy != none)
    throw std::runtime_error("Patient already in transit");
  for (auto &a : ambulances)
    if (a.patient == id && a.state != RescueState::Idle)
      throw std::runtime_error("Help is already dispatched");
  for (auto &a : ambulances)
    if (a.provider == r.medicalProvider && a.state == RescueState::Idle) {
      auto &f = facilities[a.provider];
      auto crew = [&](Role role) {
        for (Id worker : f.employees) {
          auto &s = residents[worker];
          if (s.alive && s.present && s.role == role && onShift(worker) && s.medicalDuty == none &&
              s.ambulance == none && s.activity != Activity::Fighting &&
              s.activity != Activity::Escorted && s.activity != Activity::Jailed &&
              near(worker, f.pos, 3))
            return worker;
        }
        return none;
      };
      a.medic = crew(Role::Medic);
      a.pilot = crew(Role::Pilot);
      a.guard = r.medicalTier == 2 ? crew(Role::Warden) : none;
      if (!f.open || a.medic == none || a.pilot == none || (r.medicalTier == 2 && a.guard == none))
        throw std::runtime_error("No complete on-duty medical crew is available");
      a.patient = id;
      a.tier = r.medicalTier;
      a.state = RescueState::Dispatch;
      a.readyAt = time + (a.tier == 0 ? 600 : a.tier == 1 ? 180 : 60);
      for (Id worker : {a.medic, a.pilot, a.guard})
        if (worker != none) {
          clockOut(worker);
          residents[worker].medicalDuty = a.id;
          travel(worker, medicalLanding(a.provider), TaskKind::None, none);
        }
      log("medical.dispatched", id, a.id);
      return;
    }
  throw std::runtime_error("All subscribed ambulances are busy");
}
void World::medicalTick() {
  for (auto &a : ambulances)
    if (a.state != RescueState::Idle) {
      auto alive = [&](Id id) {
        return id != none && residents[id].alive && residents[id].present;
      };
      if (a.state == RescueState::Returning) {
        if (!a.route.empty() && time >= a.route.back().time) {
          a.pos = a.route.back().p;
          a.route.clear();
          auto pad = medicalLanding(a.provider);
          int lane = 0;
          for (Id id : {a.medic, a.pilot, a.guard})
            if (id != none) {
              auto &r = residents[id];
              if (r.ambulance == a.id) {
                r.ambulance = none;
                r.pos = accessible(pad + Vec3{-float(lane++), 0, 1});
              }
              r.medicalDuty = none;
              if (alive(id)) {
                r.activity = Activity::Idle;
                try {
                  travel(id, facilities[a.provider].pos, TaskKind::Work, a.provider);
                } catch (const std::runtime_error &) {
                }
              }
            }
          if (a.patient != none) {
            auto &p = residents[a.patient];
            if (p.alive && p.ambulance == a.id) {
              p.ambulance = none;
              p.pos = accessible(pad + Vec3{0, 0, -2});
              p.activity = Activity::Idle;
            }
          }
          for (auto &body : remains)
            if (body.ambulance == a.id) {
              body.ambulance = none;
              body.pos = pad + Vec3{0, 0, 2};
            }
          a.patient = none;
          a.state = RescueState::Idle;
        }
        continue;
      }
      if (a.state == RescueState::Recalling) {
        bool ready = true;
        for (Id id : {a.medic, a.pilot, a.guard})
          if (alive(id) && residents[id].ambulance == none) {
            if (near(id, a.landing, 2)) {
              auto &r = residents[id];
              r.ambulance = a.id;
              r.route.clear();
              ++r.routeVersion;
              crowd.erase(id);
            } else {
              ready = false;
              if (residents[id].route.empty())
                try {
                  travel(id, a.landing, TaskKind::None, none);
                } catch (const std::runtime_error &) {
                }
            }
          }
        if (ready) {
          fly(*this, a, medicalLanding(a.provider) + Vec3{7, 1, 0});
          a.state = RescueState::Returning;
        }
        continue;
      }
      if (!alive(a.patient) || !alive(a.medic) || !alive(a.pilot)) {
        if (a.patient != none) {
          auto &p = residents[a.patient];
          if (p.alive && p.carriedBy != none) {
            p.pos = position(p.carriedBy) + Vec3{0, 0, .8f};
            p.carriedBy = none;
            p.activity = Activity::Idle;
          }
          // A living passenger stays aboard the returning aircraft, even when a crew member dies.
          if (p.ambulance == a.id) {
            p.pos = ambulancePosition(a.id);
          }
        }
        if (a.state == RescueState::FlyingHome || a.state == RescueState::FlyingOut) {
          fly(*this, a, medicalLanding(a.provider) + Vec3{7, 1, 0});
          a.state = RescueState::Returning;
        } else {
          if (a.state == RescueState::Dispatch || a.state == RescueState::ReachingClinic)
            a.landing = medicalLanding(a.provider);
          a.state = RescueState::Recalling;
          for (Id id : {a.medic, a.pilot, a.guard})
            if (alive(id) && residents[id].ambulance == none)
              try {
                travel(id, a.landing, TaskKind::None, none);
              } catch (const std::runtime_error &) {
              }
        }
        continue;
      }
      if (a.guard != none && !alive(a.guard)) {
        residents[a.guard].medicalDuty = none;
        a.guard = none;
      }
      auto &patient = residents[a.patient];
      auto &medic = residents[a.medic];
      if (a.state == RescueState::Dispatch && time >= a.readyAt) {
        bool ready = true;
        for (Id id : {a.medic, a.pilot, a.guard})
          if (id != none && !near(id, medicalLanding(a.provider), 2))
            ready = false;
        if (!ready)
          continue;
        for (Id id : {a.medic, a.pilot, a.guard})
          if (id != none) {
            auto &r = residents[id];
            r.route.clear();
            ++r.routeVersion;
            r.ambulance = a.id;
            r.activity = Activity::Working;
            crowd.erase(id);
          }
        Vec3 target = position(patient.id);
        int floor = std::clamp(int(std::floor(target.y / 3)), 0, config.floors - 1);
        a.landing = {
            layout(floor).halfWidth() - 2, floor * 3.f,
            std::clamp(target.z, -layout(floor).halfDepth() + 4, layout(floor).halfDepth() - 4)};
        fly(*this, a, a.landing + Vec3{7, 1, 0});
        a.state = RescueState::FlyingOut;
      } else if (a.state == RescueState::FlyingOut && !a.route.empty() &&
                 time >= a.route.back().time) {
        a.pos = a.route.back().p;
        a.route.clear();
        for (Id id : {a.medic, a.guard})
          if (id != none) {
            auto &r = residents[id];
            r.ambulance = none;
            r.activity = Activity::Idle;
            r.pos = a.landing + Vec3{id == a.guard ? 0.f : -.8f, 0, 0};
            travel(id, position(patient.id),
                   id == a.medic ? TaskKind::MedicalReach : TaskKind::None, a.id);
          }
        a.state = RescueState::ReachingPatient;
      } else if (a.state == RescueState::ReachingPatient) {
        if (a.guard != none) {
          Id offender = patient.opponent;
          if (offender != none && residents[offender].alive && !residents[offender].surrendered &&
              near(a.guard, position(offender),
                   residents[offender].lethalThreat && hasGun(a.guard) ? 30 : 2.5f) &&
              time >= residents[a.guard].nextHit) {
            try {
              if (residents[offender].lethalThreat && hasGun(a.guard))
                shoot(a.guard, offender);
              else
                attack(a.guard, offender, false);
            } catch (const std::runtime_error &) {
            }
          }
        }
        if (near(a.medic, position(patient.id), 1.8f)) {
          try {
            medicalReached(a.medic, a.id);
          } catch (const std::runtime_error &) {
          }
        } else if (medic.route.empty())
          try {
            travel(a.medic, position(patient.id), TaskKind::MedicalReach, a.id);
          } catch (const std::runtime_error &) {
          }
      } else if (a.state == RescueState::CarryingPatient && near(a.medic, a.landing, 1)) {
        try {
          medicalBoard(a.medic, a.id);
        } catch (const std::runtime_error &) {
        }
      } else if (a.state == RescueState::FlyingHome && !a.route.empty() &&
                 time >= a.route.back().time) {
        a.pos = a.route.back().p;
        a.route.clear();
        auto landing = medicalLanding(a.provider);
        for (Id id : {a.medic, a.pilot, a.guard})
          if (id != none) {
            auto &r = residents[id];
            r.ambulance = none;
            r.activity = Activity::Idle;
            r.pos = landing + Vec3{id == a.guard ? -.8f : id == a.pilot ? -1.6f : 0, 0, 0};
            travel(id, facilities[a.provider].pos + Vec3{0, 0, -1.6f},
                   id == a.medic ? TaskKind::MedicalClinic : TaskKind::Work,
                   id == a.medic ? a.id : a.provider);
          }
        patient.ambulance = none;
        patient.carriedBy = a.medic;
        a.state = RescueState::ReachingClinic;
      } else if (a.state == RescueState::ReachingClinic && medic.route.empty()) {
        try {
          if (near(a.medic, facilities[a.provider].pos, 2))
            medicalClinic(a.medic, a.id);
          else
            travel(a.medic, facilities[a.provider].pos + Vec3{0, 0, -1.6f}, TaskKind::MedicalClinic,
                   a.id);
        } catch (const std::runtime_error &) {
        }
      }
    }
  for (auto &body : remains)
    if (!body.cremated && !body.lost && body.storage != none) {
      auto &f = facilities[body.storage];
      if (f.kind == FacilityKind::Crematorium && operational(f.id)) {
        if (body.burnWorkedAt < 0)
          body.burnWorkedAt = time;
        body.burnRemaining = std::max(0.0, body.burnRemaining - (time - body.burnWorkedAt));
        body.burnWorkedAt = time;
        body.burnUntil = time + body.burnRemaining;
        if (body.burnRemaining == 0) {
          body.cremated = true;
          log("medical.cremated", body.resident);
        }
      } else
        body.burnWorkedAt = -1;
    }
}
void World::medicalReached(Id worker, Id id) {
  auto &a = ambulances.at(id);
  auto &p = residents.at(a.patient);
  if (a.medic != worker || a.state != RescueState::ReachingPatient || !p.alive ||
      !near(worker, position(p.id), 1.8f))
    throw std::runtime_error("Medic has not reached the patient");
  if (p.carriedBy != none || p.ambulance != none || p.lift != none || p.vehicle != none ||
      p.ladder >= 0 || looseBodies.contains(p.id) || looseBodies.contains(worker))
    throw std::runtime_error("Patient must reach a safe landing");
  if (p.activity == Activity::Fighting)
    throw std::runtime_error("Extraction awaits a safe scene");
  if (p.activity == Activity::Jailed || p.activity == Activity::Restrained ||
      p.activity == Activity::Escorted)
    throw std::runtime_error("Custody requires an authorized medical escort");
  Vec3 pos = position(p.id);
  clockOut(p.id);
  leaveQueue(p.id);
  p.pos = pos;
  p.route.clear();
  ++p.routeVersion;
  p.activity = Activity::Using;
  p.carriedBy = worker;
  p.health = std::min(100.f, p.health + 10);
  crowd.erase(p.id);
  travel(worker, a.landing, TaskKind::MedicalBoard, id);
  if (a.guard != none)
    travel(a.guard, a.landing + Vec3{0, 0, 1}, TaskKind::None, none);
  a.state = RescueState::CarryingPatient;
  log("medical.extracted", p.id, worker);
}
void World::medicalBoard(Id worker, Id id) {
  auto &a = ambulances.at(id);
  auto &p = residents.at(a.patient);
  if (a.medic != worker || a.state != RescueState::CarryingPatient || !near(worker, a.landing, 1) ||
      (a.guard != none && !near(a.guard, a.landing, 2)))
    throw std::runtime_error("Crew must reach the aircraft before departure");
  p.carriedBy = none;
  p.ambulance = id;
  for (Id who : {a.medic, a.guard})
    if (who != none) {
      residents[who].route.clear();
      ++residents[who].routeVersion;
      residents[who].ambulance = id;
      crowd.erase(who);
    }
  fly(*this, a, medicalLanding(a.provider) + Vec3{7, 1, 0});
  a.state = RescueState::FlyingHome;
}
void World::medicalClinic(Id worker, Id id) {
  auto &a = ambulances.at(id);
  auto &p = residents.at(a.patient);
  auto &f = facilities.at(a.provider);
  if (a.medic != worker || a.state != RescueState::ReachingClinic || !near(worker, f.pos, 2))
    throw std::runtime_error("Reach the infirmary with the patient");
  p.pos = accessible(f.pos + Vec3{0, 0, 1.6f});
  p.carriedBy = none;
  p.ambulance = none;
  p.activity = Activity::Idle;
  for (Id who : {a.medic, a.pilot, a.guard})
    if (who != none) {
      auto &r = residents[who];
      r.medicalDuty = none;
      Money wage = who == a.medic ? 4000 : who == a.guard ? 6000 : 2000;
      pay(f.owner, r.account, std::min(accounts[f.owner].balance, wage), "ambulance-crew-pay");
    }
  clockIn(worker);
  if (a.tier > 0 && accounts[p.account].balance < f.price)
    pay(f.owner, p.account, std::min(accounts[f.owner].balance, f.price),
        "insured-treatment-allowance");
  a.patient = none;
  a.state = RescueState::Idle;
  try {
    joinQueue(p.id, f.id);
  } catch (const std::runtime_error &) {
  }
  log("medical.arrived", p.id, f.id);
}
void World::transportBody(Id worker, Id id) {
  auto &r = residents.at(worker);
  auto &body = remains.at(id);
  if (!r.alive || !r.present || looseBodies.contains(worker) || r.role != Role::Cleaner ||
      !onShift(worker) || r.bodyCarrying != none || r.carrying != none || body.cremated ||
      body.lost || airborneRemains.contains(id) || body.carrier != none ||
      (body.worker != none && body.worker != worker) || body.ambulance != none || body.lift != none)
    throw std::runtime_error("Body transport worker unavailable");
  body.worker = worker;
  travel(worker, body.pos, TaskKind::BodyPickup, id);
}
void World::pickupBody(Id worker, Id id) {
  auto &r = residents.at(worker);
  auto &body = remains.at(id);
  if (looseBodies.contains(worker) || body.worker != worker || body.cremated || body.lost ||
      airborneRemains.contains(id) || body.carrier != none || !near(worker, body.pos) ||
      r.bodyCarrying != none)
    throw std::runtime_error("Reach the body with an available stretcher");
  Id destination =
      nearest(worker, body.storage == none ? FacilityKind::Morgue : FacilityKind::Crematorium);
  if (destination == none)
    throw std::runtime_error("No body storage or cremation service is available");
  int count = 0;
  for (auto &b : remains)
    if (!b.cremated && !b.lost && (b.storage == destination || b.destination == destination))
      ++count;
  if (count >= std::max(4, facilities[destination].stations))
    throw std::runtime_error("Body storage is full");
  body.carrier = worker;
  body.storage = none;
  body.destination = destination;
  r.bodyCarrying = id;
  travel(worker, facilities[destination].pos + Vec3{0, 0, 1.6f}, TaskKind::BodyDrop, destination);
}
void World::storeBody(Id worker, Id destination) {
  auto &r = residents.at(worker);
  if (r.bodyCarrying == none)
    throw std::runtime_error("No body on the stretcher");
  auto &body = remains.at(r.bodyCarrying);
  auto &f = facilities.at(destination);
  if (body.carrier != worker || !near(worker, f.pos) || !f.open ||
      (f.kind != FacilityKind::Morgue && f.kind != FacilityKind::Crematorium))
    throw std::runtime_error("Reach an open morgue or crematorium");
  Money price = f.kind == FacilityKind::Morgue ? 3000 : 12000;
  Id payer = accounts[residents[body.resident].account].balance >= price
                 ? residents[body.resident].account
                 : dormAccount;
  if (accounts[payer].balance < price)
    throw std::runtime_error("Neither estate nor dormitory can pay for body care");
  pay(payer, f.owner, price, "body-care");
  pay(f.owner, r.account, std::min(Money(1500), accounts[f.owner].balance), "body-transport-wage");
  int stored = 0;
  for (auto &b : remains)
    if (!b.cremated && !b.lost && (b.storage == destination || b.destination == destination))
      ++stored;
  body.pos = accessible(f.pos + Vec3{3.f + float(stored % 4) * .8f, 0, float(stored / 4) * 2.f});
  body.storage = destination;
  body.destination = none;
  body.carrier = none;
  body.worker = none;
  r.bodyCarrying = none;
  r.activity = Activity::Idle;
  log("medical.body_stored", body.resident, destination);
}
void World::healthTick() {
  std::map<int, int> infections;
  for (auto &r : residents)
    if (r.alive && r.present && r.infectedAt >= 0 && time - r.infectedAt > 6 * 3600)
      ++infections[int(position(r.id).y / 3)];
  for (auto &r : residents)
    if (r.alive && r.present) {
      if (r.infectedAt >= 0) {
        if (time - r.infectedAt > 4 * 86400) {
          r.infectedAt = -1;
          r.immuneUntil = time + 14 * 86400;
        } else if (time - r.infectedAt > 6 * 3600) {
          r.health -= 2 * (1.3f - r.hygiene);
          r.energy = std::max(0.f, r.energy - .025f);
        }
        if (r.health <= 0)
          kill(r.id);
      } else if (r.immuneUntil < time) {
        int ill = infections[int(position(r.id).y / 3)];
        float chance =
            (r.queuedAt != none ? .002f : .00015f) * std::min(ill, 20) * (1.3f - r.hygiene);
        if (time > 86400 && random() < chance + .000001f) {
          r.infectedAt = time;
          log("medical.infection", r.id);
        }
      }
      if (r.alive && r.automatic && r.health < 30 && r.medicalUntil > time && r.ambulance == none &&
          r.carriedBy == none) {
        try {
          callAmbulance(r.id);
        } catch (const std::runtime_error &) {
        }
      }
    }
}
void World::consumeItem(Id id, Id item) {
  auto &r = residents.at(id);
  auto &i = items.at(item);
  if (!r.alive || !r.present || i.owner != r.account || i.carrier != id || i.consumed ||
      i.reservedAt != none || itemInUse(item))
    throw std::runtime_error("Item is not available in your inventory");
  if (i.kind == ItemKind::Food) {
    ++r.personalWaste;
    r.hunger = std::max(0.f, r.hunger - .6f);
    r.energy = std::min(1.f, r.energy + .05f);
  } else if (i.kind == ItemKind::Medicine) {
    r.health = std::min(100.f, r.health + 15);
    if (r.infectedAt >= 0 && time - r.infectedAt > 3 * 86400) {
      r.infectedAt = -1;
      r.immuneUntil = time + 14 * 86400;
    }
  } else
    throw std::runtime_error("This item is not consumable");
  i.consumed = true;
  i.carrier = none;
  std::erase(r.items, item);
  log("inventory.consumed", id, item);
}
void World::sleepAtHome(Id id) {
  Id home = residentHomes.at(id);
  if (home == none || !housing[home].active || housing[home].tenant != id)
    throw std::runtime_error("No occupied bed is available");
  travel(id, homePosition(home), TaskKind::Home, id);
}
} // namespace mega
