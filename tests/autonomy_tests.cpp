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
int main() {
  test("Household reserve protects rent, arrears and necessities", [] {
    auto w = small();
    arm(w);
    auto account = w.residents[0].account;
    auto reserve = w.householdReserve(0);
    w.pay(account, 0, w.accounts[account].balance, "test-outflow");
    w.pay(0, account, reserve + 500, "test-inflow");
    w.householdShopping(0);
    require(w.orders.empty() && w.residentPlans[0].action == AutonomyAction::Essentials,
            "Spent protected budget");
    conserved(w);
  });
  test("A resident orders real stock into escrow and refuses a duplicate pending order", [] {
    auto w = small();
    Id f = shop(w);
    courier(w, f);
    place(w, 0, w.facilities[f].pos + Vec3{4, 0, 5});
    arm(w);
    auto cash = w.accounts[w.residents[0].account].balance;
    int stock = w.facilities[f].stock;
    w.householdShopping(0);
    require(w.orders.size() == 1 && w.orders[0].product == ItemKind::Food &&
                w.orders[0].state == OrderState::ToPickup,
            "No real household order");
    auto &o = w.orders[0];
    require(w.accounts[o.escrow].balance == o.price + o.deliveryFee &&
                w.accounts[w.residents[0].account].balance == cash - o.price - o.deliveryFee &&
                w.facilities[f].stock == stock - 1,
            "Order invented goods or payment");
    w.residentPlans[0].nextShopping = w.time;
    w.householdShopping(0);
    require(w.orders.size() == 1, "Duplicate pending order");
    conserved(w);
  });
  test("An autonomous courier physically completes the household purchase", [] {
    auto w = small();
    Id f = shop(w);
    courier(w, f);
    place(w, 0, w.facilities[f].pos + Vec3{8, 0, 6});
    arm(w);
    w.householdShopping(0);
    require(w.orders.size() == 1, "Purchase not placed");
    w.decide(2);
    for (int n = 0; n < 120 && w.orders[0].state != OrderState::Delivered; ++n)
      w.advance(60, 100000);
    require(w.orders[0].state == OrderState::Delivered && w.items[w.orders[0].item].carrier == 0 &&
                w.accounts[w.orders[0].escrow].balance == 0,
            "Delivery was not a physical handover");
    conserved(w);
  });
  test("Shops without working couriers or stock do not receive automatic orders", [] {
    auto w = small();
    Id f = shop(w);
    place(w, 0, w.facilities[f].pos + Vec3{4, 0, 5});
    arm(w);
    w.householdShopping(0);
    require(w.orders.empty(), "Ordered without delivery capacity");
    courier(w, f);
    w.facilities[f].stock = 0;
    w.residentPlans[0].nextShopping = w.time;
    w.householdShopping(0);
    require(w.orders.empty(), "Ordered nonexistent stock");
    conserved(w);
  });
  test("Second-hand trade requires an actual meeting and preserves ownership until then", [] {
    auto w = small();
    place(w, 0, {50, 0, 60});
    place(w, 1, {65, 0, 60});
    Id item = add(w, 1, ItemKind::Food);
    w.listItem(1, item, 400);
    arm(w);
    auto buyer = w.accounts[w.residents[0].account].balance,
         seller = w.accounts[w.residents[1].account].balance;
    w.householdShopping(0);
    require(w.residents[0].task == TaskKind::TradeMeet &&
                w.items[item].owner == w.residents[1].account &&
                w.accounts[w.residents[0].account].balance == buyer,
            "Trade teleported or charged early");
    w.advance(w.residents[0].route.back().time - w.time + .01, 100000);
    require(w.items[item].owner == w.residents[0].account && w.items[item].carrier == 0 &&
                w.accounts[w.residents[0].account].balance == buyer - 400 &&
                w.accounts[w.residents[1].account].balance == seller + 400,
            "Physical resale failed");
    conserved(w);
  });
  test("A seller changing the agreed price cancels the journey without payment", [] {
    auto w = small();
    place(w, 0, {50, 0, 60});
    place(w, 1, {65, 0, 60});
    Id item = add(w, 1, ItemKind::Food);
    w.listItem(1, item, 400);
    arm(w);
    w.householdShopping(0);
    auto cash = w.accounts[w.residents[0].account].balance;
    w.listItem(1, item, 650);
    w.advance(w.residents[0].route.back().time - w.time + .01, 100000);
    require(w.items[item].carrier == 1 && w.accounts[w.residents[0].account].balance == cash &&
                w.residentPlans[0].tradeItem == none,
            "Price hike bypassed agreement");
    conserved(w);
  });
  test("Plans and pending physical trades survive native save/reload", [] {
    auto w = small();
    place(w, 0, {50, 0, 60});
    place(w, 1, {65, 0, 60});
    Id item = add(w, 1, ItemKind::Food);
    w.listItem(1, item, 400);
    arm(w);
    w.householdShopping(0);
    auto path = std::filesystem::temp_directory_path() / "mega-autonomy-trade.mdsave";
    w.save(path);
    auto v = World::load(path);
    require(v.residentPlans.at(0).tradeItem == item && v.residents[0].task == TaskKind::TradeMeet,
            "Plan was not restored");
    v.advance(v.residents[0].route.back().time - v.time + .01, 100000);
    require(v.items[item].carrier == 0, "Restored trade did not finish");
    conserved(v);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".meta.json");
  });
  test("A resident without a device walks to a paid public computer to order a phone", [] {
    auto w = small();
    Id f = shop(w);
    courier(w, f);
    disableDevices(w, 0);
    w.pay(0, w.residents[0].account, 20000, "test-device-savings");
    place(w, 0, w.facilities[f].pos + Vec3{5, 0, 5});
    w.residents[0].hunger = .1f;
    arm(w);
    w.householdShopping(0);
    require(w.residents[0].task == TaskKind::ShoppingTerminal,
            "Device appeared without physical computer access");
    Id terminal = w.residents[0].target;
    auto income = w.accounts[w.operatingAccount(terminal)].balance;
    for (int n = 0; n < 120 && w.orders.empty(); ++n)
      w.advance(30, 100000);
    require(w.orders.size() == 1 && w.orders[0].product == ItemKind::Phone &&
                w.accounts[w.operatingAccount(terminal)].balance >=
                    income + w.facilities[terminal].price * 5,
            "Paid terminal did not enable the phone order");
    conserved(w);
  });
  test("Public computer capacity is enforced before charging", [] {
    auto w = small();
    Id f = w.nearestAt({0, 0, 0}, FacilityKind::Terminal);
    w.facilities[f].stations = 1;
    place(w, 0, w.facilities[f].pos + Vec3{0, 0, 1.6f});
    place(w, 1, w.facilities[f].pos + Vec3{.8f, 0, 1.6f});
    w.useTerminal(1, f, 5);
    auto cash = w.accounts[w.residents[0].account].balance;
    rejects([&] { w.useTerminal(0, f, 5); });
    require(w.accounts[w.residents[0].account].balance == cash,
            "Occupied terminal charged customer");
    conserved(w);
  });
  test("Job seekers choose an actual funded vacancy and receive its shift and wage", [] {
    auto w = small();
    Id f = shop(w);
    for (auto &site : w.facilities)
      site.hiring = false;
    w.facilities[f].hiring = true;
    w.facilities[f].staffNeeded = 1;
    w.facilities[f].staffRole = Role::Vendor;
    w.facilities[f].wageOffer = 2000;
    place(w, 0, w.facilities[f].pos + Vec3{5, 0, 5});
    arm(w);
    w.residentPlans[0].nextCareer = w.time;
    w.careerDecision(0);
    require(w.residents[0].jobSite == f && w.residents[0].role == Role::Vendor &&
                w.residents[0].hourlyWage == 2000 &&
                w.residents[0].employer == w.operatingAccount(f),
            "Career was decorative");
    auto old = w.residentPlans[0].lastCareer;
    w.residentPlans[0].nextCareer = w.time;
    w.careerDecision(0);
    require(w.residentPlans[0].lastCareer == old, "Resident repeatedly changed career");
    conserved(w);
  });
  test("Unfunded employers cannot recruit through autonomy", [] {
    auto w = small();
    for (auto &f : w.facilities) {
      auto account = w.operatingAccount(f.id);
      w.pay(account, 0, w.accounts[account].balance, "test-outflow");
    }
    arm(w);
    w.residentPlans[0].nextCareer = w.time;
    w.careerDecision(0);
    require(w.residents[0].role == Role::Unemployed, "Unfunded job was accepted");
    conserved(w);
  });
  test("A wealthy resident buys an operating business and earns only real subsequent fees", [] {
    auto w = small();
    Id f = w.nearestAt({0, 0, 0}, FacilityKind::Water);
    w.facilities[f].askingPrice = 100000;
    w.facilities[f].price = 1000;
    place(w, 0, w.facilities[f].pos + Vec3{4, 0, 5});
    place(w, 3, w.facilities[f].pos + Vec3{0, 0, 2.4f});
    w.joinQueue(3, f);
    w.pay(0, w.residents[0].account, 2000000, "test-capital");
    arm(w);
    w.residentPlans[0].nextBusiness = w.time;
    auto original = w.facilities[f].owner;
    auto seller = w.accounts[original].balance;
    auto cash = w.accounts[w.residents[0].account].balance;
    w.businessDecision(0);
    require(w.facilities[f].owner == w.residents[0].account &&
                w.accounts[original].balance == seller + 100000 &&
                w.accounts[w.residents[0].account].balance == cash - 100000,
            "Investment invented ownership or profit");
    double until = w.facilities[f].serving[0].until;
    w.advance(until - w.time + .01, 100000);
    require(w.accounts[w.residents[0].account].balance == cash - 100000 + 1000,
            "Business did not earn the real customer's payment");
    conserved(w);
  });
  test("Trusted neighbors hand over a spare meal while retaining their own reserve", [] {
    auto w = small();
    place(w, 0, {50, 0, 60});
    place(w, 1, {51, 0, 60});
    w.residents[0].aggression = .1f;
    w.residents[1].hunger = .8f;
    Id a = add(w, 0, ItemKind::Food), b = add(w, 0, ItemKind::Food);
    w.relationships[1] = {0, 1, .6f, -3600, 0};
    require(w.shareFood(0, 1), "Friend would not share surplus food");
    require(w.items[b].owner == w.residents[1].account && w.items[b].carrier == 1 &&
                w.items[a].carrier == 0,
            "Meal was conjured or giver lost last meal");
    require(!w.shareFood(0, 1), "Aid cooldown or reserve bypassed");
    auto trust = w.trust(0, 1);
    w.consumeItem(1, b);
    require(w.residents[1].hunger < .3f && w.trust(0, 1) == trust,
            "Gift did not become usable food");
    conserved(w);
  });
  test("Untrusted or distant requests cannot take another resident's food", [] {
    auto w = small();
    place(w, 0, {50, 0, 60});
    place(w, 1, {51, 0, 60});
    w.residents[1].hunger = .8f;
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    require(!w.shareFood(0, 1), "Stranger took food without consent");
    w.relationships[1] = {0, 1, .6f, -3600, 0};
    place(w, 1, {80, 0, 60});
    require(!w.shareFood(0, 1), "Food teleported between friends");
    conserved(w);
  });
  test("Dead pickup couriers release the parcel for another real worker", [] {
    auto w = small();
    Id f = shop(w);
    Id o = w.order(0, f, ItemKind::Food);
    courier(w, f);
    if (w.orders[o].state == OrderState::Waiting)
      w.assignDelivery(o, 2);
    w.kill(2);
    w.orderTick();
    require(w.orders[o].state == OrderState::Waiting && w.orders[o].courier == none,
            "Dead courier kept reservation");
    courier(w, f, 4);
    w.assignDelivery(o, 4);
    require(w.orders[o].courier == 4, "Parcel could not be reassigned");
    conserved(w);
  });
  test("Lost shipped parcels settle goods and refund only unearned delivery once", [] {
    auto w = small();
    Id f = shop(w);
    courier(w, f);
    Id o = w.order(0, f, ItemKind::Food);
    if (w.orders[o].state == OrderState::Waiting)
      w.assignDelivery(o, 2);
    place(w, 2, w.facilities[f].pos + Vec3{0, 0, 1.6f});
    w.pickup(o, 2);
    auto cash = w.accounts[w.residents[0].account].balance,
         shopCash = w.accounts[w.operatingAccount(f)].balance;
    w.kill(2);
    w.orderTick();
    require(w.orders[o].state == OrderState::Lost && w.accounts[w.orders[o].escrow].balance == 0 &&
                w.accounts[w.residents[0].account].balance == cash + w.orders[o].deliveryFee &&
                w.accounts[w.operatingAccount(f)].balance == shopCash + w.orders[o].price,
            "Lost parcel stranded escrow or refunded shipped goods twice");
    auto after = w.accounts[w.residents[0].account].balance;
    w.orderTick();
    require(w.accounts[w.residents[0].account].balance == after, "Refund repeated");
    conserved(w);
  });
  test("Manual, child, airborne and urgent residents are not commandeered by planning", [] {
    auto w = small();
    w.autonomyDecision(0);
    require(w.residentPlans.empty(), "Manual player was commandeered");
    arm(w);
    w.residents[0].age = 12;
    require(!w.autonomyDecision(0) && w.orders.empty(), "Child made adult financial decisions");
    w.residents[0].age = 25;
    place(w, 0, {0, 10, 0});
    w.stepBody(0, {}, .02f);
    require(!w.autonomyDecision(0) && w.orders.empty(), "Planning interrupted falling");
    conserved(w);
  });
  test("Malformed resident-plan references and enum values are rejected", [] {
    auto w = small();
    arm(w);
    w.residentPlans[0].order = 999999;
    rejects([&] { w.validate(); });
    w.residentPlans[0].order = none;
    w.residentPlans[0].action = AutonomyAction(999);
    rejects([&] { w.validate(); });
  });
  test("Residents wait briefly for nearby delivery but urgent thirst still takes priority", [] {
    auto w = small();
    Id f = shop(w);
    courier(w, f);
    place(w, 0, w.facilities[f].pos + Vec3{4, 0, 5});
    arm(w);
    w.householdShopping(0);
    require(w.autonomyDecision(0) && w.residentPlans[0].action == AutonomyAction::WaitingDelivery,
            "Buyer wandered away from an imminent handover");
    w.residents[0].thirst = 1.1f;
    w.decide(0);
    require(w.residents[0].task == TaskKind::Service &&
                w.facilities[w.residents[0].target].kind == FacilityKind::Water,
            "Waiting for a parcel overrode urgent survival");
    conserved(w);
  });
  test("A worker can leave an unpaid employer without forgiving the debt", [] {
    auto w = small();
    Id source = shop(w);
    Id target = w.nearestAt({0, 0, 0}, FacilityKind::Water);
    for (auto &f : w.facilities)
      f.hiring = false;
    w.facilities[source].hiring = true;
    w.facilities[source].staffRole = Role::Vendor;
    w.facilities[source].staffNeeded = 1;
    w.chooseJob(0, Role::Vendor, source);
    w.residents[0].hourlyWage = 1500;
    Id debtor = w.residents[0].employer;
    w.pay(debtor, 0, w.accounts[debtor].balance, "test-employer-insolvency");
    w.residents[0].routine.wageClaims[debtor] = 30000;
    auto &offer = w.facilities[target];
    offer.kind = FacilityKind::Shop;
    offer.staffRole = Role::Vendor;
    offer.staffNeeded = 1;
    offer.wageOffer = 1000;
    offer.hiring = true;
    place(w, 0, offer.pos + Vec3{4, 0, 5});
    arm(w);
    w.residentPlans[0].nextCareer = w.time;
    w.careerDecision(0);
    require(w.residents[0].jobSite == target && w.residents[0].routine.wageClaims[debtor] == 30000,
            "Changing jobs erased unpaid wages or trapped the worker");
    conserved(w);
  });
  test("Career choices fill a real night vacancy and adjust the sleep schedule", [] {
    auto w = small();
    Id f = shop(w);
    for (auto &site : w.facilities)
      site.hiring = false;
    auto &site = w.facilities[f];
    site.hiring = true;
    site.staffNeeded = 1;
    site.staffRole = Role::Vendor;
    site.wageOffer = 2500;
    for (Id id : {1, 2}) {
      auto &worker = w.residents[id];
      worker.role = Role::Vendor;
      worker.jobSite = f;
      worker.employer = w.operatingAccount(f);
      worker.routine.shiftStart = id == 1 ? 6 : 14;
      site.employees.push_back(id);
    }
    place(w, 0, site.pos + Vec3{4, 0, 5});
    arm(w);
    w.residentPlans[0].nextCareer = w.time;
    w.careerDecision(0);
    require(w.residents[0].role == Role::Vendor && w.residents[0].routine.shiftStart == 22 &&
                w.residents[0].routine.sleepStart == 8,
            "Night worker kept a daytime schedule");
    conserved(w);
  });
  test("Unattended fire stations can still recruit real professional firefighters", [] {
    auto w = small();
    Id f = w.nearestAt({0, 0, 0}, FacilityKind::FireStation);
    for (auto &site : w.facilities)
      site.hiring = false;
    w.facilities[f].hiring = true;
    w.facilities[f].staffNeeded = 0;
    place(w, 0, w.facilities[f].pos + Vec3{4, 0, 5});
    arm(w);
    w.residentPlans[0].nextCareer = w.time;
    w.careerDecision(0);
    require(w.residents[0].role == Role::Firefighter && w.residents[0].jobSite == f,
            "Self-service equipment flag prevented firefighter employment");
    conserved(w);
  });
  test("A parcel remains with its courier while the recipient is airborne", [] {
    auto w = small();
    Id f = shop(w);
    courier(w, f);
    Id order = w.order(0, f, ItemKind::Food);
    place(w, 0, {0, 4, 0});
    w.stepBody(0, {}, .02f);
    place(w, 2, w.facilities[f].pos + Vec3{1.6f, 0, 0});
    w.pickup(order, 2);
    require(w.orders[order].state == OrderState::ToCustomer && w.residents[2].route.empty() &&
                w.items[w.orders[order].item].carrier == 2,
            "Courier routed into empty air");
    w.advance(96, 100000);
    place(w, 0, w.facilities[f].pos + Vec3{5, 0, 5});
    w.decide(2);
    for (int n = 0; n < 120 && w.orders[order].state != OrderState::Delivered; ++n)
      w.advance(60, 100000);
    require(w.orders[order].state == OrderState::Delivered, "Paused delivery never resumed");
    conserved(w);
  });
  test("An interrupted courier cannot be reserved for two parcels", [] {
    auto w = small();
    Id f = shop(w);
    courier(w, f);
    Id first = w.order(0, f, ItemKind::Food);
    place(w, 2, w.facilities[f].pos + Vec3{2, 0, 2});
    Id second = w.order(0, f, ItemKind::Food);
    require(w.orders[first].courier == 2 && w.orders[second].state == OrderState::Waiting,
            "Courier was automatically double-booked");
    rejects([&] { w.assignDelivery(second, 2); });
    conserved(w);
  });
  test("Changing occupation releases an uncollected parcel without losing the buyer's money", [] {
    auto w = small();
    Id f = shop(w);
    courier(w, f);
    Id order = w.order(0, f, ItemKind::Food);
    auto balance = w.accounts[w.orders[order].escrow].balance;
    w.facilities[f].staffRole = Role::Vendor;
    w.facilities[f].staffNeeded = 1;
    w.chooseJob(2, Role::Vendor, f);
    w.orderTick();
    require(w.orders[order].state == OrderState::Waiting && w.orders[order].courier == none &&
                w.residents[2].route.empty() &&
                w.accounts[w.orders[order].escrow].balance == balance,
            "Career change kept a phantom parcel reservation");
    conserved(w);
  });
  test("A builder honors an assigned physical contract before changing career", [] {
    auto w = small();
    w.pay(0, w.residents[0].account, 1000000, "test-construction-capital");
    Id project = w.commissionFacility(0, FacilityKind::Toilet, 1, {80, 3, 80}, 1);
    Id workshop = w.nearest(2, FacilityKind::Workshop);
    w.chooseJob(2, Role::Builder, workshop);
    w.residents[2].routine.shiftStart = 0;
    w.residents[2].routine.shiftHours = 24;
    w.residents[2].routine.daysOff = 0;
    w.assignBuilder(project, 2);
    Id offer = shop(w);
    w.facilities[offer].wageOffer = 5000;
    w.facilities[offer].staffRole = Role::Vendor;
    w.facilities[offer].staffNeeded = 1;
    place(w, 2, w.facilities[offer].pos + Vec3{4, 0, 5});
    arm(w, 2);
    w.residentPlans[2].nextCareer = w.time;
    w.careerDecision(2);
    require(w.residents[2].jobSite == workshop && w.residents[2].role == Role::Builder &&
                w.projects[project].worker == 2,
            "Career change abandoned a funded construction contract");
    conserved(w);
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
