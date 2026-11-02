#include "mega/world.hpp"
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
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
  bool rejected = false;
  try {
    f();
  } catch (...) {
    rejected = true;
  }
  require(rejected, "Invalid interaction accepted");
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
  for (auto &r : w.residents) {
    w.clockOut(r.id);
    w.leaveQueue(r.id);
    r.automatic = false;
    r.age = 30;
    place(w, r.id, {100 + r.id * 2.f, 0, 100});
  }
  return w;
}
int main() {
  auto root = std::filesystem::temp_directory_path() /
              ("mega-feedback-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  test("Repeated identical paused events receive different presentation IDs", [] {
    auto w = small();
    w.log("test.event", 0, 1);
    auto a = w.news.back();
    w.log("test.event", 0, 1);
    auto b = w.news.back();
    require(a.time == b.time && a.key == b.key && a.serial > 0 && b.serial > a.serial,
            "Paused actions could not be distinguished");
  });
  test("Journal trimming never reuses IDs", [] {
    auto w = small();
    auto first = w.newsSequence;
    for (int i = 0; i < 2400; ++i)
      w.log("test.event", 0, 1);
    std::uint64_t previous = 0;
    for (const auto &n : w.news) {
      require(n.serial > previous, "Trimmed IDs reused/reordered");
      previous = n.serial;
    }
    require(w.news.size() <= 2000 && w.newsSequence == first + 2400, "Journal cap/counter changed");
  });
  test("Body appearance is a death-time snapshot after inheritance", [&] {
    auto w = small();
    Id outfit = w.residents[2].outfit;
    require(outfit != none, "No generated clothing");
    w.customizeClothes(2, outfit, {3, 4, 2, 1});
    w.nominatePersonalHeir(2, 3);
    w.kill(2);
    require(w.remains.back().look.primary == 3 && w.remains.back().look.style == 2,
            "Death did not capture clothing");
    w.time += 86400;
    w.acceptEstate(3, Id(w.estates.size() - 1));
    place(w, 3, w.position(2) + Vec3{.8f, 0, 0});
    w.collectBelonging(3, outfit);
    w.customizeClothes(3, outfit, {1, 2, 0, 0});
    require(w.remains.back().look.primary == 3, "Heir recolored the deceased remotely");
    auto file = root / "body.mdsave";
    w.save(file);
    auto q = World::load(file);
    require(q.remains.back().look.primary == 3 && q.remains.back().look.secondary == 4,
            "Body appearance was lost on reload");
    auto serial = q.newsSequence;
    q.log("test.after_load", 3, 2);
    require(q.news.back().serial > serial, "Loaded journal replayed old IDs");
    q.validate();
  });
  test("Invalid corpse styling is rejected as saved-state corruption", [] {
    auto w = small();
    w.kill(2);
    w.remains.back().look.primary = 9;
    rejects([&] { w.validate(); });
  });
  test("Pickup approach leaves room beside the object", [] {
    auto w = small();
    Id item = w.residents[0].items.front();
    w.items[item].carrier = none;
    w.items[item].pos = {110, 0, 100};
    Vec3 target = w.belongingApproach(0, item);
    require(length(target - w.items[item].pos) > 1 && length(target - w.items[item].pos) < 1.8f,
            "Pickup route targets the body/object center");
    w.travel(0, target, TaskKind::BelongingCollect, item);
    w.advance(1200);
    require(w.items[item].carrier == 0 && length(w.position(0) - w.items[item].pos) < .01f,
            "Approach did not complete physical pickup");
    w.validate();
  });
  test("Low reach does not collect property through a wall", [] {
    auto w = small();
    Id item = w.residents[0].items.front();
    w.items[item].carrier = none;
    w.items[item].pos = {110.5f, 0, 100};
    place(w, 0, {109.2f, 0, 100});
    w.interiorWalls.push_back({0, w.dormAccount, 0, {110, 0, 98}, {110, 0, 102}, false, true});
    w.invalidateGeometry(0);
    rejects([&] { w.collectBelonging(0, item); });
    auto target = w.belongingApproach(0, item);
    w.travel(0, target, TaskKind::BelongingCollect, item);
    w.advance(2400);
    require(w.items[item].carrier == 0 && w.position(0).x > 110,
            "Resident did not walk around the wall before collecting");
    w.validate();
  });
  std::filesystem::remove_all(root);
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
