#include "mega/world.hpp"
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
using namespace mega;
static int passed = 0, failed = 0;
static void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> static void rejects(F f) {
  bool bad = false;
  try {
    f();
  } catch (const std::exception &) {
    bad = true;
  }
  require(bad, "Invalid action accepted");
}
static void test(const char *name, const std::function<void()> &fn) {
  try {
    fn();
    ++passed;
    std::cout << "PASS " << name << '\n';
  } catch (const std::exception &e) {
    ++failed;
    std::cerr << "FAIL " << name << ": " << e.what() << '\n';
  }
}
static World small() {
  Config c;
  c.population = 20;
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
    r.activity = Activity::Idle;
    r.route.clear();
    r.task = TaskKind::None;
    r.role = Role::Unemployed;
    r.pos = {100 + r.id * 2.f, 0, 100};
  }
  w.crowd.clear();
  return w;
}
static void place(World &w, Id id, Vec3 p) {
  w.residents[id].pos = p;
  w.residents[id].health = 100;
  w.residents[id].energy = 1;
  w.residents[id].aggression = .1f;
}
static void advance(World &w, double seconds) {
  w.advance(seconds * (1440 / w.config.dayMinutes), 1000000);
}
static bool crime(const World &w, CrimeKind k, Id a, Id v) {
  for (auto &c : w.incidents)
    if (c.kind == k && c.offender == a && c.victim == v)
      return true;
  return false;
}
int main() {
  test("Jump survives save and lands without a camera controller", [] {
    auto w = small();
    place(w, 0, {50, 0, 50});
    auto b = w.stepBody(0, {}, 1.f / 60, true);
    require(b.position.y > 0 && w.looseBodies.contains(0), "Jump did not start");
    auto path = std::filesystem::temp_directory_path() / "mega-physics-jump.mdsave";
    w.save(path);
    auto v = World::load(path);
    require(v.physicsController == none && v.looseBodies.at(0).velocity.y > 0,
            "Momentum was not restored");
    advance(v, 2);
    require(v.looseBodies.empty() && v.nextPhysicsAt == -1 && v.position(0).y == 0 &&
                v.residents[0].health == 100,
            "Jump failed to land");
    v.validate();
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".meta.json");
  });
  test("Shaft fall damages and lands while observed", [] {
    auto w = small();
    place(w, 0, {0, 3.5f, 0});
    w.stepBody(0, {}, .02f);
    advance(w, 2);
    require(w.position(0).y == 0 && w.residents[0].health < 100 && w.residents[0].alive &&
                w.looseBodies.empty(),
            "Fall did not resolve");
    w.validate();
  });
  test("Manual controller prevents double integration and releasing it resumes gravity", [] {
    auto w = small();
    place(w, 0, {0, 9.5f, 0});
    w.stepBody(0, {}, .02f);
    w.physicsController = 0;
    float before = w.position(0).y;
    advance(w, .3);
    require(w.position(0).y == before, "Manual body integrated twice");
    w.physicsController = none;
    advance(w, .2);
    require(w.position(0).y < before, "Detached body remained suspended");
    w.validate();
  });
  test("Airborne residents cannot route or join facilities", [] {
    auto w = small();
    place(w, 0, {0, 4, 0});
    w.stepBody(0, {}, .02f);
    rejects([&] { w.travel(0, {40, 0, 40}, TaskKind::None, none); });
    rejects([&] { w.joinQueue(0, 0); });
    w.residents[0].automatic = true;
    w.decide(0);
    require(w.residents[0].route.empty(), "AI routed an airborne body");
    w.validate();
  });
  test("Death cancels physics and queued ticks cannot revive it", [] {
    auto w = small();
    place(w, 0, {0, 4, 0});
    w.stepBody(0, {}, .02f);
    w.kill(0);
    advance(w, 1);
    require(w.looseBodies.empty() && w.nextPhysicsAt == -1 && !w.residents[0].alive,
            "Death retained physics");
    w.validate();
  });
  test("Invalid physics state is rejected", [] {
    auto w = small();
    place(w, 0, {0, 4, 0});
    w.stepBody(0, {}, .02f);
    auto valid = w;
    w.looseBodies[0].velocity.x = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { w.validate(); });
    w = valid;
    w.looseBodies[0].pusher = 99999;
    rejects([&] { w.validate(); });
    w = valid;
    w.looseBodies[0].updated = w.time + 1;
    rejects([&] { w.validate(); });
    w = valid;
    while (!w.events.empty())
      w.events.pop();
    rejects([&] { w.validate(); });
  });
  test("Shove produces smooth displacement, costs stamina and reports assault", [] {
    auto w = small();
    place(w, 0, {50, 0, 50});
    place(w, 1, {50, 0, 48.5f});
    w.residents[0].strength = 100;
    w.residents[1].strength = 10;
    w.shove(0, 1);
    require(w.position(1).z == 48.5f && w.looseBodies.contains(1), "Shove teleported its victim");
    require(w.residents[0].energy < 1 && crime(w, CrimeKind::Assault, 0, 1),
            "Shove had no cost or consequences");
    advance(w, .2);
    require(w.position(1).z < 48.4f && w.position(1).y > 0, "Impulse did not move victim");
    rejects([&] { w.shove(0, 1); });
    advance(w, 1);
    require(w.looseBodies.empty() && w.residents[1].alive, "Safe shove never landed");
    w.validate();
  });
  test("Fatal shaft shove retains offender across reload", [] {
    auto w = small();
    place(w, 0, {0, 30, 22});
    place(w, 1, {0, 30, 20.5f});
    w.residents[0].strength = 100;
    w.residents[1].strength = 10;
    w.shove(0, 1);
    auto path = std::filesystem::temp_directory_path() / "mega-physics-shove.mdsave";
    w.save(path);
    auto v = World::load(path);
    advance(v, 5);
    require(!v.residents[1].alive && crime(v, CrimeKind::Murder, 0, 1),
            "Fatal shove lost culpability");
    require(v.remains.back().pos.y == 0 && !v.remains.back().lost, "Reachable body disappeared");
    v.validate();
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".meta.json");
  });
  test("Lethal threat defense stays justified through the fall", [] {
    auto w = small();
    place(w, 0, {0, 30, 22});
    place(w, 1, {0, 30, 20.5f});
    w.residents[0].strength = 100;
    w.residents[1].strength = 10;
    w.residents[1].opponent = 0;
    w.residents[1].activity = Activity::Fighting;
    w.residents[1].lethalThreat = true;
    w.shove(0, 1);
    require(w.looseBodies.at(1).justified, "Defense not recorded");
    advance(w, 5);
    require(!w.residents[1].alive && !crime(w, CrimeKind::Murder, 0, 1), "Defense became murder");
    w.validate();
  });
  test("Shoving surrendered residents is excessive force and preserves surrender", [] {
    auto w = small();
    place(w, 0, {50, 0, 50});
    place(w, 1, {50, 0, 48.5f});
    w.surrender(1);
    w.shove(0, 1);
    require(w.residents[1].surrendered && w.residents[1].activity == Activity::Surrendered &&
                crime(w, CrimeKind::ExcessiveForce, 0, 1),
            "Helpless victim lost protection");
    advance(w, 2);
    w.validate();
  });
  test("Falling beyond the structure records an unreachable body", [] {
    auto w = small();
    place(w, 0, {319, 0, 100});
    place(w, 1, {320.5f, 0, 100});
    w.residents[0].strength = 100;
    w.residents[1].strength = 10;
    w.shove(0, 1);
    advance(w, 8);
    require(!w.residents[1].alive && w.remains.back().lost && w.looseBodies.empty(),
            "Void fall left a reachable body");
    rejects([&] { w.transportBody(2, Id(w.remains.size() - 1)); });
    w.validate();
  });
  test("A resident killed midair leaves a falling body, including after reload", [] {
    auto w = small();
    place(w, 1, {0, 20, 0});
    w.stepBody(1, {}, .02f);
    w.kill(1, 0);
    require(w.looseBodies.empty() && w.airborneRemains.contains(0), "Death lost body momentum");
    auto path = std::filesystem::temp_directory_path() / "mega-physics-remains.mdsave";
    w.save(path);
    auto v = World::load(path);
    require(v.airborneRemains.contains(0), "Falling remains not restored");
    rejects([&] { v.transportBody(2, 0); });
    advance(v, 3);
    require(v.remains[0].pos.y == 0 && !v.remains[0].lost && v.airborneRemains.empty() &&
                v.nextPhysicsAt == -1,
            "Falling remains never landed");
    v.validate();
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".meta.json");
  });
  test("Death beyond the building continues into unreachable remains", [] {
    auto w = small();
    place(w, 1, {400, 10, 100});
    w.kill(1, 0);
    require(w.airborneRemains.contains(0), "Unsupported corpse remained suspended");
    advance(w, 8);
    require(w.remains[0].lost && w.airborneRemains.empty() && w.nextPhysicsAt == -1,
            "Outside corpse remained active");
    w.validate();
  });
  test("Malformed corpse physics and airborne activity escapes are rejected", [] {
    auto w = small();
    place(w, 1, {0, 10, 0});
    w.kill(1);
    auto valid = w;
    w.airborneRemains[0].velocity.z = std::numeric_limits<float>::infinity();
    rejects([&] { w.validate(); });
    w = valid;
    w.airborneRemains[0].updated = w.time + 1;
    rejects([&] { w.validate(); });
    w = valid;
    place(w, 0, {50, .2f, 50});
    w.stepBody(0, {}, .02f);
    rejects([&] { w.depart(0); });
    rejects([&] { w.usePersonalItem(0, w.residents[0].items.front()); });
    w.validate();
  });
  test("Deferred task changes cannot cancel a fall", [] {
    auto w = small();
    place(w, 0, {0, 10, 0});
    w.stepBody(0, {}, .02f);
    w.residents[0].activity = Activity::Working;
    w.residents[0].routine.workSince = w.time;
    advance(w, .1);
    require(w.looseBodies.contains(0) && w.position(0).y < 10 &&
                w.residents[0].activity == Activity::Idle && w.residents[0].routine.workSince < 0,
            "Job callback suspended gravity");
    w.residents[0].activity = Activity::Walking;
    w.residents[0].route = {{{50, 0, 50}, w.time}, {{60, 0, 50}, w.time + 60}};
    advance(w, .1);
    require(w.residents[0].route.empty() && w.position(0).y > 9 && w.position(0).y < 10,
            "Deferred route teleported an airborne body");
    w.validate();
  });
  test("Airborne work, custody and body collection fail without reserving anything", [] {
    auto w = small();
    place(w, 0, {0, 10, 0});
    place(w, 1, {0, 10, 1});
    w.stepBody(0, {}, .02f);
    w.residents[0].role = Role::Warden;
    w.surrender(1);
    w.report(CrimeKind::Assault, 1, 2);
    rejects([&] { w.apprehend(0, 1); });
    require(w.residents[0].escort == none, "Failed custody reserved an escort");
    w.residents[0].role = Role::Cleaner;
    place(w, 3, {0, 0, 0});
    w.kill(3);
    rejects([&] { w.transportBody(0, 0); });
    require(w.remains[0].worker == none, "Falling cleaner reserved a body");
    w.clockIn(0);
    require(w.residents[0].activity != Activity::Working, "Falling resident clocked in");
    w.validate();
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
