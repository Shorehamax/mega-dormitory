#include "mega/world.hpp"
#include <algorithm>
#include <cmath>

namespace mega {
namespace {
float bodyWidth(VehicleKind kind) {
  return kind <= VehicleKind::Scooter ? .65f : kind == VehicleKind::CompactCar ? 1.7f : 2.05f;
}
bool available(const Resident &r) {
  return r.alive && r.present && !r.surrendered && r.activity != Activity::Jailed &&
         r.activity != Activity::Restrained && r.activity != Activity::Escorted &&
         r.activity != Activity::Fighting;
}
Money overdue(const ParkingBay &bay, double time) {
  return time <= bay.paidUntil ? 0
                               : Money(std::ceil((time - bay.paidUntil) / 86400)) * bay.dailyPrice;
}
} // namespace

std::string vehicleKey(VehicleKind kind) {
  static const char *names[] = {"vehicle.bicycle", "vehicle.scooter", "vehicle.car", "vehicle.van"};
  return names[int(kind)];
}

void World::generateVehicles() {
  // A real ground-floor garage. Upper-floor transport needs ramps/freight lifts
  // before motor vehicles may reach those levels.
  Facility dealer;
  dealer.id = Id(facilities.size());
  dealer.kind = FacilityKind::Dealership;
  dealer.owner = account("Vehicle dealership", 12000000);
  dealer.manager = 1;
  dealer.pos = {-3, 0, -14};
  dealer.askingPrice = 25000000;
  if (config.layoutVersion > 1)
    dealer.pos = {-30, 0, layout(0).halfDepth() - 14};
  facilities.push_back(dealer);
  floorFacilities[0].push_back(dealer.id);
  Facility garage;
  garage.id = Id(facilities.size());
  garage.kind = FacilityKind::Parking;
  garage.owner = account("Parking operator", 1000000);
  garage.manager = 1;
  garage.pos = {2, 0, 11};
  garage.askingPrice = 15000000;
  if (config.layoutVersion > 1)
    garage.pos = {30, 0, layout(0).halfDepth() - 14};
  facilities.push_back(garage);
  floorFacilities[0].push_back(garage.id);
  constexpr Money prices[] = {180000, 650000, 3200000, 5500000};
  for (int i = 0; i < 4; ++i) {
    Vehicle v;
    v.id = Id(vehicles.size());
    v.owner = dealer.owner;
    v.dealer = dealer.id;
    v.kind = VehicleKind(i);
    v.fuel = v.kind == VehicleKind::Bicycle ? 0 : v.kind == VehicleKind::Scooter ? 8 : 40;
    v.look = {i % 8, (i + 3) % 8, 0, 0};
    v.pos = {i % 2 == 0 ? -1.8f : 1.8f, 0, -10.f + (i / 2) * 4.8f};
    if (config.layoutVersion > 1)
      v.pos = Vec3{i % 2 == 0 ? -34.f : -26.f, 0, -10.f + (i / 2) * 4.8f} +
              Vec3{0, 0, layout(0).halfDepth() - 10};
    v.askingPrice = v.originalPrice = prices[i];
    vehicles.push_back(v);
  }
  // Three of four legal bays are already occupied. Existing assets do not
  // introduce spendable money into the ledger.
  for (int i = 0; i < 4; ++i) {
    ParkingBay bay;
    bay.id = Id(parking.size());
    bay.facility = garage.id;
    bay.pos = {i % 2 == 0 ? -1.8f : 1.8f, 0, 5.f + (i / 2) * 5.f};
    if (config.layoutVersion > 1)
      bay.pos = bay.pos + Vec3{30, 0, layout(0).halfDepth() - 24};
    bay.dailyPrice = 1800 + i * 300;
    if (i < 3) {
      Vehicle v;
      v.id = Id(vehicles.size());
      v.owner = residents[1 + i % (residents.size() - 1)].account;
      v.kind = i == 0 ? VehicleKind::Bicycle : VehicleKind::CompactCar;
      v.fuel = v.kind == VehicleKind::Bicycle ? 0 : 40;
      v.pos = bay.pos;
      v.bay = bay.id;
      v.originalPrice = prices[int(v.kind)];
      bay.vehicle = v.id;
      bay.paidUntil = time + 86400;
      vehicles.push_back(v);
    }
    parking.push_back(bay);
  }
}

void World::generateParkingBays(Id fid) {
  auto &f = facilities.at(fid);
  if (f.kind != FacilityKind::Parking || !f.open || f.demolished ||
      std::any_of(parking.begin(), parking.end(),
                  [&](const ParkingBay &b) { return b.facility == fid; }))
    return;
  for (int n = 0; n < std::min(2, f.stations); ++n) {
    ParkingBay b;
    b.id = Id(parking.size());
    b.facility = fid;
    b.pos = f.pos + Vec3{n % 2 ? 2.2f : -2.2f, 0, 5};
    b.dailyPrice = f.price;
    parking.push_back(b);
  }
}
void World::buyVehicle(Id resident, Id id) {
  auto &r = residents.at(resident);
  auto &v = vehicles.at(id);
  if (looseBodies.contains(resident))
    throw std::runtime_error("physics.error.airborne");
  if (!available(r) || v.askingPrice <= 0 || v.driver != none || v.bay != none ||
      v.owner == r.account || v.dealer == none || !operational(v.dealer) ||
      !accessAllowed(resident, v.dealer) || !near(resident, facilities[v.dealer].pos, 3))
    throw std::runtime_error("Reach the dealership to purchase an available vehicle");
  pay(r.account, v.owner, v.askingPrice, "vehicle-purchase");
  v.owner = r.account;
  v.askingPrice = 0;
  v.dealer = none;
  log("vehicle.purchased", resident, id);
}

void World::sellVehicle(Id resident, Id id, Id fid) {
  auto &r = residents.at(resident);
  auto &v = vehicles.at(id);
  auto &f = facilities.at(fid);
  if (std::any_of(vehicleJobs.begin(), vehicleJobs.end(), [&](const VehicleJob &j) {
        return j.vehicle == id && j.state != VehicleJobState::Complete &&
               j.state != VehicleJobState::Cancelled;
      }))
    throw std::runtime_error("traffic.error.busy");
  if (!available(r) || v.owner != r.account || v.driver != none || v.bay != none ||
      v.askingPrice != 0 || f.kind != FacilityKind::Dealership || !operational(fid) ||
      !accessAllowed(resident, fid) || !near(resident, f.pos, 3) || length(v.pos - f.pos) > 6)
    throw std::runtime_error("Bring your unoccupied vehicle to the dealership");
  Money offer = Money(v.originalPrice * .6 * v.condition);
  pay(f.owner, r.account, offer, "vehicle-trade-in");
  v.owner = f.owner;
  v.dealer = fid;
  v.askingPrice = Money(v.originalPrice * .8 * v.condition);
  log("vehicle.sold", resident, id);
}

void World::reserveParking(Id resident, Id id, Id bid) {
  auto &r = residents.at(resident);
  auto &v = vehicles.at(id);
  auto &bay = parking.at(bid);
  auto &f = facilities.at(bay.facility);
  if (!available(r) || v.owner != r.account || v.bay != none || bay.vehicle != none || !f.open ||
      bodyWidth(v.kind) > bay.width || (!hasDevice(resident) && !near(resident, f.pos, 3)))
    throw std::runtime_error("No suitable available parking space, or no "
                             "access to its booking desk");
  pay(r.account, f.owner, bay.dailyPrice, "parking-day-lease");
  bay.vehicle = id;
  bay.paidUntil = time + 86400;
  v.bay = bid;
  log("parking.reserved", resident, bid);
}

void World::releaseParking(Id resident, Id id) {
  auto &r = residents.at(resident);
  auto &v = vehicles.at(id);
  if (!available(r) || v.owner != r.account || v.bay == none ||
      length(v.pos - parking[v.bay].pos) < 2.5f)
    throw std::runtime_error("Move the vehicle out of the space before releasing it");
  auto &bay = parking[v.bay];
  pay(r.account, facilities[bay.facility].owner, overdue(bay, time), "parking-overstay");
  bay.vehicle = none;
  bay.paidUntil = 0;
  v.bay = none;
}

void World::enterVehicle(Id resident, Id id) {
  auto &r = residents.at(resident);
  auto &v = vehicles.at(id);
  if (std::any_of(vehicleJobs.begin(), vehicleJobs.end(), [&](const VehicleJob &j) {
        return j.vehicle == id && j.state != VehicleJobState::Complete &&
               j.state != VehicleJobState::Cancelled;
      }))
    throw std::runtime_error("traffic.error.busy");
  if (!available(r) || r.vehicle != none ||
      (v.owner != r.account &&
       !(v.towTruck && r.role == Role::TowDriver && r.employer == v.owner)) ||
      v.driver != none || v.towedBy != none || v.airborne || looseBodies.contains(resident) ||
      r.lift != none || r.ambulance != none || r.carriedBy != none || v.askingPrice != 0 ||
      !near(resident, v.pos, 2.5f))
    throw std::runtime_error("Reach your vehicle before entering it");
  stopPersonalUse(resident);
  clockOut(resident);
  leaveQueue(resident);
  crowd.erase(resident);
  r.route.clear();
  ++r.routeVersion;
  r.task = TaskKind::None;
  r.activity = Activity::Idle;
  r.vehicle = id;
  v.driver = resident;
  v.speed = 0;
  v.updatedAt = time;
  r.pos = v.pos;
}

void World::exitVehicle(Id resident) {
  auto &r = residents.at(resident);
  if (r.vehicle == none)
    throw std::runtime_error("Not in a vehicle");
  auto &v = vehicles.at(r.vehicle);
  if (std::abs(v.speed) > .2f || v.airborne || v.freightLift != none ||
      std::abs(v.pos.y - std::round(v.pos.y / 3) * 3) > .15f)
    throw std::runtime_error("Stop before leaving the vehicle");
  auto safe = [&](Vec3 p) {
    if (!clearPosition(p) || length(moveCapsule(resident, v.pos, p - v.pos, true) - p) > .05f)
      return false;
    for (const auto &other : residents)
      if (other.id != resident && other.alive && other.present && other.vehicle == none &&
          std::abs(position(other.id).y - p.y) < 1.7f &&
          std::hypot(position(other.id).x - p.x, position(other.id).z - p.z) < .58f)
        return false;
    return true;
  };
  Vec3 exit = v.pos + Vec3{std::cos(v.yaw), 0, std::sin(v.yaw)} * (bodyWidth(v.kind) / 2 + .5f);
  if (!safe(exit)) {
    exit = v.pos - Vec3{std::cos(v.yaw), 0, std::sin(v.yaw)} * (bodyWidth(v.kind) / 2 + .5f);
    if (!safe(exit))
      throw std::runtime_error("traffic.error.exit");
  }
  r.pos = exit;

  v.driver = none;
  r.vehicle = none;
}

void World::parkVehicle(Id resident, Id id, Id bid) {
  auto &r = residents.at(resident);
  auto &v = vehicles.at(id);
  auto &bay = parking.at(bid);
  if (!available(r) || v.owner != r.account || v.bay != bid || bay.vehicle != id ||
      v.driver != resident || r.vehicle != id || length(v.pos - bay.pos) > 1.3f ||
      std::abs(v.speed) > .2f || !facilities[bay.facility].open)
    throw std::runtime_error("Drive into your reserved bay and stop before parking");
  Money charge = overdue(bay, time);
  pay(r.account, facilities[bay.facility].owner, charge, "parking-overstay");
  if (charge > 0)
    bay.paidUntil = time + 86400;
  // The vehicle stays where it was physically stopped; no remote relocation.
  exitVehicle(resident);
  log("parking.parked", resident, bid);
}

void World::driveVehicle(Id resident, float throttle, float steering, float seconds) {
  auto &r = residents.at(resident);
  if (!available(r) || r.vehicle == none || !std::isfinite(seconds) || seconds <= 0 ||
      seconds > .1f || !std::isfinite(throttle) || !std::isfinite(steering))
    throw std::runtime_error("traffic.error.driver");
  auto &v = vehicles.at(r.vehicle);
  if (v.driver != resident || v.towedBy != none)
    throw std::runtime_error("traffic.error.driver");
  if (v.freightLift != none && roads[v.freightLift].moving) {
    v.pos.y = freightHeight(v.freightLift);
    r.pos = v.pos;
    v.updatedAt = time;
    return;
  }
  throttle = std::clamp(throttle, -1.f, 1.f);
  steering = std::clamp(steering, -1.f, 1.f);
  int steps = std::max(1, int(std::ceil(seconds / .01f)));
  float dt = seconds / steps;
  for (int step = 0; step < steps; ++step) {
    bool motor = v.kind != VehicleKind::Bicycle;
    if (v.condition < .08f || (motor && v.fuel <= 0))
      throttle = 0;
    float limit = (v.kind <= VehicleKind::Scooter ? 5.f : 7.f) * std::clamp(v.condition, .25f, 1.f);
    v.speed = std::clamp(v.speed + throttle * dt * 3.f, -2.5f, limit);
    if (std::abs(throttle) < .01f)
      v.speed *= std::exp(-dt * 4.f);
    float yaw = v.yaw + steering * dt * std::clamp(v.speed, -1.5f, 1.5f);
    Vec3 next = v.pos + Vec3{std::sin(yaw), 0, -std::cos(yaw)} * (v.speed * dt);
    float support = supportHeight(next, v.pos.y + .35f);
    bool landing = false;
    if (support > -9000 && (v.airborne ? next.y + v.verticalSpeed * dt <= support
                                       : std::abs(support - v.pos.y) < .4f)) {
      if (v.airborne && v.verticalSpeed < -7) {
        v.condition = std::max(0.f, v.condition - v.verticalSpeed * v.verticalSpeed * .003f);
        applyFall(resident, -v.verticalSpeed);
      }
      next.y = support;
      v.verticalSpeed = 0;
      v.airborne = false;
      landing = true;
    } else {
      v.airborne = true;
      v.verticalSpeed -= 9.81f * dt;
      next.y += v.verticalSpeed * dt;
    }
    bool allowed = true;
    Id road = none;
    for (const auto &link : roads)
      if (link.active && link.kind == RoadKind::Ramp &&
          std::abs(next.x - link.pos.x) < link.width / 2 && next.z >= link.pos.z &&
          next.z <= link.pos.z + link.length &&
          std::abs(next.y - (link.from * 3.f + (next.z - link.pos.z) / link.length * 3)) < .4f) {
        road = link.id;
        if (v.road != link.id) {
          auto &f = facilities[link.facility];
          if (!operational(f.id) || !accessAllowed(resident, f.id))
            allowed = false;
        }
      }
    for (const auto &link : roads)
      if (link.active && link.kind == RoadKind::FreightLift &&
          std::abs(next.x - link.pos.x) < link.width / 2 &&
          std::abs(next.z - link.pos.z) < link.length / 2 && v.freightLift != link.id)
        if (link.moving || std::abs(freightHeight(link.id) - next.y) > .15f ||
            (!link.calls.empty() && link.calls.front().vehicle != v.id) ||
            std::none_of(link.calls.begin(), link.calls.end(),
                         [&](auto c) { return c.vehicle == v.id; }))
          allowed = false;
    bool fits = allowed && vehicleFits(v.id, next, yaw, true);
    Vec3 trailer{};
    float trailerYaw = 0;
    if (fits && v.towing != none) {
      auto &t = vehicles[v.towing];
      Vec3 rear = next - Vec3{std::sin(yaw), 0, -std::cos(yaw)} *
                             (vehicleLength(v.id) / 2 + vehicleLength(t.id) / 2 + .7f);
      Vec3 d = rear - t.pos;
      d.y = 0;
      trailer = t.pos + normalized(d) * std::min(length(d), std::abs(v.speed) * dt * 1.2f);
      trailer.y = supportHeight(trailer, t.pos.y + .35f);
      trailerYaw =
          length(d) > .001f
              ? t.yaw +
                    std::clamp(std::remainder(std::atan2(d.x, -d.z) - t.yaw, 6.2831853f), -dt, dt)
              : t.yaw;
      fits = trailer.y > -9000 && vehicleFits(t.id, trailer, trailerYaw, true, v.id);
    }
    if (fits && road != none && v.road != road)
      try {
        payLift(resident, roads[road].facility, int(std::round(next.y / 3)));
      } catch (const std::runtime_error &) {
        fits = false;
        allowed = false;
      }
    if (!fits) {
      float impact = std::abs(v.speed);
      if (allowed && impact > 2 && time >= v.nextImpact) {
        v.condition = std::max(0.f, v.condition - impact * impact * .003f);
        v.nextImpact = time + 1 * (1440 / config.dayMinutes);
        for (auto &other : vehicles)
          if (other.id != v.id && other.id != v.towing && vehicleContact(v.id, next, yaw, other.id))
            other.condition = std::max(0.f, other.condition - impact * impact * .0015f);
        for (auto &person : residents)
          if (person.id != resident && person.alive && person.present && person.vehicle == none &&
              std::abs(position(person.id).y - next.y) < 1.6f &&
              std::abs((position(person.id).x - next.x) * std::cos(yaw) +
                       (position(person.id).z - next.z) * std::sin(yaw)) <
                  vehicleWidth(v.id) / 2 + .3f &&
              std::abs(-(position(person.id).x - next.x) * std::sin(yaw) +
                       (position(person.id).z - next.z) * std::cos(yaw)) <
                  vehicleLength(v.id) / 2 + .3f) {
            report(CrimeKind::Assault, resident, person.id, true);
            person.health = std::max(0.f, person.health - impact * impact * 1.2f);
            if (person.health <= 0)
              kill(person.id, resident);
          }
        log("traffic.collision", resident, v.id);
      }
      v.speed = 0;
      if (landing)
        v.airborne = false;
    } else {
      float distance = std::hypot(next.x - v.pos.x, next.z - v.pos.z);
      if (motor)
        v.fuel =
            std::max(0.f, v.fuel - distance * (v.kind == VehicleKind::Scooter ? .002f : .006f));
      else
        exercise(resident, dt * (1440 / config.dayMinutes), .8f);
      v.pos = next;
      v.yaw = yaw;
      v.road = road;
      v.pitch = road == none ? 0 : std::atan(3 / roads[road].length) * std::cos(yaw);
      if (v.towing != none) {
        auto &t = vehicles[v.towing];
        t.pos = trailer;
        t.yaw = trailerYaw;
        t.speed = v.speed;
        t.road = road;
      }
      if (v.freightLift != none) {
        auto &lift = roads[v.freightLift];
        if (std::abs(v.pos.x - lift.pos.x) > lift.width / 2 ||
            std::abs(v.pos.z - lift.pos.z) > lift.length / 2) {
          lift.occupant = none;
          v.freightLift = none;
        }
      }
    }
    r.pos = v.pos;
    if (v.pos.y < -100) {
      v.condition = 0;
      v.speed = 0;
      v.airborne = false;
      kill(resident);
      break;
    }
    if (!r.alive)
      break;
  }
  v.updatedAt = time;
  ensureVehicleTick();
}
} // namespace mega
