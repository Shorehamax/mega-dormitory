#include "json.hpp"
#include "mega/world.hpp"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <zstd.h>
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
  for (const auto &road : w.roads)
    w.activateRoad(road.id, false);
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
static Id add(World &w, Id id, ItemKind kind) {
  Item i;
  i.id = Id(w.items.size());
  i.owner = w.residents[id].account;
  i.carrier = id;
  i.kind = kind;
  i.pos = w.position(id);
  w.items.push_back(i);
  w.residents[id].items.push_back(i.id);
  return i.id;
}
static Id shop(World &w) {
  for (auto &f : w.facilities)
    if (f.kind == FacilityKind::Shop && f.floor == 0)
      return f.id;
  throw std::runtime_error("No shop");
}
static void arm(World &w, Id id = 0) {
  auto &r = w.residents[id];
  r.automatic = true;
  auto &p = w.residentPlans[id];
  p.nextShopping = w.time;
  p.nextCareer = p.nextBusiness = p.nextListing = w.time + 86400 * 3;
  p.day = int(w.time / 86400);
}
static void courier(World &w, Id shop, Id id = 2) {
  auto &r = w.residents[id];
  r.automatic = true;
  r.role = Role::Courier;
  r.jobSite = shop;
  r.employer = w.operatingAccount(shop);
  r.routine.shiftStart = 0;
  r.routine.shiftHours = 24;
  r.routine.daysOff = 0;
  w.facilities[shop].employees.push_back(id);
  place(w, id, w.facilities[shop].pos + Vec3{0, 0, 1.6f});
}
static void conserved(const World &w) {
  require(std::accumulate(w.accounts.begin(), w.accounts.end(), Money(0),
                          [](Money a, const Account &b) { return a + b.balance; }) == 0,
          "Money created internally");
  w.validate();
}
static void disableDevices(World &w, Id id) {
  for (Id item : w.residents[id].items)
    if (w.items[item].kind == ItemKind::Phone || w.items[item].kind == ItemKind::Laptop)
      w.items[item].durability = 0;
}
static void strip(World &w, Id actor, ItemKind kind) {
  auto &r = w.residents[actor];
  for (Id id : r.items)
    if (w.items[id].kind == kind)
      w.items[id].carrier = none;
  std::erase_if(r.items, [&](Id id) { return w.items[id].carrier != actor; });
  if (kind == ItemKind::Clothes)
    r.outfit = none;
}
static Id group(World &w) {
  for (Id id : {0, 1, 3}) {
    w.residents[id].age = 30;
    w.residents[id].aggression = 0;
    w.residents[id].personality.generosity = 1;
    place(w, id, {100 + float(id % 2), 0, 100 + float(id / 2)});
    w.residents[id].hunger = w.residents[id].thirst = .1f;
  }
  w.relationships[1] = {0, 1, .8f, -3600, 0};
  w.relationships[3] = {0, 3, .8f, -3600, 0};
  w.relationships[(std::uint64_t(1) << 32) | 3] = {1, 3, .8f, -3600, 0};
  Id g = w.createHousehold(0);
  w.inviteHousehold(0, 1);
  w.acceptHousehold(1, g);
  w.inviteHousehold(0, 3);
  w.acceptHousehold(3, g);
  return g;
}
static void fund(World &w, Id actor, Money cash) {
  auto account = w.residents[actor].account;
  Money diff = cash - w.accounts[account].balance;
  if (diff > 0)
    w.pay(w.dormAccount, account, diff, "test-budget");
  else if (diff < 0)
    w.pay(account, w.dormAccount, -diff, "test-budget");
}
static Id need(World &w, ItemKind kind = ItemKind::Food) {
  strip(w, 1, kind);
  if (kind == ItemKind::Food)
    w.residents[1].hunger = .8f;
  if (kind == ItemKind::Medicine)
    w.residents[1].health = 50;
  return w.requestHouseholdSupply(1, kind);
}
static void privatePhone(World &w, Id actor) {
  if (!w.hasDevice(actor))
    add(w, actor, ItemKind::Phone);
}
static void oldSave(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
  auto size = ZSTD_getFrameContentSize(data.data() + 8, data.size() - 8);
  std::vector<std::uint8_t> raw(size);
  require(!ZSTD_isError(ZSTD_decompress(raw.data(), raw.size(), data.data() + 8, data.size() - 8)),
          "decode");
  auto j = nlohmann::json::from_cbor(raw);
  j.erase("householdSupplies");
  for (auto &h : j["households"])
    h.erase("dependents");
  for (auto &p : j["residentPlans"])
    p[1].erase("supply");
  raw = nlohmann::json::to_cbor(j);
  std::vector<char> packed(ZSTD_compressBound(raw.size()));
  auto n = ZSTD_compress(packed.data(), packed.size(), raw.data(), raw.size(), 3);
  require(!ZSTD_isError(n), "encode");
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write("MDORM001", 8);
  out.write(packed.data(), n);
}
static std::pair<Id, Id> pantry(World &w, bool share) {
  Id f = none;
  for (auto &x : w.facilities)
    if (x.kind == FacilityKind::Locker && x.floor == 0) {
      f = x.id;
      break;
    }
  require(f != none, "No locker");
  w.facilities[f].pos = {100, 0, 105};
  w.invalidateGeometry(0);
  place(w, 3, w.serviceApproach(f));
  auto lease = w.rentStorage(3, f, w.householdOf(3));
  place(w, 3, w.storagePosition(lease));
  w.setStorageOpen(3, lease, true);
  auto item = add(w, 3, ItemKind::Medicine);
  w.depositStorage(3, lease, item, share);
  w.setStorageOpen(3, lease, false);
  return {lease, item};
}
int main() {
  test("Requests require household membership, an actual need and a supported supply", [] {
    auto w = small();
    rejects([&] { w.requestHouseholdSupply(0, ItemKind::Food); });
    group(w);
    rejects([&] { w.requestHouseholdSupply(1, ItemKind::Handgun); });
    rejects([&] { w.requestHouseholdSupply(1, ItemKind::Food); });
    conserved(w);
  });
  test("Repeated requests identify the same active need without duplication", [] {
    auto w = small();
    group(w);
    auto id = need(w);
    require(w.requestHouseholdSupply(1, ItemKind::Food) == id && w.householdSupplies.size() == 1,
            "Duplicate request");
    conserved(w);
  });
  test("A request is visible in person or on two working awake devices", [] {
    auto w = small();
    group(w);
    auto id = need(w);
    disableDevices(w, 0);
    disableDevices(w, 1);
    place(w, 1, {130, 0, 100});
    require(!w.householdSupplyVisible(0, id), "Remote information leaked");
    rejects([&] { w.volunteerHouseholdSupply(0, id); });
    privatePhone(w, 0);
    privatePhone(w, 1);
    require(w.householdSupplyVisible(0, id), "Usable devices did not deliver board access");
    w.residents[1].activity = Activity::Sleeping;
    require(!w.householdSupplyVisible(0, id), "Sleeping recipient broadcast live request");
  });
  test("One volunteer reserves one real spare and retains their own meal", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Food);
    auto a = add(w, 0, ItemKind::Food), b = add(w, 0, ItemKind::Food);
    auto id = need(w);
    auto cash = w.accounts[w.residents[0].account].balance;
    w.volunteerHouseholdSupply(0, id);
    auto item = w.householdSupplies[id].item;
    require((item == a || item == b) && w.itemInUse(item) && w.householdItemClaims.size() == 1,
            "No real reservation");
    rejects([&] { w.volunteerHouseholdSupply(3, id); });
    rejects([&] { w.consumeItem(0, item); });
    require(w.accounts[w.residents[0].account].balance == cash, "Claim invented a charge");
    conserved(w);
  });
  test("A volunteer can cancel and use their own promised belongings", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    auto id = need(w);
    w.volunteerHouseholdSupply(0, id);
    auto item = w.householdSupplies[id].item;
    w.cancelHouseholdSupply(0, id);
    w.consumeItem(0, item);
    require(w.items[item].consumed && w.householdItemClaims.empty() &&
                w.residentPlans[0].supply == none,
            "Cancellation leaked item claim");
    conserved(w);
  });
  test("Support hands over owned food without teleporting or spending the recipient wallet", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    auto id = need(w);
    w.volunteerHouseholdSupply(0, id);
    auto item = w.householdSupplies[id].item;
    auto cash = w.accounts[w.residents[1].account].balance;
    privatePhone(w, 0);
    privatePhone(w, 1);
    place(w, 1, {112, 0, 100});
    w.advanceHouseholdSupply(0);
    require(w.residents[0].task == TaskKind::HouseholdMeet && w.items[item].carrier == 0,
            "Gift teleported");
    auto until = w.residents[0].route.back().time;
    w.advance(until - w.time + .01);
    require(w.items[item].owner == w.residents[1].account && w.items[item].carrier == 1 &&
                w.householdSupplies[id].state == SupplyState::Complete &&
                w.accounts[w.residents[1].account].balance == cash,
            "Actual handover or private wallet failed");
    conserved(w);
  });
  test("Medicine handover preserves the patient's choice to consume it", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Medicine);
    auto medicine = add(w, 0, ItemKind::Medicine);
    auto id = need(w, ItemKind::Medicine);
    w.volunteerHouseholdSupply(0, id);
    w.advanceHouseholdSupply(0);
    require(w.items[medicine].carrier == 1 && !w.items[medicine].consumed &&
                w.residents[1].health == 50,
            "Manual patient was force-treated");
    w.consumeItem(1, medicine);
    require(w.residents[1].health == 65, "Actual owned medicine was not consumed");
    conserved(w);
  });
  test("Promised clean clothes cannot be worn, washed or promised twice", [] {
    auto w = small();
    group(w);
    auto spare = add(w, 0, ItemKind::Clothes);
    w.items[spare].cleanliness = 1;
    auto id = need(w, ItemKind::Clothes);
    w.volunteerHouseholdSupply(0, id);
    auto promised = w.householdSupplies[id].item;
    require(promised != w.residents[0].outfit, "Worn outfit was donated");
    rejects([&] { w.changeClothes(0, promised); });
    w.advanceHouseholdSupply(0);
    require(w.items[promised].carrier == 1 && w.residents[1].outfit == none,
            "Clothing handover forced customization");
    w.changeClothes(1, promised);
    conserved(w);
  });
  test("Poor volunteers cannot spend rent or another member's wallet", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Food);
    auto id = need(w);
    fund(w, 0, w.householdReserve(0) + 500);
    auto peer = w.accounts[w.residents[3].account].balance;
    rejects([&] { w.volunteerHouseholdSupply(0, id); });
    require(w.householdSupplies[id].state == SupplyState::Requested &&
                w.accounts[w.residents[3].account].balance == peer,
            "Private funds pooled");
    conserved(w);
  });
  test("A household purchase reserves real escrow exactly once", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Medicine);
    auto id = need(w, ItemKind::Medicine);
    privatePhone(w, 0);
    Id f = shop(w);
    privatePhone(w, 1);
    place(w, 0, w.facilities[f].pos + Vec3{4, 0, 5});
    place(w, 1, w.position(0) + Vec3{1, 0, 0});
    courier(w, f);
    fund(w, 0, w.householdReserve(0) + 5000);
    auto total = w.internalMoney();
    auto before = w.accounts[w.residents[0].account].balance;
    w.volunteerHouseholdSupply(0, id, 5000);
    w.advanceHouseholdSupply(0);
    auto &s = w.householdSupplies[id];
    require(s.order != none && s.state == SupplyState::Ordered &&
                s.spent == w.productPrice(ItemKind::Medicine) + w.deliveryPrice(0, f),
            "No actual order");
    require(w.accounts[w.residents[0].account].balance == before - s.spent &&
                w.accounts[w.orders[s.order].escrow].balance == s.spent,
            "Escrow mismatch");
    w.advanceHouseholdSupply(0);
    require(w.orders.size() == 1 && w.internalMoney() == total, "Repeated step purchased twice");
    conserved(w);
  });
  test("Cancelling before pickup refunds the actual payer and releases every claim", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Medicine);
    auto id = need(w, ItemKind::Medicine);
    privatePhone(w, 0);
    Id f = shop(w);
    privatePhone(w, 1);
    place(w, 0, w.facilities[f].pos + Vec3{4, 0, 5});
    place(w, 1, w.position(0) + Vec3{1, 0, 0});
    courier(w, f);
    fund(w, 0, w.householdReserve(0) + 5000);
    auto before = w.accounts[w.residents[0].account].balance;
    w.volunteerHouseholdSupply(0, id);
    w.advanceHouseholdSupply(0);
    auto order = w.householdSupplies[id].order;
    require(order != none, "No order before cancellation");
    w.cancelHouseholdSupply(1, id);
    require(w.orders[order].state == OrderState::Cancelled &&
                w.accounts[w.residents[0].account].balance == before &&
                w.householdItemClaims.empty() && w.residentPlans[0].supply == none,
            "Refund or release failed");
    conserved(w);
  });
  test("A stranger cannot cancel or spend a household request", [] {
    auto w = small();
    group(w);
    auto id = need(w);
    rejects([&] { w.cancelHouseholdSupply(4, id); });
    rejects([&] { w.volunteerHouseholdSupply(4, id); });
    conserved(w);
  });
  test("Care for a young child requires nearby mutual consent and no existing household", [] {
    auto w = small();
    group(w);
    auto &child = w.residents[4];
    child.age = 3;
    child.automatic = true;
    w.relationships[4] = {0, 4, .8f, -3600, 0};
    place(w, 4, {101, 0, 101});
    auto cash = w.accounts[child.account].balance;
    w.addHouseholdDependent(0, 4);
    require(w.householdOf(4) == w.householdOf(0) &&
                w.households[w.householdOf(0)].dependents.at(4) == 0 &&
                w.accounts[child.account].balance == cash,
            "Dependent membership confiscated funds");
    rejects([&] { w.addHouseholdDependent(3, 4); });
    conserved(w);
  });
  test("A caregiver actually feeds a young child using one owned meal", [] {
    auto w = small();
    group(w);
    auto &child = w.residents[4];
    child.age = 3;
    child.automatic = true;
    w.relationships[4] = {0, 4, .8f, -3600, 0};
    place(w, 4, {101, 0, 101});
    w.addHouseholdDependent(0, 4);
    strip(w, 4, ItemKind::Food);
    child.hunger = .9f;
    strip(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    auto id = w.requestHouseholdSupply(4, ItemKind::Food);
    w.volunteerHouseholdSupply(0, id);
    auto item = w.householdSupplies[id].item;
    w.advanceHouseholdSupply(0);
    require(w.items[item].owner == child.account && w.items[item].consumed && child.hunger < .4f,
            "Child fed without actual supply or not fed");
    conserved(w);
  });
  test("Leaving a household cancels commitments without confiscating private goods", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    auto id = need(w);
    w.volunteerHouseholdSupply(0, id);
    auto item = w.householdSupplies[id].item;
    w.leaveHousehold(1);
    require(w.householdSupplies[id].state == SupplyState::Cancelled &&
                w.items[item].owner == w.residents[0].account && !w.itemInUse(item),
            "Departure confiscated gift");
    conserved(w);
  });
  test("A volunteer's urgent survival need releases the promise for real self-care", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    auto id = need(w);
    w.volunteerHouseholdSupply(0, id);
    arm(w);
    w.residents[0].hunger = 1.1f;
    w.householdSupplyNeeds(0);
    require(w.householdSupplies[id].state == SupplyState::Cancelled &&
                w.householdItemClaims.empty(),
            "Urgent actor remained locked in a gift");
    conserved(w);
  });
  test("Current requests, private ownership, guardians and claims survive reload", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    auto id = need(w);
    w.volunteerHouseholdSupply(0, id);
    auto item = w.householdSupplies[id].item;
    auto path = std::filesystem::temp_directory_path() / "mega-coordination-roundtrip.mdsave";
    w.save(path);
    auto v = World::load(path);
    std::filesystem::remove(path);
    require(v.householdSupplyActive(id) && v.householdItemClaims.at(item) == id &&
                v.residentPlans.at(0).supply == id,
            "Pending claim lost or replayed");
    conserved(v);
  });
  test("Empty household request indexes are not inserted by an idle decision", [] {
    auto w = small();
    group(w);
    arm(w);
    w.householdSupplyDecision(0);
    conserved(w);
  });
  test("A real courier delivers a paid medicine order before a private handover", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Medicine);
    auto id = need(w, ItemKind::Medicine);
    auto f = shop(w);
    privatePhone(w, 0);
    privatePhone(w, 1);
    place(w, 0, w.facilities[f].pos + Vec3{4, 0, 5});
    place(w, 1, w.position(0) + Vec3{1, 0, 0});
    courier(w, f);
    fund(w, 0, w.householdReserve(0) + 5000);
    auto cash = w.accounts[w.residents[1].account].balance;
    w.volunteerHouseholdSupply(0, id);
    w.advanceHouseholdSupply(0);
    auto order = w.householdSupplies[id].order;
    require(order != none, "No purchase");
    place(w, 2, w.items[w.orders[order].item].pos);
    w.pickup(order, 2);
    place(w, 2, w.position(0));
    w.deliver(order, 2);
    w.advanceHouseholdSupply(0);
    require(w.householdSupplies[id].state == SupplyState::Complete &&
                w.items[w.orders[order].item].carrier == 1 &&
                w.accounts[w.orders[order].escrow].balance == 0 &&
                w.accounts[w.residents[1].account].balance == cash,
            "Courier or private handover bypassed");
    conserved(w);
  });
  test("Cancellation after pickup leaves the real shipment and expense with its buyer", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Medicine);
    auto id = need(w, ItemKind::Medicine);
    auto f = shop(w);
    privatePhone(w, 0);
    privatePhone(w, 1);
    place(w, 0, w.facilities[f].pos + Vec3{4, 0, 5});
    place(w, 1, w.position(0) + Vec3{1, 0, 0});
    courier(w, f);
    fund(w, 0, w.householdReserve(0) + 5000);
    w.volunteerHouseholdSupply(0, id);
    w.advanceHouseholdSupply(0);
    auto order = w.householdSupplies[id].order;
    require(order != none, "No order");
    place(w, 2, w.items[w.orders[order].item].pos);
    w.pickup(order, 2);
    auto paid = w.accounts[w.residents[0].account].balance;
    w.cancelHouseholdSupply(1, id);
    require(w.orders[order].state == OrderState::ToCustomer &&
                w.items[w.orders[order].item].carrier == 2 &&
                w.accounts[w.residents[0].account].balance == paid && w.householdItemClaims.empty(),
            "Shipped parcel teleported or imaginary refund");
    place(w, 2, w.position(0));
    w.deliver(order, 2);
    require(w.items[w.orders[order].item].owner == w.residents[0].account,
            "Buyer lost shipped property");
    conserved(w);
  });
  test("Private pantry goods are not household property until their owner shares them", [] {
    auto w = small();
    group(w);
    auto [lease, item] = pantry(w, false);
    auto id = need(w, ItemKind::Medicine);
    strip(w, 0, ItemKind::Medicine);
    fund(w, 0, w.householdReserve(0));
    rejects([&] { w.volunteerHouseholdSupply(0, id, 0); });
    require(w.items[item].owner == w.residents[3].account && w.items[item].storedAt == lease,
            "Private pantry was pooled");
    conserved(w);
  });
  test("Two errands cannot reserve the same explicitly shared pantry medicine", [] {
    auto w = small();
    group(w);
    auto [lease, item] = pantry(w, true);
    auto id = need(w, ItemKind::Medicine);
    strip(w, 0, ItemKind::Medicine);
    w.volunteerHouseholdSupply(0, id, 0);
    w.residents[3].health = 50;
    auto other = w.requestHouseholdSupply(3, ItemKind::Medicine);
    fund(w, 1, w.householdReserve(1));
    rejects([&] { w.volunteerHouseholdSupply(1, other, 0); });
    require(w.householdItemClaims.size() == 1 && w.householdItemClaims.at(item) == id,
            "Shared medicine promised twice");
    conserved(w);
  });
  test("A reserved pantry item is physically collected and carried to its recipient", [] {
    auto w = small();
    group(w);
    auto [lease, item] = pantry(w, true);
    auto id = need(w, ItemKind::Medicine);
    strip(w, 0, ItemKind::Medicine);
    w.volunteerHouseholdSupply(0, id, 0);
    w.advanceHouseholdSupply(0);
    require(!w.residents[0].route.empty() && w.items[item].storedAt == lease,
            "Pantry collection teleported");
    for (int n = 0; n < 8 && w.householdSupplyActive(id); ++n) {
      if (!w.residents[0].route.empty())
        w.advance(std::max(.01, w.residents[0].route.back().time - w.time + .01));
      w.advanceHouseholdSupply(0);
    }
    require(w.householdSupplies[id].state == SupplyState::Complete && w.items[item].carrier == 1 &&
                w.items[item].storedAt == none,
            "Real collection/meeting did not complete");
    conserved(w);
  });
  test("A departing pantry owner immediately releases a promised private possession", [] {
    auto w = small();
    group(w);
    auto [lease, item] = pantry(w, true);
    auto id = need(w, ItemKind::Medicine);
    strip(w, 0, ItemKind::Medicine);
    w.volunteerHouseholdSupply(0, id, 0);
    w.leaveHousehold(3);
    require(w.householdSupplies[id].state == SupplyState::Cancelled &&
                !w.householdSupplyReserved(item) && w.items[item].owner == w.residents[3].account,
            "Departing owner trapped by a promise");
    place(w, 3, w.storagePosition(lease));
    w.setStorageOpen(3, lease, true);
    w.withdrawStorage(3, item);
    conserved(w);
  });
  test("A caregiver leaving the household releases dependents without taking their belongings", [] {
    auto w = small();
    group(w);
    auto &child = w.residents[4];
    child.age = 3;
    child.automatic = true;
    w.relationships[4] = {0, 4, .8f, -3600, 0};
    place(w, 4, {101, 0, 101});
    w.addHouseholdDependent(0, 4);
    auto cash = w.accounts[child.account].balance;
    w.leaveHousehold(0);
    require(w.householdOf(4) == none && w.accounts[child.account].balance == cash,
            "Dependent's funds or membership trapped");
    conserved(w);
  });
  test("Coming of age clears caregiver authority without removing household membership", [] {
    auto w = small();
    group(w);
    auto &child = w.residents[4];
    child.age = 17;
    child.automatic = true;
    w.relationships[4] = {0, 4, .8f, -3600, 0};
    place(w, 4, {101, 0, 101});
    w.addHouseholdDependent(0, 4);
    auto g = w.householdOf(4);
    child.age = 18;
    w.householdSupplyTick();
    require(w.householdOf(4) == g && !w.households[g].dependents.contains(4),
            "Adult kept a caregiver restriction");
    conserved(w);
  });
  test("A paid public computer is included in the volunteer's spending limit", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Medicine);
    auto id = need(w, ItemKind::Medicine);
    disableDevices(w, 0);
    Id terminal = none;
    for (auto &f : w.facilities)
      if (f.kind == FacilityKind::Terminal) {
        if (terminal == none && f.floor == 0)
          terminal = f.id;
        else
          f.open = false;
      }
    require(terminal != none, "No terminal");
    auto &pc = w.facilities[terminal];
    pc.pos = {100, 0, 102};
    pc.price = 10;
    w.invalidateGeometry(0);
    auto f = shop(w);
    w.facilities[f].pos = {110, 0, 106};
    courier(w, f);
    fund(w, 0, w.householdReserve(0) + 5000);
    place(w, 0, pc.pos + Vec3{0, 0, 1.6f});
    place(w, 1, w.position(0) + Vec3{1, 0, 0});
    auto before = w.accounts[w.residents[0].account].balance;
    w.volunteerHouseholdSupply(0, id, 2250);
    w.advanceHouseholdSupply(0);
    require(w.householdSupplies[id].order != none && w.householdSupplies[id].spent == 2250 &&
                before - w.accounts[w.residents[0].account].balance == 2250 && w.hasDevice(0),
            "Computer charge bypassed actual payment or budget");
    conserved(w);
  });
  test("Older saves gain no fictional requests, dependents, charges or reservations", [] {
    auto w = small();
    group(w);
    arm(w);
    auto cash = w.accounts[w.residents[0].account].balance;
    auto path =
        std::filesystem::temp_directory_path() /
        ("mega-coord-legacy-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mdsave");
    w.save(path);
    oldSave(path);
    auto v = World::load(path);
    std::filesystem::remove(path);
    require(v.householdSupplies.empty() && v.householdItemClaims.empty() &&
                v.residentPlans.at(0).supply == none &&
                v.accounts[v.residents[0].account].balance == cash,
            "Legacy load invented a household action");
    conserved(v);
  });
  test("Cancelling an owned meeting stops the route at its actual interpolated position", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    privatePhone(w, 0);
    privatePhone(w, 1);
    auto id = need(w);
    w.volunteerHouseholdSupply(0, id);
    place(w, 1, {115, 0, 100});
    w.advanceHouseholdSupply(0);
    require(!w.residents[0].route.empty(), "No meeting route");
    w.advance(30);
    auto before = w.position(0);
    w.cancelHouseholdSupply(1, id);
    require(w.residents[0].route.empty() && w.residents[0].task == TaskKind::None &&
                length(w.position(0) - before) < .02f,
            "Cancellation kept stale meeting or moved the body");
    conserved(w);
  });
  test("Paid lift travel counts against a support budget and cannot consume rent", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Medicine);
    add(w, 0, ItemKind::Medicine);
    auto id = need(w, ItemKind::Medicine);
    w.volunteerHouseholdSupply(0, id, 600);
    auto lift = w.nearestAt({0, 0, 0}, FacilityKind::Elevator);
    require(lift != none, "No elevator");
    auto &f = w.facilities[lift];
    f.stops = {0, 12};
    f.price = 600;
    f.owner = w.dormAccount;
    f.whitelist.clear();
    f.subscribers.clear();
    place(w, 0, w.liftDoor(0));
    fund(w, 0, w.householdReserve(0) + 100);
    rejects([&] { w.payLift(0, lift, 12); });
    require(w.householdSupplies[id].spent == 0, "Rejected fare was charged");
    fund(w, 0, w.householdReserve(0) + 2000);
    auto before = w.accounts[w.residents[0].account].balance;
    w.payLift(0, lift, 12);
    require(w.householdSupplies[id].spent == 600 &&
                before - w.accounts[w.residents[0].account].balance == 600,
            "Fare escaped budget accounting");
    rejects([&] { w.payLift(0, lift, 12); });
    conserved(w);
  });
  test("A transport fare cannot spend the money still needed for the promised purchase", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Medicine);
    auto id = need(w, ItemKind::Medicine);
    fund(w, 0, w.householdReserve(0) + 10000);
    w.volunteerHouseholdSupply(0, id, 2500);
    auto lift = w.nearestAt({0, 0, 0}, FacilityKind::Elevator);
    require(lift != none, "No elevator");
    auto &f = w.facilities[lift];
    f.stops = {0, 12};
    f.price = 400;
    f.owner = w.dormAccount;
    f.whitelist.clear();
    f.subscribers.clear();
    place(w, 0, w.liftDoor(0));
    auto before = w.accounts[w.residents[0].account].balance;
    rejects([&] { w.payLift(0, lift, 12); });
    require(w.accounts[w.residents[0].account].balance == before &&
                w.householdSupplies[id].spent == 0,
            "Fare consumed future purchase budget");
    conserved(w);
  });
  test("A promised gift can still be stolen and the stale promise releases the real loot", [] {
    auto w = small();
    group(w);
    strip(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    auto id = need(w);
    w.volunteerHouseholdSupply(0, id);
    auto item = w.householdSupplies[id].item;
    place(w, 4, w.position(0) + Vec3{.5f, 0, 0});
    w.residents[4].strength = 300;
    w.residents[0].strength = 10;
    w.residents[0].surrendered = true;
    require(w.stealItem(4, 0, item, true), "Promised property became immune to robbery");
    require(w.householdSupplies[id].state == SupplyState::Cancelled && !w.itemInUse(item) &&
                w.items[item].owner == w.residents[4].account &&
                w.items[item].stolenFrom == w.residents[0].account,
            "Stale promise locked stolen loot or erased theft provenance");
    w.consumeItem(4, item);
    conserved(w);
  });
  test("Laundry cannot reserve a garment already promised to another household member", [] {
    auto w = small();
    group(w);
    auto id = need(w, ItemKind::Clothes);
    auto spare = add(w, 0, ItemKind::Clothes);
    w.items[spare].cleanliness = .7f;
    w.volunteerHouseholdSupply(0, id);
    auto promised = w.householdSupplies[id].item;
    auto f = w.nearestAt({0, 0, 0}, FacilityKind::Laundry);
    require(f != none, "No laundry");
    place(w, 0, w.serviceApproach(f));
    w.joinQueue(0, f);
    require(w.items[promised].reservedAt == none && w.items[promised].carrier == 0 &&
                w.householdItemClaims.at(promised) == id,
            "Laundry and care both reserved the same garment");
    conserved(w);
  });
  test("An unattended caregiver buys, receives and administers real medicine to a dependent", [] {
    auto w = small();
    group(w);
    auto &child = w.residents[4];
    child.age = 3;
    child.automatic = true;
    child.routine.sleepStart = 23;
    child.routine.sleepHours = 8;
    w.residents[0].routine.sleepStart = 23;
    w.residents[0].routine.sleepHours = 8;
    w.relationships[4] = {0, 4, .8f, -3600, 0};
    place(w, 4, {100, 0, 102});
    w.addHouseholdDependent(0, 4);
    strip(w, 4, ItemKind::Medicine);
    strip(w, 0, ItemKind::Medicine);
    child.health = 50;
    auto f = shop(w);
    w.facilities[f].pos = {100, 0, 106};
    w.facilities[f].staffNeeded = 1;
    w.invalidateGeometry(0);
    w.residents[5].age = 30;
    w.chooseJob(5, Role::Vendor, f);
    auto &vendor = w.residents[5];
    vendor.routine.shiftStart = 0;
    vendor.routine.shiftHours = 24;
    vendor.routine.daysOff = 0;
    place(w, 5, w.facilities[f].pos + Vec3{-1.6f, 0, -1.6f});
    w.clockIn(5);
    courier(w, f);
    privatePhone(w, 0);
    fund(w, 0, w.householdReserve(0) + 6000);
    fund(w, 4, 0); // The dependent cannot fund a duplicate clinic trip themselves.
    auto childCash = w.accounts[child.account].balance;
    arm(w);
    auto &p = w.residentPlans[0];
    p.nextShopping = p.nextCareer = p.nextBusiness = p.nextListing = w.time + 86400;
    w.schedule(w.time + .01, EventKind::Decide, 0);
    w.schedule(w.time + .02, EventKind::Decide, 4);
    vendor.automatic = true;
    w.schedule(w.time + .03, EventKind::Decide, 5);
    // After kickoff every request, purchase, movement, delivery and care action is automatic.
    for (int n = 0; n < 120 && child.health < 65; ++n)
      w.advance(60);
    if (child.health < 65) {
      std::cerr << "AUTO CARE TRACE child=" << child.health << " activity=" << int(child.activity)
                << " task=" << int(child.task) << " helper=" << int(w.residents[0].activity) << "/"
                << int(w.residents[0].task) << " requests=" << w.householdSupplies.size()
                << " orders=" << w.orders.size() << "\n";
      for (const auto &j : w.householdSupplies)
        std::cerr << "AUTO CARE request=" << j.id << " state=" << int(j.state)
                  << " buyer=" << j.buyer << " reason=" << j.reason << " order=" << j.order
                  << " item=" << j.item << "\n";
    }
    require(child.health >= 65 && !w.householdSupplies.empty(),
            "Unattended household did not provide medicine");
    const auto &request = w.householdSupplies.front();
    require(request.state == SupplyState::Complete && request.order != none &&
                w.orders[request.order].state == OrderState::Delivered &&
                w.items[request.item].consumed && w.items[request.item].owner == child.account &&
                w.householdItemClaims.empty() && w.accounts[child.account].balance == childCash,
            "Automatic care bypassed real supplies, courier or private funds");
    conserved(w);
  });
  test("A shared item's owner can inspect and retract its reservation without another person's "
       "device",
       [] {
         auto w = small();
         group(w);
         auto [lease, item] = pantry(w, true);
         auto id = need(w, ItemKind::Medicine);
         strip(w, 0, ItemKind::Medicine);
         w.volunteerHouseholdSupply(0, id, 0);
         disableDevices(w, 3);
         place(w, 3, {130, 0, 100});
         require(w.householdSupplyVisible(3, id),
                 "Owner could not inspect their own property reservation");
         w.cancelHouseholdSupply(3, id);
         require(!w.householdSupplyReserved(item) &&
                     w.items[item].owner == w.residents[3].account &&
                     w.items[item].storedAt == lease,
                 "Owner's veto confiscated or moved their private property");
         conserved(w);
       });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
