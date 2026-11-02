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
  for (const auto &road : w.roads)
    w.activateRoad(road.id, false);
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
static Id crew(World &w, Id shop) {
  builder(w, 2, shop);
  builder(w, 3, shop);
  Id t = w.createConstructionTeam(2, shop, 2);
  w.recruitConstructionWorker(2, t, 3);
  return t;
}
static Id renovation(World &w) {
  rich(w);
  Id f = w.nearestAt({0, 0, 0}, FacilityKind::Food);
  Id p = w.proposeProject(w.dormOwner, f, false, 200000);
  auto affected = w.projects[p].affected;
  for (Id r : affected)
    w.consentProject(p, r);
  w.fundProject(p, w.dormOwner);
  return p;
}
static void attend(World &w, Id c, Id worker = 3) {
  place(w, worker, w.crewContractDestination(worker, c));
  w.beginCrewContractWork(worker, c);
}
int main() {
  test("Real helper labor is paid from the original escrow", [] {
    auto w = small();
    Id p = renovation(w),
       t = crew(w, w.nearestAt(w.facilities[w.projects[p].facility].pos, FacilityKind::Workshop));
    Id c = w.assignCrewContract(2, t, CrewContractKind::Facility, p);
    Money before = w.accounts[w.residents[3].account].balance,
          escrow = w.accounts[w.projects[p].escrow].balance;
    double remaining = w.projects[p].remaining;
    attend(w, c);
    w.time += 60;
    w.crewContractTick();
    require(w.projects[p].remaining < remaining, "No helper progress");
    Money wage = w.accounts[w.residents[3].account].balance - before;
    require(wage > 0 && w.accounts[w.projects[p].escrow].balance == escrow - wage,
            "Invented or unpaid helper wages");
    require(total(w) == 0, "Crew created money");
    w.validate();
  });
  test("Remote, unstaffed and off-shift work earns nothing", [] {
    auto w = small();
    Id p = renovation(w),
       t = crew(w, w.nearestAt(w.facilities[w.projects[p].facility].pos, FacilityKind::Workshop));
    Id c = w.assignCrewContract(2, t, CrewContractKind::Facility, p);
    rejects([&] { w.beginCrewContractWork(3, c); });
    attend(w, c);
    double before = w.projects[p].remaining;
    w.residents[3].routine.shiftStart = 18;
    w.residents[3].routine.shiftHours = 1;
    w.time += 60;
    w.crewContractTick();
    require(w.projects[p].remaining == before, "Off-shift work progressed");
    w.residents[3].routine.shiftHours = 24;
    attend(w, c);
    place(w, 3, {120, 0, 120});
    w.residents[3].activity = Activity::Working;
    w.residents[3].task = TaskKind::CrewWork;
    w.residents[3].target = c;
    w.time += 60;
    w.crewContractTick();
    require(w.projects[p].remaining == before, "Remote work progressed");
  });
  test("Helpers cannot build without physically delivered materials", [] {
    auto w = small();
    rich(w);
    Id p = w.commissionFacility(w.dormOwner, FacilityKind::Toilet, 0, {70, 0, 100}, 1);
    Id t = crew(w, w.nearestAt(w.facilities[w.projects[p].facility].pos, FacilityKind::Workshop)),
       c = w.assignCrewContract(2, t, CrewContractKind::Facility, p);
    attend(w, c);
    double before = w.projects[p].remaining;
    w.time += 60;
    w.crewContractTick();
    require(w.projects[p].remaining == before, "Unsupplied construction progressed");
    place(w, 2, w.facilities[w.projects[p].source].pos);
    w.collectMaterials(2, p);
    place(w, 2, w.facilities[w.projects[p].facility].pos);
    w.deliverMaterials(2, p);
    w.time += 60;
    w.crewContractTick();
    require(w.projects[p].remaining < before, "Supplied crew cannot build");
    w.validate();
  });
  test("The physical lead performs final inspection and settles once", [] {
    auto w = small();
    Id p = renovation(w),
       t = crew(w, w.nearestAt(w.facilities[w.projects[p].facility].pos, FacilityKind::Workshop));
    Id c = w.assignCrewContract(2, t, CrewContractKind::Facility, p);
    w.projects[p].remaining = 30;
    attend(w, c);
    w.time += 60;
    w.crewContractTick();
    require(w.projects[p].remaining >= .999 && w.projects[p].state == ProjectState::Working,
            "Helpers bypassed final inspection");
    w.workProject(p, 60);
    require(w.projects[p].state == ProjectState::Working, "Remote lead completed job");
    Id f = w.projects[p].facility;
    int stations = w.facilities[f].stations;
    place(w, 2, w.facilities[f].pos);
    w.beginProjectWork(2, p);
    w.workProject(p, 60);
    w.crewContractTick();
    require(w.projects[p].state == ProjectState::Completed && !w.crewContracts[c].active,
            "Contract not released");
    w.workProject(p, 60);
    require(w.facilities[f].stations == stations + 1, "Completion ran twice");
    w.validate();
  });
  test("A worker cannot hold simultaneous structural and generic contracts", [] {
    auto w = small();
    Id p = renovation(w),
       t = crew(w, w.nearestAt(w.facilities[w.projects[p].facility].pos, FacilityKind::Workshop));
    w.assignCrewContract(2, t, CrewContractKind::Facility, p);
    rich(w);
    Id structure = w.proposePartition(w.dormOwner, 0, {70, 0, 100}, {78, 0, 100}, true);
    w.fundStructuralWork(w.dormOwner, structure);
    rejects([&] { w.assignStructuralTeam(2, structure, t); });
    rejects([&] { w.assignCrewContract(2, t, CrewContractKind::Facility, p); });
  });
  test("Dismissal cancels a helper route without moving the worker", [] {
    auto w = small();
    Id p = renovation(w),
       t = crew(w, w.nearestAt(w.facilities[w.projects[p].facility].pos, FacilityKind::Workshop));
    Id c = w.assignCrewContract(2, t, CrewContractKind::Facility, p);
    w.travel(3, w.crewContractDestination(3, c), TaskKind::CrewWork, c);
    Vec3 before = w.position(3);
    w.removeConstructionWorker(3, t, 3);
    require(length(w.position(3) - before) < .001f && w.residents[3].route.empty() &&
                w.residents[3].task == TaskKind::None,
            "Dismissal teleported or retained helper task");
    w.validate();
  });
  test("Releasing helpers preserves the primary contract and escrow", [] {
    auto w = small();
    Id p = renovation(w),
       t = crew(w, w.nearestAt(w.facilities[w.projects[p].facility].pos, FacilityKind::Workshop));
    Id c = w.assignCrewContract(2, t, CrewContractKind::Facility, p);
    attend(w, c);
    Money cash = w.accounts[w.projects[p].escrow].balance;
    w.cancelCrewContract(2, c);
    require(w.projects[p].worker == 2 && w.projects[p].state == ProjectState::Working &&
                w.accounts[w.projects[p].escrow].balance == cash,
            "Cancellation destroyed original contract");
    w.validate();
  });
  test("Voluntary crews respect a manually controlled foreman", [] {
    auto w = small();
    Id p = renovation(w),
       t = crew(w, w.nearestAt(w.facilities[w.projects[p].facility].pos, FacilityKind::Workshop));
    w.residents[2].automatic = false;
    rejects([&] { w.assignCrewContract(w.dormOwner, t, CrewContractKind::Facility, p); });
    w.assignCrewContract(2, t, CrewContractKind::Facility, p);
  });
  test("Save and restore preserves the assignment and attendance", [] {
    auto w = small();
    Id p = renovation(w),
       t = crew(w, w.nearestAt(w.facilities[w.projects[p].facility].pos, FacilityKind::Workshop));
    Id c = w.assignCrewContract(2, t, CrewContractKind::Facility, p);
    attend(w, c);
    std::string file = "/tmp/mega-crew-test.mdsave";
    w.save(file);
    auto v = World::load(file);
    require(v.crewContractFor(3) == c && v.crewContracts[c].attendance.contains(3),
            "Lost saved attendance");
    v.time += 30;
    v.crewContractTick();
    require(v.projects[p].remaining < w.projects[p].remaining, "Restored helper did not work");
    v.validate();
  });
  test("Housing assistants wait for the lead to deliver actual supplies", [] {
    auto w = small();
    rich(w);
    Id p = w.commissionBed(w.dormOwner, 0, {70, 0, 100});
    Id t = crew(w, w.housingWorks[p].workshop);
    Id c = w.assignCrewContract(2, t, CrewContractKind::Housing, p);
    attend(w, c);
    double before = w.housingWorks[p].remaining;
    w.time += 60;
    w.crewContractTick();
    require(w.housingWorks[p].remaining == before, "Housing built without materials");
    place(w, 2, w.housingWorkDestination(p));
    w.residents[2].activity = Activity::Working;
    w.housingWorkTick();
    require(w.housingWorks[p].material != none, "Lead did not collect material");
    place(w, 2, w.housingWorkDestination(p));
    w.residents[2].activity = Activity::Working;
    w.housingWorkTick();
    require(w.housingWorks[p].supplied, "Lead did not supply housing");
    w.time += 60;
    w.crewContractTick();
    require(w.housingWorks[p].remaining < before, "Housing helper did not work");
    w.validate();
  });
  test("Expansion helper wages conserve its progress payments", [] {
    auto w = small();
    rich(w);
    Id p = w.commissionExpansion(w.dormOwner, 0, 656, 640);
    Id t = crew(w, w.expansions[p].workshop);
    Id c = w.assignCrewContract(2, t, CrewContractKind::Expansion, p);
    attend(w, c);
    double before = w.expansions[p].remaining;
    w.time += 60;
    w.crewContractTick();
    require(w.expansions[p].remaining == before, "Unsupplied expansion progressed");
    place(w, 2, w.expansionDestination(p));
    w.beginExpansionWork(2, p);
    w.workExpansion(p, 60);
    place(w, 2, w.expansionDestination(p));
    w.beginExpansionWork(2, p);
    w.workExpansion(p, 60);
    require(w.expansions[p].supplied, "Lead failed to supply expansion");
    w.time += 60;
    w.crewContractTick();
    require(w.expansions[p].remaining < before && w.expansions[p].paid > 0,
            "Expansion did not pay crew progress");
    require(total(w) == 0, "Expansion crew created money");
    w.validate();
  });
  test("Repair assistants preserve the specialist completion wage", [] {
    auto w = small();
    rich(w);
    Id f = w.nearestAt({0, 0, 0}, FacilityKind::Food);
    w.facilities[f].condition = .5f;
    w.facilities[f].manager = w.dormOwner;
    Id p = w.requestRepair(w.dormOwner, f), shop = w.repairs[p].workshop;
    w.chooseJob(4, Role::Repairer, shop);
    w.residents[4].routine.shiftStart = 0;
    w.residents[4].routine.shiftHours = 24;
    w.residents[4].routine.daysOff = 0;
    w.residents[4].automatic = true;
    w.assignRepair(4, p);
    Id t = crew(w, shop), c = w.assignCrewContract(2, t, CrewContractKind::Repair, p);
    attend(w, c);
    double before = w.repairs[p].remaining;
    w.time += 60;
    w.crewContractTick();
    require(w.repairs[p].remaining == before, "Unsupplied repair progressed");
    place(w, 4, w.facilities[shop].pos);
    w.residents[4].activity = Activity::Working;
    w.repairTick();
    place(w, 4, w.facilities[f].pos);
    w.residents[4].activity = Activity::Working;
    w.time += 60;
    w.crewContractTick();
    require(w.repairs[p].remaining < before, "Supplied repair did not progress");
    w.repairs[p].remaining = 1;
    w.repairTick();
    w.time += 60;
    w.repairTick();
    w.crewContractTick();
    require(w.repairs[p].complete && w.accounts[w.repairs[p].escrow].balance == 0,
            "Specialist final settlement failed");
    w.validate();
  });
  test("Idle foremen choose funded local work without conscripting manual members", [] {
    auto w = small();
    Id p = renovation(w),
       t = crew(w, w.nearestAt(w.facilities[w.projects[p].facility].pos, FacilityKind::Workshop));
    w.residents[3].automatic = false;
    Vec3 before = w.position(3);
    w.crewContractDecision(2);
    require(w.constructionTeams[t].contract != none && w.projects[p].worker == 2,
            "Foreman ignored available contract");
    require(w.residents[3].route.empty() && length(w.position(3) - before) < .001f,
            "Manual helper commandeered");
    w.validate();
  });
  test("Expansion materials survive interruption and death as recoverable cargo", [] {
    auto w = small();
    rich(w);
    Id p = w.commissionExpansion(w.dormOwner, 0, 656, 640);
    Id shop = w.expansions[p].workshop;
    builder(w, 2, shop);
    w.assignExpansion(2, p);
    place(w, 2, w.expansionDestination(p));
    w.beginExpansionWork(2, p);
    w.workExpansion(p, 60);
    Id material = w.expansions[p].material;
    require(material != none, "No expansion cargo");
    Vec3 at = w.position(2);
    w.kill(2);
    require(w.items[material].carrier == none && !w.items[material].consumed &&
                length(w.items[material].pos - at) < .001f,
            "Death destroyed/teleported material");
    builder(w, 3, shop);
    w.assignExpansion(3, p);
    place(w, 3, w.expansionDestination(p));
    w.beginExpansionWork(3, p);
    w.workExpansion(p, 60);
    require(w.residents[3].carrying == material,
            "Replacement worker did not recover original cargo");
    w.validate();
  });
  test("Material delivery at clock-off remains delivered without unpaid construction", [] {
    auto w = small();
    rich(w);
    Id shop = workshop(w);
    builder(w, 2, shop);
    Id job = w.commissionFacility(w.dormOwner, FacilityKind::Toilet, 0, {70, 0, 100}, 1);
    w.assignBuilder(job, 2);
    place(w, 2, w.facilities[w.projects[job].source].pos);
    w.collectMaterials(2, job);
    auto &p = w.projects[job];
    Id cargo = p.materialItem;
    place(w, 2, w.facilities[p.facility].pos + Vec3{0, 0, 1.6f});
    auto &r = w.residents[2];
    r.routine.shiftStart = 6;
    r.routine.shiftHours = 8;
    w.time = 20 * 3600;
    r.task = TaskKind::ConstructionDrop;
    r.target = job;
    Money before = w.accounts[p.escrow].balance;
    double remaining = p.remaining;
    w.arrive(2);
    require(p.materialsDelivered && r.carrying == none && w.items[cargo].carrier == none &&
                length(w.items[cargo].pos - w.facilities[p.facility].pos) < .01f,
            "Clock-off lost or relocated the physical shipment");
    require(r.activity == Activity::Idle && p.workAt < 0,
            "Off-shift delivery started a construction shift");
    w.time += 60;
    w.workProject(job, 60);
    require(p.remaining == remaining && w.accounts[p.escrow].balance == before,
            "Clocked-off worker advanced construction or received wages");
    w.time = 34 * 3600;
    w.beginProjectWork(2, job);
    w.time += 60;
    w.workProject(job, 60);
    require(p.remaining < remaining && w.accounts[p.escrow].balance < before,
            "Next shift did not resume the same supplied paid contract");
    require(total(w) == 0, "Resumption created money");
    w.validate();
  });
  test("Construction-site arrival after shift end is a normal wait", [] {
    auto w = small();
    rich(w);
    Id shop = workshop(w);
    builder(w, 2, shop);
    Id job = w.commissionFacility(w.dormOwner, FacilityKind::Toilet, 0, {70, 0, 100}, 1);
    w.assignBuilder(job, 2);
    place(w, 2, w.facilities[w.projects[job].source].pos);
    w.collectMaterials(2, job);
    auto &p = w.projects[job];
    place(w, 2, w.facilities[p.facility].pos + Vec3{0, 0, 1.6f});
    w.deliverMaterials(2, job);
    auto &r = w.residents[2];
    r.routine.shiftStart = 6;
    r.routine.shiftHours = 8;
    w.time = 20 * 3600;
    r.task = TaskKind::Work;
    r.target = p.facility;
    w.arrive(2);
    require(r.activity == Activity::Idle && p.workAt < 0 && p.worker == 2 &&
                p.state == ProjectState::Working && p.materialsDelivered,
            "End-of-shift arrival lost the contract or began work");
    w.validate();
  });
  test("An off-shift workshop arrival does not buy another material shipment", [] {
    auto w = small();
    rich(w);
    Id shop = workshop(w);
    builder(w, 2, shop);
    Id job = w.commissionFacility(w.dormOwner, FacilityKind::Toilet, 0, {70, 0, 100}, 1);
    w.assignBuilder(job, 2);
    place(w, 2, w.facilities[w.projects[job].source].pos + Vec3{0, 0, 1.6f});
    auto &r = w.residents[2];
    r.routine.shiftStart = 6;
    r.routine.shiftHours = 8;
    w.time = 20 * 3600;
    r.task = TaskKind::ConstructionPickup;
    r.target = job;
    Money escrow = w.accounts[w.projects[job].escrow].balance;
    int stock = w.facilities[w.projects[job].source].stock;
    w.arrive(2);
    require(w.projects[job].materialItem == none && r.carrying == none &&
                w.accounts[w.projects[job].escrow].balance == escrow &&
                w.facilities[w.projects[job].source].stock == stock,
            "Off-shift workshop arrival spent reserved money or stock");
    w.time = 34 * 3600;
    w.collectMaterials(2, job);
    require(w.projects[job].materialItem != none && r.carrying == w.projects[job].materialItem,
            "Next shift could not collect its contracted material");
    w.validate();
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
