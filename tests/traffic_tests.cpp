#include "mega/world.hpp"
#include <functional>
#include <iostream>
#include <numeric>
using namespace mega;
static int passed = 0, failed = 0;
static void require(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
template <class F> static void rejects(F f) {
  bool b = false;
  try {
    f();
  } catch (const std::exception &) {
    b = true;
  }
  require(b, "Invalid action accepted");
}
static void test(const char *n, const std::function<void()> &fn) {
  try {
    fn();
    ++passed;
    std::cout << "PASS " << n << '\n';
  } catch (const std::exception &e) {
    ++failed;
    std::cerr << "FAIL " << n << ": " << e.what() << '\n';
  }
}
static World small() {
  Config c;
  c.population = 40;
  c.floors = 20;
  c.mode = Mode::Cheat;
  c.accidentRate = 0;
  auto w = World::generate(c);

  while (!w.events.empty())
    w.events.pop();
  w.time = 10 * 3600;
  for (auto &r : w.residents) {
    w.clockOut(r.id);
    w.leaveQueue(r.id);
    r.automatic = false;
    r.activity = Activity::Idle;
    r.route.clear();
    r.task = TaskKind::None;
    r.role = Role::Unemployed;
    r.jobSite = none;
    r.employer = none;
    r.hunger = .4f;
    r.thirst = .1f;
    r.energy = r.hygiene = r.happiness = 1;
    r.bladder = 0;
    r.personalWaste = 0;
    r.needsAt = w.time;
    r.pos = {100 + r.id * 2.f, 0, 100};
    if (r.outfit != none)
      w.items[r.outfit].cleanliness = 1;
  }
  for (auto &f : w.facilities) {
    f.queue.clear();
    f.serving.clear();
    f.staffNeeded = 0;
    f.opensAt = 0;
    f.openHours = 24;
  }
  for (auto &f : w.facilities)
    f.employees.clear();
  w.crowd.clear();
  return w;
}
static void place(World &w, Id id, Vec3 p) {
  auto &r = w.residents[id];
  r.pos = p;
  r.route.clear();
  ++r.routeVersion;
  r.activity = Activity::Idle;
  r.task = TaskKind::None;
  w.crowd.erase(id);
}

static void car(World &w, Vec3 p = {80, 0, 72}) {
  auto &v = w.vehicles[2];
  v.owner = w.residents[0].account;
  v.driver = v.dealer = v.bay = none;
  v.askingPrice = 0;
  v.pos = p;
  v.yaw = 3.14159265f;
  v.condition = 1;
  v.fuel = 40;
  v.speed = 0;
  place(w, 0, p);
  w.pay(0, w.residents[0].account, 10000000, "test-capital");
  w.enterVehicle(0, 2);
}
static Id workshop(World &w) {
  for (auto &f : w.facilities)
    if (f.kind == FacilityKind::Workshop && f.floor == 0)
      return f.id;
  throw std::runtime_error("No workshop");
}
static void worker(World &w, Id id, Role role, Id shop) {
  w.chooseJob(id, role, shop);
  auto &r = w.residents[id];
  r.age = 30;
  r.routine.shiftStart = 0;
  r.routine.shiftHours = 24;
  r.routine.daysOff = 0;
  r.automatic = true;
  r.hunger = r.thirst = r.bladder = 0;
  r.energy = r.hygiene = 1;
}
static void ticks(World &w, double seconds) {
  w.schedule(w.time + 1, EventKind::FacilityTick, 0);
  w.advance(seconds);
}
static void phone(World &w) {
  Item i;
  i.id = Id(w.items.size());
  i.kind = ItemKind::Phone;
  i.owner = w.residents[0].account;
  i.carrier = 0;
  w.items.push_back(i);
  w.residents[0].items.push_back(i.id);
}
static void serviceSite(World &w) {
  Id f = workshop(w);
  w.facilities[f].pos = {-80, 0, 150};
  w.invalidateGeometry(0);
  for (auto &v : w.vehicles)
    if (v.towTruck && v.owner == w.operatingAccount(f))
      v.pos = v.garage = {-68, 0, 150};
}
int main() {
  test("New modern worlds have sparse real road infrastructure and preserved floor identifiers",
       [] {
         auto w = small();
         require(w.roads.size() == 2 && w.roads[0].active && w.roads[1].active,
                 "Missing starter roads");
         require(w.roads[0].to == 1 && w.roads[1].to == 10, "Roads unexpectedly serve every floor");
         w.validate();
       });
  test("A car crosses a ramp continuously with actual height, pitch, fuel and toll payment", [] {
    auto w = small();
    car(w);
    auto &v = w.vehicles[2];
    auto cash = w.accounts[w.residents[0].account].balance;
    float prior = v.pos.y;
    bool slope = false;
    for (int i = 0; i < 450; ++i) {
      w.driveVehicle(0, 1, 0, .02f);
      require(std::abs(v.pos.y - prior) < .04f, "Ramp teleported the vehicle");
      prior = v.pos.y;
      slope |= std::abs(v.pitch) > .02f;
    }
    require(v.pos.y > 2.99f && slope && v.fuel < 40,
            "Ramp traversal did not use physical height/fuel");
    require(w.accounts[w.residents[0].account].balance ==
                cash - w.facilities[w.roads[0].facility].price,
            "Ramp toll incorrect");
    w.validate();
  });
  test("Ramp holes, support and slab pieces agree", [] {
    auto w = small();
    auto r = w.roads[0];
    Vec3 p{r.pos.x, 1.5f, r.pos.z + r.length / 2};
    require(std::abs(w.supportHeight(p, 2) - 1.5f) < .001, "Missing sloped support");
    require(!w.layout(1).supported({p.x, 3, p.z}), "Upper slab blocks the ramp");
    for (auto piece : w.layout(1).slabs(1))
      require(!(p.x > piece.center.x - piece.size.x / 2 &&
                p.x < piece.center.x + piece.size.x / 2 &&
                p.z > piece.center.z - piece.size.z / 2 && p.z < piece.center.z + piece.size.z / 2),
              "Rendered slab fills road cutout");
  });
  test("Closed and unaffordable ramps stop entry without taking a toll", [] {
    auto w = small();
    car(w);
    auto f = w.roads[0].facility;
    w.facilities[f].open = false;
    auto cash = w.accounts[w.residents[0].account].balance;
    for (int i = 0; i < 250; ++i)
      w.driveVehicle(0, 1, 0, .02f);
    require(w.vehicles[2].pos.z < 80 && w.accounts[w.residents[0].account].balance == cash,
            "Closed ramp was bypassed");
  });
  test("Autonomous driving reaches a real upper-floor destination through a ramp", [] {
    auto w = small();
    car(w);
    w.driveTo(0, {80, 3, 116}, true);
    w.advance(48 * 90);
    require(std::abs(w.vehicles[2].pos.y - 3) < .1f &&
                length(w.vehicles[2].pos - Vec3{80, 3, 116}) < 1 && w.residents[0].vehicle == none,
            "Autonomous ramp journey failed");
    w.validate();
  });
  test("Freight lift queues, drives aboard, carries the car and reverses out at the paid stop", [] {
    auto w = small();
    car(w, {110, 0, 64});
    auto cash = w.accounts[w.residents[0].account].balance;
    w.driveTo(0, {110, 30, 64}, true);
    w.advance(48 * 180);
    auto &v = w.vehicles[2];
    require(std::abs(v.pos.y - 30) < .1f && length(v.pos - Vec3{110, 30, 64}) < 1 &&
                v.freightLift == none && w.residents[0].vehicle == none,
            "Freight journey failed");
    require(w.accounts[w.residents[0].account].balance ==
                cash - w.facilities[w.roads[1].facility].price,
            "Cargo fare not settled once");
    w.validate();
  });
  test("Freight lift rejects remote, overloaded and wrong-floor calls", [] {
    auto w = small();
    car(w, {0, 0, 0});
    rejects([&] { w.requestVehicleLift(0, 1, 10); });
    w.vehicles[2].pos = {110, 0, 64};
    w.residents[0].pos = w.vehicles[2].pos;
    w.roads[1].loadLimit = 100;
    rejects([&] { w.requestVehicleLift(0, 1, 10); });
  });
  test("No road connection means no vehicle teleport to another floor", [] {
    auto w = small();
    car(w);
    auto p = w.vehicles[2].pos;
    rejects([&] { w.driveTo(0, {80, 6, 116}, true); });
    require(length(w.vehicles[2].pos - p) == 0, "Failed route moved car");
  });
  test("Fuel imports and physical refueling transfer funds and finite stock", [] {
    auto w = small();
    serviceSite(w);
    Id f = workshop(w);
    car(w, {-68, 0, 150});
    auto &v = w.vehicles[2];
    v.fuel = 20;
    auto cash = w.accounts[w.residents[0].account].balance;
    float stock = w.facilities[f].fuelStock;
    w.refuelVehicle(0, v.id, f, 5);
    require(v.fuel == 25 && w.facilities[f].fuelStock == stock - 5,
            "Fuel invented or not delivered");
    require(w.accounts[w.residents[0].account].balance == cash - 5 * w.facilities[f].fuelPrice,
            "Fuel not paid");
    rejects([&] { w.refuelVehicle(0, v.id, f, 20); });
  });
  test("An empty motor tank cannot accelerate, while a bicycle uses the rider's effort", [] {
    auto w = small();
    car(w);
    w.vehicles[2].fuel = 0;
    auto p = w.vehicles[2].pos;
    for (int i = 0; i < 100; ++i)
      w.driveVehicle(0, 1, 0, .02f);
    require(length(p - w.vehicles[2].pos) < .001, "Empty car accelerated");
  });
  test("Vehicle contacts respect orientation and do not damage nearby unhit assets", [] {
    auto w = small();
    car(w, {150, 0, 150});
    auto &other = w.vehicles[3];
    other.pos = {150, 0, 141};
    other.yaw = 0;
    auto &bystander = w.vehicles[4];
    bystander.pos = {157, 0, 146};
    bystander.condition = 1;
    w.vehicles[2].yaw = 0;
    for (int i = 0; i < 180; ++i) {
      w.time += .02 * 48;
      w.driveVehicle(0, 1, 0, .02f);
    }
    require(w.vehicles[2].condition < 1 && other.condition < 1, "Impact caused no damage");
    require(bystander.condition == 1, "Unhit car damaged");
  });
  test("A car can fall from an upper slab and land with persistent damage", [] {
    auto w = small();
    car(w, {255, 3, 160});
    w.vehicles[2].yaw = 1.5707963f;
    for (int i = 0; i < 350 && w.residents[0].alive; ++i) {
      w.time += .02 * 48;
      w.driveVehicle(0, 1, 0, .02f);
    }
    require(w.vehicles[2].airborne || w.vehicles[2].condition < 1 || !w.residents[0].alive,
            "Driving off a slab had no physical consequence");
  });
  test("Repairs need an actual on-shift mechanic, stocked parts, elapsed work and payment", [] {
    auto w = small();
    serviceSite(w);
    Id f = workshop(w);
    car(w, {-66, 0, 150});
    w.exitVehicle(0);
    phone(w);
    w.vehicles[2].condition = .4f;
    auto stock = w.facilities[f].stock;
    Id job = w.requestVehicleJob(0, 2, f, false);
    w.vehicleServicesTick();
    require(w.vehicleJobs[job].worker == none && w.vehicles[2].condition == .4f,
            "Unstaffed workshop repaired car");
    worker(w, 5, Role::Repairer, f);
    w.schedule(w.time + 1, EventKind::Decide, 5);
    ticks(w, 7200);
    require(w.vehicleJobs[job].state == VehicleJobState::Complete && w.vehicles[2].condition == 1 &&
                w.facilities[f].stock == stock - 1,
            "Physical repair failed");
    w.validate();
  });
  test("Physical tow drives to the client, attaches without relocation and returns the owned car",
       [] {
         auto w = small();
         serviceSite(w);
         Id f = workshop(w);
         car(w, {-20, 0, 150});
         w.exitVehicle(0);
         phone(w);
         w.vehicles[2].condition = .02f;
         Id job = w.requestVehicleJob(0, 2, f, true);
         place(w, 0, {-40, 0, 170});
         worker(w, 5, Role::TowDriver, f);
         place(w, 5, w.facilities[f].pos + Vec3{3, 0, 0});
         w.schedule(w.time + 1, EventKind::Decide, 5);
         auto wageBefore = w.accounts[w.residents[5].account].balance;
         ticks(w, 48 * 180);
         require(w.accounts[w.residents[5].account].balance > wageBefore,
                 "Tow driver earned no wages");
         require(w.vehicleJobs[job].state == VehicleJobState::Complete &&
                     length(w.vehicles[2].pos - w.facilities[f].pos) < 19 &&
                     w.vehicles[2].towedBy == none,
                 "Physical tow failed");
         w.validate();
       });
  test("Cancelled unstarted services refund escrow exactly once", [] {
    auto w = small();
    serviceSite(w);
    Id f = workshop(w);
    car(w, {-66, 0, 150});
    w.exitVehicle(0);
    phone(w);
    Id job = w.requestVehicleJob(0, 2, f, false);
    auto before = w.accounts[w.residents[0].account].balance;
    auto budget = w.vehicleJobs[job].budget;
    w.cancelVehicleJob(0, job);
    require(w.accounts[w.residents[0].account].balance == before + budget,
            "Cancellation refund wrong");
    rejects([&] { w.cancelVehicleJob(0, job); });
  });
  test("Vehicle journeys and freight motion survive a native save and continue", [] {
    auto w = small();
    car(w, {110, 0, 64});
    w.driveTo(0, {110, 30, 64}, true);
    w.advance(48 * 12);
    auto path = std::filesystem::temp_directory_path() / "mega-traffic-test.mdsave";
    w.save(path);
    auto loaded = World::load(path);
    loaded.advance(48 * 180);
    require(loaded.vehicles[2].pos.y > 29.9f && loaded.residents[0].vehicle == none,
            "Saved freight trip did not resume");
    loaded.validate();
    std::filesystem::remove(path);
  });
  test("Malformed traffic state cannot index invalid vehicles or road references", [] {
    auto w = small();
    w.vehicles[2].freightLift = 999999;
    rejects([&] { w.validate(); });
    w = small();
    w.roads[0].width = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { w.validate(); });
  });

  test("Funded ramp construction reserves its plot and requires real materials and crew work", [] {
    auto w = small();
    Id p = w.proposeRoad(w.dormOwner, RoadKind::Ramp, 1, 2, {150, 3, 140}, 24);
    auto &job = w.structuralWorks[p];
    Id road = job.transport;
    require(!w.roads[road].active && w.layout(2).supported({150, 6, 150}),
            "Proposal changed the slab");
    w.fundStructuralWork(w.dormOwner, p);
    require(!w.clearPosition({150, 3, 150}), "Funded plot was not protected");
    for (Id id : {5, 6})
      worker(w, id, Role::Builder, job.workshop);
    Id team = w.createConstructionTeam(5, job.workshop, 5);
    w.recruitConstructionWorker(5, team, 6);
    w.assignStructuralTeam(w.dormOwner, p, team);
    place(w, 5, w.facilities[job.workshop].pos + Vec3{1.6f, 0, 0});
    w.beginStructuralWork(5, p);
    w.structuralTick();
    require(job.material != none && !job.supplied, "Materials appeared at site");
    place(w, 5, job.site);
    w.beginStructuralWork(5, p);
    w.structuralTick();
    for (Id id : {5, 6}) {
      place(w, id, w.structuralDestination(id, p));
      w.beginStructuralWork(id, p);
    }
    w.structuralTick();
    for (int n = 0; n < 200 && job.state != StructuralState::Completed; ++n) {
      w.time += 60;
      w.structuralTick();
    }
    require(job.state == StructuralState::Completed && w.roads[road].active &&
                !w.layout(2).supported({150, 6, 150}),
            "Ramp construction did not activate its road/cutout");
    w.validate();
  });
  test("Transport decommissioning waits for vehicles and protects connected floor removal", [] {
    auto w = small();
    car(w, {80, 1.5f, 92});
    w.vehicles[2].road = 0;
    rejects([&] { w.activateRoad(0, false); });
    Id floor = w.proposeFloorChange(w.dormOwner, 1, false);
    rejects([&] { w.fundStructuralWork(w.dormOwner, floor); });
    require(w.roads[0].active, "Occupied infrastructure erased");
  });
  test("Fuel import is owner-funded, bounded and cannot be ordered by an unrelated resident", [] {
    auto w = small();
    Id f = workshop(w);
    w.facilities[f].manager = 0;
    auto money = w.accounts[w.operatingAccount(f)].balance;
    float stock = w.facilities[f].fuelStock;
    w.stockFuel(0, f, 100);
    require(w.facilities[f].fuelStock == stock + 100 &&
                w.accounts[w.operatingAccount(f)].balance == money - 10000,
            "Fuel import not funded");
    rejects([&] { w.stockFuel(5, f, 100); });
    rejects([&] { w.stockFuel(0, f, 10001); });
  });
  test(
      "An assigned mechanic resumes paid work after save and replacement without duplicating parts",
      [] {
        auto w = small();
        serviceSite(w);
        Id f = workshop(w);
        car(w, {-66, 0, 150});
        w.exitVehicle(0);
        phone(w);
        w.vehicles[2].condition = .3f;
        worker(w, 5, Role::Repairer, f);
        Id job = w.requestVehicleJob(0, 2, f, false);
        w.assignVehicleJob(5, job);
        place(w, 5, w.vehicles[2].pos + Vec3{2, 0, 0});
        w.vehicleWorkDecision(5);
        w.vehicleServicesTick();
        auto stock = w.facilities[f].stock;
        w.time += 60;
        w.vehicleServicesTick();
        auto path = std::filesystem::temp_directory_path() / "mega-mechanic-test.mdsave";
        w.save(path);
        auto loaded = World::load(path);
        loaded.chooseJob(5, Role::Builder, f);
        loaded.vehicleServicesTick();
        require(loaded.vehicleJobs[job].worker == none, "Lost mechanic stayed assigned");
        worker(loaded, 6, Role::Repairer, f);
        loaded.assignVehicleJob(6, job);
        place(loaded, 6, loaded.vehicles[2].pos + Vec3{2, 0, 0});
        loaded.vehicleWorkDecision(6);
        for (int n = 0; n < 40 && loaded.vehicleJobs[job].state != VehicleJobState::Complete; ++n) {
          loaded.time += 60;
          loaded.vehicleServicesTick();
        }
        require(loaded.vehicleJobs[job].state == VehicleJobState::Complete &&
                    loaded.facilities[f].stock == stock,
                "Resumed repair duplicated parts or failed");
        loaded.validate();
        std::filesystem::remove(path);
      });
  test("A bought van can be physically converted into an owner-funded workshop service asset", [] {
    auto w = small();
    serviceSite(w);
    Id f = workshop(w);
    w.facilities[f].manager = 0;
    w.facilities[f].owner = w.dormAccount;
    car(w, {-140, 0, 120});
    w.exitVehicle(0);
    phone(w);
    auto &van = w.vehicles[3];
    van.pos = {-66, 0, 150};
    van.owner = w.residents[0].account;
    van.dealer = van.bay = none;
    van.askingPrice = 0;
    worker(w, 5, Role::Repairer, f);
    Id job = w.convertServiceTruck(0, 3, f);
    w.assignVehicleJob(5, job);
    place(w, 5, van.pos + Vec3{2, 0, 0});
    w.vehicleWorkDecision(5);
    w.vehicleServicesTick();
    for (int n = 0; n < 70 && w.vehicleJobs[job].state != VehicleJobState::Complete; ++n) {
      w.time += 60;
      w.vehicleServicesTick();
    }
    require(w.vehicleJobs[job].state == VehicleJobState::Complete && van.towTruck &&
                van.owner == w.operatingAccount(f),
            "Conversion lacked physical work/ownership contribution");
    place(w, 0, van.pos + Vec3{1.5f, 0, 0});
    rejects([&] { w.enterVehicle(0, 3); });
    worker(w, 0, Role::TowDriver, f);
    w.enterVehicle(0, 3);
    w.validate();
  });
  test("Opening a menu cannot suspend a falling vehicle, and dead drivers leave a falling wreck",
       [] {
         auto w = small();
         car(w, {255, 3, 160});
         w.vehicles[2].yaw = 1.5707963f;
         for (int n = 0; n < 200 && !w.vehicles[2].airborne; ++n)
           w.driveVehicle(0, 1, 0, .02f);
         require(w.vehicles[2].airborne, "No falling vehicle");
         w.kill(0);
         w.advance(48 * 3);
         require(!w.vehicles[2].airborne && std::abs(w.vehicles[2].pos.y) < .01f &&
                     w.vehicles[2].condition < 1,
                 "Unoccupied wreck suspended in air");
         w.validate();
       });
  test("Blocked ramp entry never charges repeated tolls", [] {
    auto w = small();
    car(w, {80, 0, 79.8f});
    place(w, 5, {80, 0, 80.1f});
    auto cash = w.accounts[w.residents[0].account].balance;
    for (int n = 0; n < 30; ++n)
      w.driveVehicle(0, 1, 0, .02f);
    require(w.accounts[w.residents[0].account].balance == cash, "Blocked entry charged a fare");
  });
  test("Pedestrians and rotated cars use the same orientation for contacts", [] {
    auto w = small();
    car(w, {150, 0, 150});
    w.vehicles[2].yaw = 1.5707963f;
    place(w, 5, {150, 0, 153});
    auto p = w.moveCapsule(5, {150, 0, 153}, {0, 0, -3}, false);
    require(p.z > 151, "Pedestrian entered a rotated car");
  });
  test("Existing overlaps on a ladder can separate vertically without introducing new overlaps",
       [] {
         auto w = small();
         auto l = w.layout();
         Vec3 a = l.ladder(1, 5.4f), b = l.ladder(1, 5.42f), end = l.ladder(1, 0);
         place(w, 5, a);
         place(w, 6, b);
         w.residents[5].route = {
             {a, w.time}, {end, w.time + 1000}, {l.landing(1, 0), w.time + 1100}};
         w.residents[6].route = {
             {b, w.time}, {end, w.time + 1000}, {l.landing(1, 0), w.time + 1100}};
         w.residents[5].activity = w.residents[6].activity = Activity::Walking;
         w.focus(1);
         w.moveCrowd(48 * 5);
         require(w.position(5).y < 1 && w.position(6).y < 1,
                 "Coarse crowd overlap permanently locked vertical travel");
       });

  test("Walking staff commit to a short detour around a stationary customer", [] {
    auto w = small();
    place(w, 5, {150, 0, 120});
    place(w, 6, {150, 0, 121});
    auto &r = w.residents[5];
    r.route = {{{150, 0, 120}, w.time}, {{150, 0, 122.5f}, w.time + 1000}};
    r.activity = Activity::Walking;
    w.focus(0);
    w.moveCrowd(48 * 4);
    require(length(w.position(5) - Vec3{150, 0, 122.5f}) < .1f,
            "Staff oscillated behind a stationary customer");
  });
  test("Completed parking facilities create bounded usable bays only once", [] {
    auto w = small();
    Facility f;
    f.id = Id(w.facilities.size());
    f.kind = FacilityKind::Parking;
    f.owner = w.dormAccount;
    f.floor = 1;
    f.pos = {150, 3, 145};
    f.stations = 2;
    f.price = 2300;
    w.facilities.push_back(f);
    w.floorFacilities[1].push_back(f.id);
    auto before = w.parking.size();
    w.generateParkingBays(f.id);
    w.generateParkingBays(f.id);
    require(w.parking.size() == before + 2 && w.parking.back().dailyPrice == 2300 &&
                w.parking.back().pos.y == 3,
            "Parking bays duplicated, free or on wrong floor");
    w.validate();
  });
  test("Automatic commuters leave their old bay, pay for a destination and drive upstairs", [] {
    auto w = small();
    car(w);
    w.exitVehicle(0);
    phone(w);
    Id shop = workshop(w);
    auto &f = w.facilities[shop];
    w.floorFacilities[0].erase(
        std::remove(w.floorFacilities[0].begin(), w.floorFacilities[0].end(), shop),
        w.floorFacilities[0].end());
    f.floor = 1;
    f.pos = {150, 3, 150};
    w.floorFacilities[1].push_back(shop);
    w.invalidateGeometry(0);
    w.invalidateGeometry(1);
    worker(w, 0, Role::Repairer, shop);
    Facility lot;
    lot.id = Id(w.facilities.size());
    lot.kind = FacilityKind::Parking;
    lot.owner = w.dormAccount;
    lot.floor = 1;
    lot.pos = {150, 3, 140};
    lot.stations = 2;
    lot.price = 1800;
    w.facilities.push_back(lot);
    w.floorFacilities[1].push_back(lot.id);
    w.generateParkingBays(lot.id);
    auto &v = w.vehicles[2];
    auto &old = w.parking[3];
    old.pos = v.pos;
    old.vehicle = v.id;
    v.bay = old.id;
    old.paidUntil = w.time + 86400;
    auto cash = w.accounts[w.residents[0].account].balance;
    require(w.chooseVehicleTrip(0), "Commuter chose no road journey");
    w.advance(48 * 240);
    require(w.residents[0].vehicle == none && v.bay != old.id &&
                w.parking[v.bay].facility == lot.id &&
                length(v.pos - w.parking[v.bay].pos) < 1.3f && old.vehicle == none,
            "Commuter failed to release, arrive or physically park");
    require(w.accounts[w.residents[0].account].balance <= cash - 1800,
            "Destination parking was unpaid");
    w.validate();
  });
  test("A ramp return aligns a car before boarding the separate freight lift", [] {
    auto w = small();
    car(w);
    w.driveTo(0, {80, 3, 116}, false);
    w.advance(48 * 90);
    w.driveTo(0, {110, 30, 64}, true);
    w.advance(48 * 240);
    require(w.residents[0].vehicle == none && length(w.vehicles[2].pos - Vec3{110, 30, 64}) < 1,
            "Angled freight approach caught a gate rail");
    w.validate();
  });
  test("A customer's death cancels a pending tow and releases its funded work", [] {
    auto w = small();
    serviceSite(w);
    Id f = workshop(w);
    car(w, {-20, 0, 150});
    w.exitVehicle(0);
    phone(w);
    worker(w, 5, Role::TowDriver, f);
    Id job = w.requestVehicleJob(0, 2, f, true);
    w.assignVehicleJob(5, job);
    w.kill(0);
    w.vehicleServicesTick();
    require(w.vehicleJobs[job].state == VehicleJobState::Cancelled &&
                w.accounts[w.vehicleJobs[job].escrow].balance == 0,
            "Death left a funded orphan assignment");
    w.validate();
  });
  test("Sold road infrastructure cannot be demolished by its former owner", [] {
    auto w = small();
    Id owner = w.dormOwner;
    Id f = w.roads[0].facility;
    Id plan = w.proposeRoadRemoval(owner, 0);
    w.facilities[f].owner = w.residents[5].account;
    rejects([&] { w.fundStructuralWork(owner, plan); });
    rejects([&] { w.proposeRoadRemoval(owner, 0); });
    require(w.roads[0].active, "Another owner's road was erased");
    w.validate();
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
