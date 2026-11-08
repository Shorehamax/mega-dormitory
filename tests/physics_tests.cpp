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
static Id idleReservation(World &w) {
  Id fid = w.nearestAt({0, 0, 0}, FacilityKind::Water);
  auto &f = w.facilities[fid];
  f.pos = {100, 0, 102};
  f.stock = 0;
  f.staffNeeded = 0;
  f.opensAt = 0;
  f.openHours = 24;
  f.publicAccess = true;
  f.autoRestock = false;
  w.invalidateGeometry(0);
  w.joinQueue(0, fid);
  auto &r = w.residents[0];
  r.pos = w.position(0);
  r.route.clear();
  ++r.routeVersion;
  // A deferred activity/error callback may restore this label before its reservation clears.
  r.activity = Activity::Idle;
  return fid;
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
  test("A blocked controlled resident stays still while a crowd presses in", [] {
    auto w = small();
    place(w, 0, {50, 0, 50});
    for (Id id = 1; id <= 6; ++id) {
      float angle = (id - 1) * 1.04719755f;
      place(w, id, {50 + .60f * std::cos(angle), 0, 50 + .60f * std::sin(angle)});
    }
    w.focus(0);
    w.physicsController = 0;
    Vec3 origin = w.position(0);
    for (int n = 0; n < 600; ++n) {
      w.moveCrowd((1440 / w.config.dayMinutes) / 60);
      w.advance((1440 / w.config.dayMinutes) / 60);
      w.stepBody(0, {0, 0, -7.2f}, 1.f / 60);
      require(length(w.position(0) - origin) < .03f, "A jammed body was displaced or oscillated");
      for (Id id = 1; id <= 6; ++id)
        require(length(w.position(0) - w.position(id)) >= .579f, "Crowd collision was disabled");
    }
    w.validate();
  });
  test("A pedestrian waits at an occupied destination instead of orbiting its occupant", [] {
    auto w = small();
    place(w, 0, {50, 0, 50});
    place(w, 1, {50, 0, 49.2f});
    w.focus(0);
    w.travel(0, w.position(1), TaskKind::None, none);
    float sideways = 0, reversals = 0;
    Vec3 prior = w.position(0), direction{};
    for (int n = 0; n < 600; ++n) {
      w.moveCrowd((1440 / w.config.dayMinutes) / 60);
      w.advance((1440 / w.config.dayMinutes) / 60);
      auto p = w.position(0);
      Vec3 delta = p - prior;
      sideways = std::max(sideways, std::abs(p.x - 50));
      if (length(delta) > .0001f && length(direction) > .0001f && dot(delta, direction) < 0)
        ++reversals;
      if (length(delta) > .0001f)
        direction = delta;
      require(length(p - w.position(1)) >= .579f, "Waiting pedestrian penetrated another resident");
      prior = p;
    }
    require(sideways < .001f && reversals == 0, "Blocked destination caused repeated sidesteps");
    require(w.residents[0].route.empty(), "A nearby occupied walking goal never completed");
    w.validate();
  });
  test("A crowded navigation corner can be passed without stepping onto its occupant", [] {
    auto w = small();
    place(w, 0, {50, 0, 50});
    place(w, 1, {50, 0, 49});
    auto &r = w.residents[0];
    r.activity = Activity::Walking;
    r.route = {{{50, 0, 50}, w.time}, {{50, 0, 49}, w.time + 100}, {{54, 0, 49}, w.time + 300}};
    ++r.routeVersion;
    w.schedule(w.time + 300, EventKind::Arrive, 0, none, r.routeVersion);
    w.focus(0);
    Vec3 prior = w.position(0);
    for (int n = 0; n < 1200; ++n) {
      w.moveCrowd((1440 / w.config.dayMinutes) / 60);
      w.advance((1440 / w.config.dayMinutes) / 60);
      auto p = w.position(0);
      require(length(p - w.position(1)) >= .579f, "Corner look-ahead removed person collision");
      require(length(p - prior) < .065f, "Corner look-ahead teleported a pedestrian");
      prior = p;
    }
    require(length(w.position(0) - Vec3{54, 0, 49}) < .2f && r.route.empty(),
            "A shared navigation corner permanently blocked pedestrian flow");
    w.validate();
  });
  test("An idle label with a real queue reservation cannot crash stationary first person", [] {
    auto w = small();
    Id fid = idleReservation(w);
    w.validate();
    auto before = w.position(0);
    auto cash = w.accounts[w.residents[0].account].balance;
    require(!w.canStepBody(0), "Reserved body was considered freely movable");
    // This strict call is the old UI path and reproduces the reported exception.
    rejects([&] { w.stepBody(0, {}, 1.f / 60); });
    for (int frame = 0; frame < 120; ++frame)
      require(!w.stepControlledBody(0, {}, 1.f / 60), "Waiting input stole a reservation");
    require(w.residents[0].queuedAt == fid && w.facilities[fid].queue.size() == 1 &&
                length(w.position(0) - before) == 0 &&
                w.accounts[w.residents[0].account].balance == cash && w.looseBodies.empty(),
            "Stationary controller mutated the service or body");
    w.validate();
  });
  test("Walking explicitly cancels a pending service once before integrating the body", [] {
    auto w = small();
    Id fid = idleReservation(w);
    auto cash = w.accounts[w.residents[0].account].balance;
    auto before = w.position(0);
    auto body = w.stepControlledBody(0, {-3.6f, 0, 0}, 1.f / 60);
    require(body && w.residents[0].queuedAt == none && w.facilities[fid].queue.empty() &&
                w.residents[0].route.empty() && w.residents[0].task == TaskKind::None &&
                length(w.position(0) - before) < .07f &&
                w.accounts[w.residents[0].account].balance == cash,
            "Walking bypassed cancellation or relocated the waiting resident");
    require(w.stepControlledBody(0, {-3.6f, 0, 0}, 1.f / 60).has_value(),
            "Walking did not continue");
    w.validate();
  });
  test("Manual interruption preserves an interpolated route position", [] {
    auto w = small();
    w.travel(0, {112, 0, 100}, TaskKind::None, none);
    w.time = (w.time + w.residents[0].route.back().time) / 2;
    auto before = w.position(0);
    require(before.x > 103 && before.x < 110, "Route interpolation fixture failed");
    auto body = w.stepControlledBody(0, {0, 0, -3.6f}, 1.f / 60);
    require(body && w.residents[0].route.empty() && length(body->position - before) < .07f,
            "Taking control snapped to the old route origin");
    w.validate();
  });
  test("A departed inspected resident cannot receive movement updates", [] {
    auto w = small();
    place(w, 0, w.layout(0).entrance());
    w.depart(0);
    auto before = w.position(0);
    auto activity = w.residents[0].activity;
    require(!w.canControlBody(0) && !w.stepControlledBody(0, {3.6f, 0, 0}, 1.f / 60) &&
                !w.stepControlledBody(0, {}, 1.f / 60, true),
            "Departed resident was controlled");
    require(w.residents[0].activity == activity && length(w.position(0) - before) == 0 &&
                w.looseBodies.empty() && !w.gameOver,
            "Inspection revived or moved an emigrant");
    w.validate();
  });
  test("Transport and custody transitions reject manual input without throwing or detaching", [] {
    auto base = small();
    auto check = [](World &w) {
      auto before = w.position(0);
      auto version = w.residents[0].routeVersion;
      require(!w.canControlBody(0) && !w.stepControlledBody(0, {3.6f, 0, 0}, 1.f / 60),
              "Reserved actor accepted manual walking");
      require(length(w.position(0) - before) == 0 && w.residents[0].routeVersion == version,
              "Rejected input changed an attached actor");
    };
    for (Activity state : {Activity::Jailed, Activity::Restrained, Activity::Escorted}) {
      auto w = base;
      w.residents[0].activity = state;
      check(w);
      require(w.residents[0].activity == state, "Movement escaped custody");
    }
    {
      auto w = base;
      w.residents[0].medicalDuty = 0;
      check(w);
      require(w.residents[0].medicalDuty == 0, "Movement abandoned a medical assignment");
    }
    {
      auto w = base;
      auto landing = w.layout().landing(0, 0);
      place(w, 0, landing);
      w.grabLadder(0);
      check(w);
      require(w.residents[0].ladder == 0, "Walking detached a ladder climber");
      w.validate();
    }
  });
  test("A sleeping resident stays at rest until walking explicitly wakes them", [] {
    auto w = small();
    w.residents[0].activity = Activity::Sleeping;
    auto before = w.position(0);
    require(!w.stepControlledBody(0, {}, 1.f / 60) &&
                w.residents[0].activity == Activity::Sleeping &&
                length(w.position(0) - before) == 0,
            "No-input frame woke a sleeping resident");
    require(w.stepControlledBody(0, {3.6f, 0, 0}, 1.f / 60).has_value() &&
                w.residents[0].activity == Activity::Idle,
            "Walking could not wake the resident");
    w.validate();
  });
  test("Surrender blocks control but keeps physical gravity active", [] {
    auto w = small();
    place(w, 0, {0, 4, 0});
    w.surrender(0);
    require(!w.stepControlledBody(0, {3.6f, 0, 0}, 1.f / 60),
            "Surrender was cancelled by movement");
    w.stepBody(0, {}, 1.f / 60);
    advance(w, 2);
    require(w.position(0).y == 0 && w.residents[0].surrendered && w.looseBodies.empty(),
            "A surrendered body stopped falling or regained control");
    w.validate();
  });
  test("Walking cannot cancel a real shove before physics settles", [] {
    auto w = small();
    place(w, 0, {50, 0, 50});
    place(w, 1, {50, 0, 48.5f});
    w.shove(1, 0);
    auto momentum = w.looseBodies.at(0).velocity;
    require(!w.stepControlledBody(0, {0, 0, -7.2f}, 1.f / 60) &&
                length(w.looseBodies.at(0).velocity - momentum) == 0,
            "Manual input erased real knockback");
    advance(w, .5);
    require(length(w.position(0) - Vec3{50, 0, 50}) > .1f, "Rejected input froze knockback");
    w.validate();
  });
  test("Interrupting a deferred activity while airborne never snaps onto a floor", [] {
    auto w = small();
    place(w, 0, {0, 4, 0});
    w.stepBody(0, {}, 1.f / 60);
    auto before = w.position(0);
    w.residents[0].activity = Activity::Sleeping;
    auto body = w.stepControlledBody(0, {1, 0, 0}, 1.f / 60);
    require(body && body->position.y > 3.9f && body->position.y < before.y && !body->grounded &&
                w.looseBodies.contains(0),
            "A late sleep callback cancelled real gravity");
    w.validate();
  });
  test("Invalid numeric control input is still rejected before any state mutation", [] {
    auto w = small();
    Id fid = idleReservation(w);
    auto before = w.position(0);
    float nan = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { w.stepControlledBody(0, {nan, 0, 0}, 1.f / 60); });
    rejects([&] { w.stepControlledBody(0, {}, nan); });
    rejects([&] { w.stepControlledBody(0, {}, -.1f); });
    rejects([&] { w.stepControlledBody(0, {}, .2f); });
    rejects([&] { w.stepControlledBody(0, {101, 0, 0}, .01f); });
    require(w.residents[0].queuedAt == fid && length(w.position(0) - before) == 0 &&
                !w.canControlBody(none) && !w.canStepBody(1000000),
            "Malformed input mutated a reservation or accepted an invalid actor");
    w.validate();
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
