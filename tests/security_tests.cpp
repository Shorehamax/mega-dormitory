#include "mega/world.hpp"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
using namespace mega;
int passed = 0, failed = 0;
void require(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
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
void test(const char *n, std::function<void()> f) {
  try {
    f();
    ++passed;
    std::cout << "PASS " << n << '\n';
  } catch (const std::exception &e) {
    ++failed;
    std::cout << "FAIL " << n << ": " << e.what() << '\n';
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
    r.task = TaskKind::None;
    r.activity = Activity::Idle;
    r.pos = {100 + r.id * 2.f, 57, 100};
    r.role = Role::Unemployed;
    r.jobSite = r.employer = none;
    r.age = 30;
    r.energy = r.hygiene = 1;
    r.hunger = r.thirst = r.bladder = .1f;
  }
  w.crowd.clear();
  for (auto &f : w.facilities) {
    f.employees.clear();
    f.staffNeeded = 0;
    f.opensAt = 0;
    f.openHours = 24;
  }
  return w;
}
void place(World &w, Id r, Vec3 p) {
  w.residents[r].pos = p;
  w.residents[r].route.clear();
  ++w.residents[r].routeVersion;
  w.crowd.erase(r);
}
Id phone(World &w, Id r) {
  Item i;
  i.id = Id(w.items.size());
  i.kind = ItemKind::Phone;
  i.owner = w.residents[r].account;
  i.carrier = r;
  w.items.push_back(i);
  w.residents[r].items.push_back(i.id);
  return i.id;
}
void scene(World &w) {
  place(w, 0, {151, 0, 150});
  place(w, 1, {150, 0, 150});
  place(w, 2, {150, 0, 154});
  place(w, 3, {180, 0, 150});
}
Id jail(World &w) {
  for (const auto &f : w.facilities)
    if (f.kind == FacilityKind::Jail)
      return f.id;
  throw std::runtime_error("No jail");
}
void guard(World &w, Id r) {
  Id f = jail(w);
  w.chooseJob(r, Role::Warden, f);
  w.residents[r].routine.shiftStart = 0;
  w.residents[r].routine.shiftHours = 24;
  w.residents[r].routine.daysOff = 0;
}
std::pair<Id, Id> unseenDeath(World &w) {
  place(w, 0, {150, 0, 150});
  place(w, 1, {151, 0, 150});
  place(w, 2, {-150, 30, 150});
  place(w, 3, {-150, 30, 155});
  w.residents[1].activity = Activity::Sleeping;
  Id body = Id(w.remains.size());
  w.kill(1, 0);
  return {Id(w.incidents.size() - 1), body};
}
int main() {
  test("Unreported acts retain causal facts without giving wardens omniscient identity", [] {
    auto w = small();
    scene(w);
    Id c = w.recordCrime(CrimeKind::Assault, 0, 1);
    require(w.incidents[c].offender == 0 && !w.incidents[c].reported &&
                w.incidents[c].suspect == none && !w.hasWarrant(0),
            "Unreported case granted a warrant");
    guard(w, 3);
    w.wardenRespond(3);
    require(w.residents[3].route.empty(), "Guard pursued an unknown act");
    w.validate();
  });
  test("A direct eyewitness can report through a real device and establish identity", [] {
    auto w = small();
    scene(w);
    phone(w, 2);
    Id c = w.recordCrime(CrimeKind::Assault, 0, 1);
    w.fileCrimeReport(2, c);
    require(w.canEnforce(w.incidents[c]) && w.incidents[c].suspect == 0 &&
                w.incidents[c].reporter == 2,
            "Testimony did not create a credible report");
    w.validate();
  });
  test("Uninvolved distant residents cannot invent a witnessed complaint", [] {
    auto w = small();
    scene(w);
    phone(w, 4);
    Id c = w.recordCrime(CrimeKind::Assault, 0, 1);
    rejects([&] { w.fileCrimeReport(4, c); });
  });
  test("Walls block eyewitness identification and a scene alone does not reveal the killer", [] {
    auto w = small();
    scene(w);
    place(w, 2, {150, 0, 165});
    Id p = w.proposePartition(w.dormOwner, 0, {146, 0, 158}, {155, 0, 158}, false);
    w.interiorWalls[w.structuralWorks[p].wall].active = true;
    w.invalidateGeometry(0);
    Id c = w.recordCrime(CrimeKind::Murder, 0, 1);
    require(std::none_of(w.incidents[c].witnesses.begin(), w.incidents[c].witnesses.end(),
                         [](const Testimony &t) { return t.witness == 2; }),
            "Witness saw through a wall");
    guard(w, 3);
    place(w, 3, {150, 0, 151});
    w.inspectCrimeScene(3, c);
    require(w.incidents[c].suspect == none && !w.canEnforce(w.incidents[c]),
            "Scene inspection fabricated an identity");
    w.validate();
  });
  test("An awake victim with no device can report directly to a nearby warden", [] {
    auto w = small();
    scene(w);
    for (Id i : w.residents[1].items)
      w.items[i].carrier = none;
    Id c = w.recordCrime(CrimeKind::Assault, 0, 1);
    rejects([&] { w.fileCrimeReport(1, c); });
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    w.fileCrimeReport(1, c);
    require(w.canEnforce(w.incidents[c]), "Physical report failed");
  });
  test("Investigations search last-seen positions rather than the offender's hidden current "
       "position",
       [] {
         auto w = small();
         scene(w);
         phone(w, 2);
         Id c = w.recordCrime(CrimeKind::Assault, 0, 1);
         w.fileCrimeReport(2, c);
         guard(w, 3);
         place(w, 3, {150, 0, 152});
         auto last = w.incidents[c].lastKnown;
         place(w, 0, {-150, 30, 150});
         auto goal = w.investigationDestination(3, c);
         require(length(goal - last) < .1f && length(goal - w.position(0)) > 100,
                 "Warden received live hidden position");
         w.validate();
       });
  test("Property seizure requires actual reach, custody and a valid crime", [] {
    auto w = small();
    scene(w);
    Id item = phone(w, 1);
    w.residents[0].strength = 100;
    w.surrender(1);
    require(w.stealItem(0, 1, item, true), "Robbery failed");
    Id c = Id(w.incidents.size() - 1);
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    w.fileCrimeReport(1, c);
    rejects([&] { w.recoverEvidence(3, c, item); });
    w.surrender(0);
    w.recoverEvidence(3, c, item);
    require(w.items[item].carrier == 3 && w.items[item].owner == w.residents[1].account &&
                w.evidenceReceipts.size() == 1,
            "Evidence lost actual ownership");
    w.validate();
  });
  test("Seized goods travel with the officer and require an actual locker claim", [] {
    auto w = small();
    scene(w);
    Id item = phone(w, 1);
    w.residents[0].strength = 100;
    w.surrender(1);
    w.stealItem(0, 1, item, true);
    Id c = Id(w.incidents.size() - 1);
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    w.fileCrimeReport(1, c);
    w.surrender(0);
    w.recoverEvidence(3, c, item);
    rejects([&] { w.claimEvidence(1, 0); });
    Id f = jail(w);
    place(w, 3, w.facilities[f].pos + Vec3{1.6f, 0, 0});
    w.depositEvidence(3, f);
    place(w, 1, w.facilities[f].pos + Vec3{0, 0, 1.6f});
    w.residents[1].surrendered = false;
    w.residents[1].activity = Activity::Idle;
    w.claimEvidence(1, 0);
    require(w.items[item].carrier == 1 && w.items[item].stolenFrom == none &&
                w.items[item].reservedAt == none,
            "Claim did not return property");
    rejects([&] { w.claimEvidence(1, 0); });
    w.validate();
  });
  test("Murder sentences are longer than ordinary assault and theft", [] {
    auto w = small();
    scene(w);
    w.report(CrimeKind::Assault, 0, 1);
    double a = w.sentenceHours(0);
    w.report(CrimeKind::Theft, 0, 1);
    double t = w.sentenceHours(0);
    w.report(CrimeKind::Murder, 0, 1);
    require(w.sentenceHours(0) > t && t > a, "Crime severity had no sentencing effect");
  });
  test("An unattended prisoner can work to escape but retains wanted consequences", [] {
    auto w = small();
    Id f = jail(w);
    auto &r = w.residents[0];
    place(w, 0, w.facilities[f].pos + Vec3{1.6f, 0, 1.6f});
    r.activity = Activity::Jailed;
    r.target = f;
    r.releaseAt = w.time + 86400;
    r.surrendered = true;
    r.strength = 90;
    w.startEscape(0);
    for (int n = 0; n < 40 && r.activity == Activity::Jailed; ++n) {
      w.time += 60;
      w.securityTick();
    }
    require(r.activity == Activity::Idle && !r.surrendered && w.hasWarrant(0) &&
                w.custodyRecords[0].escaped,
            "Unstaffed custody could not be escaped");
    w.validate();
  });
  test("Actual attending wardens stop a cell escape and extend its sentence", [] {
    auto w = small();
    Id f = jail(w);
    guard(w, 3);
    auto &r = w.residents[0];
    place(w, 0, w.facilities[f].pos + Vec3{1.6f, 0, 1.6f});
    place(w, 3, w.facilities[f].pos + Vec3{0, 0, 1.6f});
    r.activity = Activity::Jailed;
    r.target = f;
    r.releaseAt = w.time + 86400;
    r.surrendered = true;
    w.startEscape(0);
    auto until = r.releaseAt;
    w.time += 60;
    w.securityTick();
    require(r.activity == Activity::Jailed && r.releaseAt == until + 3600 &&
                w.custodyRecords[0].escapeAt < 0,
            "Present guard did not stop escape");
    w.validate();
  });
  test("Saved evidence, testimony and an unfinished escape resume consistently", [] {
    auto w = small();
    scene(w);
    phone(w, 2);
    Id c = w.recordCrime(CrimeKind::Theft, 0, 1);
    w.fileCrimeReport(2, c);
    Id f = jail(w);
    auto &r = w.residents[0];
    place(w, 0, w.facilities[f].pos + Vec3{1.6f, 0, 1.6f});
    r.activity = Activity::Jailed;
    r.target = f;
    r.releaseAt = w.time + 86400;
    r.surrendered = true;
    w.startEscape(0);
    w.time += 60;
    w.securityTick();
    auto path = std::filesystem::temp_directory_path() / "mega-security-save.mdsave";
    w.save(path);
    auto v = World::load(path);
    require(v.incidents[c].reported && v.incidents[c].witnesses[0].witness != none &&
                v.custodyRecords[0].escapeProgress == 60,
            "Saved investigation or escape lost state");
    v.validate();
    std::filesystem::remove(path);
  });
  test("Malformed testimony, evidence and custody references are rejected", [] {
    auto w = small();
    scene(w);
    Id c = w.recordCrime(CrimeKind::Theft, 0, 1);
    w.incidents[c].witnesses[0].witness = 999999;
    rejects([&] { w.validate(); });
  });

  test("Rechecking a scene does not manufacture stronger evidence", [] {
    auto w = small();
    scene(w);
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    Id c = w.recordCrime(CrimeKind::Murder, 0, 1);
    w.incidents[c].suspect = none;
    w.incidents[c].confidence = 0;
    w.inspectCrimeScene(3, c);
    float confidence = w.incidents[c].confidence;
    for (int n = 0; n < 10; ++n)
      w.inspectCrimeScene(3, c);
    require(w.incidents[c].confidence == confidence && !w.canEnforce(w.incidents[c]),
            "Repeated clicks fabricated a warrant");
  });
  test("An unrelated case cannot be used to seize a surrendered person's property", [] {
    auto w = small();
    scene(w);
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    Id item = phone(w, 1);
    w.residents[0].strength = 100;
    w.surrender(1);
    w.stealItem(0, 1, item, true);
    Id other = w.report(CrimeKind::Assault, 0, 2);
    w.surrender(0);
    rejects([&] { w.recoverEvidence(3, other, item); });
    require(w.items[item].carrier == 0 && w.evidenceReceipts.empty(),
            "Rejected seizure changed possession");
  });
  test("A killed evidence carrier drops the same recoverable property receipt", [] {
    auto w = small();
    scene(w);
    guard(w, 3);
    guard(w, 4);
    place(w, 3, {150, 0, 152});
    place(w, 4, {150, 0, 154});
    Id item = phone(w, 1);
    w.residents[0].strength = 100;
    w.surrender(1);
    w.stealItem(0, 1, item, true);
    Id c = Id(w.incidents.size() - 1);
    w.fileCrimeReport(1, c);
    w.surrender(0);
    w.recoverEvidence(3, c, item);
    w.kill(3);
    w.securityTick();
    require(w.items[item].carrier == none && w.items[item].reservedAt == none &&
                w.evidenceReceipts[0].carrier == none,
            "Officer death erased or retained carried evidence");
    place(w, 4, w.items[item].pos + Vec3{0, 0, 1});
    w.recoverEvidence(4, c, item);
    require(w.evidenceReceipts.size() == 1 && w.evidenceReceipts[0].carrier == 4 &&
                w.items[item].owner == w.residents[1].account,
            "Recovery duplicated or reassigned property");
    w.validate();
  });
  test("A missing locker rejects seizure before mutating the victim's property", [] {
    auto w = small();
    scene(w);
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    Id item = phone(w, 1);
    w.residents[0].strength = 100;
    w.surrender(1);
    w.stealItem(0, 1, item, true);
    Id c = Id(w.incidents.size() - 1);
    w.fileCrimeReport(1, c);
    w.surrender(0);
    w.residents[3].jobSite = none;
    for (auto &f : w.facilities)
      if (f.kind == FacilityKind::Jail) {
        f.open = false;
        f.demolished = true;
      }
    rejects([&] { w.recoverEvidence(3, c, item); });
    require(w.items[item].carrier == 0 && w.items[item].owner == w.residents[0].account &&
                w.evidenceReceipts.empty(),
            "Failed seizure partially transferred ownership");
  });
  test("Escape alerts use the actual jail location even when the original guard is elsewhere", [] {
    auto w = small();
    Id f = jail(w);
    guard(w, 3);
    auto &r = w.residents[0];
    place(w, 0, w.facilities[f].pos + Vec3{1.6f, 0, 1.6f});
    r.activity = Activity::Jailed;
    r.target = f;
    r.releaseAt = w.time + 86400;
    r.surrendered = true;
    w.startEscape(0);
    w.custodyRecords[0].guard = 3;
    Vec3 actual = w.position(0);
    w.escapeCustody(0);
    require(length(w.incidents.back().pos - actual) < .01f &&
                length(w.incidents.back().lastKnown - actual) < .01f,
            "Escape investigation teleported to the absent guard");
    w.validate();
  });
  test("An interrupted escort releases the suspect in place and reopens existing charges", [] {
    auto w = small();
    scene(w);
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    Id c = w.report(CrimeKind::Assault, 0, 1);
    w.surrender(0);
    w.apprehend(3, 0);
    Vec3 before = w.position(0);
    w.kill(3);
    w.securityTick();
    require(!w.custodyRecords[0].active && !w.incidents[c].resolved && w.hasWarrant(0) &&
                w.residents[0].activity == Activity::Idle && length(w.position(0) - before) < .01f,
            "Interrupted escort lost the warrant or teleported the suspect");
    w.validate();
  });
  test("Jail care consumes delivered stock and recovers energy without releasing the prisoner", [] {
    auto w = small();
    Id f = jail(w);
    auto &r = w.residents[0];
    place(w, 0, w.facilities[f].pos + Vec3{1.6f, 0, 1.6f});
    r.activity = Activity::Jailed;
    r.target = f;
    r.releaseAt = w.time + 86400;
    r.surrendered = true;
    r.routine.sleepStart = 0;
    r.routine.sleepHours = 24;
    r.hunger = r.thirst = .8f;
    r.hygiene = .3f;
    r.energy = .2f;
    w.startEscape(0);
    w.stopEscape(0);
    int stock = w.facilities[f].stock;
    Money total = w.internalMoney();
    w.time += 3600;
    w.securityTick();
    require(w.facilities[f].stock == stock - 1 && r.hunger < .8f && r.thirst < .8f &&
                r.energy > .2f && r.activity == Activity::Jailed && w.internalMoney() == total,
            "Custody care created rations, money or freedom");
    w.validate();
  });
  test("An empty jail cannot conjure prison rations", [] {
    auto w = small();
    Id f = jail(w);
    auto &r = w.residents[0];
    place(w, 0, w.facilities[f].pos + Vec3{1.6f, 0, 1.6f});
    r.activity = Activity::Jailed;
    r.target = f;
    r.releaseAt = w.time + 86400;
    r.surrendered = true;
    r.hunger = r.thirst = .8f;
    w.facilities[f].stock = 0;
    w.startEscape(0);
    w.stopEscape(0);
    w.time += 3600;
    w.securityTick();
    require(w.facilities[f].stock == 0 && r.hunger > .8f && r.thirst > .8f,
            "An unsupplied jail fed its prisoners");
  });
  test("Robbery wakes a sleeping victim before eyewitness testimony is recorded", [] {
    auto w = small();
    scene(w);
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    Id item = phone(w, 1);
    w.residents[0].strength = 100;
    w.residents[1].activity = Activity::Sleeping;
    w.residents[1].surrendered = true;
    w.stealItem(0, 1, item, true);
    auto &c = w.incidents.back();
    require(w.residents[1].activity != Activity::Sleeping &&
                std::any_of(c.witnesses.begin(), c.witnesses.end(),
                            [](const Testimony &s) { return s.witness == 1 && s.identifies; }),
            "A robbed awake victim had no eyewitness account");
  });

  test("A warden finishes a real crowded approach without needing to enter the suspect's body", [] {
    auto w = small();
    scene(w);
    guard(w, 3);
    place(w, 3, {150, 0, 143});
    phone(w, 2);
    Id c = w.recordCrime(CrimeKind::Theft, 0, 1);
    w.fileCrimeReport(2, c);
    w.residents[3].strength = 100;
    w.residents[0].strength = 20;
    w.focus(0);
    w.wardenRespond(3);
    for (int n = 0; n < 600 && !w.custodyRecords.contains(0); ++n) {
      w.moveCrowd(.02 * (1440 / w.config.dayMinutes));
      w.advance(.02 * (1440 / w.config.dayMinutes));
    }
    require(w.custodyRecords.contains(0) && w.residents[0].activity == Activity::Escorted &&
                w.residents[3].escort == 0,
            "The officer endlessly orbited an occupied investigation goal");
    w.validate();
  });
  test("An ordinary surrendered victim recovers without remaining frozen forever", [] {
    auto w = small();
    scene(w);
    w.surrender(1);
    w.time += 899;
    w.securityTick();
    require(w.residents[1].surrendered, "Surrender ended immediately");
    w.time += 2;
    w.securityTick();
    require(!w.residents[1].surrendered && w.residents[1].activity == Activity::Idle,
            "A non-custodial victim stayed permanently frozen");
    w.validate();
  });
  test("An escorted suspect physically reaches intake even when the guard occupies the final "
       "approach",
       [] {
         auto w = small();
         scene(w);
         guard(w, 3);
         place(w, 3, {150, 0, 152});
         w.report(CrimeKind::Theft, 0, 1);
         w.surrender(0);
         w.apprehend(3, 0);
         w.focus(0);
         Vec3 previous = w.position(0);
         for (int n = 0; n < 12000 && w.residents[0].activity != Activity::Jailed; ++n) {
           w.moveCrowd(.02 * (1440 / w.config.dayMinutes));
           w.advance(.02 * (1440 / w.config.dayMinutes));
           auto actual = w.position(0);
           require(length(actual - previous) < .16f, "Jail intake teleported its prisoner");
           previous = actual;
         }
         require(w.residents[0].activity == Activity::Jailed &&
                     w.near(0, w.facilities[jail(w)].pos, 3.3f),
                 "The guard blocked their own prisoner's intake forever");
         w.validate();
       });
  test("A visible on-duty officer is not excluded when a crowd already fills the witness limit",
       [] {
         auto w = small();
         scene(w);
         for (Id id = 3; id < 25; ++id)
           place(w, id, {150 + float(id % 4), 0, 152 + float(id % 3)});
         guard(w, 39);
         place(w, 39, {150, 0, 170});
         Id c = w.recordCrime(CrimeKind::Assault, 0, 1);
         require(w.incidents[c].witnesses.size() == 12 && w.canEnforce(w.incidents[c]) &&
                     std::any_of(w.incidents[c].witnesses.begin(), w.incidents[c].witnesses.end(),
                                 [](const Testimony &s) { return s.witness == 39 && s.filed; }),
                 "The witness cap made a real observing officer blind");
         w.validate();
       });
  test("Walls prevent an in-person report, scene inspection and witness interview", [] {
    auto w = small();
    scene(w);
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    for (Id item : w.residents[1].items)
      w.items[item].carrier = none;
    Id wall = w.proposePartition(w.dormOwner, 0, {146, 0, 151}, {155, 0, 151}, false);
    w.interiorWalls[w.structuralWorks[wall].wall].active = true;
    w.invalidateGeometry(0);
    Id c = w.recordCrime(CrimeKind::Assault, 0, 1);
    rejects([&] { w.fileCrimeReport(1, c); });
    rejects([&] { w.inspectCrimeScene(3, c); });
    rejects([&] { w.interviewWitness(3, c, 1); });
    require(!w.incidents[c].reported, "A wall was bypassed by police interaction");
    w.validate();
  });
  test("An escorted prisoner cannot bypass custody by selecting a walking destination", [] {
    auto w = small();
    scene(w);
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    w.report(CrimeKind::Assault, 0, 1);
    w.surrender(0);
    w.apprehend(3, 0);
    auto route = w.residents[0].route;
    rejects([&] { w.travel(0, {180, 0, 180}, TaskKind::None, none); });
    require(w.residents[0].activity == Activity::Escorted &&
                w.residents[0].route.size() == route.size() && w.residents[3].escort == 0,
            "Rejected travel broke the escort");
    w.validate();
  });
  test("An officer abandoning the escort releases it in place and clears both references", [] {
    auto w = small();
    scene(w);
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    Id c = w.report(CrimeKind::Assault, 0, 1);
    w.surrender(0);
    w.apprehend(3, 0);
    Vec3 actual = w.position(0);
    w.travel(3, {180, 0, 180}, TaskKind::None, none);
    w.securityTick();
    require(!w.custodyRecords[0].active && w.residents[3].escort == none &&
                w.residents[0].escort == none && !w.incidents[c].resolved &&
                length(w.position(0) - actual) < .001f,
            "Abandonment left an invisible restraint");
    w.validate();
  });
  test("A surrendered warden cannot use arrest to regain control of a fight", [] {
    auto w = small();
    scene(w);
    guard(w, 3);
    place(w, 3, {150, 0, 152});
    w.report(CrimeKind::Assault, 0, 1);
    w.surrender(0);
    w.surrender(3);
    rejects([&] { w.apprehend(3, 0); });
    w.wardenRespond(3);
    require(w.residents[3].escort == none && w.custodyRecords.empty(),
            "A surrendered officer booked someone");
    w.validate();
  });
  test("A warden can discover an unwitnessed body without learning the killer", [] {
    auto w = small();
    auto [c, b] = unseenDeath(w);
    require(!w.incidents[c].reported, "Unseen death was already reported");
    guard(w, 3);
    w.residents[3].automatic = true;
    place(w, 3, w.remains[b].pos + Vec3{1, 0, 1});
    w.securityTick();
    require(w.incidents[c].reported && w.incidents[c].sceneLocated &&
                w.incidents[c].suspect == none && !w.hasWarrant(0),
            "Body discovery fabricated an offender");
    require(w.incidents[c].witnesses.size() == 1 && w.incidents[c].witnesses[0].bodyOnly &&
                !w.incidents[c].witnesses[0].identifies,
            "Discovery was recorded as a murder eyewitness");
    w.validate();
  });
  test("A moved body is found at its real location instead of the hidden original crime scene", [] {
    auto w = small();
    auto [c, b] = unseenDeath(w);
    w.remains[b].pos = {150, 6, 150};
    guard(w, 3);
    w.residents[3].automatic = true;
    place(w, 3, {150, 0, 151});
    w.securityTick();
    require(!w.incidents[c].reported, "Guard discovered a body on another floor");
    place(w, 3, {150, 6, 151});
    w.securityTick();
    guard(w, 4);
    place(w, 4, {150, 0, 151});
    auto goal = w.investigationDestination(4, c);
    require(w.incidents[c].scene.y == 6 && goal.y == 6 && w.incidents[c].pos.y == 0,
            "A body report revealed a hidden original scene");
    rejects([&] { w.inspectCrimeScene(4, c); });
    w.validate();
  });
  test("A witness with no device can remember a body and report it at a real desk", [] {
    auto w = small();
    auto [c, b] = unseenDeath(w);
    for (Id item : w.residents[2].items)
      w.items[item].carrier = none;
    place(w, 2, w.remains[b].pos + Vec3{1, 0, 0});
    require(w.discoverBody(2, b) == c && !w.incidents[c].reported,
            "Discovery made a remote report without a connection");
    rejects([&] { w.fileCrimeReport(2, c); });
    auto seen = w.remains[b].pos;
    place(w, 2, w.facilities[jail(w)].pos + Vec3{1.6f, 0, 1.6f});
    w.fileCrimeReport(2, c);
    require(w.incidents[c].reported && length(w.incidents[c].scene - seen) < .001f &&
                w.incidents[c].suspect == none,
            "The report lost the remembered scene or invented identity");
    w.validate();
  });
  test("Walls prevent examining a corpse", [] {
    auto w = small();
    auto [c, b] = unseenDeath(w);
    place(w, 2, {151, 0, 152});
    InteriorWall wall;
    wall.id = Id(w.interiorWalls.size());
    wall.owner = w.dormAccount;
    wall.floor = 0;
    wall.a = {147, 0, 151};
    wall.b = {155, 0, 151};
    wall.active = true;
    wall.door = false;
    w.interiorWalls.push_back(wall);
    w.invalidateGeometry(0);
    rejects([&] { w.discoverBody(2, b); });
    require(!w.incidents[c].reported && w.incidents[c].witnesses.empty(),
            "Discovery saw through a partition");
    w.validate();
  });
  test("Lost and cremated remains cannot be examined", [] {
    auto w = small();
    auto [c, b] = unseenDeath(w);
    place(w, 2, w.remains[b].pos + Vec3{1, 0, 0});
    w.remains[b].lost = true;
    rejects([&] { w.discoverBody(2, b); });
    w.remains[b].lost = false;
    w.remains[b].cremated = true;
    rejects([&] { w.discoverBody(2, b); });
    require(!w.incidents[c].reported, "Destroyed evidence produced a report");
  });
  test("A private morgue does not expose bodies to an unauthorized passerby", [] {
    auto w = small();
    auto [c, b] = unseenDeath(w);
    Id morgue = none;
    for (auto &f : w.facilities)
      if (f.kind == FacilityKind::Morgue)
        morgue = f.id;
    require(morgue != none, "Missing morgue");
    auto &f = w.facilities[morgue];
    f.publicAccess = false;
    f.manager = none;
    f.whitelist.clear();
    w.remains[b].storage = morgue;
    w.remains[b].pos = f.pos;
    place(w, 2, f.pos + Vec3{1.6f, 0, 1.6f});
    rejects([&] { w.discoverBody(2, b); });
    require(!w.incidents[c].reported, "Private storage was bypassed");
    w.validate();
  });
  test("A real eyewitness retains their identifying account when they later examine the body", [] {
    auto w = small();
    scene(w);
    phone(w, 2);
    w.kill(1, 0);
    Id c = Id(w.incidents.size() - 1);
    w.discoverBody(2, Id(w.remains.size() - 1));
    require(w.canEnforce(w.incidents[c]) && w.incidents[c].suspect == 0,
            "Examining remains erased real eyewitness identification");
    for (auto &s : w.incidents[c].witnesses)
      if (s.witness == 2)
        require(!s.bodyOnly && s.identifies, "A direct account became body-only evidence");
    w.validate();
  });
  test("Natural deaths do not manufacture murder allegations", [] {
    auto w = small();
    place(w, 1, {151, 0, 150});
    place(w, 2, {150, 0, 150});
    w.kill(1);
    auto count = w.incidents.size();
    require(w.discoverBody(2, Id(w.remains.size() - 1)) == none && w.incidents.size() == count,
            "A natural death invented a criminal");
    w.validate();
  });
  test("Body-discovery location and limited testimony survive a save", [] {
    auto w = small();
    auto [c, b] = unseenDeath(w);
    w.remains[b].pos = {150, 6, 150};
    guard(w, 3);
    w.residents[3].automatic = true;
    place(w, 3, {150, 6, 151});
    w.securityTick();
    auto file = std::filesystem::temp_directory_path() / "mega-body-scene.mdsave";
    w.save(file);
    auto v = World::load(file);
    require(v.incidents[c].sceneLocated && v.incidents[c].scene.y == 6 &&
                v.incidents[c].witnesses[0].bodyOnly && v.incidents[c].suspect == none,
            "Saved discovery leaked identity or lost the body location");
    v.validate();
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".meta.json");
  });
  test("A body-only observation cannot be forged into identifying testimony", [] {
    auto w = small();
    auto [c, b] = unseenDeath(w);
    guard(w, 3);
    place(w, 3, w.remains[b].pos + Vec3{1, 0, 1});
    w.discoverBody(3, b);
    w.incidents[c].witnesses[0].identifies = true;
    rejects([&] { w.validate(); });
  });
  test("A company director recovers corporate property without changing its legal title", [] {
    auto w = small();
    w.pay(0, w.residents[0].account, 100000, "test-capital");
    Id company = w.foundCompany(0, "Recovery Company", 50000);
    Id f = jail(w);
    Item item;
    item.id = Id(w.items.size());
    item.kind = ItemKind::Tools;
    item.owner = w.companies[company].account;
    item.reservedAt = f;
    item.pos = w.facilities[f].pos;
    w.items.push_back(item);
    Id c = w.report(CrimeKind::Assault, 2, 1);
    EvidenceReceipt e;
    e.id = 0;
    e.item = item.id;
    e.incident = c;
    e.owner = item.owner;
    e.locker = f;
    w.evidenceReceipts.push_back(e);
    place(w, 4, w.facilities[f].pos + Vec3{1.6f, 0, 1.6f});
    rejects([&] { w.claimEvidence(4, 0); });
    place(w, 0, w.facilities[f].pos + Vec3{1.6f, 0, 1.6f});
    w.claimEvidence(0, 0);
    require(w.items[item.id].owner == w.companies[company].account && w.items[item.id].carrier == 0,
            "Recovery privatized a company-owned tool");
    w.validate();
  });
  test("A shop manager cannot collect their employer's private belongings", [] {
    auto w = small();
    Id f = jail(w);
    auto &shop = w.facilities[f];
    shop.owner = w.residents[0].account;
    shop.manager = 4;
    Item item;
    item.id = Id(w.items.size());
    item.kind = ItemKind::Phone;
    item.owner = shop.owner;
    item.reservedAt = f;
    item.pos = shop.pos;
    w.items.push_back(item);
    Id c = w.report(CrimeKind::Theft, 2, 0);
    EvidenceReceipt e;
    e.id = 0;
    e.item = item.id;
    e.incident = c;
    e.owner = item.owner;
    e.locker = f;
    w.evidenceReceipts.push_back(e);
    place(w, 4, shop.pos + Vec3{1.6f, 0, 1.6f});
    rejects([&] { w.claimEvidence(4, 0); });
    require(!w.evidenceReceipts[0].claimed && w.items[item.id].carrier == none,
            "A work mandate appropriated the owner's private phone");
    w.validate();
  });
  test("Automatic undertakers can report a violent death when they physically collect the body",
       [] {
         auto w = small();
         auto [c, b] = unseenDeath(w);
         Id morgue = none;
         for (const auto &f : w.facilities)
           if (f.kind == FacilityKind::Morgue)
             morgue = f.id;
         w.chooseJob(2, Role::Cleaner, morgue);
         auto &r = w.residents[2];
         r.automatic = true;
         r.routine.shiftStart = 0;
         r.routine.shiftHours = 24;
         r.routine.daysOff = 0;
         phone(w, 2);
         place(w, 2, w.remains[b].pos + Vec3{1, 0, 0});
         w.transportBody(2, b);
         w.pickupBody(2, b);
         require(w.incidents[c].reported && w.incidents[c].suspect == none &&
                     w.remains[b].carrier == 2,
                 "Physical collection skipped the report or invented the killer");
         guard(w, 3);
         place(w, 3, {151, 0, 150});
         require(w.investigationDestination(3, c).y == 0,
                 "Stretcher height became a floating walking goal");
         w.validate();
       });
  test("Manual body transport does not force a player to report the death", [] {
    auto w = small();
    auto [c, b] = unseenDeath(w);
    Id morgue = none;
    for (const auto &f : w.facilities)
      if (f.kind == FacilityKind::Morgue)
        morgue = f.id;
    w.chooseJob(2, Role::Cleaner, morgue);
    auto &r = w.residents[2];
    r.automatic = false;
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    r.routine.daysOff = 0;
    phone(w, 2);
    place(w, 2, w.remains[b].pos + Vec3{1, 0, 0});
    w.transportBody(2, b);
    w.pickupBody(2, b);
    require(!w.incidents[c].reported && w.remains[b].carrier == 2,
            "Manual transport silently filed a complaint");
    w.validate();
  });
  test("A full set of offline body observations does not stop a later warden report", [] {
    auto w = small();
    auto [c, b] = unseenDeath(w);
    for (Id id = 2; id < 26; ++id) {
      for (Id item : w.residents[id].items)
        w.items[item].carrier = none;
      place(w, id, w.remains[b].pos + Vec3{1, 0, 0});
      w.discoverBody(id, b);
    }
    require(w.incidents[c].witnesses.size() == 24 && !w.incidents[c].reported,
            "Offline witnesses unexpectedly reported the body");
    guard(w, 39);
    place(w, 39, w.remains[b].pos + Vec3{1, 0, 1});
    w.discoverBody(39, b);
    require(w.incidents[c].reported && w.incidents[c].witnesses.size() == 24 &&
                w.incidents[c].suspect == none,
            "A saturated witness list made an actual officer blind");
    w.validate();
  });
  test("A witness cannot file a complaint while asleep", [] {
    auto w = small();
    scene(w);
    phone(w, 2);
    Id c = w.recordCrime(CrimeKind::Assault, 0, 1);
    w.residents[2].activity = Activity::Sleeping;
    rejects([&] { w.fileCrimeReport(2, c); });
    require(!w.incidents[c].reported, "A sleeping resident used their phone to report");
    w.residents[2].activity = Activity::Idle;
    w.fileCrimeReport(2, c);
    require(w.canEnforce(w.incidents[c]), "Awake witness could not report after waking");
    w.validate();
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
