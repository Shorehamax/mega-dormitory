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
Id item(World &w, Id who, ItemKind kind) {
  Item i;
  i.id = Id(w.items.size());
  i.kind = kind;
  i.owner = w.residents[who].account;
  i.carrier = who;
  i.pos = w.position(who);
  w.items.push_back(i);
  w.residents[who].items.push_back(i.id);
  return i.id;
}
Id facility(World &w) {
  Id f = w.nearestAt({0, 0, 0}, FacilityKind::Locker);
  require(f != none, "No generated locker facility");
  return f;
}
Id rent(World &w, Id who = 0, Id group = none) {
  Id f = facility(w);
  place(w, who, w.serviceApproach(f));
  return w.rentStorage(who, f, group);
}
Id group(World &w) {
  place(w, 0, {50, 0, 60});
  place(w, 1, {51, 0, 60});
  w.relationships[1] = {0, 1, .8f, -3600, 0};
  Id h = w.createHousehold(0);
  w.inviteHousehold(0, 1);
  w.acceptHousehold(1, h);
  return h;
}
void open(World &w, Id who, Id lease) {
  place(w, who, w.storagePosition(lease));
  w.setStorageOpen(who, lease, true);
}
void conserved(const World &w) {
  Money sum = std::accumulate(w.accounts.begin(), w.accounts.end(), Money(0),
                              [](Money n, const Account &a) { return n + a.balance; });
  require(sum == 0, "Storage created internal money");
  w.validate();
}
int main() {
  test("A real nearby locker lease allocates capacity and pays the operator", [] {
    auto w = small();
    Id f = facility(w);
    auto cash = w.accounts[w.residents[0].account].balance,
         income = w.accounts[w.operatingAccount(f)].balance;
    rejects([&] { w.rentStorage(0, f); });
    Id l = rent(w);
    require(w.storageLeases[l].slot == 0 &&
                w.accounts[w.residents[0].account].balance == cash - w.facilities[f].price &&
                w.accounts[w.operatingAccount(f)].balance == income + w.facilities[f].price,
            "Lease did not settle real rent");
    conserved(w);
  });
  test("Full storage cannot reserve phantom slots or charge twice", [] {
    auto w = small();
    w.facilities[facility(w)].stations = 1;
    Id l = rent(w);
    auto cash = w.accounts[w.residents[0].account].balance;
    rejects([&] { w.rentStorage(0, facility(w)); });
    require(w.storageLeases.size() == 1 && w.accounts[w.residents[0].account].balance == cash,
            "Full rental charged or created lease");
    conserved(w);
  });
  test("Depositing and collecting an owned item preserves title and physical identity", [] {
    auto w = small();
    Id l = rent(w);
    open(w, 0, l);
    Id id = item(w, 0, ItemKind::Phone);
    Id owner = w.items[id].owner;
    w.depositStorage(0, l, id);
    require(w.items[id].storedAt == l && w.items[id].carrier == none && w.items[id].owner == owner,
            "Storage state not physical");
    require(std::find(w.residents[0].items.begin(), w.residents[0].items.end(), id) ==
                w.residents[0].items.end(),
            "Stored item duplicated in carried inventory");
    w.withdrawStorage(0, id);
    require(w.items[id].storedAt == none && w.items[id].carrier == 0 &&
                w.items[id].owner == owner && w.storageLeases[l].items.empty(),
            "Withdrawal copied or changed title");
    conserved(w);
  });
  test("A closed locker and remote inventory cannot be accessed", [] {
    auto w = small();
    Id l = rent(w);
    open(w, 0, l);
    Id id = item(w, 0, ItemKind::Food);
    w.depositStorage(0, l, id);
    w.setStorageOpen(0, l, false);
    rejects([&] { w.withdrawStorage(0, id); });
    place(w, 0, {100, 0, 100});
    rejects([&] { w.setStorageOpen(0, l, true); });
    require(w.items[id].storedAt == l, "Failed access stole property");
    conserved(w);
  });
  test("Household access permits donated meals but protects private belongings", [] {
    auto w = small();
    Id h = group(w), l = rent(w, 0, h);
    open(w, 0, l);
    Id food = item(w, 0, ItemKind::Food), phone = item(w, 0, ItemKind::Phone);
    w.depositStorage(0, l, food, true);
    w.depositStorage(0, l, phone);
    open(w, 1, l);
    rejects([&] { w.withdrawStorage(1, phone); });
    w.withdrawStorage(1, food);
    require(w.items[food].owner == w.residents[1].account && w.items[food].carrier == 1 &&
                w.items[phone].owner == w.residents[0].account,
            "Household consent bypassed private ownership");
    conserved(w);
  });
  test("A departing donor retains collection rights and revokes uncollected food sharing", [] {
    auto w = small();
    Id h = group(w), l = rent(w, 0, h);
    open(w, 1, l);
    Id food = item(w, 1, ItemKind::Food);
    w.depositStorage(1, l, food, true);
    w.leaveHousehold(1);
    open(w, 0, l);
    require(!w.storageItemAllowed(0, food) && w.sharedStoredMeals(h) == 0,
            "Departure left involuntary sharing active");
    rejects([&] { w.withdrawStorage(0, food); });
    open(w, 1, l);
    w.withdrawStorage(1, food);
    require(w.items[food].carrier == 1, "Former member lost private property");
    conserved(w);
  });
  test("Recurring rent consumes real money while missed bills preserve property and debt", [] {
    auto w = small();
    Id l = rent(w);
    open(w, 0, l);
    Id food = item(w, 0, ItemKind::Food);
    w.depositStorage(0, l, food);
    auto account = w.residents[0].account;
    auto cash = w.accounts[account].balance;
    w.time += 86400;
    w.storageTick();
    require(w.accounts[account].balance == cash - w.storageLeases[l].dailyRent,
            "Recurring rent missing");
    w.pay(account, 0, w.accounts[account].balance, "test-empty-wallet");
    w.time += 4 * 86400;
    w.storageTick();
    require(!w.storageLeases[l].active &&
                w.storageLeases[l].arrears == 4 * w.storageLeases[l].dailyRent &&
                w.items[food].storedAt == l,
            "Default erased goods/debt or stayed funded");
    open(w, 0, l);
    w.withdrawStorage(0, food);
    w.releaseStorage(0, l);
    require(w.storageLeases[l].released && w.storageLeases[l].arrears > 0,
            "Leaving forgave storage debt");
    conserved(w);
  });
  test("A landlord cannot demolish leased lockers or erase customer contents", [] {
    auto w = small();
    Id l = rent(w);
    Id f = w.storageLeases[l].facility;
    rejects([&] { w.proposeProject(w.dormOwner, f, true, 10000); });
    require(w.storageFacilityBusy(f), "Occupied lease did not protect facility");
    conserved(w);
  });
  test("Saved contents, sharing and billing survive reload without a duplicate inventory", [] {
    auto w = small();
    Id h = group(w), l = rent(w, 0, h);
    open(w, 0, l);
    Id food = item(w, 0, ItemKind::Food);
    w.depositStorage(0, l, food, true);
    auto path = std::filesystem::temp_directory_path() / "mega-locker.mdsave";
    w.save(path);
    auto v = World::load(path);
    require(v.items[food].storedAt == l && v.items[food].sharedWith == h &&
                v.storageLeases[l].items == std::vector<Id>{food},
            "Storage save lost links or consent");
    open(v, 1, l);
    v.withdrawStorage(1, food);
    conserved(v);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".meta.json");
  });
  test("An autonomous hungry member physically retrieves household food", [] {
    auto w = small();
    Id h = group(w), l = rent(w, 0, h);
    open(w, 0, l);
    Id food = item(w, 0, ItemKind::Food);
    w.depositStorage(0, l, food, true);
    place(w, 1, w.storagePosition(l) + Vec3{8, 0, 5});
    w.residents[1].automatic = true;
    w.residents[1].hunger = .8f;
    require(w.storageDecision(1, true) && w.residents[1].task == TaskKind::StorageRetrieve &&
                w.items[food].carrier == none,
            "Food teleported or no errand was planned");
    w.advance(w.residents[1].route.back().time - w.time + .01);
    require(w.items[food].carrier == 1 && w.items[food].owner == w.residents[1].account,
            "Actual storage visit failed handover");
    conserved(w);
  });
  test("Private stored devices prevent duplicate shopping without providing remote network access",
       [] {
         auto w = small();
         Id l = rent(w);
         open(w, 0, l);
         auto held = w.residents[0].items;
         for (Id id : held)
           if (w.items[id].kind == ItemKind::Phone || w.items[id].kind == ItemKind::Laptop)
             w.depositStorage(0, l, id);
         require(!w.hasDevice(0) && (w.storedPossessions(0, ItemKind::Phone) +
                                     w.storedPossessions(0, ItemKind::Laptop)) > 0,
                 "Stored phone provided invisible network access");
         conserved(w);
       });
  test("Stealing an exposed item preserves provenance and does not notify a remote owner magically",
       [] {
         auto w = small();
         Id l = rent(w);
         open(w, 0, l);
         Id food = item(w, 0, ItemKind::Food);
         w.depositStorage(0, l, food);
         place(w, 0, {100, 0, 100});
         place(w, 1, w.storagePosition(l));
         w.withdrawStorage(1, food, true);
         Id crime = w.incidents.back().id;
         require(w.items[food].stolenFrom == w.residents[0].account && w.items[food].carrier == 1 &&
                     !w.incidents[crime].reported,
                 "Theft lost provenance or invented report");
         rejects([&] { w.fileCrimeReport(0, crime); });
         place(w, 0, w.storagePosition(l));
         w.fileCrimeReport(0, crime);
         require(w.incidents[crime].suspect == none && w.incidents[crime].sceneLocated,
                 "Missing stored property identified unseen thief");
         conserved(w);
       });
  test("Stolen goods can be physically recovered from an opened locker without stale storage links",
       [] {
         auto w = small();
         Id l = rent(w, 1);
         open(w, 1, l);
         Id stolen = item(w, 1, ItemKind::Phone);
         w.items[stolen].stolenFrom = w.residents[0].account;
         w.depositStorage(1, l, stolen);
         Id crime = w.report(CrimeKind::Theft, 1, 0);
         w.incidents[crime].item = stolen;
         Id jail = w.nearestAt({0, 0, 0}, FacilityKind::Jail);
         w.chooseJob(2, Role::Warden, jail);
         place(w, 2, w.storagePosition(l));
         w.recoverEvidence(2, crime, stolen);
         require(w.items[stolen].storedAt == none && w.items[stolen].carrier == 2 &&
                     w.storageLeases[l].items.empty(),
                 "Evidence recovery left phantom locker contents");
         conserved(w);
       });
  test("Malformed storage links, capacity, sharing and duplicate slots are rejected", [] {
    auto w = small();
    Id l = rent(w);
    open(w, 0, l);
    Id id = item(w, 0, ItemKind::Food);
    w.depositStorage(0, l, id);
    w.items[id].storedAt = 99999;
    rejects([&] { w.validate(); });
    w.items[id].storedAt = l;
    w.storageLeases.push_back(w.storageLeases[0]);
    w.storageLeases.back().id = 1;
    w.rebuildStorage();
    rejects([&] { w.validate(); });
  });
  test("Ruined consumables do not trap residents in interrupted survival decisions", [] {
    auto w = small();
    for (Id id : w.residents[0].items)
      if (w.items[id].kind == ItemKind::Food || w.items[id].kind == ItemKind::Medicine)
        w.items[id].durability = 0;
    Id good = item(w, 0, ItemKind::Food);
    Id bad = item(w, 0, ItemKind::Food);
    w.items[bad].durability = 0;
    Id medicine = item(w, 0, ItemKind::Medicine);
    w.items[medicine].durability = 0;
    auto &r = w.residents[0];
    r.automatic = true;
    r.hunger = .8f;
    r.health = 80;
    r.routine.sleepStart = 22;
    r.routine.sleepHours = 7;
    w.decide(0, false);
    require(w.items[good].consumed && !w.items[bad].consumed && !w.items[medicine].consumed &&
                r.hunger < .4f,
            "Ruined food or medicine blocked a valid meal");
    conserved(w);
  });

  test("Stored shelf positions remain unique after taking and replacing an item", [] {
    auto w = small();
    Id l = rent(w);
    open(w, 0, l);
    Id a = item(w, 0, ItemKind::Food), b = item(w, 0, ItemKind::Food),
       c = item(w, 0, ItemKind::Food);
    w.depositStorage(0, l, a);
    w.depositStorage(0, l, b);
    w.depositStorage(0, l, c);
    auto before = w.items[c].pos;
    w.withdrawStorage(0, b);
    w.depositStorage(0, l, b);
    require(length(w.items[c].pos - before) < .001f &&
                w.items[b].storageSlot != w.items[c].storageSlot,
            "Replacing a middle item overlapped existing contents");
    conserved(w);
  });
  test("A departing resident physically collects stored property before leaving", [] {
    auto w = small();
    Id l = rent(w);
    open(w, 0, l);
    Id food = item(w, 0, ItemKind::Food);
    w.depositStorage(0, l, food);
    place(w, 0, w.layout(0).entrance());
    rejects([&] { w.depart(0); });
    w.residents[0].leaving = true;
    w.residents[0].automatic = true;
    require(w.storageDepartureDecision(0) && w.residents[0].task == TaskKind::StorageRetrieve,
            "Departure did not plan a real collection trip");
    w.advance(w.residents[0].route.back().time - w.time + .01);
    require(w.items[food].carrier == 0 && w.items[food].storedAt == none,
            "Departure teleported or abandoned stored goods");
    w.travel(0, w.layout(0).entrance(), TaskKind::Emigrate, 0);
    w.advance(w.residents[0].route.back().time - w.time + .01);
    w.storageTick();
    require(!w.residents[0].present && w.storageLeases[l].released && w.items[food].carrier == 0,
            "Departure did not free its empty locker");
    conserved(w);
  });
  test("An emigrant's shared locker preserves another resident's private belongings", [] {
    auto w = small();
    Id h = group(w), l = rent(w, 0, h);
    open(w, 1, l);
    Id phone = item(w, 1, ItemKind::Phone);
    w.depositStorage(1, l, phone);
    place(w, 0, w.layout(0).entrance());
    w.depart(0);
    w.storageTick();
    require(!w.storageLeases[l].active && !w.storageLeases[l].released &&
                w.items[phone].storedAt == l,
            "Emigration deleted someone else's belongings");
    open(w, 1, l);
    w.withdrawStorage(1, phone);
    w.storageTick();
    require(w.storageLeases[l].released && w.items[phone].carrier == 1,
            "Remaining owner could not collect their property");
    conserved(w);
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
