#include "mega/world.hpp"
#include <algorithm>
#include <limits>
namespace mega {
namespace {
bool openJob(const VehicleJob &j) {
  return j.state != VehicleJobState::Complete && j.state != VehicleJobState::Cancelled;
}
bool ready(const Resident &r) {
  return r.alive && r.present && !r.surrendered && r.activity != Activity::Fighting &&
         r.activity != Activity::Jailed && r.activity != Activity::Restrained &&
         r.activity != Activity::Escorted && r.ambulance == none && r.carriedBy == none &&
         r.medicalDuty == none;
}
} // namespace
void World::stockFuel(Id actor, Id fid, float litres) {
  auto &f = facilities.at(fid);
  if (!ready(residents.at(actor)) || f.kind != FacilityKind::Workshop || !std::isfinite(litres) ||
      litres <= 0 || litres > 10000 || f.fuelStock + litres > 10000 ||
      (residents[actor].account != f.owner && f.manager != actor &&
       !directsCompany(actor, companyForAccount(operatingAccount(fid)), true)))
    throw std::runtime_error("traffic.error.fuel");
  pay(operatingAccount(fid), 0, Money(std::ceil(litres * 100)), "fuel-import");
  f.fuelStock += litres;
}
void World::refuelVehicle(Id actor, Id id, Id fid, float litres) {
  auto &v = vehicles.at(id);
  auto &f = facilities.at(fid);
  auto &r = residents.at(actor);
  float capacity = v.kind == VehicleKind::Scooter ? 8 : 40;
  bool employee = v.towTruck && r.employer == v.owner && r.role == Role::TowDriver;
  if (!ready(r) || (v.owner != r.account && !employee) || v.kind == VehicleKind::Bicycle ||
      v.airborne || v.towedBy != none || std::abs(v.speed) > .1f || !std::isfinite(litres) ||
      litres <= 0 || v.fuel + litres > capacity + .001f || f.fuelStock + .001f < litres ||
      !operational(fid) || f.kind != FacilityKind::Workshop || !accessAllowed(actor, fid) ||
      length(v.pos - f.pos) > 18 || !near(actor, v.pos, 3))
    throw std::runtime_error("traffic.error.fuel");
  pay(employee ? r.employer : r.account, operatingAccount(fid),
      Money(std::ceil(litres * f.fuelPrice)), "vehicle-fuel");
  f.fuelStock = std::max(0.f, f.fuelStock - litres);
  v.fuel = std::min(capacity, v.fuel + litres);
}
Id World::requestVehicleJob(Id customer, Id id, Id fid, bool tow) {
  const auto &r = residents.at(customer);
  const auto &v = vehicles.at(id);
  const auto &f = facilities.at(fid);
  bool authority = v.owner == r.account;
  if (!authority)
    for (const auto &business : facilities)
      if (operatingAccount(business.id) == v.owner &&
          (business.manager == customer || business.owner == r.account ||
           directsCompany(customer, companyForAccount(v.owner), true)))
        authority = true;
  if (!ready(r) || !authority || v.driver != none || v.airborne || v.freightLift != none ||
      v.towedBy != none || f.kind != FacilityKind::Workshop || !operational(fid) ||
      !accessAllowed(customer, fid) || (!hasDevice(customer) && !near(customer, f.pos, 3)) ||
      (!tow && length(v.pos - f.pos) > 18))
    throw std::runtime_error("traffic.error.service");
  for (const auto &j : vehicleJobs)
    if (j.vehicle == id && openJob(j))
      throw std::runtime_error("traffic.error.busy");
  VehicleJob j;
  j.id = Id(vehicleJobs.size());
  j.customer = customer;
  j.vehicle = id;
  j.workshop = fid;
  j.payer = v.owner;
  j.tow = tow;
  j.startingCondition = v.condition;
  j.budget = tow ? 5000 + Money(length(v.pos - f.pos) * 20)
                 : 10000 + Money(std::ceil((1 - v.condition) * 40000));
  if (accounts[j.payer].balance < j.budget)
    throw std::runtime_error("structure.error.funds");
  j.escrow = account("Vehicle service escrow");
  pay(j.payer, j.escrow, j.budget, "vehicle-service-funding");
  vehicleJobs.push_back(j);
  return j.id;
}
Id World::convertServiceTruck(Id actor, Id id, Id workshop) {
  const auto &v = vehicles.at(id);
  const auto &f = facilities.at(workshop);
  const auto &r = residents.at(actor);
  if (v.kind != VehicleKind::DeliveryVan || v.towTruck || v.owner != r.account ||
      (f.owner != r.account && f.manager != actor &&
       !directsCompany(actor, companyForAccount(operatingAccount(workshop)), true)))
    throw std::runtime_error("traffic.error.service");
  Money total = 260000 + Money(std::ceil((1 - v.condition) * 40000));
  if (accounts[v.owner].balance < total)
    throw std::runtime_error("structure.error.funds");
  Id job = requestVehicleJob(actor, id, workshop, false);
  auto &j = vehicleJobs[job];
  pay(j.payer, j.escrow, 250000, "tow-equipment-conversion");
  j.budget += 250000;
  j.retrofit = true;
  j.remaining = 3600;
  return job;
}
void World::assignVehicleJob(Id worker, Id id) {
  auto &j = vehicleJobs.at(id);
  auto &r = residents.at(worker);
  if (!openJob(j) || j.worker != none || !ready(r) || !onShift(worker) ||
      r.role != (j.tow ? Role::TowDriver : Role::Repairer) || r.jobSite != j.workshop ||
      builderCommitted(worker) || r.vehicle != none || r.carrying != none ||
      looseBodies.contains(worker))
    throw std::runtime_error("traffic.error.worker");
  for (const auto &other : vehicleJobs)
    if (other.worker == worker && openJob(other))
      throw std::runtime_error("traffic.error.worker");
  if (j.tow) {
    for (const auto &v : vehicles)
      if (v.towTruck && v.owner == operatingAccount(j.workshop) && v.driver == none &&
          v.towing == none && v.condition > .1f && v.fuel > 0 &&
          std::none_of(vehicleJobs.begin(), vehicleJobs.end(), [&](const VehicleJob &other) {
            return other.truck == v.id && openJob(other);
          })) {
        j.truck = v.id;
        break;
      }
    if (j.truck == none)
      throw std::runtime_error("traffic.error.worker");
  }
  j.worker = worker;
  j.state = VehicleJobState::ToVehicle;
  clockOut(worker);
}
void World::cancelVehicleJob(Id customer, Id id) {
  auto &j = vehicleJobs.at(id);
  if (j.customer != customer || !openJob(j) || j.state == VehicleJobState::Returning)
    throw std::runtime_error("traffic.error.service");
  if (j.truck != none && vehicles[j.truck].towing != none)
    throw std::runtime_error("traffic.error.busy");
  if (j.worker != none) {
    auto &r = residents[j.worker];
    if (r.vehicle != none) {
      vehicleJourneys.erase(r.vehicle);
      vehicles[r.vehicle].speed = 0;
      try {
        if (r.automatic && j.truck != none && r.vehicle == j.truck &&
            length(vehicles[j.truck].pos - vehicles[j.truck].garage) > 2.f)
          driveTo(r.id, vehicles[j.truck].garage, true);
        else
          exitVehicle(r.id);
      } catch (const std::runtime_error &) {
      }
    } else if (r.task == TaskKind::None &&
               (r.activity == Activity::Working || r.activity == Activity::Walking)) {
      r.pos = position(r.id);
      r.route.clear();
      ++r.routeVersion;
      r.activity = Activity::Idle;
    }
  }
  pay(j.escrow,
      (j.payer == residents[customer].account && !residents[customer].present) ? 0 : j.payer,
      accounts[j.escrow].balance, "vehicle-service-refund");
  j.state = VehicleJobState::Cancelled;
}
bool World::vehicleWorkDecision(Id worker) {
  for (auto &j : vehicleJobs)
    if (j.worker == worker && openJob(j)) {
      auto &r = residents[worker];
      if (!ready(r) || !onShift(worker))
        return false;
      Vec3 goal = j.tow ? vehicles[j.truck].pos
                        : vehicles[j.vehicle].pos + Vec3{vehicleWidth(j.vehicle) / 2 + 1, 0, 0};
      if (r.vehicle != none)
        return true;
      if (!near(worker, goal, 1.6f)) {
        if (r.route.empty())
          try {
            travel(worker, goal, TaskKind::None, none);
          } catch (const std::runtime_error &) {
          }
        return true;
      }
      if (j.tow) {
        try {
          enterVehicle(worker, j.truck);
        } catch (const std::runtime_error &) {
        }
        return true;
      }
      r.activity = Activity::Working;
      j.state = VehicleJobState::Working;
      return true;
    }
  return false;
}
void World::vehicleServicesTick() {
  for (const auto &j : vehicleJobs)
    if (j.tow && (j.state == VehicleJobState::Complete || j.state == VehicleJobState::Cancelled) &&
        j.worker != none && j.truck != none && residents[j.worker].vehicle == j.truck &&
        !vehicleJourneys.contains(j.truck) && std::abs(vehicles[j.truck].speed) < .1f)
      try {
        exitVehicle(j.worker);
      } catch (const std::runtime_error &) {
      }
  for (auto &j : vehicleJobs)
    if (openJob(j)) {
      auto &target = vehicles[j.vehicle];
      if (!residents[j.customer].alive || !residents[j.customer].present) {
        if (j.state != VehicleJobState::Returning) {
          cancelVehicleJob(j.customer, j.id);
          continue;
        }
      }
      if (j.worker != none &&
          (!ready(residents[j.worker]) ||
           residents[j.worker].role != (j.tow ? Role::TowDriver : Role::Repairer))) {
        if (j.truck != none) {
          auto &truck = vehicles[j.truck];
          vehicleJourneys.erase(truck.id);
          truck.speed = 0;
          if (truck.towing != none) {
            vehicles[truck.towing].towedBy = none;
            truck.towing = none;
          }
        }
        j.worker = j.truck = none;
        j.state = VehicleJobState::Waiting;
        j.workedAt = -1;
      }
      if (j.worker == none) {
        for (Id worker : facilities[j.workshop].employees) {
          const auto &r = residents[worker];
          if (r.automatic && onShift(worker) && ready(r) &&
              r.role == (j.tow ? Role::TowDriver : Role::Repairer) && r.route.empty() &&
              r.queuedAt == none)
            try {
              assignVehicleJob(worker, j.id);
              break;
            } catch (const std::runtime_error &) {
            }
        }
        continue;
      }
      auto &r = residents[j.worker];
      if (!(j.tow && j.state == VehicleJobState::Returning) &&
          (!onShift(r.id) || !operational(j.workshop))) {
        j.workedAt = -1;
        continue;
      }
      if (j.tow) {
        auto &truck = vehicles[j.truck];
        if (r.vehicle != truck.id) {
          j.workedAt = -1;
          continue;
        }
        if (j.workedAt >= 0) {
          double dt = std::clamp(time - j.workedAt, 0., 60.);
          Money due = Money(std::ceil(dt / 3600 * r.hourlyWage)),
                paid = std::min(due, accounts[j.escrow].balance);
          pay(j.escrow, r.account, paid, "tow-driver-wages");
          if (paid < due)
            r.routine.wageClaims[operatingAccount(j.workshop)] += due - paid;
        }
        j.workedAt = time;
        if (j.state == VehicleJobState::ToVehicle) {
          if (vehicleJourneys.contains(truck.id))
            continue;
          if (length(truck.pos - target.pos) > 9) {
            try {
              driveTo(r.id, target.pos + Vec3{0, 0, 7}, false);
            } catch (const std::runtime_error &) {
            }
            continue;
          }
          if (target.driver != none || target.airborne || target.freightLift != none ||
              target.towedBy != none || std::abs(truck.speed) > .1f)
            continue;
          // Hitching changes references only. Both vehicles retain their actual positions.
          truck.towing = target.id;
          target.towedBy = truck.id;
          j.state = VehicleJobState::Returning;
        }
        if (j.state == VehicleJobState::Returning) {
          Vec3 bay = truck.garage;
          if (length(truck.pos - bay) > 2) {
            if (!vehicleJourneys.contains(truck.id))
              try {
                driveTo(r.id, bay, false);
              } catch (const std::runtime_error &) {
              }
            continue;
          }
          if (std::abs(truck.speed) > .1f)
            continue;
          truck.towing = none;
          target.towedBy = none;
          target.speed = 0;
          pay(j.escrow, operatingAccount(j.workshop), accounts[j.escrow].balance,
              "completed-physical-tow");
          j.state = VehicleJobState::Complete;
          try {
            exitVehicle(r.id);
          } catch (const std::runtime_error &) {
          }
        }
        continue;
      }
      if (r.activity != Activity::Working || !near(r.id, target.pos, 3) || target.driver != none ||
          target.airborne) {
        j.workedAt = -1;
        continue;
      }
      if (!j.parts) {
        auto &shop = facilities[j.workshop];
        int parts = j.retrofit ? 4 : 1;
        Money cost = parts * 8000;
        if (shop.stock < parts || accounts[j.escrow].balance < cost)
          continue;
        pay(j.escrow, operatingAccount(shop.id), cost, "vehicle-repair-parts");
        shop.stock -= parts;
        j.parts = true;
        j.workedAt = time;
        continue;
      }
      if (j.workedAt < 0) {
        j.workedAt = time;
        continue;
      }
      double dt = std::min({60., std::max(0., time - j.workedAt), j.remaining});
      j.workedAt = time;
      Money wage = Money(std::ceil(dt / 3600 * r.hourlyWage));
      if (accounts[j.escrow].balance < wage)
        continue;
      pay(j.escrow, r.account, wage, "vehicle-mechanic-wages");
      j.remaining -= dt;
      if (j.remaining <= 0) {
        target.condition = 1;
        if (j.retrofit) {
          target.towTruck = true;
          target.owner = operatingAccount(j.workshop);
          target.garage = target.pos;
        }
        pay(j.escrow, operatingAccount(j.workshop), accounts[j.escrow].balance,
            "completed-vehicle-repair");
        j.state = VehicleJobState::Complete;
        r.activity = Activity::Idle;
      }
    }
}
bool World::chooseVehicleTrip(Id id) {
  auto &r = residents.at(id);
  if (!r.automatic || !ready(r) || r.role == Role::TowDriver || r.vehicle != none ||
      r.queuedAt != none || r.carrying != none || r.hunger > .7f || r.thirst > .6f ||
      r.energy < .4f)
    return false;
  Vec3 goal{};
  bool trip = false;
  if (onShift(id) && r.jobSite != none) {
    goal = facilities[r.jobSite].pos + Vec3{8, 0, 0};
    trip = true;
  }
  if (!onShift(id) && sleepTime(id) && residentHomes[id] != none) {
    goal = homePosition(residentHomes[id]) + Vec3{2, 0, 0};
    trip = true;
  }
  if (!trip || length(position(id) - goal) < 80)
    return false;
  for (const auto &v : vehicles)
    if (v.owner == r.account && !v.towTruck && v.driver == none && v.dealer == none &&
        v.condition > .3f && !v.airborne && v.towedBy == none &&
        (v.kind == VehicleKind::Bicycle || v.fuel > 2) && near(id, v.pos, 30)) {
      Id bay = none;
      float best = 120;
      for (const auto &candidate : parking) {
        bool ours = candidate.vehicle == v.id && v.bay == candidate.id;
        if ((candidate.vehicle != none && !ours) || std::abs(candidate.pos.y - goal.y) > .1f ||
            !operational(candidate.facility) || !accessAllowed(id, candidate.facility) ||
            vehicleWidth(v.id) > candidate.width)
          continue;
        float d = length(candidate.pos - goal);
        if (d < best &&
            (ours || accounts[r.account].balance >= householdReserve(id) + candidate.dailyPrice)) {
          best = d;
          bay = candidate.id;
        }
      }
      if (bay == none)
        continue;
      try {
        planVehicleRoute(id, v.id, parking[bay].pos);
      } catch (const std::runtime_error &) {
        continue;
      }
      if (v.bay == none) {
        if (!hasDevice(id))
          continue;
        try {
          reserveParking(id, v.id, bay);
        } catch (const std::runtime_error &) {
          continue;
        }
      }
      if (!near(id, v.pos, 2.5f)) {
        travel(id, v.pos + Vec3{vehicleWidth(v.id) / 2 + .6f, 0, 0}, TaskKind::None, none);
        return true;
      }
      goal = parking[bay].pos;
      enterVehicle(id, v.id);
      try {
        driveTo(id, goal, true);
        vehicleJourneys[v.id].parkingDestination = bay;
      } catch (const std::runtime_error &) {
        exitVehicle(id);
        return false;
      }
      return true;
    }
  return false;
}
void World::validateVehicles() const {
  auto ref = [](Id id, std::size_t n) { return id >= 0 && std::size_t(id) < n; };
  for (const auto &v : vehicles) {
    if (!std::isfinite(v.garage.x) || !std::isfinite(v.garage.y) || !std::isfinite(v.garage.z) ||
        !std::isfinite(v.fuel) || v.fuel < 0 ||
        v.fuel > (v.kind == VehicleKind::Bicycle   ? 0.001f
                  : v.kind == VehicleKind::Scooter ? 8.001f
                                                   : 40.001f) ||
        !std::isfinite(v.verticalSpeed) || std::abs(v.verticalSpeed) > 1000 ||
        !std::isfinite(v.pitch) || !std::isfinite(v.updatedAt) || !std::isfinite(v.nextImpact) ||
        (v.road != none && !ref(v.road, roads.size())) ||
        (v.freightLift != none &&
         (!ref(v.freightLift, roads.size()) || roads[v.freightLift].occupant != v.id)) ||
        (v.towing != none && (!ref(v.towing, vehicles.size()) ||
                              vehicles[v.towing].towedBy != v.id || v.towing == v.id)) ||
        (v.towedBy != none && (!ref(v.towedBy, vehicles.size()) ||
                               vehicles[v.towedBy].towing != v.id || v.towedBy == v.id)))
      throw std::runtime_error("Invalid vehicle physics");
  }
  std::set<Id> providers;
  for (std::size_t i = 0; i < roads.size(); ++i) {
    const auto &r = roads[i];
    if (r.id != Id(i) || !ref(r.facility, facilities.size()) ||
        !providers.insert(r.facility).second ||
        facilities[r.facility].kind !=
            (r.kind == RoadKind::Ramp ? FacilityKind::Ramp : FacilityKind::FreightLift) ||
        r.from < 0 || r.to <= r.from || r.to >= config.floors || int(r.kind) < 0 ||
        int(r.kind) > 1 || (r.kind == RoadKind::Ramp && (r.to != r.from + 1 || r.length < 24)) ||
        r.pos.y != r.from * 3.f || !std::isfinite(r.pos.x) || !std::isfinite(r.pos.y) ||
        !std::isfinite(r.pos.z) || r.width < 3 || r.width > 8 || r.length < 14 || r.length > 64 ||
        !std::isfinite(r.width) || !std::isfinite(r.length) || r.loadLimit < 100 ||
        !std::isfinite(r.loadLimit) || !std::isfinite(r.height) || !std::isfinite(r.startHeight) ||
        !std::isfinite(r.targetHeight) || !std::isfinite(r.departAt) ||
        !std::isfinite(r.arriveAt) || !std::isfinite(r.doorsUntil) ||
        (r.occupant != none &&
         (!ref(r.occupant, vehicles.size()) || vehicles[r.occupant].freightLift != r.id)) ||
        r.calls.size() > 32)
      throw std::runtime_error("Invalid road link");
    std::set<Id> waiting;
    for (auto c : r.calls)
      if (!ref(c.vehicle, vehicles.size()) || !ref(c.driver, residents.size()) ||
          !waiting.insert(c.vehicle).second || !std::isfinite(c.called) || c.called > time ||
          (c.from != r.from && c.from != r.to) || (c.to != r.from && c.to != r.to) ||
          c.from == c.to)
        throw std::runtime_error("Invalid freight-lift queue");
  }
  for (auto [id, j] : vehicleJourneys) {
    if (!ref(id, vehicles.size()) || id != j.vehicle ||
        (j.parkingDestination != none && !ref(j.parkingDestination, parking.size())) ||
        !ref(j.driver, residents.size()) || j.path.size() > 10000 || j.next > j.path.size() ||
        !std::isfinite(j.blockedAt) || !std::isfinite(j.goal.x) || !std::isfinite(j.goal.y) ||
        !std::isfinite(j.goal.z))
      throw std::runtime_error("Invalid vehicle journey");
    for (auto p : j.path)
      if (p.destination < -1 ||
          (p.destination >= 0 &&
           (p.link == none || !ref(p.link, roads.size()) ||
            roads[p.link].kind != RoadKind::FreightLift ||
            (p.destination != roads[p.link].from && p.destination != roads[p.link].to))) ||
          !std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) || !std::isfinite(p.pos.z) ||
          (p.link != none && !ref(p.link, roads.size())))
        throw std::runtime_error("Invalid vehicle waypoint");
  }
  for (std::size_t i = 0; i < vehicleJobs.size(); ++i) {
    const auto &j = vehicleJobs[i];
    if (j.id != Id(i) || !ref(j.customer, residents.size()) || !ref(j.vehicle, vehicles.size()) ||
        !ref(j.workshop, facilities.size()) ||
        facilities[j.workshop].kind != FacilityKind::Workshop || !ref(j.escrow, accounts.size()) ||
        !ref(j.payer, accounts.size()) || (j.worker != none && !ref(j.worker, residents.size())) ||
        (j.truck != none && !ref(j.truck, vehicles.size())) || int(j.state) < 0 ||
        int(j.state) > int(VehicleJobState::Cancelled) || j.payer == 0 || j.budget < 0 ||
        !std::isfinite(j.remaining) || j.remaining < 0 ||
        j.remaining > (j.retrofit ? 3600 : 1800) || !std::isfinite(j.workedAt) ||
        !std::isfinite(j.startingCondition) || j.startingCondition < 0 || j.startingCondition > 1 ||
        ((j.state == VehicleJobState::Complete || j.state == VehicleJobState::Cancelled) &&
         accounts[j.escrow].balance != 0))
      throw std::runtime_error("Invalid vehicle service job");
  }
  for (const auto &f : facilities)
    if (!std::isfinite(f.fuelStock) || f.fuelStock < 0 || f.fuelStock > 10000 || f.fuelPrice < 0 ||
        f.fuelPrice > 1000000)
      throw std::runtime_error("Invalid workshop fuel");
  if (!std::isfinite(nextVehicleAt) || nextVehicleAt < -1)
    throw std::runtime_error("Invalid vehicle clock");
}
} // namespace mega
