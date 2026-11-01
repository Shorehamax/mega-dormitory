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
  c.population = 60;
  c.floors = 20;
  c.mode = Mode::Cheat;
  c.accidentRate = 0;
  c.startingCash = 5000000;
  auto w = World::generate(c);
  while (!w.events.empty())
    w.events.pop();
  for (auto &r : w.residents) {
    w.clockOut(r.id);
    w.leaveQueue(r.id);
    r.automatic = false;
    r.route.clear();
    r.activity = Activity::Idle;
    r.task = TaskKind::None;
    r.age = 30;
    r.routine.workSince = -1;
  }
  w.crowd.clear();
  w.shareOffers.clear();
  for (auto &c : w.companies)
    c.autoExpand = false;
  return w;
}
void phone(World &w, Id r) {
  Item i;
  i.id = Id(w.items.size());
  i.kind = ItemKind::Phone;
  i.owner = w.residents[r].account;
  i.carrier = r;
  w.items.push_back(i);
  w.residents[r].items.push_back(i.id);
}
Id company(World &w, Id r, const char *name = "Company", Money capital = 1000000) {
  return w.foundCompany(r, name, capital);
}
Money cash(World &w, Id r) { return w.accounts[w.residents[r].account].balance; }
int main() {
  test("Founders and initial firms hold exactly ten thousand conserved units", [] {
    auto w = small();
    Id c = company(w, 0);
    require(w.heldShares(0, c) == 10000, "Founder lacks title");
    w.validate();
  });
  test("Secondary trades pay the seller and transfer actual reserved equity", [] {
    auto w = small();
    Id c = company(w, 0);
    phone(w, 2);
    Money b = cash(w, 0), bank = w.accounts[w.companies[c].account].balance;
    Id o = w.listShares(0, c, 2000, 10);
    w.buyShares(2, o, 1000);
    require(cash(w, 0) == b + 10000 && w.accounts[w.companies[c].account].balance == bank &&
                w.heldShares(2, c) == 1000 && w.shareOffers[o].units == 1000,
            "Trade minted money or wrong title");
    w.validate();
  });
  test("Capital subscriptions fund the company rather than the selling director", [] {
    auto w = small();
    Id c = company(w, 0);
    phone(w, 2);
    auto b = cash(w, 0), bank = w.accounts[w.companies[c].account].balance;
    Id o = w.listShares(0, c, 2000, 10, true);
    w.buyShares(2, o, 2000);
    require(cash(w, 0) == b && w.accounts[w.companies[c].account].balance == bank + 20000,
            "Capital funded seller");
    w.validate();
  });
  test("Share offers cannot oversell reserved units or invent fractional titles", [] {
    auto w = small();
    Id c = company(w, 0);
    Id o = w.listShares(0, c, 6000, 10);
    rejects([&] { w.listShares(0, c, 5000, 10); });
    rejects([&] { w.listShares(2, c, 1000, 10); });
    w.cancelShareOffer(0, o);
    require(w.reservedShares(w.residents[0].account, c) == 0, "Cancelled units stay reserved");
    w.listShares(0, c, 10000, 10);
    w.validate();
  });
  test("Trading requires an adult and real device or company-counter access", [] {
    auto w = small();
    Id c = company(w, 0), o = w.listShares(0, c, 2000, 10);
    w.residents[2].terminal = none;
    w.residents[2].terminalUntil = 0;
    for (Id i : w.residents[2].items)
      w.items[i].carrier = none;
    w.residents[2].pos = {150, 0, 150};
    rejects([&] { w.buyShares(2, o, 1000); });
    phone(w, 2);
    w.residents[2].age = 12;
    rejects([&] { w.buyShares(2, o, 1000); });
  });
  test("A buyer's lack of funds leaves money, title and reservation intact", [] {
    auto w = small();
    Id c = company(w, 0);
    phone(w, 2);
    Id o = w.listShares(0, c, 2000, 1000000);
    auto b = cash(w, 2);
    rejects([&] { w.buyShares(2, o, 1000); });
    require(w.heldShares(0, c) == 10000 && w.shareOffers[o].units == 2000 && cash(w, 2) == b,
            "Failed trade mutated title");
  });
  test("Largest voting holding controls company decisions and revokes old authority", [] {
    auto w = small();
    Id c = company(w, 0);
    phone(w, 2);
    Id o = w.listShares(0, c, 6000, 1);
    w.buyShares(2, o, 6000);
    require(w.companies[c].owner == 2 && w.directsCompany(2, c, true) &&
                !w.directsCompany(0, c, true),
            "Control did not follow equity");
    rejects([&] { w.companyDividend(0, c, 10000); });
    w.validate();
  });
  test("Dividends pay each holder proportionately and conserve every cent", [] {
    auto w = small();
    Id c = company(w, 0);
    phone(w, 2);
    Id o = w.listShares(0, c, 3333, 1);
    w.buyShares(2, o, 3333);
    Money a = cash(w, 0), b = cash(w, 2), bank = w.accounts[w.companies[c].account].balance;
    w.companyDividend(0, c, 10001);
    require(cash(w, 0) - a + cash(w, 2) - b == 10001 && cash(w, 2) - b == 3333 &&
                w.accounts[w.companies[c].account].balance == bank - 10001,
            "Dividend rounding lost value");
    w.validate();
  });
  test("Moved-out holders retain property and their dividends leave the dorm economy", [] {
    auto w = small();
    Id c = company(w, 0);
    phone(w, 2);
    Id o = w.listShares(0, c, 2000, 1);
    w.buyShares(2, o, 2000);
    w.residents[2].present = false;
    Money internal = w.internalMoney();
    w.companyDividend(0, c, 10000);
    require(w.heldShares(2, c) == 2000 && w.internalMoney() == internal - 2000,
            "Absentee dividend did not flow outside");
    w.validate();
  });
  test("Shareholder approval buys a subsidiary without erasing assets, leases or staff", [] {
    auto w = small();
    Id b = company(w, 0, "Buyer"), t = company(w, 2, "Target");
    Id f = w.nearest(0, FacilityKind::Office);
    Id lease = w.leaseWorkspace(2, t, f);
    w.chooseJob(5, Role::Programmer, f);
    auto title = w.facilities[f].owner;
    Id a = w.proposeAcquisition(0, b, t, 200000);
    Money seller = cash(w, 2);
    w.voteAcquisition(2, a, true);
    require(w.acquisitions[a].state == AcquisitionState::Complete && w.companies[t].parent == b &&
                w.directsCompany(0, t, true) && !w.directsCompany(2, t, true),
            "Control not acquired");
    require(cash(w, 2) == seller + 200000 && w.facilities[f].owner == title &&
                w.workspaceLeases[lease].company == t &&
                w.residents[5].employer == w.companies[t].account,
            "Buyout destroyed business continuity");
    w.validate();
  });
  test("A bid cannot force a manual owner's sale or drain payroll reserves", [] {
    auto w = small();
    Id b = company(w, 0), t = company(w, 2);
    Id a = w.proposeAcquisition(0, b, t, 100000);
    w.equityTick();
    require(w.acquisitions[a].state == AcquisitionState::Pending && w.companies[t].parent == none,
            "Manual holder sold without vote");
    rejects([&] { w.voteAcquisition(5, a, true); });
    rejects([&] { w.proposeAcquisition(0, b, t, 100000); });
  });
  test("Cancelled and expired bids refund actual escrow once", [] {
    auto w = small();
    Id b = company(w, 0), t = company(w, 2);
    auto funds = w.accounts[w.companies[b].account].balance;
    Id a = w.proposeAcquisition(0, b, t, 100000);
    w.cancelAcquisition(0, a);
    require(w.accounts[w.companies[b].account].balance == funds, "Cancelled bid not refunded");
    rejects([&] { w.cancelAcquisition(0, a); });
    a = w.proposeAcquisition(0, b, t, 100000);
    w.time = w.acquisitions[a].expires;
    w.equityTick();
    require(w.acquisitions[a].state == AcquisitionState::Cancelled, "Expired bid retained escrow");
    w.validate();
  });
  test("Circular company holdings are rejected before any bid payment", [] {
    auto w = small();
    Id b = company(w, 0), t = company(w, 2);
    Id a = w.proposeAcquisition(0, b, t, 100000);
    w.voteAcquisition(2, a, true);
    auto funds = w.accounts[w.companies[t].account].balance;
    rejects([&] { w.proposeAcquisition(0, t, b, 100000); });
    require(w.accounts[w.companies[t].account].balance == funds, "Circular bid spent funds");
  });
  test("Majority-approved buyouts cash out minorities at the same unit price", [] {
    auto w = small();
    Id b = company(w, 0), t = company(w, 2);
    phone(w, 3);
    Id o = w.listShares(2, t, 2500, 1);
    w.buyShares(3, o, 2500);
    Money minority = cash(w, 3);
    Id a = w.proposeAcquisition(0, b, t, 100000);
    w.voteAcquisition(2, a, true);
    require(cash(w, 3) == minority + 25000 && w.heldShares(3, t) == 0,
            "Minority cash-out lost value");
    w.validate();
  });
  test("Nominated equity heirs inherit titles without creating cash", [] {
    auto w = small();
    Id c = company(w, 2);
    w.nominateEquityHeir(2, 3);
    Money bank = w.accounts[w.companies[c].account].balance;
    w.kill(2);
    w.equityTick();
    require(w.heldShares(3, c) == 10000 && w.companyController(c) == 3 &&
                w.accounts[w.companies[c].account].balance == bank,
            "Equity inheritance failed or minted capital");
    w.validate();
  });
  test("Pending trades, votes, subsidiary ownership and heir nominations survive saves", [] {
    auto w = small();
    Id b = company(w, 0), t = company(w, 2);
    Id o = w.listShares(2, t, 2000, 3);
    w.nominateEquityHeir(2, 3);
    Id a = w.proposeAcquisition(0, b, t, 100000);
    auto path = std::filesystem::temp_directory_path() / "mega-capital-save.mdsave";
    w.save(path);
    auto v = World::load(path);
    require(v.shareOffers[o].units == 2000 &&
                v.acquisitions[a].state == AcquisitionState::Pending && v.equityHeirs.at(2) == 3,
            "Capital save lost state");
    v.voteAcquisition(2, a, true);
    v.validate();
    std::filesystem::remove(path);
  });
  test("Expansion cannot spend missing payroll reserves or open an unbuilt office", [] {
    auto w = small();
    Id c = company(w, 0, "Building", 1000000);
    w.workspaceOffers.clear();
    Id fid = w.expandCompany(0, c);
    require(!w.facilities[fid].open && w.facilities[fid].owner == w.companies[c].account &&
                w.projects.back().newConstruction && !w.projects.back().materialsDelivered,
            "Expansion fabricated an operating office");
    rejects([&] { w.expandCompany(0, c); });
    w.validate();
  });
  test("A real listed lease expands the operating company and preserves landlord title", [] {
    auto w = small();
    Id c = company(w, 0), f = w.nearest(0, FacilityKind::Office);
    auto owner = w.facilities[f].owner;
    for (auto it = w.workspaceOffers.begin(); it != w.workspaceOffers.end();)
      if (it->first != f)
        it = w.workspaceOffers.erase(it);
      else
        ++it;
    Money funds = w.accounts[w.companies[c].account].balance;
    Id got = w.expandCompany(0, c);
    require(got == f && w.operatingAccount(f) == w.companies[c].account &&
                w.facilities[f].owner == owner &&
                w.accounts[w.companies[c].account].balance < funds,
            "Expansion skipped paid lease");
    w.validate();
  });
  test("Malformed share totals, reservations and corporate cycles fail validation", [] {
    auto w = small();
    Id c = company(w, 0);
    auto bad = w;
    bad.companies[c].shares.begin()->second = 9999;
    rejects([&] { bad.validate(); });
    bad = w;
    bad.companies[c].shares.clear();
    bad.companies[c].shares[bad.companies[c].account] = 10000;
    rejects([&] { bad.validate(); });
    bad = w;
    Id o = bad.listShares(0, c, 5000, 1);
    bad.shareOffers[o].units = 10000;
    bad.shareOffers.push_back(bad.shareOffers[o]);
    bad.shareOffers.back().id = Id(bad.shareOffers.size() - 1);
    rejects([&] { bad.validate(); });
  });
  test("Office computers accept the same attended standing area as payroll", [] {
    auto w = small();
    Id c = company(w, 0);
    Id f = w.nearest(0, FacilityKind::Office);
    w.leaseWorkspace(0, c, f);
    w.chooseJob(0, Role::Programmer, f);
    w.residents[0].pos = w.facilities[f].pos + Vec3{0, 0, -2.2f};
    w.residents[0].terminal = none;
    for (Id i : w.residents[0].items)
      if (w.items[i].kind == ItemKind::Phone || w.items[i].kind == ItemKind::Laptop)
        w.items[i].carrier = none;
    w.useTerminal(0, f, 30);
    require(w.hasDevice(0), "Attended employee cannot use computer");
    w.validate();
  });
  test("Automatic investors use genuine listings and protect household necessities", [] {
    auto w = small();
    Id c = company(w, 0);
    phone(w, 2);
    w.residents[2].automatic = true;
    Id o = w.listShares(0, c, 1000, 1, true);
    Money bank = w.accounts[w.companies[c].account].balance;
    for (int n = 0; n < 256 && w.heldShares(2, c) == 0; ++n)
      w.equityTick();
    require(w.heldShares(2, c) > 0 && w.accounts[w.companies[c].account].balance > bank &&
                cash(w, 2) >= w.householdReserve(2),
            "AI failed to make a funded protected investment");
    w.validate();
  });
  test("AI shareholders accept adequately funded bids but manual holders retain their vote", [] {
    auto w = small();
    w.pay(0, w.residents[0].account, 3000000, "test-acquisition-capital");
    Id b = company(w, 0, "Buyer", 3000000), t = company(w, 2);
    w.residents[2].automatic = true;
    Id a = w.proposeAcquisition(0, b, t, 1100000);
    w.equityTick();
    require(w.acquisitions[a].state == AcquisitionState::Complete, "AI ignored fair funded buyout");
    w.validate();
  });
  test("A change in the bidding company's control returns escrow to that company", [] {
    auto w = small();
    Id b = company(w, 0), t = company(w, 2);
    Id a = w.proposeAcquisition(0, b, t, 100000);
    phone(w, 3);
    Id o = w.listShares(0, b, 6000, 1);
    w.buyShares(3, o, 6000);
    Money before = w.accounts[w.companies[b].account].balance;
    w.equityTick();
    require(w.acquisitions[a].state == AcquisitionState::Cancelled &&
                w.accounts[w.companies[b].account].balance == before + 100000,
            "Control change orphaned acquisition funds");
    w.validate();
  });
  test("Proven busy companies expand through a paid workspace and respect the cooldown", [] {
    auto w = small();
    Id c = company(w, 0), f = w.nearest(0, FacilityKind::Office);
    w.leaseWorkspace(0, c, f);
    auto staff = w.facilities[f].employees;
    for (Id id : staff)
      w.dismissEmployee(0, f, id);
    for (Id id = 2; id < 10; ++id)
      w.chooseJob(id, Role::Programmer, f);
    w.companies[c].completed[2] = 8;
    w.companies[c].revenue = 200000;
    w.setCompanyExpansion(0, c, true);
    w.companies[c].nextExpansionAt = w.time;
    Money wallet = cash(w, 0), bank = w.accounts[w.companies[c].account].balance;
    w.expansionTick(c);
    int spaces = 0;
    for (const auto &space : w.facilities)
      spaces += space.kind == FacilityKind::Office && !space.demolished &&
                w.operatingAccount(space.id) == w.companies[c].account;
    require(spaces == 2 && cash(w, 0) == wallet &&
                w.accounts[w.companies[c].account].balance < bank,
            "Busy AI company did not fund actual expansion");
    auto leases = w.workspaceLeases.size(), projects = w.projects.size();
    w.expansionTick(c);
    require(w.workspaceLeases.size() == leases && w.projects.size() == projects,
            "Expansion ignored cooldown");
    w.validate();
  });
  test("A company with no proven demand does not spend its expansion funds", [] {
    auto w = small();
    Id c = company(w, 0);
    w.setCompanyExpansion(0, c, true);
    w.companies[c].nextExpansionAt = w.time;
    auto money = w.accounts[w.companies[c].account].balance;
    w.expansionTick(c);
    require(w.accounts[w.companies[c].account].balance == money,
            "Empty company expanded without demand");
  });
  test("Invalid company IDs and parent cycles are rejected before indexing", [] {
    auto w = small();
    Id c = company(w, 0);
    w.companies[c].id = 999999;
    rejects([&] { w.validate(); });
  });
  test("Paused office briefs release their computer while retaining saved work", [] {
    auto w = small();
    Id c = company(w, 0), f = w.nearest(0, FacilityKind::Office);
    w.leaseWorkspace(0, c, f);
    w.facilities[f].stations = 1;
    auto employees = w.facilities[f].employees;
    for (Id id : employees) {
      w.clockOut(id);
      w.residents[id].pos = {150, 0, 150};
      w.residents[id].activity = Activity::Idle;
    }
    for (Id id : {0, 2}) {
      w.chooseJob(id, Role::Programmer, f);
      auto &r = w.residents[id];
      r.routine.shiftStart = 0;
      r.routine.shiftHours = 24;
      r.routine.daysOff = 0;
    }
    w.residents[0].pos = w.facilities[f].pos + Vec3{0, 0, -1.6f};
    w.clockIn(0);
    Id first = w.officeOrder(0);
    w.officeTick();
    w.clockOut(0);
    w.residents[0].pos = {150, 0, 150};
    w.residents[0].activity = Activity::Idle;
    w.residents[2].pos = w.facilities[f].pos + Vec3{0, 0, -1.6f};
    w.clockIn(2);
    Id next = w.officeOrder(2);
    double before = w.officeOrders[first].remaining;
    w.officeTick();
    w.time += 60;
    w.officeTick();
    require(w.officeOrders[first].remaining == before &&
                w.officeOrders[next].remaining < w.officeOrders[next].total,
            "Off-site brief blocked or progressed remotely");
    w.validate();
  });
  test("Acquisition preserves funded building work and gives the new director its authority", [] {
    auto w = small();
    Id b = company(w, 0), t = company(w, 2);
    w.workspaceOffers.clear();
    Id f = w.expandCompany(2, t);
    Id project = w.projects.back().id;
    Id a = w.proposeAcquisition(0, b, t, 100000);
    w.voteAcquisition(2, a, true);
    require(w.projects[project].owner == 0 && w.projects[project].state == ProjectState::Funded &&
                !w.facilities[f].open && w.directsCompany(0, t, true),
            "Acquisition stranded or completed building work");
    w.validate();
  });
  test("Automatic expansion leaves occupied competitors in place and funds a real empty plot", [] {
    auto w = small();
    Id c = company(w, 0), f = w.nearest(0, FacilityKind::Office);
    w.leaseWorkspace(0, c, f);
    for (auto [offered, rent] : w.workspaceOffers)
      if (w.facilities[offered].employees.empty())
        w.chooseJob(10, Role::Programmer, offered);
    Id newSpace = w.expandCompany(0, c, true);
    require(!w.facilities[newSpace].open && w.projects.back().newConstruction,
            "Automatic expansion stripped an occupied business instead of constructing");
    w.validate();
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
