#include "json.hpp"
#include "mega/world.hpp"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <zstd.h>
using namespace mega;
int passed = 0, failed = 0;
void require(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
void test(const char *n, std::function<void()> f) {
  try {
    f();
    ++passed;
    std::cout << "PASS " << n << '\n';
  } catch (const std::exception &e) {
    ++failed;
    std::cerr << "FAIL " << n << ": " << e.what() << '\n';
  }
}
template <class F> void rejects(F f) {
  bool b = false;
  try {
    f();
  } catch (const std::exception &) {
    b = true;
  }
  require(b, "Invalid state accepted");
}
void place(World &w, Id id, Vec3 p) {
  auto &r = w.residents[id];
  r.pos = p;
  r.route.clear();
  ++r.routeVersion;
  r.activity = Activity::Idle;
  r.task = TaskKind::None;
  w.crowd.erase(id);
}
World small() {
  Config c;
  c.population = 40;
  c.floors = 20;
  c.mode = Mode::Cheat;
  c.accidentRate = 0;
  auto w = World::generate(c);
  while (!w.events.empty())
    w.events.pop();
  w.dormOwner = w.player;
  for (auto &r : w.residents) {
    w.clockOut(r.id);
    w.leaveQueue(r.id);
    r.automatic = false;
    r.role = Role::Unemployed;
    r.jobSite = r.employer = none;
    place(w, r.id, {100 + r.id * 2.f, 57, 100});
  }
  for (auto &f : w.facilities) {
    f.manager = none;
    f.employees.clear();
    f.staffNeeded = 0;
    f.opensAt = 0;
    f.openHours = 24;
    f.queue.clear();
    f.serving.clear();
  }
  w.crowd.clear();
  return w;
}
Id move(World &w, FacilityKind kind, Vec3 p) {
  Id id = none;
  for (const auto &f : w.facilities)
    if (f.kind == kind) {
      id = f.id;
      break;
    }
  require(id != none, "Missing site");
  auto &f = w.facilities[id];
  std::erase(w.floorFacilities[f.floor], id);
  f.pos = p;
  f.floor = int(p.y / 3);
  w.floorFacilities[f.floor].push_back(id);
  w.invalidateGeometry(f.floor);
  return id;
}
void tick(World &w, int n = 1) {
  for (int i = 0; i < n; ++i) {
    w.time += 60;
    w.emergencyTick();
  }
}
Money total(const World &w) {
  Money n = 0;
  for (const auto &a : w.accounts)
    n += a.balance;
  return n;
}
struct Cargo {
  Id fire, source, destination, shipment, item;
};
Cargo cargo(World &w, bool waste = false) {
  Id a = move(w, FacilityKind::Cafe, {150, 0, 150}),
     dest = move(w, FacilityKind::Food, {180, 0, 150});
  auto &f = w.facilities[waste ? a : dest];
  f.owner = w.residents[0].account;
  f.manager = 0;
  w.pay(0, f.owner, 1000000, "test-capital");
  Id s;
  if (waste) {
    f.waste = 100;
    s = w.requestWaste(0, a);
  } else
    s = w.requestStock(0, dest, 100);
  Id item = w.freight[s].item;
  w.items[item].pos = {151, 0, 150};
  w.facilities[a].manager = none;
  w.facilities[dest].manager = none;
  return {w.ignite(a, 1), a, w.freight[s].facility, s, item};
}
int main() {
  auto root = std::filesystem::temp_directory_path() /
              ("mega-fire-transport-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  test("An intact slab blocks vertical heat and smoke", [] {
    auto w = small();
    Id a = move(w, FacilityKind::Cafe, {150, 0, 150}),
       b = move(w, FacilityKind::Shop, {150, 3, 150});
    w.ignite(a, 1);
    tick(w, 20);
    require(w.smokeAt({150, 3, 150}) == 0 && !w.activeFires.contains(b),
            "Fire crossed intact slab");
    w.validate();
  });
  test("Open atrium smoke advances one layer at a time", [] {
    auto w = small();
    Id a = move(w, FacilityKind::Cafe, {4, 0, 0});
    w.ignite(a, 1);
    require(w.smokeAt({7, 3, 0}) == 0, "Smoke appeared before transport");
    tick(w);
    require(w.smokeAt({7, 3, 0}) > 0 && w.smokeAt({7, 6, 0}) == 0, "Smoke skipped travel delay");
    tick(w, 6);
    require(w.smokeAt({7, 6, 0}) > 0, "Chimney did not carry smoke upward");
    require(w.smokeAt({7, -3, 0}) == 0, "Smoke descended below fire");
    w.validate();
  });
  test("Openings carry accumulated heat into a real upper business", [] {
    auto w = small();
    Id a = move(w, FacilityKind::Cafe, {4, 0, 0}), b = move(w, FacilityKind::Shop, {7, 3, 0});
    w.ignite(a, 1);
    tick(w, 2);
    require(!w.activeFires.contains(b), "Upper ignition instant");
    tick(w, 20);
    require(w.activeFires.contains(b), "Upper exposed business never ignited");
    w.validate();
  });
  test("A removed intermediate deck opens the path, restoring it closes the path", [] {
    auto w = small();
    Vec3 a{150, 0, 150}, b{150, 3, 150};
    require(w.fireHeatBetween(a, b) == 0, "Solid floor transmitted heat");
    w.floorPlans[1].deck = false;
    w.invalidateGeometry(1);
    require(w.fireHeatBetween(a, b) > 0, "Removed slab still sealed");
    w.floorPlans[1].deck = true;
    w.invalidateGeometry(1);
    require(w.fireHeatBetween(a, b) == 0, "Restored floor stayed open");
  });
  test("Upper-floor partitions block smoke and heat while doorways pass gas", [] {
    auto w = small();
    Id a = move(w, FacilityKind::Cafe, {4, 0, 0});
    w.ignite(a, 1);
    w.interiorWalls.push_back({0, w.dormAccount, 1, {6.5f, 3, -5}, {6.5f, 3, 5}, false, true});
    w.invalidateGeometry(1);
    tick(w, 8);
    require(w.smokeAt({8, 3, 0}) == 0 && w.fireHeatBetween({4, 0, 0}, {8, 3, 0}) == 0,
            "Partition leaked hazard");
    w.interiorWalls[0].door = true;
    w.invalidateGeometry(1);
    require(w.smokeAt({8, 3, 0}) > 0, "Actual door opening blocked all gas");
    w.validate();
  });
  test("Freight platform at the landing seals its shaft aperture", [] {
    auto w = small();
    Id road = none;
    for (const auto &r : w.roads)
      if (r.kind == RoadKind::FreightLift) {
        road = r.id;
        break;
      }
    require(road != none, "Missing freight lift");
    auto &r = w.roads[road];
    int upper = r.to;
    Vec3 p = r.pos;
    p.y = upper * 3.f;
    r.height = p.y;
    r.moving = false;
    require(!w.fireOpening(upper, p), "Parked platform was invisible to fire");
    r.height = r.from * 3.f;
    require(w.fireOpening(upper, p), "Empty freight shaft stayed sealed");
  });
  test("Suppression clears local flames before upper smoke and retains response escrow", [] {
    auto w = small();
    Id a = move(w, FacilityKind::Cafe, {4, 0, 0}), id = w.ignite(a, 1);
    w.pay(0, w.dormAccount, 100000, "test-response");
    w.requestFireResponse(0, id);
    tick(w, 10);
    auto &f = w.fires[id];
    require(!f.smokeLayers.empty(), "No upper smoke");
    f.contained = true;
    f.heat = f.smoke = 0;
    tick(w);
    require(!f.out && w.accounts[f.escrow].balance == 18000, "Early all-clear/refund");
    tick(w, 100);
    require(f.out && f.smokeLayers.empty() && w.accounts[f.escrow].balance == 0,
            "Smoke never cleared/refunded");
    require(total(w) == 0, "Invented response money");
    w.validate();
  });
  test("Visible smoke masks and simulation share partition occlusion", [] {
    auto w = small();
    Vec3 p{150, 0, 150};
    auto clear = w.smokeVisibility(0, p);
    w.interiorWalls.push_back({0, w.dormAccount, 0, {152, 0, 130}, {152, 0, 170}, false, true});
    auto rev = w.geometryRevision;
    w.invalidateGeometry(0);
    auto blocked = w.smokeVisibility(0, p);
    require(w.geometryRevision > rev && clear != blocked,
            "Geometry failed to invalidate smoke mask");
    require((clear[16] & (1u << 22)) != 0 && (blocked[16] & (1u << 22)) == 0,
            "GPU visibility mask leaks through wall");
  });
  test("Partial freight delivers only surviving units and settles only earned wages", [] {
    auto w = small();
    auto c = cargo(w);
    tick(w, 3);
    auto &s = w.freight[c.shipment];
    int remaining = s.remaining();
    require(remaining > 0 && remaining < 100, "No partial damage");
    auto &r = w.residents[2];
    r.role = Role::Courier;
    r.jobSite = c.destination;
    r.employer = w.operatingAccount(c.destination);
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    r.routine.daysOff = 0;
    place(w, 2, w.items[c.item].pos);
    w.assignFreight(2, s.id);
    place(w, 2, w.items[c.item].pos);
    w.collectFreight(2, s.id);
    Money fee = s.payableFee(), before = w.accounts[r.account].balance;
    int stock = w.facilities[c.destination].stock;
    place(w, 2, w.facilities[c.destination].pos + Vec3{0, 0, 1.6f});
    w.deliverFreight(2, s.id);
    require(s.state == FreightState::Delivered &&
                w.facilities[c.destination].stock == stock + remaining,
            "Burnt stock reappeared at client");
    require(w.accounts[r.account].balance == before + fee && w.accounts[s.escrow].balance == 0 &&
                total(w) == 0,
            "Partial delivery invented/overpaid money");
    w.validate();
  });
  test("Total cargo loss closes the reservation and refunds logistics, not imported goods", [] {
    auto w = small();
    auto c = cargo(w);
    auto &s = w.freight[c.shipment];
    Money before = w.accounts[w.items[c.item].owner].balance;
    w.items[c.item].durability = .001f;
    tick(w);
    require(s.state == FreightState::Lost && s.remaining() == 0 && !w.items[c.item].consumed &&
                w.items[c.item].carrier == none,
            "Cargo loss erased title or stayed active");
    require(w.accounts[w.items[c.item].owner].balance == before + s.fee &&
                w.accounts[s.escrow].balance == 0,
            "Burnt imported goods received a free replacement refund");
    auto seq = w.newsSequence;
    tick(w, 3);
    require(w.accounts[s.escrow].balance == 0 && total(w) == 0, "Loss settled more than once");
    w.validate();
  });
  test("Destroyed carried freight releases the actual carrier without teleportation", [] {
    auto w = small();
    auto c = cargo(w);
    auto &s = w.freight[c.shipment];
    auto &r = w.residents[2];
    r.role = Role::Courier;
    r.jobSite = c.destination;
    r.employer = w.operatingAccount(c.destination);
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    r.routine.daysOff = 0;
    place(w, 2, w.items[c.item].pos);
    w.assignFreight(2, s.id);
    place(w, 2, w.items[c.item].pos);
    w.collectFreight(2, s.id);
    place(w, 2, {151, 0, 150});
    r.task = TaskKind::FreightDrop;
    r.target = s.id;
    w.items[c.item].durability = .001f;
    Vec3 before = w.position(2);
    tick(w);
    require(s.finished() && r.carrying == none && length(w.items[c.item].pos - before) < .001f &&
                length(w.position(2) - before) < .001f,
            "Loss teleported cargo or actor");
    r.task = TaskKind::FreightDrop;
    r.target = s.id;
    w.arrive(2);
    require(s.state == FreightState::Lost && r.task == TaskKind::None,
            "Restored cancelled transport delivered burnt stock");
    w.validate();
  });
  test("Burning reserved waste removes only the lost source units before collection", [] {
    auto w = small();
    auto c = cargo(w, true);
    auto &s = w.freight[c.shipment];
    int before = w.facilities[c.source].waste;
    tick(w, 3);
    require(s.lostQuantity > 0 && s.remaining() > 0 &&
                w.facilities[c.source].waste == before - s.lostQuantity,
            "Reserved waste was not physically reduced");
    auto &r = w.residents[2];
    r.role = Role::Cleaner;
    r.jobSite = s.facility;
    r.employer = w.operatingAccount(s.facility);
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    r.routine.daysOff = 0;
    place(w, 2, w.items[c.item].pos);
    w.assignFreight(2, s.id);
    place(w, 2, w.items[c.item].pos);
    w.collectFreight(2, s.id);
    require(w.facilities[c.source].waste == 0, "Partial pickup duplicated or lost source waste");
    w.validate();
  });
  test("Smoke and damaged cargo resume across save/load", [&] {
    auto w = small();
    Id a = move(w, FacilityKind::Office, {4, 0, 0});
    w.ignite(a, 1);
    tick(w, 5);
    auto c = cargo(w);
    tick(w, 2);
    auto path = root / "smoke.mdsave";
    w.save(path);
    auto q = World::load(path);
    require(q.fires[0].smokeLayers.size() == w.fires[0].smokeLayers.size() &&
                q.freight[c.shipment].lostQuantity == w.freight[c.shipment].lostQuantity,
            "Saved emergency state lost");
    float before = q.smokeAt({7, 6, 0});
    tick(q);
    require(q.smokeAt({7, 6, 0}) >= before, "Loaded smoke restarted from zero");
    q.validate();
  });
  test("Malformed layer positions, concentrations and cargo counts are rejected", [] {
    auto w = small();
    Id a = move(w, FacilityKind::Office, {4, 0, 0}), id = w.ignite(a, 1);
    w.fires[id].smokeLayers[1] = {{0, 3, 0}, 1.1f};
    rejects([&] { w.validate(); });
    w.fires[id].smokeLayers[1] = {{0, 6, 0}, .1f};
    rejects([&] { w.validate(); });
    w.fires[id].smokeLayers.clear();
    auto c = cargo(w);
    w.freight[c.shipment].lostQuantity = 101;
    rejects([&] { w.validate(); });
  });
  test("A responder arriving after containment finishes normally instead of restarting spray", [] {
    auto w = small();
    Id source = move(w, FacilityKind::Cafe, {4, 0, 0}), id = w.ignite(source, 1);
    tick(w, 5);
    auto &fire = w.fires[id];
    Id station = w.nearestAt({0, 0, 0}, FacilityKind::FireStation);
    auto &r = w.residents[2];
    r.role = Role::Firefighter;
    r.jobSite = station;
    r.employer = w.operatingAccount(station);
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    r.routine.daysOff = 0;
    fire.worker = 2;
    fire.station = station;
    place(w, 2, w.fireApproach(id));
    r.task = TaskKind::FireResponse;
    r.target = id;
    fire.contained = true;
    fire.heat = 0;
    auto news = w.newsSequence;
    Vec3 at = w.position(2);
    w.arrive(2);
    require(r.task == TaskKind::None && r.activity == Activity::Idle && w.newsSequence == news &&
                !w.fireSprays.contains(2) && length(at - w.position(2)) < .001f,
            "An obsolete fire response became a blocked task or resumed paid spray");
    tick(w);
    require(fire.worker == none, "A completed responder stayed reserved by upper smoke");
    w.validate();
  });
  test("Late partial and total-loss refunds leave the economy with an emigrated owner", [] {
    for (bool full : {false, true}) {
      auto w = small();
      auto c = cargo(w);
      w.dormOwner = 1;
      if (!full) {
        tick(w, 3);
        auto &fire = w.fires[c.fire];
        fire.contained = true;
        fire.heat = fire.smoke = 0;
        tick(w);
      } else
        w.items[c.item].durability = .001f;
      place(w, 0, w.layout(0).entrance());
      w.depart(0);
      Money outside = w.accounts[0].balance;
      auto &s = w.freight[c.shipment];
      Money refund = s.fee;
      if (full)
        tick(w);
      else {
        auto &r = w.residents[2];
        r.role = Role::Courier;
        r.jobSite = c.destination;
        r.employer = w.operatingAccount(c.destination);
        r.routine.shiftStart = 0;
        r.routine.shiftHours = 24;
        r.routine.daysOff = 0;
        place(w, 2, w.items[c.item].pos);
        w.assignFreight(2, s.id);
        place(w, 2, w.items[c.item].pos);
        w.collectFreight(2, s.id);
        refund -= s.payableFee();
        place(w, 2, w.facilities[c.destination].pos + Vec3{0, 0, 1.6f});
        w.deliverFreight(2, s.id);
      }
      require(w.accounts[w.residents[0].account].balance == 0 &&
                  w.accounts[0].balance == outside + refund && total(w) == 0,
              "A late freight refund stranded money in an absent resident's wallet");
      w.validate();
    }
  });
  test("Automatic couriers and cleaners hold real cargo when its destination ignites and resume "
       "after clearance",
       [] {
         for (bool waste : {false, true}) {
           auto w = small();
           auto c = cargo(w, waste);
           w.fires[c.fire].contained = true;
           w.fires[c.fire].heat = w.fires[c.fire].smoke = 0;
           tick(w);
           auto &r = w.residents[2];
           r.role = waste ? Role::Cleaner : Role::Courier;
           r.jobSite = c.destination;
           r.employer = w.operatingAccount(c.destination);
           r.routine.shiftStart = 0;
           r.routine.shiftHours = 24;
           r.routine.daysOff = 0;
           r.routine.sleepStart = 22;
           r.routine.sleepHours = 7;
           r.age = 30;
           r.energy = r.hygiene = 1;
           r.hunger = r.thirst = r.bladder = .1f;
           r.personalWaste = 0;
           r.needsAt = w.time;
           place(w, 2, w.items[c.item].pos);
           w.assignFreight(2, c.shipment);
           place(w, 2, w.items[c.item].pos);
           w.collectFreight(2, c.shipment);
           r.automatic = true;
           Vec3 at = w.position(2);
           auto escrow = w.accounts[w.freight[c.shipment].escrow].balance;
           Id fire = w.ignite(c.destination, 1);
           w.decide(2);
           require(r.route.empty() && r.carrying == c.item && length(w.position(2) - at) < .001f &&
                       w.accounts[w.freight[c.shipment].escrow].balance == escrow,
                   "Unsafe job moved, discarded or remotely delivered cargo");
           w.fires[fire].contained = true;
           w.fires[fire].heat = w.fires[fire].smoke = 0;
           tick(w);
           w.decide(2);
           require(!r.route.empty() && r.task == TaskKind::FreightDrop &&
                       length(w.position(2) - at) < .001f,
                   "Cleared job did not resume physical travel");
           w.advance(3600);
           require(w.freight[c.shipment].state == FreightState::Delivered,
                   "Resumed supply delivery never reached its client");
           w.validate();
         }
       });
  test("Automatic dispatch skips a crate exposed to an active fire", [] {
    for (bool waste : {false, true}) {
      auto w = small();
      auto c = cargo(w, waste);
      auto &r = w.residents[2];
      r.role = waste ? Role::Cleaner : Role::Courier;
      r.jobSite = c.destination;
      r.employer = w.operatingAccount(c.destination);
      r.automatic = true;
      r.routine.shiftStart = 0;
      r.routine.shiftHours = 24;
      r.routine.daysOff = 0;
      r.routine.sleepStart = 22;
      r.routine.sleepHours = 7;
      r.age = 30;
      r.energy = r.hygiene = 1;
      r.hunger = r.thirst = r.bladder = .1f;
      r.personalWaste = 0;
      r.needsAt = w.time;
      place(w, 2, w.facilities[c.destination].pos + Vec3{0, 0, 2});
      w.decide(2);
      require(w.freight[c.shipment].state == FreightState::Waiting &&
                  w.freight[c.shipment].worker == none,
              "Dispatch sent a courier into a burning pickup");
      w.validate();
    }
  });
  test("An old automatic freight arrival at a newly burning site evacuates with the cargo", [] {
    for (bool waste : {false, true}) {
      auto w = small();
      auto c = cargo(w, waste);
      w.fires[c.fire].contained = true;
      w.fires[c.fire].heat = w.fires[c.fire].smoke = 0;
      tick(w);
      auto &r = w.residents[2];
      r.role = waste ? Role::Cleaner : Role::Courier;
      r.jobSite = c.destination;
      r.employer = w.operatingAccount(c.destination);
      r.routine.shiftStart = 0;
      r.routine.shiftHours = 24;
      r.routine.daysOff = 0;
      place(w, 2, w.items[c.item].pos);
      w.assignFreight(2, c.shipment);
      place(w, 2, w.items[c.item].pos);
      w.collectFreight(2, c.shipment);
      w.ignite(c.destination, 1);
      place(w, 2, w.facilities[c.destination].pos + Vec3{0, 0, 1.6f});
      r.automatic = true;
      r.task = TaskKind::FreightDrop;
      r.target = c.shipment;
      Vec3 at = w.position(2);
      int stock = w.facilities[c.destination].stock;
      w.arrive(2);
      require(w.freight[c.shipment].state == FreightState::Carrying && r.carrying == c.item &&
                  r.task == TaskKind::Evacuate && w.facilities[c.destination].stock == stock &&
                  length(w.position(2) - at) < .001f,
              "Stale transport handed stock to a fire or teleported its worker");
      w.validate();
    }
  });
  test("Older saves without smoke layers or cargo loss fields load without invented damage", [&] {
    auto w = small();
    auto c = cargo(w);
    auto path = root / "old-source.mdsave";
    w.save(path);
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
    auto size = ZSTD_getFrameContentSize(bytes.data() + 8, bytes.size() - 8);
    require(size < 100000000, "Unexpected test frame size");
    std::vector<unsigned char> raw(size);
    require(
        !ZSTD_isError(ZSTD_decompress(raw.data(), raw.size(), bytes.data() + 8, bytes.size() - 8)),
        "Test decompression failed");
    auto j = nlohmann::json::from_cbor(raw);
    for (auto &f : j["fires"])
      f.erase("smokeLayers");
    for (auto &s : j["freight"])
      s.erase("lostQuantity");
    raw = nlohmann::json::to_cbor(j);
    std::vector<char> compressed(ZSTD_compressBound(raw.size()));
    auto n = ZSTD_compress(compressed.data(), compressed.size(), raw.data(), raw.size(), 3);
    require(!ZSTD_isError(n), "Test compression failed");
    auto legacy = root / "old-frame.mdsave";
    std::ofstream out(legacy, std::ios::binary);
    out.write("MDORM001", 8);
    out.write(compressed.data(), n);
    out.close();
    auto q = World::load(legacy);
    require(q.fires[c.fire].smokeLayers.empty() && q.freight[c.shipment].lostQuantity == 0 &&
                q.accounts.size() == w.accounts.size() &&
                q.items[c.item].owner == w.items[c.item].owner,
            "Migration invented thermal damage or changed ownership");
    q.validate();
  });
  std::filesystem::remove_all(root);
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
