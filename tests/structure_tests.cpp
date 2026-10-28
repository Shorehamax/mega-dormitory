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

static Id workshop(World &w) {
  for (auto &f : w.facilities)
    if (f.kind == FacilityKind::Workshop && f.floor == 0)
      return f.id;
  throw std::runtime_error("Missing workshop");
}
static void builder(World &w, Id id, Id shop) {
  w.chooseJob(id, Role::Builder, shop);
  auto &r = w.residents[id];
  r.age = 30;
  r.routine.shiftStart = 0;
  r.routine.shiftHours = 24;
  r.routine.daysOff = 0;
  r.automatic = true;
}
static Money total(const World &w) {
  Money n = 0;
  for (const auto &a : w.accounts)
    n += a.balance;
  return n;
}
static void rich(World &w) { w.pay(0, w.dormAccount, 2000000000, "test-capital"); }
static Id partition(World &w, bool door = true) {
  return w.proposePartition(w.dormOwner, 0, {70, 0, 100}, {78, 0, 100}, door);
}
static Id crew(World &w, Id shop) {
  builder(w, 2, shop);
  builder(w, 3, shop);
  Id t = w.createConstructionTeam(2, shop, 2);
  w.recruitConstructionWorker(2, t, 3);
  return t;
}
static void cleared(World &w, int floor) {
  for (auto &h : w.housing)
    if (h.floor == floor)
      h.active = false;
  for (auto &f : w.facilities)
    if (f.floor == floor) {
      f.open = false;
      f.demolished = true;
    }
  w.invalidateGeometry(floor);
}
static void supply(World &w, Id job) {
  auto &p = w.structuralWorks[job];
  auto &t = w.constructionTeams[p.team];
  p.hauler = t.foreman;
  place(w, p.hauler, w.facilities[p.workshop].pos + Vec3{1.6f, 0, 0});
  w.beginStructuralWork(p.hauler, job);
  w.structuralTick();
  require(p.material != none && !p.supplied, "Materials were not physically collected");
  place(w, p.hauler, p.site);
  w.beginStructuralWork(p.hauler, job);
  w.structuralTick();
  require(p.supplied && w.residents[p.hauler].carrying == none, "Materials not unloaded at site");
}
static void attend(World &w, Id job) {
  auto &p = w.structuralWorks[job];
  for (Id worker : w.constructionTeams[p.team].members) {
    place(w, worker, w.structuralDestination(worker, job));
    w.beginStructuralWork(worker, job);
  }
}
static void finish(World &w, Id job) {
  attend(w, job);
  w.structuralTick();
  for (int i = 0; i < 1000 && w.structuralWorks[job].state != StructuralState::Completed; ++i) {
    w.time += 60;
    w.structuralTick();
  }
  require(w.structuralWorks[job].state == StructuralState::Completed,
          "Physical crew did not finish");
}
int main() {
  test("Partition proposal is reversible and requires permission, supported space and funds", [] {
    auto w = small();
    Id p = partition(w);
    require(!w.interiorWalls[0].active, "Proposal changed collision");
    w.config.mode = Mode::Survival;
    rejects([&] { w.proposePartition(2, 0, {80, 0, 100}, {88, 0, 100}, true); });
    rejects([&] { w.proposePartition(w.dormOwner, 0, {70, 0, 100}, {78, 0, 108}, true); });
    w.accounts[w.dormAccount].balance = 0;
    rejects([&] { w.fundStructuralWork(w.dormOwner, p); });
    require(w.structuralWorks[p].state == StructuralState::Proposed, "Unfunded project advanced");
  });
  test("Actual materials, attending crew, wages and contractor profit complete a partition", [] {
    auto w = small();
    rich(w);
    auto before = total(w);
    Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
    w.fundStructuralWork(w.dormOwner, p);
    w.assignStructuralTeam(w.dormOwner, p, t);
    auto amount = w.accounts[w.residents[2].account].balance;
    supply(w, p);
    finish(w, p);
    require(w.interiorWalls[0].active && w.accounts[w.structuralWorks[p].escrow].balance == 0,
            "Completion not settled");
    require(w.accounts[w.residents[2].account].balance > amount, "Worker not paid");
    require(total(w) == before, "Construction created money");
    w.validate();
  });
  test("Partition doorway, wall contact, sight and paths share geometry", [] {
    auto w = small();
    rich(w);
    Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
    w.fundStructuralWork(w.dormOwner, p);
    w.assignStructuralTeam(w.dormOwner, p, t);
    supply(w, p);
    finish(w, p);
    require(w.clearPosition({74, 0, 100}) && !w.clearPosition({71, 0, 100}),
            "Doorway collision mismatch");
    require(!w.lineOfSight({71, 1.4f, 99}, {71, 1.4f, 101}), "Sight penetrates partition");
    require(w.lineOfSight({74, 1.4f, 99}, {74, 1.4f, 101}), "Open doorway blocks sight");
    auto route = w.floorPath({74, 0, 98}, {74, 0, 102});
    require(!route.empty(), "Doorway cannot be walked through");
    auto moved = w.moveCapsule(4, {71, 0, 99}, {0, 0, 2}, false);
    require(moved.z < 100, "Body penetrated partition");
  });
  test("Only physically attending workers contribute; foreman supervision requires presence", [] {
    auto w = small();
    rich(w);
    Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
    w.fundStructuralWork(w.dormOwner, p);
    w.assignStructuralTeam(w.dormOwner, p, t);
    supply(w, p);
    place(w, 2, {150, 0, 100});
    auto &j = w.structuralWorks[p];
    auto left = j.remaining;
    w.time += 60;
    w.structuralTick();
    require(j.remaining == left, "Remote work credited");
    place(w, 3, w.structuralDestination(3, p));
    w.beginStructuralWork(3, p);
    w.structuralTick();
    w.time += 60;
    w.structuralTick();
    require(std::abs(left - j.remaining - 60) < .01, "Absent foreman granted supervision");
    place(w, 2, w.structuralDestination(2, p));
    w.beginStructuralWork(2, p);
    w.structuralTick();
    left = j.remaining;
    w.time += 60;
    w.structuralTick();
    require(std::abs(left - j.remaining - 138) < .01, "Onsite team did not contribute");
  });
  test("A crew cannot take a second assignment or a builder's existing contract", [] {
    auto w = small();
    rich(w);
    Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
    w.fundStructuralWork(w.dormOwner, p);
    w.assignStructuralTeam(w.dormOwner, p, t);
    Id q = w.proposePartition(w.dormOwner, 0, {90, 0, 100}, {98, 0, 100}, true);
    w.fundStructuralWork(w.dormOwner, q);
    rejects([&] { w.assignStructuralTeam(w.dormOwner, q, t); });
    auto bed = w.commissionBed(w.dormOwner, 0, {110, 0, 100});
    rejects([&] { w.assignHousingWork(2, bed); });
  });
  test("Dead hauler drops actual materials and another crew member can recover them", [] {
    auto w = small();
    rich(w);
    Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
    w.fundStructuralWork(w.dormOwner, p);
    w.assignStructuralTeam(w.dormOwner, p, t);
    auto &j = w.structuralWorks[p];
    j.hauler = 2;
    place(w, 2, w.facilities[j.workshop].pos + Vec3{1.6f, 0, 0});
    w.beginStructuralWork(2, p);
    w.structuralTick();
    Id parcel = j.material;
    w.kill(2);
    w.structuralTick();
    require(w.constructionTeams[t].foreman == 3, "Foreman not replaced");
    place(w, 3, w.items[parcel].pos);
    w.beginStructuralWork(3, p);
    w.structuralTick();
    require(w.items[parcel].carrier == 3, "Dropped materials not recovered");
    place(w, 3, j.site);
    w.beginStructuralWork(3, p);
    w.structuralTick();
    finish(w, p);
    require(j.material == parcel, "Replacement materials were invented");
  });
  test("Cancelled delivered construction refunds only unspent funds and retains real materials",
       [] {
         auto w = small();
         rich(w);
         Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
         w.fundStructuralWork(w.dormOwner, p);
         w.assignStructuralTeam(w.dormOwner, p, t);
         supply(w, p);
         auto before = w.accounts[w.dormAccount].balance;
         auto escrow = w.accounts[w.structuralWorks[p].escrow].balance;
         w.cancelStructuralWork(w.dormOwner, p);
         require(w.accounts[w.dormAccount].balance == before + escrow,
                 "Unspent balance not refunded");
         auto &i = w.items[w.structuralWorks[p].material];
         require(!i.consumed && i.reservedAt == none && i.owner == w.dormAccount,
                 "Paid materials disappeared");
         require(!w.interiorWalls[0].active && w.constructionTeams[t].work == none,
                 "Cancelled crew or geometry remained active");
         w.validate();
       });
  test("Completed partitions can be physically removed by a new contract", [] {
    auto w = small();
    rich(w);
    Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
    w.fundStructuralWork(w.dormOwner, p);
    w.assignStructuralTeam(w.dormOwner, p, t);
    supply(w, p);
    finish(w, p);
    Id q = w.proposePartitionRemoval(w.dormOwner, 0);
    w.fundStructuralWork(w.dormOwner, q);
    w.assignStructuralTeam(w.dormOwner, q, t);
    supply(w, q);
    finish(w, q);
    require(!w.interiorWalls[0].active && w.clearPosition({71, 0, 100}),
            "Demolished wall still blocks walking");
    w.validate();
  });
  test("Intermediate-floor removal preserves indices and affects gravity; rebuilding restores "
       "support",
       [] {
         auto w = small();
         rich(w);
         cleared(w, 5);
         int floors = w.config.floors;
         Id p = w.proposeFloorChange(w.dormOwner, 5, false),
            t = crew(w, w.structuralWorks[p].workshop);
         w.fundStructuralWork(w.dormOwner, p);
         w.assignStructuralTeam(w.dormOwner, p, t);
         supply(w, p);
         finish(w, p);
         require(!w.layout(5).supported({100, 15, 100}) && w.config.floors == floors &&
                     w.layout(6).supported({100, 18, 100}),
                 "Floor identity/support changed incorrectly");
         place(w, 4, {100, 15, 100});
         auto hp = w.residents[4].health;
         w.stepBody(4, {}, .1f);
         w.advance(100);
         require(std::abs(w.position(4).y - 12) < .01f && w.residents[4].health < hp,
                 "Body failed to fall through removed slab");
         place(w, 4, {100, 0, 100});
         Id q = w.proposeFloorChange(w.dormOwner, 5, true);
         w.fundStructuralWork(w.dormOwner, q);
         w.assignStructuralTeam(w.dormOwner, q, t);
         supply(w, q);
         finish(w, q);
         require(w.layout(5).supported({100, 15, 100}), "Rebuilt slab has no support");
         w.validate();
       });
  test("Floor removal requires protected housing/business clearance and rejects the entrance", [] {
    auto w = small();
    rich(w);
    rejects([&] { w.proposeFloorChange(w.dormOwner, 0, false); });
    Id p = w.proposeFloorChange(w.dormOwner, 1, false);
    rejects([&] { w.fundStructuralWork(w.dormOwner, p); });
    require(w.floorPlans[1].deck, "Occupied floor was removed");
    cleared(w, 5);
    Id q = w.proposeFloorChange(w.dormOwner, 5, false);
    place(w, 7, {100, 15, 100});
    rejects([&] { w.fundStructuralWork(w.dormOwner, q); });
  });
  test("An active structural floor contract blocks new beds, businesses and expansions", [] {
    auto w = small();
    rich(w);
    cleared(w, 5);
    Id p = w.proposeFloorChange(w.dormOwner, 5, false);
    w.fundStructuralWork(w.dormOwner, p);
    rejects([&] { w.commissionBed(w.dormOwner, 5, {100, 15, 100}); });
    rejects([&] { w.commissionFacility(w.dormOwner, FacilityKind::Toilet, 5, {100, 15, 100}, 1); });
    rejects([&] { w.commissionExpansion(w.dormOwner, 5, 600, 512); });
  });
  test("A ladder cannot dismount onto a removed floor", [] {
    auto w = small();
    cleared(w, 5);
    w.floorPlans[5].deck = false;
    auto &r = w.residents[4];
    r.ladder = 0;
    r.pos = w.layout().ladder(0, 15);
    rejects([&] { w.releaseLadder(4); });
    require(r.ladder == 0, "Unsafe dismount accepted");
  });
  test("Negotiations preserve owner rights, counteroffers, replacement selection and rent debt",
       [] {
         auto w = small();
         rich(w);
         Id home = w.residentHomes[4];
         auto &h = w.housing[home];
         h.owner = w.residents[5].account;
         h.arrears = 5000;
         Id p = w.proposeHomeRemoval(w.dormOwner, home, 1000);
         w.counterHomeRemoval(4, p, 90000);
         w.counterHomeRemoval(5, p, h.price + 50000);
         rejects([&] { w.fundHomeRemoval(w.dormOwner, p); });
         Id replacement = none;
         for (Id i = 0; i < Id(w.housing.size()); ++i)
           if (i != home && w.housing[i].active && w.housing[i].tenant == none) {
             replacement = i;
             break;
           }
         require(replacement != none, "No replacement");
         w.residents[4].automatic = w.residents[5].automatic = true;
         w.reviseHomeRemoval(w.dormOwner, p, 90000, h.price + 50000, replacement);
         require(w.housingWorks[p].consented.contains(4) && w.housingWorks[p].consented.contains(5),
                 "Fair counteroffer not accepted");
         auto before = w.accounts[w.residents[4].account].balance;
         w.fundHomeRemoval(w.dormOwner, p);
         require(w.residentHomes[4] == replacement &&
                     w.accounts[w.residents[4].account].balance == before + 85000,
                 "Compensation/debt/replacement inconsistent");
         require(h.arrears == 0 && h.tenant == none, "Old lease debt survived settlement");
         w.validate();
       });
  test("Refused relocation and changed ownership cannot reuse stale consent", [] {
    auto w = small();
    rich(w);
    Id home = w.residentHomes[4];
    auto &h = w.housing[home];
    Id p = w.proposeHomeRemoval(w.dormOwner, home, 100000);
    w.replyHomeRemoval(4, p, false);
    rejects([&] { w.fundHomeRemoval(w.dormOwner, p); });
    w.consentHomeRemoval(4, p);
    h.owner = w.residents[6].account;
    rejects([&] { w.fundHomeRemoval(w.dormOwner, p); });
    w.cancelHomeRemoval(w.dormOwner, p);
    require(w.housing[home].active && w.housing[home].tenant == 4, "Withdrawal changed property");
    w.validate();
  });
  test("Crew, active materials, attendance and negotiation state survive save/reload", [] {
    auto w = small();
    rich(w);
    Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
    w.fundStructuralWork(w.dormOwner, p);
    w.assignStructuralTeam(w.dormOwner, p, t);
    supply(w, p);
    attend(w, p);
    w.structuralTick();
    Id h = w.proposeHomeRemoval(w.dormOwner, w.residentHomes[6], 100);
    w.counterHomeRemoval(6, h, 100000);
    auto path = std::filesystem::temp_directory_path() / "mega-structural-test.mdsave";
    w.save(path);
    auto loaded = World::load(path);
    require(loaded.constructionTeams[t].members.size() == 2 &&
                loaded.housingWorks[h].demands.at(6) == 100000,
            "Saved work/claims missing");
    finish(loaded, p);
    loaded.validate();
    std::filesystem::remove(path);
  });
  test("Malformed construction references and missing entrance support are rejected", [] {
    auto w = small();
    rich(w);
    Id p = partition(w);
    w.structuralWorks[p].workshop = 999999;
    rejects([&] { w.validate(); });
    w = small();
    w.floorPlans[0].deck = false;
    rejects([&] { w.validate(); });
  });
  test("Autonomous crews walk, collect supplies and finish without scripted position changes", [] {
    auto w = small();
    rich(w);
    Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
    w.fundStructuralWork(w.dormOwner, p);
    w.assignStructuralTeam(w.dormOwner, p, t);
    w.schedule(w.time + 1, EventKind::FacilityTick, 0);
    for (Id id : w.constructionTeams[t].members)
      w.schedule(w.time + 1, EventKind::Decide, id);
    w.advance(6 * 3600);
    require(w.structuralWorks[p].state == StructuralState::Completed,
            "Autonomous real-travel construction failed");
    w.validate();
  });

  test("Connected room partitions allow joints but reject sealing an occupied escape route", [] {
    auto w = small();
    rich(w);
    for (auto &r : w.residents)
      place(w, r.id, {130 + r.id * 2.f, 0, 130});
    Id t = none;
    for (auto ends : {std::pair{Vec3{90, 0, 100}, Vec3{100, 0, 100}},
                      std::pair{Vec3{90, 0, 100}, Vec3{90, 0, 110}},
                      std::pair{Vec3{100, 0, 100}, Vec3{100, 0, 110}}}) {
      Id p = w.proposePartition(w.dormOwner, 0, ends.first, ends.second, false);
      if (t == none)
        t = crew(w, w.structuralWorks[p].workshop);
      w.fundStructuralWork(w.dormOwner, p);
      w.assignStructuralTeam(w.dormOwner, p, t);
      supply(w, p);
      finish(w, p);
    }
    place(w, 7, {95, 0, 105});
    rejects([&] { w.proposePartition(w.dormOwner, 0, {90, 0, 110}, {100, 0, 110}, false); });
    Id door = w.proposePartition(w.dormOwner, 0, {90, 0, 110}, {100, 0, 110}, true);
    require(w.structuralWorks[door].state == StructuralState::Proposed,
            "Safe door proposal rejected");
  });
  test(
      "Forced relocation requires actual confrontation and preserves compensation and consequences",
      [] {
        auto w = small();
        rich(w);
        Id home = w.residentHomes[4];
        Id p = w.proposeHomeRemoval(w.dormOwner, home, 100000);
        w.replyHomeRemoval(4, p, false);
        rejects([&] { w.fundHomeRemoval(w.dormOwner, p, true); });
        w.residents[4].surrendered = true;
        rejects([&] { w.fundHomeRemoval(w.dormOwner, p, true); });
        place(w, w.dormOwner, w.position(4) + Vec3{1, 0, 0});
        auto cash = w.accounts[w.residents[4].account].balance;
        w.fundHomeRemoval(w.dormOwner, p, true);
        require(w.accounts[w.residents[4].account].balance == cash + 100000 &&
                    !w.incidents.empty() && w.housingWorks[p].forced,
                "Forced displacement escaped payment or consequences");
      });
  test("Manual residents cannot be conscripted into a crew and a player can voluntarily join", [] {
    auto w = small();
    rich(w);
    Id p = partition(w);
    Id shop = w.structuralWorks[p].workshop;
    builder(w, 2, shop);
    builder(w, 3, shop);
    Id t = w.createConstructionTeam(2, shop, 2);
    w.residents[3].automatic = false;
    rejects([&] { w.recruitConstructionWorker(2, t, 3); });
    w.recruitConstructionWorker(3, t, 3);
    require(w.constructionTeams[t].members.contains(3), "Voluntary crew join failed");
    w.removeConstructionWorker(3, t, 3);
    require(!w.constructionTeams[t].members.contains(3), "Worker could not resign from team");
  });
  test("A tenant who also owns their bed must accept both the property buyout and moving offer",
       [] {
         auto w = small();
         rich(w);
         Id home = w.residentHomes[4];
         w.housing[home].owner = w.residents[4].account;
         w.residents[4].automatic = true;
         Id p = w.proposeHomeRemoval(w.dormOwner, home, 100000);
         w.reviseHomeRemoval(w.dormOwner, p, 100000, 0);
         rejects([&] { w.fundHomeRemoval(w.dormOwner, p); });
         require(!w.housingWorks[p].consented.contains(4),
                 "Tenant consent bypassed ownership rights");
       });

  test("Ladder landing entrants retreat physically to let descending traffic pass", [] {
    auto w = small();
    auto l = w.layout();
    Vec3 upper = l.ladder(1, 4.6f), entry = l.ladder(1, 3), bottom = l.ladder(1, 0);
    place(w, 2, upper);
    place(w, 3, entry);
    auto &a = w.residents[2];
    auto &b = w.residents[3];
    a.route = {{upper, w.time}, {bottom, w.time + 1000}, {l.landing(1, 0), w.time + 1100}};
    a.activity = Activity::Walking;
    b.route = {{l.landing(1, 3), w.time - 1},
               {entry, w.time},
               {bottom, w.time + 1000},
               {l.landing(1, 0), w.time + 1100}};
    b.activity = Activity::Walking;
    w.focus(1);
    w.moveCrowd(48 * 5);
    require(w.position(2).y < 1 && w.position(3).y < 2, "Landing entrant deadlocked the ladder");
  });

  test("Whole-floor clearance prepares separate protected offers without paying or removing "
       "property",
       [] {
         auto w = small();
         rich(w);
         Id p = w.proposeFloorChange(w.dormOwner, 1, false);
         auto cash = w.accounts[w.dormAccount].balance;
         int homes = 0;
         for (const auto &h : w.housing)
           if (h.floor == 1 && h.active)
             ++homes;
         w.planFloorClearance(w.dormOwner, p);
         require(int(w.housingWorks.size()) == homes, "Clearance missed housing claims");
         auto count = w.housingWorks.size();
         w.planFloorClearance(w.dormOwner, p);
         require(w.housingWorks.size() == count, "Duplicate clearance claims");
         require(w.accounts[w.dormAccount].balance == cash && w.floorPlans[1].deck,
                 "Preparing offers spent money or erased a floor");
         rejects([&] { w.fundStructuralWork(w.dormOwner, p); });
         w.validate();
       });
  test("Cancelled material crates require actual collection and workshop return for a single "
       "buyback",
       [] {
         auto w = small();
         rich(w);
         Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
         w.fundStructuralWork(w.dormOwner, p);
         w.assignStructuralTeam(w.dormOwner, p, t);
         supply(w, p);
         auto &j = w.structuralWorks[p];
         w.cancelStructuralWork(w.dormOwner, p);
         auto stock = w.facilities[j.workshop].stock;
         place(w, 2, {150, 0, 130});
         rejects([&] { w.collectStructuralSalvage(2, p); });
         place(w, 2, w.items[j.material].pos);
         w.collectStructuralSalvage(2, p);
         rejects([&] { w.returnStructuralSalvage(2, p); });
         place(w, 2, w.facilities[j.workshop].pos + Vec3{1.6f, 0, 0});
         auto cash = w.accounts[j.payer].balance;
         w.returnStructuralSalvage(2, p);
         require(w.accounts[j.payer].balance == cash + 80000 &&
                     w.facilities[j.workshop].stock == stock + 4 && w.items[j.material].consumed,
                 "Buyback did not return real stock and money");
         rejects([&] { w.returnStructuralSalvage(2, p); });
         w.validate();
       });
  test("Foremen can resign with a real replacement, and an empty crew releases its contract", [] {
    auto w = small();
    rich(w);
    Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
    w.fundStructuralWork(w.dormOwner, p);
    w.assignStructuralTeam(w.dormOwner, p, t);
    w.removeConstructionWorker(2, t, 2);
    require(w.constructionTeams[t].foreman == 3, "No replacement foreman");
    w.removeConstructionWorker(3, t, 3);
    require(w.structuralWorks[p].state == StructuralState::Funded &&
                w.structuralWorks[p].team == none,
            "Empty team retained work");
    rejects([&] { w.recruitConstructionWorker(3, t, 3); });
    w.validate();
  });
  test("Unrelated ladder traffic cannot indefinitely block a distant partition's completion", [] {
    auto w = small();
    rich(w);
    Id p = partition(w), t = crew(w, w.structuralWorks[p].workshop);
    w.fundStructuralWork(w.dormOwner, p);
    w.assignStructuralTeam(w.dormOwner, p, t);
    supply(w, p);
    auto a = w.layout().ladder(1, 4.6f);
    place(w, 9, a);
    w.residents[9].route = {{a, w.time},
                            {w.layout().ladder(1, 0), w.time + 1000000},
                            {w.layout().landing(1, 0), w.time + 1100000},
                            {Vec3{8, 0, 8}, w.time + 1200000}};
    w.residents[9].activity = Activity::Walking;
    finish(w, p);
    require(w.interiorWalls[0].active, "Unrelated traffic prevented construction");
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
