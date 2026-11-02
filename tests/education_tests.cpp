#include "mega/world.hpp"
#include <functional>
#include <iostream>
using namespace mega;
int passed = 0, failed = 0;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
  bool threw = false;
  try {
    fn();
  } catch (const std::exception &) {
    threw = true;
  }
  require(threw, "Invalid action succeeded");
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
void place(World &w, Id who, Vec3 at) {
  auto &r = w.residents[who];
  r.pos = at;
  r.route.clear();
  ++r.routeVersion;
  r.activity = Activity::Idle;
  r.task = TaskKind::None;
  w.crowd.erase(who);
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
  w.time = 10 * 3600;
  for (auto &r : w.residents) {
    w.clockOut(r.id);
    w.leaveQueue(r.id);
    place(w, r.id, {100 + r.id * 2.f, 0, 100});
    r.automatic = false;
    r.age = 25;
    r.role = Role::Unemployed;
    r.jobSite = r.employer = none;
    r.hunger = r.thirst = r.bladder = .1f;
    r.hygiene = r.energy = 1;
    r.needsAt = w.time;
  }
  w.crowd.clear();
  return w;
}
Id school(World &w) {
  Id site = w.nearestAt({0, 0, 0}, FacilityKind::School);
  require(site != none, "No generated classroom");
  auto &f = w.facilities[site];
  f.opensAt = 0;
  f.openHours = 24;
  f.staffNeeded = 1;
  f.stock = 100;
  f.employees.clear();
  w.chooseJob(2, Role::Teacher, site);
  auto &r = w.residents[2];
  r.routine.shiftStart = 0;
  r.routine.shiftHours = 24;
  r.routine.daysOff = 0;
  place(w, 2, f.pos + Vec3{0, 0, -1.6f});
  w.clockIn(2);
  require(w.operational(site), "Teacher did not clock in");
  return site;
}
void begin(World &w, Id f, int subject) {
  place(w, 0, w.serviceApproach(f));
  w.enrollLesson(0, f, subject);
  place(w, 0, w.serviceApproach(f));
  w.joinQueue(0, f);
  require(w.residents[0].activity == Activity::Using, "Student did not enter class");
  place(w, 0, w.servicePosition(f, w.facilities[f].serving[0].station));
  w.residents[0].activity = Activity::Using;
}
void finish(World &w, Id f) {
  double until = w.facilities[f].serving[0].until;
  require(until > w.time, "No lesson event");
  w.advance(until - w.time + .01);
}
int main() {
  test("A lesson requires a real on-site teacher, access and tuition", [] {
    auto w = small();
    Id f = school(w);
    w.clockOut(2);
    rejects([&] { w.enrollLesson(0, f, 0); });
    require(!w.education.contains(0), "Unstaffed class awarded enrollment");
    w.clockIn(2);
    w.facilities[f].publicAccess = false;
    rejects([&] { w.enrollLesson(0, f, 0); });
    w.facilities[f].publicAccess = true;
    w.pay(w.residents[0].account, 0, w.accounts[w.residents[0].account].balance, "test-spend");
    rejects([&] { w.enrollLesson(0, f, 0); });
    w.validate();
  });
  test("Exercises require reaching the seat during an actual active lesson", [] {
    auto w = small();
    Id f = school(w);
    place(w, 0, w.facilities[f].pos + Vec3{20, 0, 20});
    w.enrollLesson(0, f, 0);
    rejects([&] { w.answerLesson(0, 0, 0); });
    require(w.education[0].skills[0] == 0, "Remote enrollment taught skills");
    w.validate();
  });
  test("Finishing a paid class with correct exercises grants persistent skills", [] {
    auto w = small();
    Id f = school(w);
    begin(w, f, 0);
    auto cash = w.accounts[w.residents[0].account].balance;
    auto seller = w.accounts[w.operatingAccount(f)].balance;
    for (int q = 0; q < 3; ++q)
      w.answerLesson(0, q, (w.education[0].seed + q) % 4);
    finish(w, f);
    require(w.education[0].lessons[0] == 1 &&
                std::abs(w.jobSkill(0, Role::Programmer) - .04f) < .0001f,
            "Lesson or exercise credit missing");
    require(w.accounts[w.residents[0].account].balance == cash - w.facilities[f].price &&
                w.accounts[w.operatingAccount(f)].balance == seller + w.facilities[f].price,
            "Tuition did not reach school owner exactly once");
    rejects([&] { w.enrollLesson(0, f, 1); });
    w.validate();
  });
  test("Wrong or unanswered exercises yield attendance credit rather than full mastery", [] {
    auto w = small();
    Id f = school(w);
    begin(w, f, 1);
    finish(w, f);
    require(std::abs(w.jobSkill(0, Role::VideoEditor) - .01f) < .0001f,
            "Unanswered class received full marks");
    w.validate();
  });
  test("A teacher leaving pauses teaching without tuition or new knowledge", [] {
    auto w = small();
    Id f = school(w);
    begin(w, f, 0);
    auto cash = w.accounts[w.residents[0].account].balance;
    w.advance(600);
    w.clockOut(2);
    require(w.facilities[f].serving[0].until < 0, "Unstaffed lesson kept running");
    w.advance(3600);
    require(w.education[0].lessons[0] == 0 && w.accounts[w.residents[0].account].balance == cash,
            "Absent teacher produced paid knowledge");
    w.clockIn(2);
    finish(w, f);
    require(w.education[0].lessons[0] == 1, "Paused lesson never resumed");
    w.validate();
  });
  test("Leaving a class releases its seat without skill or tuition", [] {
    auto w = small();
    Id f = school(w);
    begin(w, f, 0);
    auto cash = w.accounts[w.residents[0].account].balance;
    w.leaveQueue(0);
    w.advance(7200);
    require(w.education[0].school == none && w.education[0].lessons[0] == 0 &&
                w.accounts[w.residents[0].account].balance == cash,
            "Interrupted lesson granted benefits or took tuition");
    w.validate();
  });
  test("Class progress, answers and exact remaining teaching survive saves", [] {
    auto w = small();
    Id f = school(w);
    begin(w, f, 2);
    w.answerLesson(0, 0, (w.education[0].seed) % 4);
    w.advance(600);
    auto path = std::filesystem::temp_directory_path() / "mega-training.mdsave";
    w.save(path);
    auto v = World::load(path);
    require(v.education[0].answers[0] == w.education[0].answers[0] &&
                v.facilities[f].serving[0].until == w.facilities[f].serving[0].until,
            "Class record lost in save");
    finish(v, f);
    require(v.education[0].lessons[2] == 1, "Reloaded class did not complete");
    v.validate();
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".meta.json");
  });
  test("Knowledge makes a later real office commission faster", [] {
    auto w = small();
    Id f = school(w);
    begin(w, f, 0);
    for (int q = 0; q < 3; ++q)
      w.answerLesson(0, q, (w.education[0].seed + q) % 4);
    finish(w, f);
    Id office = w.nearestAt(w.position(0), FacilityKind::Office);
    auto &site = w.facilities[office];
    site.staffNeeded = 0;
    site.opensAt = 0;
    site.openHours = 24;
    w.chooseJob(0, Role::Programmer, office);
    w.residents[0].routine.shiftStart = 0;
    w.residents[0].routine.shiftHours = 24;
    w.residents[0].routine.daysOff = 0;
    place(w, 0, site.pos + Vec3{0, 0, -1.6f});
    Id order = w.officeOrder(0);
    require(w.officeOrders[order].total < 5400 && w.officeOrders[order].total > 5000,
            "Education had no real work effect");
    w.validate();
  });
  test("Children attend class during weekday hours without adult purchasing decisions", [] {
    auto w = small();
    Id f = school(w);
    place(w, 0, w.facilities[f].pos + Vec3{6, 0, 6});
    auto &r = w.residents[0];
    r.age = 12;
    r.automatic = true;
    require(w.educationDecision(0) && r.task == TaskKind::Service && r.target == f,
            "Student did not travel to class");
    require(w.orders.empty(), "Schooling invented an adult purchase");
    w.validate();
  });
  test("Malformed education references, nonfinite skills and choices are rejected", [] {
    auto w = small();
    w.education[0].skills[0] = 2;
    rejects([&] { w.validate(); });
    w.education[0].skills[0] = 0;
    w.education[0].answers[0] = 4;
    rejects([&] { w.validate(); });
    w.education[0].answers[0] = -1;
    w.education[0].school = 999999;
    rejects([&] { w.validate(); });
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
