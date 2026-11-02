#include "mega/world.hpp"
#include <filesystem>
#include <functional>
#include <iostream>
using namespace mega;
int passed = 0, failed = 0;
void require(bool yes, const char *message) {
  if (!yes)
    throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "Invalid queue action succeeded");
}
void test(const char *name, std::function<void()> fn) {
  try {
    fn();
    ++passed;
    std::cout << "PASS " << name << '\n';
  } catch (const std::exception &e) {
    ++failed;
    std::cout << "FAIL " << name << ": " << e.what() << '\n';
  }
}
World small() {
  Config c;
  c.population = 80;
  c.floors = 20;
  c.mode = Mode::Cheat;
  c.accidentRate = 0;
  auto w = World::generate(c);
  while (!w.events.empty())
    w.events.pop();
  for (auto &r : w.residents) {
    w.clockOut(r.id);
    w.leaveQueue(r.id);
    r.automatic = false;
    r.route.clear();
    r.task = TaskKind::None;
    r.activity = Activity::Idle;
    r.pos = {100 + r.id * 1.f, 57, 100};
    r.role = Role::Unemployed;
    r.jobSite = r.employer = none;
    r.age = 30;
    r.routine.sleepStart = 22;
    r.routine.sleepHours = 7;
    r.hunger = r.thirst = r.bladder = .1f;
    r.energy = r.hygiene = r.happiness = 1;
    r.needsAt = w.time;
  }
  w.crowd.clear();
  for (auto &f : w.facilities) {
    f.queue.clear();
    f.serving.clear();
    f.staffNeeded = 0;
    f.employees.clear();
    f.opensAt = 0;
    f.openHours = 24;
  }
  return w;
}
Id site(World &w, FacilityKind kind = FacilityKind::Water) {
  for (auto &f : w.facilities)
    if (f.floor == 0 && f.kind == kind) {
      f.stations = 1;
      f.duration = 86400;
      f.stock = 1000;
      return f.id;
    }
  throw std::runtime_error("Missing fixture service");
}
void place(World &w, Id who, Vec3 p) {
  auto &r = w.residents[who];
  r.pos = p;
  r.route.clear();
  ++r.routeVersion;
  w.crowd.erase(who);
}
void settleInitialQueue(World &w, Id fid) {
  auto &f = w.facilities[fid];
  auto slots = w.queueSlots(fid, f.queue.size());
  for (std::size_t i = 0; i < f.queue.size(); ++i)
    place(w, f.queue[i], slots[i]);
  for (auto &s : f.serving)
    place(w, s.resident, w.servicePosition(fid, s.station));
}
void line(World &w, Id fid, int count) {
  for (Id id = 0; id < count; ++id) {
    place(w, id, w.facilities[fid].pos + Vec3{0, 0, 1.6f});
    w.joinQueue(id, fid);
  }
  settleInitialQueue(w, fid);
}
void tick(World &w, float real = .02f) {
  double game = real * (1440 / w.config.dayMinutes);
  w.moveCrowd(game);
  w.advance(game);
}
int decisions(const World &w, Id person) {
  auto es = w.events;
  int n = 0;
  while (!es.empty()) {
    auto e = es.top();
    es.pop();
    n += e.kind == EventKind::Decide && e.id == person;
  }
  return n;
}
int main() {
  test("Residents join the actual tail of a long line without pushing to the counter", [] {
    auto w = small();
    Id f = site(w);
    line(w, f, 25);
    auto tail = w.serviceApproach(f);
    require(length(tail - w.facilities[f].pos) > 3, "Fixture line was not long");
    place(w, 40, tail + Vec3{0, 0, .9f});
    Vec3 before = w.position(40);
    Money cash = w.internalMoney();
    w.joinQueue(40, f);
    require(w.facilities[f].queue.back() == 40 && w.residents[40].queuedAt == f,
            "Tail admission lost FIFO");
    require(length(w.position(40) - before) < .001f && w.internalMoney() == cash,
            "Joining teleported or charged the resident");
    w.validate();
  });
  test("A distant resident cannot remotely reserve a queue position", [] {
    auto w = small();
    Id f = site(w);
    line(w, f, 20);
    place(w, 40, w.serviceApproach(f) + Vec3{0, 0, 10});
    rejects([&] { w.joinQueue(40, f); });
    require(w.residents[40].queuedAt == none, "Failed admission reserved a place");
  });
  test("A partition prevents joining through a wall at the tail", [] {
    auto w = small();
    Id f = site(w);
    line(w, f, 20);
    auto tail = w.serviceApproach(f);
    InteriorWall wall;
    wall.id = Id(w.interiorWalls.size());
    wall.owner = w.dormAccount;
    wall.floor = 0;
    wall.a = tail + Vec3{-2, 0, .4f};
    wall.b = tail + Vec3{2, 0, .4f};
    wall.door = false;
    wall.active = true;
    w.interiorWalls.push_back(wall);
    w.invalidateGeometry(0);
    place(w, 40, tail + Vec3{0, 0, 1});
    rejects([&] { w.joinQueue(40, f); });
    require(w.residents[40].queuedAt == none, "Wall admission mutated membership");
    w.validate();
  });
  test("A walker follows a changed queue tail and keeps real collision", [] {
    auto w = small();
    Id f = site(w);
    line(w, f, 8);
    place(w, 40, w.facilities[f].pos + Vec3{-8, 0, 15});
    w.travel(40, w.facilities[f].pos, TaskKind::Service, f);
    auto old = w.residents[40].route.back().p;
    for (Id id = 8; id < 36; ++id) {
      place(w, id, w.facilities[f].pos + Vec3{0, 0, 1.6f});
      w.joinQueue(id, f);
    }
    settleInitialQueue(w, f);
    require(length(w.serviceApproach(f) - old) > 3, "Fixture tail did not move");
    w.focus(0);
    Vec3 prior = w.position(40);
    for (int n = 0; n < 3000 && w.residents[40].queuedAt != f; ++n) {
      tick(w);
      auto p = w.position(40);
      require(length(p - prior) < .08f, "Tail following teleported the walker");
      for (Id other : w.facilities[f].queue)
        if (other != 40)
          require(length(p - w.position(other)) >= .575f, "Tail following disabled body collision");
      prior = p;
    }
    require(w.residents[40].queuedAt == f && w.facilities[f].queue.back() == 40,
            "Walker failed to find the moved tail");
    w.validate();
  });
  test("A customer leaving service resumes their actual shift and no extra decision chain", [] {
    auto w = small();
    Id f = site(w, FacilityKind::Food);
    auto &r = w.residents[40];
    Id shop = none;
    for (auto &g : w.facilities)
      if (g.floor == 0 && g.kind == FacilityKind::Dealership)
        shop = g.id;
    w.facilities[shop].staffNeeded = 1;
    w.chooseJob(40, Role::Vendor, shop);
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    r.routine.daysOff = 0;
    r.automatic = true;
    r.hunger = .45f;
    w.residentPlans[40].nextShopping = w.residentPlans[40].nextCareer =
        w.residentPlans[40].nextBusiness = w.time + 86400;
    w.schedule(w.time + 10000, EventKind::Decide, 40);
    place(w, 40, w.facilities[f].pos + Vec3{0, 0, 1.6f});
    w.facilities[f].duration = 20;
    w.joinQueue(40, f);
    auto until = w.facilities[f].serving[0].until;
    auto price = w.facilities[f].price;
    auto cash = w.accounts[r.account].balance;
    w.advance(until - w.time + .01);
    require(r.task == TaskKind::Work && r.target == shop,
            "Satisfied worker did not return to work");
    require(decisions(w, 40) == 1, "Service spawned a duplicate decision chain");
    require(w.accounts[r.account].balance == cash - price, "Meal was not paid exactly once");
    w.validate();
  });
  test("Idle customers use separate clear waiting destinations", [] {
    auto w = small();
    Id f = site(w);
    for (Id id = 40; id < 56; ++id) {
      auto &r = w.residents[id];
      place(w, id, w.facilities[f].pos + Vec3{0, 0, 1.6f});
      r.automatic = true;
      w.residentPlans[id].nextShopping = w.residentPlans[id].nextCareer =
          w.residentPlans[id].nextBusiness = w.time + 86400;
      w.clearServiceArea(id, f);
      require(r.task == TaskKind::LeaveService, "Customer failed to leave the counter");
      for (Id old = 40; old < id; ++old)
        require(length(r.route.back().p - w.residents[old].route.back().p) > .1f,
                "Customers share one exit destination");
      require(length(w.position(id) - (w.facilities[f].pos + Vec3{0, 0, 1.6f})) < .001f,
              "Departure teleported a customer");
    }
    w.validate();
  });
  test("Old saved common-exit routes can resume purposeful decisions", [] {
    auto w = small();
    Id f = site(w);
    auto &r = w.residents[40];
    place(w, 40, w.facilities[f].pos + Vec3{3, 0, 2});
    r.automatic = true;
    w.residentPlans[40].nextShopping = w.residentPlans[40].nextCareer =
        w.residentPlans[40].nextBusiness = w.time + 86400;
    w.travel(40, w.facilities[f].pos + Vec3{5, 0, 1.5f}, TaskKind::None, none);
    Vec3 before = w.position(40);
    w.decide(40, false);
    require(r.task == TaskKind::LeaveService &&
                length(r.route.back().p - (w.facilities[f].pos + Vec3{5, 0, 1.5f})) > 1,
            "Legacy exit was not redirected");
    require(length(w.position(40) - before) < .001f, "Legacy recovery teleported the resident");
    w.validate();
  });
  test("Manual player destinations are preserved", [] {
    auto w = small();
    Id f = site(w);
    place(w, 40, w.facilities[f].pos + Vec3{3, 0, 2});
    w.travel(40, w.facilities[f].pos + Vec3{5, 0, 1.5f}, TaskKind::None, none);
    auto goal = w.residents[40].route.back().p;
    w.decide(40, false);
    require(w.residents[40].task == TaskKind::None &&
                length(w.residents[40].route.back().p - goal) < .001f,
            "AI overwrote a manually controlled walk");
  });
  test("A saved departure retains its purpose and exact physical position", [] {
    auto w = small();
    Id f = site(w);
    auto &r = w.residents[40];
    r.automatic = true;
    place(w, 40, w.facilities[f].pos + Vec3{0, 0, 1.6f});
    w.clearServiceArea(40, f);
    w.focus(0);
    tick(w);
    Vec3 actual = w.position(40);
    auto file = std::filesystem::temp_directory_path() / "mega-queue-departure.mdsave";
    w.save(file);
    auto v = World::load(file);
    require(v.residents[40].task == TaskKind::LeaveService && v.residents[40].target == f &&
                length(v.position(40) - actual) < .001f,
            "Saved departure lost purpose or position");
    v.validate();
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".meta.json");
  });
  test("Changed funds, hours or access during a service trip replan without false interruption",
       [] {
         for (int change = 0; change < 3; ++change) {
           auto w = small();
           Id f = site(w);
           for (auto &other : w.facilities)
             if (other.kind == w.facilities[f].kind && other.id != f)
               other.open = false;
           auto &r = w.residents[40];
           r.automatic = true;
           w.facilities[f].price = 100;
           place(w, 40, w.facilities[f].pos + Vec3{0, 0, 20});
           w.travel(40, w.facilities[f].pos, TaskKind::Service, f);
           place(w, 40, w.serviceApproach(f));
           if (change == 0)
             w.pay(r.account, 0, w.accounts[r.account].balance, "test-funds-changed");
           else if (change == 1) {
             w.facilities[f].opensAt = 12;
             w.facilities[f].openHours = 1;
           } else {
             w.facilities[f].publicAccess = false;
             w.facilities[f].whitelist.erase(40);
             require(!w.accessAllowed(40, f), "Fixture resident still has private access");
           }
           auto sequence = w.newsSequence;
           Money cash = w.accounts[r.account].balance;
           w.arrive(40);
           require(r.queuedAt == none && w.facilities[f].queue.empty() &&
                       w.facilities[f].serving.empty() && w.accounts[r.account].balance == cash &&
                       w.newsSequence == sequence,
                   "Unavailable arrival charged, reserved a station or logged a blocked action");
           rejects([&] { w.joinQueue(40, f); });
           w.validate();
         }
       });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
