#include "mega/world.hpp"
#include <functional>
#include <iostream>
using namespace mega;
int passed = 0, failed = 0;
void require(bool yes, const char *why) {
  if (!yes)
    throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
  bool no = false;
  try {
    fn();
  } catch (const std::exception &) {
    no = true;
  }
  require(no, "Invalid action accepted");
}
void test(const char *name, std::function<void()> fn) {
  try {
    fn();
    ++passed;
    std::cout << "PASS " << name << '\n';
  } catch (const std::exception &e) {
    ++failed;
    std::cerr << "FAIL " << name << ": " << e.what() << '\n';
  }
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
  for (auto &r : w.residents) {
    w.clockOut(r.id);
    w.leaveQueue(r.id);
    r.automatic = false;
    r.route.clear();
    r.pos = {100 + r.id * 2.f, 57, 100};
    r.activity = Activity::Idle;
    r.task = TaskKind::None;
    r.role = Role::Unemployed;
    r.jobSite = r.employer = none;
    r.energy = r.hygiene = 1;
    r.hunger = r.thirst = .1f;
    r.needsAt = w.time;
  }
  w.crowd.clear();
  for (auto &f : w.facilities) {
    f.employees.clear();
    f.manager = none;
    f.staffNeeded = 0;
    f.queue.clear();
    f.serving.clear();
  }
  return w;
}
std::pair<Id, Id> sites(World &w) {
  Id source = w.nearestAt({0, 0, 0}, FacilityKind::Food),
     target = w.nearestAt({0, 0, 0}, FacilityKind::Shop);
  auto &a = w.facilities[source], &b = w.facilities[target];
  std::erase(w.floorFacilities[a.floor], source);
  a.floor = 0;
  w.floorFacilities[0].push_back(source);
  a.pos = {150, 0, 150};
  a.stock = 1000;
  std::erase(w.floorFacilities[b.floor], target);
  b.floor = 0;
  b.pos = {155, 0, 150};
  b.stock = 1000;
  w.floorFacilities[0].push_back(target);
  w.invalidateGeometry(0);
  return {source, target};
}
void ticks(World &w, int n) {
  for (int i = 0; i < n; ++i) {
    w.time += 60;
    w.emergencyTick();
  }
}
Id dropped(World &w, ItemKind kind, Vec3 at, float durability = 1) {
  Item item;
  item.id = Id(w.items.size());
  item.owner = w.residents[0].account;
  item.kind = kind;
  item.pos = at;
  item.durability = durability;
  w.items.push_back(item);
  return item.id;
}
int main() {
  test("Strong fire spreads only after accumulated exposure at an actual nearby business", [] {
    auto w = small();
    auto [a, b] = sites(w);
    w.ignite(a, 1);
    ticks(w, 3);
    require(!w.activeFires.contains(b), "Fire spread instantly");
    require(!w.fires[0].spreadExposure.empty(), "Neighbor did not accumulate heat");
    ticks(w, 15);
    require(w.activeFires.contains(b), "Nearby combustible business never ignited");
    w.validate();
  });
  test("A partition blocks direct heat and fire spread", [] {
    auto w = small();
    auto [a, b] = sites(w);
    w.interiorWalls.push_back({0, w.dormAccount, 0, {152, 0, 145}, {152, 0, 155}, false, true});
    w.invalidateGeometry(0);
    Id item = dropped(w, ItemKind::Phone, {155, 0, 150});
    w.ignite(a, 1);
    ticks(w, 30);
    require(!w.activeFires.contains(b) && w.items[item].durability == 1,
            "Fire or direct heat crossed solid wall");
    w.validate();
  });
  test("A wet utility does not become a combustible spreading target", [] {
    auto w = small();
    auto [a, b] = sites(w);
    w.facilities[b].kind = FacilityKind::Water;
    w.ignite(a, 1);
    ticks(w, 30);
    require(!w.activeFires.contains(b), "Wet utility became a spread target");
    w.validate();
  });
  test("Containment clears accumulated spread exposure", [] {
    auto w = small();
    auto [a, b] = sites(w);
    Id fire = w.ignite(a, 1);
    ticks(w, 3);
    require(w.fires[fire].spreadExposure.contains(b), "No pre-containment exposure");
    w.fires[fire].contained = true;
    ticks(w, 20);
    require(!w.activeFires.contains(b) && w.fires[fire].spreadExposure.empty(),
            "Contained fire kept spreading");
    w.validate();
  });
  test("Closed premises still burn when exposed", [] {
    auto w = small();
    auto [a, b] = sites(w);
    w.facilities[b].open = false;
    w.ignite(a, 1);
    ticks(w, 20);
    require(w.activeFires.contains(b), "Closing a business made it fireproof");
    w.validate();
  });
  test("Physical belongings lose condition but keep title and cannot be eaten when ruined", [] {
    auto w = small();
    auto [a, b] = sites(w);
    Id food = dropped(w, ItemKind::Food, {151, 0, 150}, .01f);
    Id phone = dropped(w, ItemKind::Phone, {151, 0, 150}, .01f);
    Id remote = dropped(w, ItemKind::Phone, {151, 3, 150}, .01f);
    Id owner = w.items[food].owner;
    w.ignite(a, 1);
    ticks(w, 1);
    require(w.items[food].durability == 0 && w.items[phone].durability == 0 &&
                w.items[remote].durability == .01f && w.items[food].owner == owner,
            "Fire ignored position, condition or ownership");
    w.items[food].carrier = 0;
    w.residents[0].items.push_back(food);
    rejects([&] { w.consumeItem(0, food); });
    w.validate();
  });
  test("Carried possessions use the carrier's actual location, rather than stale item coordinates",
       [] {
         auto w = small();
         auto [a, b] = sites(w);
         Id item = dropped(w, ItemKind::Clothes, {155, 0, 150}, .2f);
         w.items[item].carrier = 0;
         w.residents[0].items.push_back(item);
         w.ignite(a, 1);
         ticks(w, 1);
         require(w.items[item].durability == .2f,
                 "Remote carrier's item burned at stale coordinates");
         w.residents[0].pos = {151, 0, 150};
         ticks(w, 1);
         require(w.items[item].durability < .2f, "Carried item did not burn at actual position");
         w.validate();
       });
  test("Accumulated spread exposure survives save and resumes once", [] {
    auto w = small();
    auto [a, b] = sites(w);
    w.ignite(a, 1);
    ticks(w, 3);
    auto exposure = w.fires[0].spreadExposure.at(b);
    auto path = std::filesystem::temp_directory_path() / "mega-fire-spread.mdsave";
    w.save(path);
    auto v = World::load(path);
    require(v.fires[0].spreadExposure.at(b) == exposure, "Saved thermal exposure was lost");
    ticks(v, 15);
    require(v.activeFires.contains(b), "Reloaded fire did not spread");
    int count = 0;
    for (const auto &f : v.fires)
      count += f.facility == b;
    require(count == 1, "Target ignited more than once");
    v.validate();
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".meta.json");
  });
  test("Malformed fire spread references and unbounded exposure are rejected", [] {
    auto w = small();
    auto [a, b] = sites(w);
    Id fire = w.ignite(a, 1);
    w.fires[fire].spreadExposure[999999] = 1;
    rejects([&] { w.validate(); });
    w.fires[fire].spreadExposure.clear();
    w.fires[fire].spreadExposure[b] = 301;
    rejects([&] { w.validate(); });
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
