#include "mega/world.hpp"
#include <algorithm>
#include <cmath>

namespace mega {
namespace {
float vehicleWidth(VehicleKind kind) {
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
    v.look = {i % 8, (i + 3) % 8, 0, 0};
    v.pos = {i % 2 == 0 ? -1.8f : 1.8f, 0, -10.f + (i / 2) * 4.8f};
    if (config.layoutVersion > 1)
      v.pos = v.pos + Vec3{-30, 0, layout(0).halfDepth() - 10};
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
      vehicleWidth(v.kind) > bay.width || (!hasDevice(resident) && !near(resident, f.pos, 3)))
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
  if (!available(r) || r.vehicle != none || v.owner != r.account || v.driver != none ||
      v.askingPrice != 0 || !near(resident, v.pos, 2.5f))
    throw std::runtime_error("Reach your vehicle before entering it");
  leaveQueue(resident);
  r.route.clear();
  ++r.routeVersion;
  r.task = TaskKind::None;
  r.activity = Activity::Idle;
  r.vehicle = id;
  v.driver = resident;
  v.speed = 0;
  r.pos = v.pos;
}

void World::exitVehicle(Id resident) {
  auto &r = residents.at(resident);
  if (r.vehicle == none)
    throw std::runtime_error("Not in a vehicle");
  auto &v = vehicles.at(r.vehicle);
  if (std::abs(v.speed) > .2f)
    throw std::runtime_error("Stop before leaving the vehicle");
  r.pos = v.pos + Vec3{vehicleWidth(v.kind) / 2 + .45f, 0, 0};
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
    throw std::runtime_error("Invalid driving input");
  auto &v = vehicles.at(r.vehicle);
  if (v.driver != resident)
    throw std::runtime_error("Driver ownership mismatch");
  throttle = std::clamp(throttle, -1.f, 1.f);
  steering = std::clamp(steering, -1.f, 1.f);
  float limit = v.kind <= VehicleKind::Scooter ? 4.f : 5.f;
  v.speed = std::clamp(v.speed + throttle * seconds * 3.f, -2.f, limit);
  if (std::abs(throttle) < .01f)
    v.speed *= std::exp(-seconds * 7.f);
  v.yaw += steering * seconds * std::clamp(v.speed, -1.2f, 1.2f);
  Vec3 next = v.pos + Vec3{std::sin(v.yaw), 0, -std::cos(v.yaw)} * (v.speed * seconds);
  float radius = vehicleWidth(v.kind) * .5f;
  // Motor traffic is restricted to the ground-floor central corridor in this
  // slice. There is no teleporting car access to the upper residential floors.
  bool blocked = std::abs(next.y) > .1f;
  if (config.layoutVersion == 1)
    blocked |= std::abs(next.x) + radius > 3.6f || std::abs(next.z) + 2.1f > 16.7f;
  else {
    auto l = layout(0);
    blocked |= std::abs(next.x) + radius > l.halfWidth() || std::abs(next.z) + 2.1f > l.halfDepth();
    for (auto &o : floorObstacles(0))
      if (overlaps(next, o, std::max(radius, 1.8f)))
        blocked = true;
  }
  for (const auto &other : vehicles)
    if (other.id != v.id && std::abs(next.y - other.pos.y) < 2 &&
        std::abs(next.x - other.pos.x) < radius + vehicleWidth(other.kind) / 2 + .1f &&
        std::abs(next.z - other.pos.z) < 2.4f)
      blocked = true;
  for (const auto &other : residents)
    if (other.id != resident && other.alive && other.present && other.vehicle == none &&
        std::abs(other.pos.y - next.y) < 4) {
      auto p = position(other.id);
      if (std::abs(next.y - p.y) < 1 && std::abs(next.x - p.x) < radius + .35f &&
          std::abs(next.z - p.z) < 1.5f)
        blocked = true;
    }
  if (blocked)
    v.speed = 0;
  else
    v.pos = next;
  r.pos = v.pos;
}
} // namespace mega
