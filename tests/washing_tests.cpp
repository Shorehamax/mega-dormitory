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
  require(b, "Invalid action was accepted");
}
static void test(const char *n, const std::function<void()> &f) {
  try {
    f();
    ++passed;
    std::cout << "PASS " << n << '\n';
  } catch (const std::exception &e) {
    ++failed;
    std::cerr << "FAIL " << n << ": " << e.what() << '\n';
  }
}
static World small() {
  Config c;
  c.population = 20;
  c.floors = 20;
  c.mode = Mode::Cheat;
  c.accidentRate = 0;
  auto w = World::generate(c);
  for (const auto &road : w.roads)
    w.activateRoad(road.id, false);
  while (!w.events.empty())
    w.events.pop();
  for (auto &r : w.residents) {
    w.clockOut(r.id);
    w.leaveQueue(r.id);
    r.automatic = false;
    r.activity = Activity::Idle;
    r.route.clear();
    r.task = TaskKind::None;
    r.role = Role::Unemployed;
    r.pos = {150 + r.id * 2.f, 0, 150};
  }
  for (auto &f : w.facilities) {
    f.queue.clear();
    f.serving.clear();
    f.staffNeeded = 0;
    f.opensAt = 0;
    f.openHours = 24;
  }
  w.crowd.clear();
  return w;
}
static Id tool(World &w, Id id, ItemKind kind = ItemKind::Tableware, float clean = .1f) {
  Item i;
  i.id = Id(w.items.size());
  i.kind = kind;
  i.owner = w.residents[id].account;
  i.carrier = id;
  i.cleanliness = clean;
  i.durability = .6f;
  i.pos = w.position(id);
  w.items.push_back(i);
  w.residents[id].items.push_back(i.id);
  return i.id;
}
static Id basin(World &w) {
  Id f = w.nearestAt({0, 0, 0}, FacilityKind::Washbasin);
  require(f != none, "Missing basin");
  w.facilities[f].stations = 1;
  w.invalidateGeometry(w.facilities[f].floor);
  return f;
}
static void approach(World &w, Id id, Id f) {
  auto &r = w.residents[id];
  r.pos = w.accessible(w.facilities[f].pos + Vec3{0, 0, 2.4f});
  r.route.clear();
  ++r.routeVersion;
  r.activity = Activity::Idle;
  w.crowd.erase(id);
}
static void complete(World &w, Id f, Id id) {
  auto &s = w.facilities[f].serving;
  auto a = std::find_if(s.begin(), s.end(), [&](const Service &t) { return t.resident == id; });
  require(a != s.end() && a->until >= 0, "Missing active service");
  w.advance(a->until - w.time + .01, 100000);
}
static void conserved(const World &w) {
  require(std::accumulate(w.accounts.begin(), w.accounts.end(), Money(0),
                          [](Money a, const Account &b) { return a + b.balance; }) == 0,
          "Created internal money");
  w.validate();
}
int main() {
  test("Default basins are sparse, self-service and available at night", [] {
    Config cfg;
    cfg.population = 20;
    cfg.floors = 20;
    cfg.mode = Mode::Cheat;
    cfg.accidentRate = 0;
    auto w = World::generate(cfg);
    while (!w.events.empty())
      w.events.pop();
    int count = 0;
    for (auto &f : w.facilities)
      if (f.kind == FacilityKind::Washbasin) {
        ++count;
        require(f.staffNeeded == 0 && f.openHours == 24,
                "Basin requires staff or closes by default");
      }
    require(count == 2, "Unexpected initial density");
    w.time = 23 * 3600;
    require(w.operational(basin(w)), "Self service requires worker");
    conserved(w);
  });
  test("Washing reserves physical owned tools and charges owner only on completion", [] {
    auto w = small();
    Id f = basin(w), a = tool(w, 0), b = tool(w, 0, ItemKind::RiceCooker);
    approach(w, 0, f);
    auto cash = w.accounts[w.residents[0].account].balance,
         owner = w.accounts[w.operatingAccount(f)].balance;
    int stock = w.facilities[f].stock;
    w.joinQueue(0, f);
    require(w.items[a].carrier == none && w.items[a].reservedAt == f &&
                w.items[a].cleanliness == .1f && w.items[b].reservedAt == f,
            "Tools were not physically reserved");
    require(w.accounts[w.residents[0].account].balance == cash &&
                w.facilities[f].stock == stock - 1,
            "Early payment or missing supplies");
    require(w.items[a].pos.x == w.facilities[f].pos.x, "Tool not at basin");
    complete(w, f, 0);
    require(w.items[a].cleanliness == 1 && w.items[b].cleanliness == 1 &&
                w.items[a].durability == .6f && w.items[a].carrier == 0 &&
                w.items[a].reservedAt == none,
            "Washing lost or repaired items");
    require(w.accounts[w.residents[0].account].balance == cash - w.facilities[f].price &&
                w.accounts[w.operatingAccount(f)].balance == owner + w.facilities[f].price &&
                w.facilities[f].waste == 1,
            "Revenue or waste missing");
    conserved(w);
  });
  test("Cancellation returns dirty items and never equips cookware", [] {
    auto w = small();
    Id f = basin(w), a = tool(w, 0, ItemKind::RiceCooker);
    w.residents[0].outfit = none;
    approach(w, 0, f);
    auto cash = w.accounts[w.residents[0].account].balance;
    int stock = w.facilities[f].stock;
    w.joinQueue(0, f);
    w.leaveQueue(0);
    require(w.items[a].carrier == 0 && w.items[a].cleanliness == .1f &&
                w.items[a].reservedAt == none && w.residents[0].outfit == none,
            "Cancellation cleaned or equipped cooker");
    require(w.accounts[w.residents[0].account].balance == cash && w.facilities[f].stock == stock,
            "Cancellation consumed supplies or money");
    conserved(w);
  });
  test("At most four dirtiest owned washable items are reserved", [] {
    auto w = small();
    Id f = basin(w);
    std::vector<Id> dirty;
    for (int n = 0; n < 6; ++n)
      dirty.push_back(tool(w, 0, ItemKind::Tableware, .1f + n * .1f));
    Id fan = tool(w, 0, ItemKind::Fan), foreign = tool(w, 0),
       clean = tool(w, 0, ItemKind::RiceCooker, 1);
    w.items[foreign].owner = w.residents[1].account;
    approach(w, 0, f);
    w.joinQueue(0, f);
    for (int n = 0; n < 6; ++n)
      require((w.items[dirty[n]].reservedAt == f) == (n < 4), "Wrong washing batch");
    require(w.items[fan].reservedAt == none && w.items[foreign].reservedAt == none &&
                w.items[clean].reservedAt == none,
            "Washed electronics or someone else's property");
    complete(w, f, 0);
    conserved(w);
  });
  test("FIFO station reuse returns first customer's items without charging", [] {
    auto w = small();
    Id f = basin(w), a = tool(w, 0), b = tool(w, 1);
    approach(w, 0, f);
    approach(w, 1, f);
    auto cash = w.accounts[w.residents[0].account].balance;
    w.joinQueue(0, f);
    w.joinQueue(1, f);
    require(w.facilities[f].queue.front() == 1 && w.items[b].carrier == 1,
            "Waiting customer lost items");
    w.leaveQueue(0);
    require(w.facilities[f].serving.front().resident == 1 && w.items[a].carrier == 0,
            "Station not reused in order");
    complete(w, f, 1);
    require(w.items[b].cleanliness == 1 && w.accounts[w.residents[0].account].balance == cash,
            "Cancellation charged wrong resident");
    conserved(w);
  });
  test("Supply outage, access policy and unaffordable visits cannot clean remotely", [] {
    auto w = small();
    Id f = basin(w), a = tool(w, 0);
    rejects([&] { w.joinQueue(0, f); });
    approach(w, 0, f);
    w.facilities[f].stock = 0;
    w.joinQueue(0, f);
    require(w.facilities[f].serving.empty() && w.items[a].carrier == 0,
            "No-supply basin began washing");
    w.leaveQueue(0);
    w.facilities[f].stock = 10;
    w.facilities[f].publicAccess = false;
    rejects([&] { w.joinQueue(0, f); });
    w.facilities[f].publicAccess = true;
    auto account = w.residents[0].account;
    w.pay(account, 0, w.accounts[account].balance, "test-outflow");
    rejects([&] { w.joinQueue(0, f); });
    require(w.items[a].cleanliness == .1f, "Failed access cleaned item");
    conserved(w);
  });
  test("An unavailable basin pauses and resumes saved reservations", [] {
    auto w = small();
    Id f = basin(w), a = tool(w, 0);
    approach(w, 0, f);
    w.joinQueue(0, f);
    w.facilities[f].open = false;
    w.serve(f);
    require(w.facilities[f].serving[0].until < 0 && w.items[a].reservedAt == f,
            "Closed basin lost work");
    auto path = std::filesystem::temp_directory_path() / "mega-washing-persist.mdsave";
    w.save(path);
    auto v = World::load(path);
    v.advance(500, 100000);
    require(v.items[a].cleanliness == .1f && v.items[a].reservedAt == f,
            "Paused save completed washing");
    v.facilities[f].open = true;
    v.serve(f);
    complete(v, f, 0);
    require(v.items[a].cleanliness == 1 && v.items[a].carrier == 0,
            "Resumed wash did not return item");
    conserved(v);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".meta.json");
  });
  test("Payment failure and death return unwashed belongings", [] {
    auto w = small();
    Id f = basin(w), a = tool(w, 0);
    approach(w, 0, f);
    w.joinQueue(0, f);
    auto account = w.residents[0].account;
    w.pay(account, 0, w.accounts[account].balance, "test-outflow");
    complete(w, f, 0);
    require(w.items[a].carrier == 0 && w.items[a].cleanliness == .1f &&
                w.items[a].reservedAt == none,
            "Insolvent customer received cleaning");
    w.pay(0, account, 1000, "test-inflow");
    approach(w, 0, f);
    w.joinQueue(0, f);
    w.kill(0);
    require(w.items[a].carrier == none && w.items[a].reservedAt == none &&
                w.items[a].cleanliness == .1f,
            "Dead resident retained sink reservation");
    conserved(w);
  });
  test("Reserved tools cannot be sold, stolen or used and dirty tools require cleaning", [] {
    auto w = small();
    Id f = basin(w), a = tool(w, 0);
    approach(w, 0, f);
    approach(w, 1, f);
    w.joinQueue(0, f);
    rejects([&] { w.listItem(0, a, 100); });
    rejects([&] { w.stealItem(1, 0, a, true); });
    rejects([&] { w.usePersonalItem(0, a); });
    w.leaveQueue(0);
    auto home = w.residentHomes[0];
    require(home != none, "No test home");
    w.residents[0].pos = w.homePosition(home);
    rejects([&] { w.usePersonalItem(0, a); });
    require(w.items[a].cleanliness == .1f, "Dirty tool bypassed washing");
    conserved(w);
  });
  test("Autonomous residents walk to washbasins during free time and prioritize thirst", [] {
    auto w = small();
    Id f = basin(w);
    tool(w, 0);
    approach(w, 0, f);
    auto &r = w.residents[0];
    r.automatic = true;
    r.hunger = r.thirst = r.bladder = 0;
    r.energy = r.hygiene = r.happiness = 1;
    r.personalWaste = 0;
    w.time = 10 * 3600;
    r.needsAt = w.time;
    w.decide(0);
    require(r.task == TaskKind::Service && r.target == f && !r.route.empty(),
            "Resident did not really walk to wash");
    r.route.clear();
    r.activity = Activity::Idle;
    r.task = TaskKind::None;
    r.thirst = 1.1f;
    r.needsAt = w.time;
    w.decide(0);
    require(r.task == TaskKind::Service && w.facilities[r.target].kind == FacilityKind::Water,
            "Washing overrode urgent thirst");
    conserved(w);
  });
  test("New washbasin construction has funded ownership and remains closed until built", [] {
    auto w = small();
    rejects([&] { w.commissionFacility(0, FacilityKind::Washbasin, 1, {80, 3, 80}, 2); });
    w.pay(0, w.residents[0].account, 1000000, "test-construction-capital");
    Id p = w.commissionFacility(0, FacilityKind::Washbasin, 1, {80, 3, 80}, 2);
    auto &build = w.projects[p];
    auto &f = w.facilities[build.facility];
    require(f.owner == w.residents[0].account && !f.open && f.staffNeeded == 0 &&
                f.openHours == 24 && w.accounts[build.escrow].balance == build.budget,
            "Washbasin construction bypassed funding or owner");
    auto &worker = w.residents[2];
    worker.role = Role::Builder;
    worker.jobSite = w.nearest(2, FacilityKind::Workshop);
    worker.routine.shiftStart = 0;
    worker.routine.shiftHours = 24;
    worker.routine.daysOff = 0;
    w.assignBuilder(p, 2);
    rejects([&] { w.deliverMaterials(2, p); });
    worker.pos = w.facilities[build.source].pos + Vec3{0, 0, 1.6f};
    worker.route.clear();
    ++worker.routeVersion;
    w.collectMaterials(2, p);
    worker.pos = w.facilities[build.facility].pos + Vec3{0, 0, 1.6f};
    worker.route.clear();
    ++worker.routeVersion;
    w.deliverMaterials(2, p);
    for (int n = 0; n < 12 && w.projects[p].state != ProjectState::Completed; ++n) {
      w.time += 3600;
      w.workProject(p, 3600);
    }
    require(w.facilities[build.facility].open && w.projects[p].state == ProjectState::Completed,
            "Supplied construction did not open the new washbasin");
    while (!w.events.empty())
      w.events.pop();
    Id item = tool(w, 1);
    approach(w, 1, build.facility);
    w.joinQueue(1, build.facility);
    complete(w, build.facility, 1);
    require(w.items[item].cleanliness == 1, "Constructed washbasin was not usable");
    conserved(w);
  });
  test("Hand washing helps hygiene without replacing a shower", [] {
    auto w = small();
    Id f = basin(w);
    approach(w, 0, f);
    w.residents[0].hygiene = .7f;
    w.residents[0].needsAt = w.time;
    w.joinQueue(0, f);
    complete(w, f, 0);
    require(w.residents[0].hygiene <= .8f && w.residents[0].hygiene > .7f,
            "Hand wash replaced shower");
    conserved(w);
  });
  test("Sixteen washing stations have reachable standing space and solid basins", [] {
    auto w = small();
    Id f = basin(w);
    w.facilities[f].stations = 16;
    w.invalidateGeometry(w.facilities[f].floor);
    for (int n = 0; n < 16; ++n) {
      auto standing = w.servicePosition(f, n);
      require(w.clearPosition(standing), "Another basin blocks a washing station");
      require(!w.clearPosition(standing + Vec3{0, 0, -.65f}), "Residents can walk through a basin");
      tool(w, n);
      approach(w, n, f);
      w.joinQueue(n, f);
    }
    approach(w, 16, f);
    w.joinQueue(16, f);
    require(w.facilities[f].serving.size() == 16 && w.facilities[f].queue.size() == 1 &&
                w.clearPosition(w.residents[16].route.back().p),
            "Full bank has no safe queue");
    conserved(w);
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
