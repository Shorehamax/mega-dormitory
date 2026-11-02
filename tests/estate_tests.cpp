#include "mega/world.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <numeric>
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
  require(no, "Invalid storage action succeeded");
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
void place(World &w, Id who, Vec3 p) {
  auto &r = w.residents[who];
  r.pos = p;
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
  for (auto &r : w.residents) {
    w.clockOut(r.id);
    w.leaveQueue(r.id);
    place(w, r.id, {100 + r.id * 2.f, 0, 100});
    r.automatic = false;
    r.role = Role::Unemployed;
    r.jobSite = r.employer = none;
    r.age = 30;
    r.hunger = r.thirst = r.bladder = .1f;
    r.health = 100;
    r.energy = r.hygiene = 1;
    r.needsAt = w.time;
  }
  for (auto &f : w.facilities) {
    f.employees.clear();
    f.staffNeeded = 0;
    f.opensAt = 0;
    f.openHours = 24;
    f.queue.clear();
    f.serving.clear();
  }
  w.crowd.clear();
  return w;
}
Id item(World &w, Id who, ItemKind kind = ItemKind::Phone) {
  Item i;
  i.id = Id(w.items.size());
  i.owner = w.residents[who].account;
  i.carrier = who;
  i.pos = w.position(who);
  i.kind = kind;
  w.items.push_back(i);
  w.residents[who].items.push_back(i.id);
  return i.id;
}
Id death(World &w, Id who = 0, Id heir = 2) {
  w.nominatePersonalHeir(who, heir);
  w.kill(who);
  return Id(w.estates.size() - 1);
}
void mature(World &w) { w.time += 86400; }
Money total(const World &w) {
  Money n = 0;
  for (const auto &a : w.accounts)
    n += a.balance;
  return n;
}
int main() {
  test("Personal nominations require a different living adult", [] {
    auto w = small();
    rejects([&] { w.nominatePersonalHeir(0, 0); });
    w.residents[2].age = 16;
    rejects([&] { w.nominatePersonalHeir(0, 2); });
    w.residents[2].age = 30;
    w.nominatePersonalHeir(0, 2);
    w.nominatePersonalHeir(0, none);
    require(!w.personalHeirs.contains(0), "Will not removed");
  });
  test("Death opens one estate and review protects against immediate transfer", [] {
    auto w = small();
    Id e = death(w);
    Id account = w.residents[0].account;
    require(w.estates.size() == 1 && !w.estates[e].accepted, "Estate missing or instant");
    w.openEstate(0);
    require(w.estates.size() == 1, "Duplicate estate");
    rejects([&] { w.acceptEstate(2, e); });
    mature(w);
    rejects([&] { w.acceptEstate(3, e); });
    Money cash = w.accounts[account].balance;
    Money before = w.accounts[w.residents[2].account].balance;
    w.acceptEstate(2, e);
    require(w.accounts[account].balance == 0 &&
                w.accounts[w.residents[2].account].balance == before + cash,
            "Cash not inherited exactly");
    rejects([&] { w.acceptEstate(2, e); });
    require(total(w) == 0, "Inheritance invented money");
    w.validate();
  });
  test("Inherited belongings stay at the body until physically collected", [] {
    auto w = small();
    Id i = item(w, 0);
    Vec3 original = w.position(0);
    Id e = death(w);
    mature(w);
    w.acceptEstate(2, e);
    require(w.items[i].owner == w.residents[2].account && w.items[i].carrier == none &&
                length(w.items[i].pos - original) < .001f,
            "Inherited item teleported");
    rejects([&] { w.collectBelonging(2, i); });
    rejects([&] { w.collectBelonging(3, i); });
    place(w, 2, original + Vec3{.8f, 0, 0});
    w.collectBelonging(2, i);
    require(w.items[i].carrier == 2 && w.hasDevice(2), "Real collection did not grant device");
    rejects([&] { w.collectBelonging(2, i); });
    w.validate();
  });
  test("Occupied housing, vehicles and personal businesses keep real occupants", [] {
    auto w = small();
    Id source = w.residents[0].account, dest = w.residents[2].account;
    auto &h = w.housing[w.residentHomes[3]];
    h.owner = source;
    Id tenant = h.tenant;
    h.arrears = 1234;
    auto &f = w.facilities[0];
    f.owner = source;
    f.manager = 0;
    Id fid = f.id;
    auto employees = f.employees;
    auto &v = w.vehicles[2];
    v.owner = source;
    Vec3 car = v.pos;
    Id bay = v.bay;
    Money treasury = w.accounts[w.dormAccount].balance;
    Id e = death(w);
    mature(w);
    w.acceptEstate(2, e);
    require(h.owner == dest && h.tenant == tenant && h.arrears == 1234,
            "Tenant rights/arrears lost");
    require(w.facilities[fid].owner == dest && w.facilities[fid].manager == 2 &&
                w.facilities[fid].employees == employees,
            "Business staff or title lost");
    require(v.owner == dest && length(v.pos - car) < .001f && v.bay == bay,
            "Vehicle teleported or lost parking");
    require(w.accounts[w.dormAccount].balance == treasury, "Personal estate took dorm treasury");
    w.validate();
  });
  test("Deceased tenant arrears survive cancellation of their own lease", [] {
    auto w = small();
    Id home = w.residentHomes[0];
    require(home != none, "No original lease");
    auto &h = w.housing[home];
    h.arrears = 25000;
    Id creditor = h.owner;
    Money before = w.accounts[creditor].balance;
    Id e = death(w);
    require(h.tenant == none && w.residentHomes[0] == none && w.estates[e].debts.size() == 1,
            "Lease debt lost at death");
    mature(w);
    w.acceptEstate(2, e);
    require(w.accounts[creditor].balance == before + 25000 && w.estates[e].debts[0].remaining == 0,
            "Creditor was not paid");
    w.validate();
  });
  test("Employee wage claims follow the inherited employer without forgiveness", [] {
    auto w = small();
    Id source = w.residents[0].account, dest = w.residents[2].account;
    w.residents[3].routine.wageClaims[source] = 6000;
    w.accounts[source].balance = 0;
    // Preserve the conserved ledger while making the estate cash-poor.
    Money imbalance = total(w);
    w.accounts[0].balance -= imbalance;
    Id e = death(w);
    mature(w);
    w.acceptEstate(2, e);
    require(!w.residents[3].routine.wageClaims.contains(source) &&
                w.residents[3].routine.wageClaims[dest] == 6000,
            "Employee debt disappeared");
    Money before = w.accounts[w.residents[3].account].balance;
    w.clockOut(3);
    require(w.accounts[w.residents[3].account].balance == before + 6000,
            "Inherited wage debt not collectible");
    w.validate();
  });
  test("An heir also receives the deceased's unpaid wage receivables", [] {
    auto w = small();
    Id employer = w.facilities[0].owner;
    w.residents[0].routine.wageClaims[employer] = 9000;
    w.pay(employer, 0, w.accounts[employer].balance, "fixture-empty-employer");
    Id e = death(w);
    mature(w);
    w.acceptEstate(2, e);
    require(w.residents[0].routine.wageClaims.empty() &&
                w.residents[2].routine.wageClaims[employer] == 9000,
            "Unpaid salary not inherited");
    w.pay(0, employer, 9000, "fixture-fund-employer");
    Money before = w.accounts[w.residents[2].account].balance;
    w.clockOut(2);
    require(w.accounts[w.residents[2].account].balance == before + 9000,
            "Receivable cannot be collected");
    w.validate();
  });
  test("Stored property is inherited in place without sharing other depositors' goods", [] {
    auto w = small();
    Id f = w.nearestAt({0, 0, 0}, FacilityKind::Locker);
    place(w, 0, w.serviceApproach(f));
    Id lease = w.rentStorage(0, f);
    place(w, 0, w.storagePosition(lease));
    w.setStorageOpen(0, lease, true);
    Id i = item(w, 0);
    w.depositStorage(0, lease, i, false);
    Vec3 shelf = w.items[i].pos;
    Id e = death(w);
    w.storageTick();
    require(!w.storageLeases[lease].active, "Dead custodian kept paying rent");
    mature(w);
    w.acceptEstate(2, e);
    require(w.items[i].storedAt == lease && length(w.items[i].pos - shelf) < .001f &&
                w.storageLeases[lease].holder == w.residents[2].account,
            "Storage teleported or lease lost");
    place(w, 2, w.storagePosition(lease));
    w.setStorageOpen(2, lease, true);
    w.withdrawStorage(2, i);
    require(w.items[i].carrier == 2, "Heir cannot collect stored inheritance");
    w.validate();
  });
  test("Stolen-property claims follow title without recovering the item remotely", [] {
    auto w = small();
    Id i = item(w, 3);
    w.items[i].stolenFrom = w.residents[0].account;
    Id e = death(w);
    mature(w);
    w.acceptEstate(2, e);
    require(w.items[i].stolenFrom == w.residents[2].account && w.items[i].carrier == 3 &&
                w.items[i].owner == w.residents[3].account,
            "Inheritance erased theft or recovered remotely");
    w.validate();
  });
  test("Late construction refunds are distributed exactly once", [] {
    auto w = small();
    Id e = death(w);
    mature(w);
    w.acceptEstate(2, e);
    Id dest = w.residents[2].account;
    Money before = w.accounts[dest].balance;
    w.pay(0, w.residents[0].account, 12345, "fixture-late-refund");
    w.estateTick();
    require(w.accounts[dest].balance == before + 12345, "Late refund stranded");
    w.estateTick();
    require(w.accounts[dest].balance == before + 12345, "Late refund duplicated");
    w.validate();
  });
  test("Rejecting an indebted estate creates no personal obligation", [] {
    auto w = small();
    Id e = death(w);
    mature(w);
    Money before = w.accounts[w.residents[2].account].balance;
    w.declineEstate(2, e);
    require(!w.estates[e].accepted && w.estates[e].beneficiary == none, "Refusal ignored");
    w.estateTick();
    require(w.accounts[w.residents[2].account].balance == before, "Rejected heir paid estate debt");
    w.validate();
  });
  test("Automatic heirs can accept but manually controlled heirs are not forced", [] {
    auto w = small();
    Id e = death(w);
    mature(w);
    w.estateTick();
    require(!w.estates[e].accepted, "Manual heir accepted without consent");
    w.residents[2].automatic = true;
    w.estateTick();
    require(w.estates[e].accepted, "NPC ignored solvent inheritance");
    w.validate();
  });
  test("A successor must consent again to an unfunded property change", [] {
    auto w = small();
    Id f = w.nearestAt({0, 0, 0}, FacilityKind::Food);
    w.facilities[f].owner = w.residents[0].account;
    w.facilities[f].manager = 0;
    Id project = w.proposeProject(w.dormOwner, f, true, 100000);
    require(w.projects[project].affected.contains(0), "Owner not protected");
    w.consentProject(project, 0);
    Id e = death(w);
    mature(w);
    w.acceptEstate(2, e);
    require(w.projects[project].affected.contains(2) &&
                !w.projects[project].consented.contains(2) &&
                !w.projects[project].consented.contains(0),
            "Old consent displaced new owner");
    w.consentProject(project, 2);
    w.validate();
  });
  test("Estate status, obligations and wills survive save and load", [] {
    auto w = small();
    Id e = death(w);
    std::string path = "/tmp/mega-estates-test.mdsave";
    w.save(path);
    auto v = World::load(path);
    require(v.personalHeirs.at(0) == 2 && v.estates[e].beneficiary == 2 && !v.estates[e].accepted,
            "Estate review lost");
    mature(v);
    v.acceptEstate(2, e);
    v.save(path);
    auto q = World::load(path);
    require(q.estates[e].accepted && q.estates[e].acceptedAt == v.time, "Accepted estate lost");
    q.validate();
  });
  test("Estate validation rejects invented heirs and impossible times", [] {
    auto w = small();
    Id e = death(w);
    w.estates[e].beneficiary = Id(w.residents.size());
    rejects([&] { w.validate(); });
    w.estates[e].beneficiary = 2;
    w.estates[e].reviewAt = w.time;
    rejects([&] { w.validate(); });
  });
  test("Automatic heirs actually walk to collect inherited goods", [] {
    auto w = small();
    Id i = w.residents[0].items.front();
    Vec3 body = w.position(0);
    Id e = death(w);
    mature(w);
    w.acceptEstate(2, e);
    place(w, 2, body + Vec3{9, 0, 1});
    w.residents[2].automatic = true;
    w.residents[2].routine.sleepStart = 22;
    require(w.estateDecision(2), "Heir ignored nearby belongings");
    require(w.items[i].carrier == none && !w.residents[2].route.empty(),
            "Collection teleported goods");
    for (int step = 0; step < 180 && w.items[i].carrier != 2; ++step)
      w.advance(10);
    require(w.items[i].carrier == 2, "Heir did not reach and collect inheritance");
    w.validate();
  });
  test("Late money leaves the building when the heir emigrates", [] {
    auto w = small();
    Id e = death(w);
    mature(w);
    w.acceptEstate(2, e);
    place(w, 2, w.layout(0).entrance());
    w.depart(2);
    Money before = w.accounts[0].balance;
    w.pay(0, w.residents[0].account, 12000, "fixture-external-refund");
    w.estateTick();
    require(w.accounts[0].balance == before && w.accounts[w.residents[2].account].balance == 0,
            "Late inheritance stranded cash in absent wallet");
    w.validate();
  });
  test("Unclaimed estates retain their assets instead of defaulting to the dorm owner", [] {
    auto w = small();
    Id account = w.residents[0].account;
    Money cash = w.accounts[account].balance;
    w.kill(0);
    mature(w);
    w.estateTick();
    require(w.estates[0].beneficiary == none && !w.estates[0].accepted &&
                w.accounts[account].balance == cash,
            "Unclaimed assets disappeared or were confiscated");
    w.validate();
  });
  test("An existing funded parcel can be delivered to the accepting heir", [] {
    auto w = small();
    Id shop = w.nearestAt({0, 0, 0}, FacilityKind::Shop), order = w.order(0, shop, ItemKind::Food);
    w.residentPlans[0].order = order;
    Id e = death(w);
    mature(w);
    w.acceptEstate(2, e);
    require(w.orders[order].buyer == 2 && w.residentPlans[0].order == none &&
                w.residentPlans[2].order == order,
            "Inherited purchase retained stale buyer plan");
    w.chooseJob(3, Role::Courier, shop);
    w.residents[3].routine.shiftStart = 0;
    w.residents[3].routine.shiftHours = 24;
    w.residents[3].routine.daysOff = 0;
    w.assignDelivery(order, 3);
    place(w, 3, w.items[w.orders[order].item].pos);
    w.pickup(order, 3);
    place(w, 3, w.position(2) + Vec3{.8f, 0, 0});
    w.deliver(order, 3);
    require(w.items[w.orders[order].item].carrier == 2 &&
                w.accounts[w.orders[order].escrow].balance == 0,
            "Inherited parcel failed real handover/escrow settlement");
    w.validate();
  });
  test("Accepting an estate nets obligations and receivables against the heir", [] {
    auto w = small();
    Id source = w.residents[0].account, dest = w.residents[2].account;
    w.pay(source, 0, w.accounts[source].balance, "fixture-empty-estate");
    w.pay(dest, 0, w.accounts[dest].balance, "fixture-empty-heir");
    auto &home = w.housing[w.residentHomes[0]];
    home.owner = dest;
    home.arrears = 25000;
    w.residents[2].routine.wageClaims[source] = 6000;
    w.residents[0].routine.wageClaims[dest] = 8000;
    Id e = death(w);
    mature(w);
    w.acceptEstate(2, e);
    require(w.estates[e].debts[0].remaining == 0 &&
                !w.residents[2].routine.wageClaims.contains(dest) &&
                !w.residents[2].routine.wageClaims.contains(source),
            "Heir owes wages or rent to their own account");
    require(w.accounts[source].balance == 0 && w.accounts[dest].balance == 0 && total(w) == 0,
            "Netting self-claims invented cash");
    w.validate();
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
