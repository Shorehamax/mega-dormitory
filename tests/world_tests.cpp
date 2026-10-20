#include "mega/world.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
using namespace mega;
static int passed = 0, failed = 0;
static void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> static void rejects(F fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "Expected action to be rejected");
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
  c.floors = 10;
  c.layoutVersion = 1;
  c.bedsPerFloor = 64;
  c.startingCash = 200000;
  c.mode = Mode::Cheat;
  auto w = World::generate(c);
  while (!w.events.empty())
    w.events.pop();
  for (auto &r : w.residents) {
    r.automatic = false;
    r.routine.workSince = -1;
    r.queuedAt = none;
    r.activity = Activity::Idle;
    r.route.clear();
  }
  for (auto &f : w.facilities) {
    f.staffNeeded = 0;
    f.opensAt = 0;
    f.openHours = 24;
    f.queue.clear();
    f.serving.clear();
  }
  return w;
}
static void place(World &w, Id id, Vec3 p) {
  auto &r = w.residents[id];
  r.pos = p;
  r.route.clear();
  ++r.routeVersion;
  r.activity = Activity::Idle;
  r.task = TaskKind::None;
}
static void conserved(const World &w) {
  require(std::accumulate(w.accounts.begin(), w.accounts.end(), Money(0),
                          [](Money a, const Account &b) { return a + b.balance; }) == 0,
          "Money was created internally");
}
static Id prepareAdmin(World &w) {
  Id facility = w.nearest(0, FacilityKind::Office);
  w.chooseJob(0, Role::Administrator, facility);
  auto &r = w.residents[0];
  r.routine.shiftStart = 0;
  r.routine.shiftHours = 24;
  place(w, 0, w.facilities[facility].pos + Vec3{0, 0, -1.6f});
  w.clockIn(0);
  return facility;
}
static std::vector<int> adminAnswers(const OfficeOrder &order) {
  std::vector<int> answer;
  for (int slot = 0; slot < 4; ++slot)
    answer.push_back((slot + order.brief) % 4);
  return answer;
}
static void finishAttendance(World &w) {
  w.officeTick();
  for (int n = 0; n < 11; ++n) {
    w.time += 60;
    w.officeTick();
  }
}

int main() {
  test("Default generation is 2,000 floors and 100,000 residents", [] {
    Config c;
    require(c.floors == 2000 && c.population == 100000, "Wrong defaults");
    require(c.dayMinutes == 30, "Wrong day length");
  });
  test("World settings validate before allocation", [] {
    Config c;
    c.population = -1;
    rejects([&] { World::generate(c); });
    c.population = 20;
    c.scarcity = NAN;
    rejects([&] { c.validate(); });
  });
  test("Same seed preserves identities and initial economy", [] {
    auto a = small(), b = small();
    require(a.rng == b.rng && a.names[3].zh == b.names[3].zh &&
                a.accounts[7].balance == b.accounts[7].balance,
            "Seed changed output");
  });
  test("Names switch languages without changing identity", [] {
    auto w = small();
    require(w.displayName(0, "en") == "Alex Chen" && w.displayName(0, "fr") == "Alexandre Chen" &&
                w.displayName(0, "zh") == "陈明",
            "Name localization missing");
    require(w.residents[0].id == 0, "Identity changed");
  });
  test("No internal money creation", [] {
    auto w = small();
    conserved(w);
    w.pay(w.residents[0].account, w.residents[1].account, 1000, "test");
    conserved(w);
  });
  test("Insufficient funds is atomic", [] {
    auto w = small();
    auto sum = w.internalMoney();
    auto from = w.residents[0].account;
    auto before = w.accounts[from].balance;
    rejects([&] { w.pay(from, 1, before + 1, "bad"); });
    require(w.accounts[from].balance == before && w.internalMoney() == sum,
            "Failed payment mutated balances");
  });
  test("Negative payment rejected", [] {
    auto w = small();
    rejects([&] { w.pay(2, 3, -1, "bad"); });
  });
  test("Cannot receive service remotely", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Food);
    place(w, 0, {500, 0, 0});
    rejects([&] { w.joinQueue(0, f); });
  });
  test("Services obey capacity and FIFO", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Food);
    w.facilities[f].stations = 1;
    for (Id i : {0, 1, 2}) {
      place(w, i, w.facilities[f].pos);
      w.joinQueue(i, f);
    }
    require(w.facilities[f].serving.size() == 1 && w.facilities[f].queue == std::vector<Id>({1, 2}),
            "Queue or station capacity wrong");
    w.advance(w.facilities[f].serving[0].until - w.time + .01);
    require(w.facilities[f].serving[0].resident == 1 && w.facilities[f].queue[0] == 2,
            "FCFS not respected");
    conserved(w);
  });
  test("Queue cutting changes actual order and records misconduct", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Food);
    w.facilities[f].stations = 1;
    for (Id i : {0, 1, 2}) {
      place(w, i, w.facilities[f].pos);
      w.joinQueue(i, f);
    }
    w.cutQueue(2, f, 0);
    require(w.facilities[f].queue == std::vector<Id>({2, 1}), "Queue did not change");
    require(w.incidents.back().kind == CrimeKind::QueueCutting, "No misconduct recorded");
  });
  test("Closed service cannot charge", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Food);
    place(w, 0, w.facilities[f].pos);
    w.facilities[f].open = false;
    Money before = w.accounts[w.residents[0].account].balance;
    rejects([&] { w.joinQueue(0, f); });
    require(w.accounts[w.residents[0].account].balance == before, "Closed venue charged");
  });
  test("Movement takes a continuous route around the shaft", [] {
    auto w = small();
    place(w, 0, {-10, 0, -5});
    w.travel(0, {10, 6, -5}, TaskKind::None, none);
    require(w.residents[0].route.size() >= 5, "Missing corridor waypoints");
    auto start = w.position(0);
    w.advance(10);
    require(length(w.position(0) - start) > 0 && length(w.position(0) - Vec3{10, 6, -5}) > 2,
            "Movement teleported");
  });
  test("Internet requires an owned device", [] {
    auto w = small();
    w.residents[0].items.clear();
    rejects([&] { w.order(0, w.nearest(0, FacilityKind::Shop), ItemKind::Food); });
  });
  test("Paid computer access expires and requires proximity", [] {
    auto w = small();
    w.residents[0].items.clear();
    Id f = w.nearest(0, FacilityKind::Terminal);
    place(w, 0, w.facilities[f].pos);
    w.useTerminal(0, f, 1);
    require(w.hasDevice(0), "Terminal not accessible");
    w.advance(61);
    require(!w.hasDevice(0), "Terminal never expires");
    conserved(w);
  });
  test("Messages persist with stable participants", [] {
    auto w = small();
    w.sendMessage(0, 1, "你好 / Bonjour / Hello");
    require(w.messages.size() == 1 && w.messages[0].from == 0 && w.messages[0].to == 1,
            "Message missing");
  });
  test("Order reserves stock and escrow, not seller revenue", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Shop);
    Money seller = w.accounts[w.facilities[f].owner].balance;
    int stock = w.facilities[f].stock;
    Id id = w.order(0, f, ItemKind::Food);
    auto &o = w.orders[id];
    require(w.accounts[o.escrow].balance == o.price + o.deliveryFee &&
                w.accounts[w.facilities[f].owner].balance == seller,
            "Premature settlement");
    require(w.facilities[f].stock == stock - 1, "Stock not reserved");
    conserved(w);
  });
  test("Order cancellation refunds once", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Shop);
    Money cash = w.accounts[w.residents[0].account].balance;
    Id id = w.order(0, f, ItemKind::Food);
    w.cancelOrder(id, 0);
    require(w.accounts[w.residents[0].account].balance == cash, "Incomplete refund");
    rejects([&] { w.cancelOrder(id, 0); });
    conserved(w);
  });
  test("Delivery cannot finish without pickup and proximity", [] {
    auto w = small();
    Id id = w.order(0, w.nearest(0, FacilityKind::Shop), ItemKind::Food);
    w.residents[2].role = Role::Courier;
    w.assignDelivery(id, 2);
    rejects([&] { w.deliver(id, 2); });
    rejects([&] { w.pickup(id, 2); });
  });
  test("Courier physically collects, carries, hands over and gets paid", [] {
    auto w = small();
    Id shop = w.nearest(0, FacilityKind::Shop);
    place(w, 0, {-10, 0, 8});
    place(w, 2, {10, 0, 8});
    w.residents[2].role = Role::Courier;
    Money before = w.accounts[w.residents[2].account].balance;
    Id id = w.order(0, shop, ItemKind::Food);
    w.assignDelivery(id, 2);
    double pickup = w.residents[2].route.back().time;
    w.advance(pickup - w.time + .01);
    require(w.orders[id].state == OrderState::ToCustomer && w.items[w.orders[id].item].carrier == 2,
            "Parcel not picked up");
    double delivery = w.residents[2].route.back().time;
    w.advance(delivery - w.time + .01);
    require(w.orders[id].state == OrderState::Delivered && w.items[w.orders[id].item].carrier == 0,
            "Parcel not delivered");
    require(w.accounts[w.residents[2].account].balance == before + w.orders[id].deliveryFee,
            "Courier not paid correctly");
    rejects([&] { w.deliver(id, 2); });
    conserved(w);
    w.validate();
  });
  test("Video editing rewards coherent sequencing", [] {
    auto w = small();
    require(w.scoreVideo({0, 1, 2, 3, 4, 5}) > w.scoreVideo({4, 2, 0, 5, 1, 3}),
            "Sequence irrelevant");
    require(w.scoreVideo({0, 0, 1}) == 0, "Duplicate clips rewarded");
  });
  test("Video payment is client-funded and cannot repeat", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    w.chooseJob(0, Role::VideoEditor, f);
    place(w, 0, w.facilities[f].pos);
    Id v = w.commissionVideo(0, w.dormAccount, w.facilities[f].owner, 24000);
    w.submitVideo(v, {0, 1, 2, 3, 4, 5});
    require(w.videos[v].submitted && w.accounts[w.videos[v].escrow].balance == 0,
            "Commission not settled");
    rejects([&] { w.submitVideo(v, {0, 1, 2, 3, 4, 5}); });
    conserved(w);
  });
  test("Housing value follows nearby services", [] {
    auto w = small();
    for (auto &f : w.facilities)
      f.open = false;
    w.facilities[0].open = true;
    w.facilities[0].floor = 0;
    require(w.housingPrice(0) > w.housingPrice(9), "Amenities do not affect price");
  });
  test("Repricing does not rewrite existing lease rents", [] {
    auto w = small();
    Money before = w.housing[0].rent;
    w.facilities[0].open = false;
    w.repriceHousing();
    require(w.housing[0].rent == before, "Contract rent changed");
  });
  test("Buying a home preserves the tenant", [] {
    auto w = small();
    Id unit = w.residentHomes[1];
    w.pay(0, w.residents[0].account, 1000000, "test-capital");
    w.buyHome(0, unit);
    require(w.housing[unit].owner == w.residents[0].account && w.housing[unit].tenant == 1,
            "Purchase evicted tenant");
    conserved(w);
  });
  test("Demolition cannot bypass occupant consent", [] {
    auto w = small();
    w.dormOwner = 0;
    Id p = w.proposeProject(0, 0, true, 100000);
    require(!w.projects[p].affected.empty(), "No occupant claims");
    rejects([&] { w.fundProject(p, 0); });
    require(w.facilities[0].open, "Proposal removed facility");
  });
  test("Compensation comes from project owner's budget", [] {
    auto w = small();
    w.dormOwner = 0;
    w.facilities[0].owner = w.dormAccount;
    Id p = w.proposeProject(0, 0, false, 100000);
    for (Id r : w.projects[p].affected)
      w.consentProject(p, r);
    Money treasury = w.accounts[w.dormAccount].balance;
    w.fundProject(p, 0);
    require(w.accounts[w.dormAccount].balance == treasury - 100000 - w.projects[p].compensation,
            "Budget not charged");
    conserved(w);
  });
  test("Only a real builder at the site progresses work", [] {
    auto w = small();
    w.dormOwner = 0;
    w.facilities[0].owner = w.dormAccount;
    Id p = w.proposeProject(0, 0, false, 100000);
    for (Id r : w.projects[p].affected)
      w.consentProject(p, r);
    w.fundProject(p, 0);
    w.residents[2].role = Role::Builder;
    w.assignBuilder(p, 2);
    double initial = w.projects[p].remaining;
    w.workProject(p, 3600);
    require(w.projects[p].remaining == initial, "Remote construction progressed");
    w.advance(w.residents[2].route.back().time - w.time + .1);
    w.workProject(p, 3600);
    require(w.projects[p].state == ProjectState::Completed, "Work did not complete");
    conserved(w);
  });
  test("Stronger attacker causes surrender", [] {
    auto w = small();
    place(w, 0, {10, 0, 0});
    place(w, 1, {10, 0, 1});
    w.residents[0].strength = 90;
    w.residents[1].strength = 20;
    w.attack(0, 1, false);
    require(w.residents[1].surrendered, "Opponent did not surrender");
    require(w.residents[1].opponent == none, "Surrendered opponent still attacking");
  });
  test("Executing a surrendered person is murder", [] {
    auto w = small();
    place(w, 0, {10, 0, 0});
    place(w, 1, {10, 0, 1});
    w.surrender(1);
    w.attack(0, 1, true);
    require(!w.residents[1].alive && w.incidents.back().kind == CrimeKind::Murder,
            "Murder consequence missing");
  });
  test("Arrest requires a nearby warden and a reported crime", [] {
    auto w = small();
    w.residents[2].role = Role::Warden;
    w.surrender(1);
    rejects([&] { w.apprehend(2, 1); });
    w.report(CrimeKind::Assault, 1, 0);
    place(w, 2, {500, 0, 0});
    rejects([&] { w.apprehend(2, 1); });
  });
  test("Prisoner is escorted physically before entering jail", [] {
    auto w = small();
    w.residents[2].role = Role::Warden;
    place(w, 2, {-10, 0, 3});
    place(w, 1, {-10, 0, 4});
    w.report(CrimeKind::Assault, 1, 0);
    w.surrender(1);
    w.apprehend(2, 1);
    require(w.residents[1].activity == Activity::Escorted, "Missing escort state");
    double end = w.residents[2].route.back().time;
    w.advance((end - w.time) / 2);
    require(length(w.position(2) - w.position(1)) < 1, "Prisoner detached from escort");
    require(w.residents[1].activity != Activity::Jailed, "Prisoner teleported to jail");
    w.advance(end - w.time + .1);
    require(w.residents[1].activity == Activity::Jailed, "No custody on arrival");
  });
  test("Fall damage depends on impact velocity", [] {
    require(fallDamage(5) == 0 && fallDamage(12) > 0 && fallDamage(20) > 100,
            "Incorrect fall damage curve");
    auto w = small();
    w.applyFall(0, 20);
    require(!w.residents[0].alive, "Fatal fall survived");
  });
  test("Pushing a resident to death records responsibility", [] {
    auto w = small();
    w.applyFall(1, 20, 0);
    require(w.incidents.back().offender == 0 && w.incidents.back().kind == CrimeKind::Murder,
            "Pusher responsibility lost");
  });
  test("Leaving Survival ends the run and exports cash", [] {
    auto w = small();
    w.config.mode = Mode::Survival;
    place(w, 0, {-12, 0, 14});
    Money total = w.internalMoney(), cash = w.accounts[w.residents[0].account].balance;
    w.depart(0);
    require(w.gameOver && w.internalMoney() == total - cash, "Departure semantics incorrect");
    conserved(w);
  });
  test("Empty population ends the world", [] {
    auto w = small();
    for (Id i = 0; i < Id(w.residents.size()); ++i)
      w.kill(i);
    require(w.gameOver && w.living() == 0, "Empty world did not end");
  });
  test("Save/load preserves queues, routes, names, money and messages", [] {
    auto w = small();
    w.sendMessage(0, 1, "Bonjour 你好");
    w.travel(0, {10, 3, 7}, TaskKind::None, none);
    auto path = std::filesystem::temp_directory_path() / "mega-native-test-slot.mdsave";
    w.save(path);
    auto restored = World::load(path);
    require(restored.time == w.time && restored.rng == w.rng && restored.sequence == w.sequence &&
                restored.events.size() == w.events.size() &&
                restored.names[0].zh == w.names[0].zh &&
                restored.internalMoney() == w.internalMoney() &&
                restored.messages[0].text == w.messages[0].text,
            "Save lost state");
    require(length(restored.position(0) - w.position(0)) < .001f, "Save moved resident");
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
  });
  test("Corrupted save is rejected", [] {
    auto path = std::filesystem::temp_directory_path() / "mega-native-bad-slot.mdsave";
    {
      std::ofstream f(path);
      f << "MDORM001bad";
    }
    rejects([&] { World::load(path); });
    std::filesystem::remove(path);
  });
  test("Queue positions remain distinct and inside the building", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Food);
    w.facilities[f].stock = 0;
    for (Id id = 0; id < 12; ++id) {
      place(w, id, w.facilities[f].pos);
      w.joinQueue(id, f);
    }
    w.advance(2000);
    for (Id id = 0; id < 12; ++id) {
      auto p = w.position(id);
      require(walkable(p), "Queue outside walkable bank");
      for (Id j = 0; j < id; ++j)
        require(length(p - w.position(j)) > .6f, "Queue members overlap");
    }
    w.validate();
  });
  test("Cancelling laundry returns dirty clothes and frees its station", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Laundry);
    Id garment = w.residents[0].outfit;
    w.items[garment].cleanliness = .1f;
    place(w, 0, w.facilities[f].pos);
    w.joinQueue(0, f);
    require(w.items[garment].carrier == none, "Garment not at laundry");
    w.leaveQueue(0);
    require(w.facilities[f].serving.empty() && w.items[garment].carrier == 0 &&
                w.items[garment].cleanliness == .1f,
            "Cancellation leaked a station or washed instantly");
    w.advance(2000);
    require(w.items[garment].cleanliness == .1f, "Stale service event washed cancelled clothes");
    w.validate();
  });
  test("Completed laundry returns clean owned clothing", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Laundry), garment = w.residents[0].outfit;
    w.items[garment].cleanliness = .1f;
    place(w, 0, w.facilities[f].pos);
    w.joinQueue(0, f);
    w.advance(w.facilities[f].serving[0].until - w.time + .01);
    require(w.items[garment].cleanliness == 1 && w.residents[0].outfit == garment,
            "Laundry did not finish");
    w.validate();
  });
  test("Changing clothes checks possession and ownership", [] {
    auto w = small();
    Id other = w.residents[1].outfit;
    rejects([&] { w.changeClothes(0, other); });
    Id own = w.residents[0].items.back();
    w.changeClothes(0, own);
    require(w.residents[0].outfit == own, "Outfit not changed");
  });
  test("A service cannot restore a resident who died while waiting", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Food);
    place(w, 0, w.facilities[f].pos);
    w.residents[0].health = .001f;
    w.residents[0].hunger = 1.4f;
    w.joinQueue(0, f);
    w.advance(w.facilities[f].serving[0].until - w.time + .01);
    require(!w.residents[0].alive && w.residents[0].activity == Activity::Dead,
            "Service revived a dead resident");
    w.validate();
  });
  test("Ground floor corridor supports a falling body", [] {
    Body b;
    b.position = {0, .1f, 0};
    for (int i = 0; i < 30; ++i)
      integrateBody(b, {}, .02f);
    require(b.grounded && b.position.y == 0, "Fell through the lobby");
  });
  test("Insufficient vehicle purchase does not transfer ownership", [] {
    auto w = small();
    auto &v = w.vehicles[2];
    place(w, 0, w.facilities[v.dealer].pos);
    Id owner = v.owner;
    Money cash = w.accounts[w.residents[0].account].balance;
    rejects([&] { w.buyVehicle(0, 2); });
    require(v.owner == owner && w.accounts[w.residents[0].account].balance == cash,
            "Failed purchase mutated ownership");
    conserved(w);
  });
  test("Vehicle purchase is physical, seller-funded and grants no parking", [] {
    auto w = small();
    Id dealer = w.vehicles[2].dealer, owner = w.vehicles[2].owner;
    w.pay(0, w.residents[0].account, 5000000, "test");
    rejects([&] { w.buyVehicle(0, 2); });
    place(w, 0, w.facilities[dealer].pos);
    Money seller = w.accounts[owner].balance, price = w.vehicles[2].askingPrice;
    w.buyVehicle(0, 2);
    require(w.vehicles[2].owner == w.residents[0].account && w.vehicles[2].bay == none &&
                w.accounts[owner].balance == seller + price,
            "Incorrect ownership settlement");
    rejects([&] { w.buyVehicle(0, 2); });
    conserved(w);
    w.validate();
  });
  test("Parking is scarce and double reservation cannot charge", [] {
    auto w = small();
    require(std::count_if(w.parking.begin(), w.parking.end(),
                          [](auto &b) { return b.vehicle == none; }) == 1,
            "Initial parking scarcity missing");
    auto &v = w.vehicles[0];
    v.owner = w.residents[0].account;
    v.askingPrice = 0;
    v.dealer = none;
    Money before = w.accounts[w.residents[0].account].balance;
    w.reserveParking(0, 0, 3);
    require(w.accounts[w.residents[0].account].balance == before - w.parking[3].dailyPrice,
            "Parking not paid");
    Money after = w.accounts[w.residents[0].account].balance;
    rejects([&] { w.reserveParking(0, 0, 3); });
    require(w.accounts[w.residents[0].account].balance == after, "Double charge");
    conserved(w);
    w.validate();
  });
  test("Parking reservation does not teleport the vehicle", [] {
    auto w = small();
    auto &v = w.vehicles[0];
    v.owner = w.residents[0].account;
    v.askingPrice = 0;
    v.dealer = none;
    Vec3 before = v.pos;
    w.reserveParking(0, 0, 3);
    require(length(v.pos - before) == 0, "Booking moved vehicle");
    rejects([&] { w.parkVehicle(0, 0, 3); });
  });
  test("Only the owner near a vehicle may drive it", [] {
    auto w = small();
    rejects([&] { w.enterVehicle(0, 0); });
    auto &v = w.vehicles[0];
    v.owner = w.residents[0].account;
    v.askingPrice = 0;
    v.dealer = none;
    rejects([&] { w.enterVehicle(0, 0); });
    place(w, 0, v.pos);
    w.enterVehicle(0, 0);
    rejects([&] { w.travel(0, {5, 0, 0}, TaskKind::None, none); });
    require(v.driver == 0 && w.residents[0].vehicle == 0, "Driver not linked");
    w.validate();
  });
  test("Vehicle motion accelerates continuously and cannot drive through walls", [] {
    auto w = small();
    auto &v = w.vehicles[0];
    v.owner = w.residents[0].account;
    v.askingPrice = 0;
    v.dealer = none;
    v.pos = {0, 0, 0};
    place(w, 0, v.pos);
    w.enterVehicle(0, 0);
    Vec3 start = v.pos;
    w.driveVehicle(0, 1, 0, .05f);
    require(length(v.pos - start) > 0 && length(v.pos - start) < .05f, "Discontinuous movement");
    for (int i = 0; i < 300; ++i)
      w.driveVehicle(0, 1, 0, .05f);
    require(std::abs(v.pos.z) < 14.61f, "Vehicle left the building");
    w.validate();
  });
  test("Parking requires arrival and retains occupancy after exit", [] {
    auto w = small();
    auto &v = w.vehicles[0];
    v.owner = w.residents[0].account;
    v.askingPrice = 0;
    v.dealer = none;
    w.reserveParking(0, 0, 3);
    v.pos = w.parking[3].pos;
    place(w, 0, v.pos);
    w.enterVehicle(0, 0);
    w.parkVehicle(0, 0, 3);
    require(v.driver == none && w.residents[0].vehicle == none && w.parking[3].vehicle == 0,
            "Exiting lost parked occupancy");
    rejects([&] { w.releaseParking(0, 0); });
    w.validate();
  });
  test("Vehicle trade-in is paid by the dealer and cannot repeat", [] {
    auto w = small();
    auto &v = w.vehicles[0];
    Id dealer = v.dealer;
    w.pay(0, w.residents[0].account, 300000, "test");
    place(w, 0, w.facilities[dealer].pos);
    w.buyVehicle(0, 0);
    v.pos = w.facilities[dealer].pos;
    Money bank = w.accounts[w.facilities[dealer].owner].balance;
    w.sellVehicle(0, 0, dealer);
    require(w.accounts[w.facilities[dealer].owner].balance == bank - Money(v.originalPrice * .6),
            "Resale did not debit dealership");
    rejects([&] { w.sellVehicle(0, 0, dealer); });
    conserved(w);
  });
  test("Vehicle ownership and parking survive save/load", [] {
    auto w = small();
    auto &v = w.vehicles[0];
    v.owner = w.residents[0].account;
    v.askingPrice = 0;
    v.dealer = none;
    w.reserveParking(0, 0, 3);
    auto path = std::filesystem::temp_directory_path() / "mega-vehicle-slot.mdsave";
    w.save(path);
    auto loaded = World::load(path);
    require(loaded.vehicles[0].owner == v.owner && loaded.parking[3].vehicle == 0 &&
                loaded.vehicles[0].bay == 3,
            "Vehicle save lost ownership or reservation");
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
  });
  test("Corrupt vehicle and inventory references are rejected", [] {
    auto w = small();
    w.residents[0].items.push_back(9999999);
    rejects([&] { w.validate(); });
    w.residents[0].items.pop_back();
    w.parking[3].vehicle = 9999999;
    rejects([&] { w.validate(); });
  });
  test("Damaged latest slot recovers its previous valid backup", [] {
    auto w = small();
    auto path = std::filesystem::temp_directory_path() / "mega-recovery-slot.mdsave";
    w.save(path);
    double before = w.time;
    w.advance(100);
    w.save(path);
    {
      std::ofstream f(path, std::ios::binary | std::ios::trunc);
      f << "damaged";
    }
    auto restored = World::load(path);
    require(restored.time == before && restored.news.back().key == "save.recovered",
            "Valid previous version not recovered");
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
  });
  test("Overnight windows wrap and office days off apply to the shift start", [] {
    auto w = small();
    auto &r = w.residents[2];
    r.role = Role::Chef;
    r.jobSite = 0;
    r.routine.shiftStart = 22;
    r.routine.shiftHours = 8;
    w.time = 25 * 3600;
    require(w.onShift(2), "Night shift lost after midnight");
    w.time = 30 * 3600;
    require(!w.onShift(2), "Shift never ended");
    r.routine.daysOff = 1;
    w.time = 25 * 3600;
    require(!w.onShift(2), "After-midnight shift used wrong weekday");
    require(World::inHours(2 * 3600, 23, 8) && !World::inHours(12 * 3600, 23, 8),
            "Sleep window broken");
  });
  test("Sleep follows the clock even when energy is full", [] {
    auto w = small();
    auto &r = w.residents[2];
    r.automatic = true;
    r.role = Role::Unemployed;
    r.jobSite = none;
    r.routine.sleepStart = 23;
    r.routine.sleepHours = 8;
    w.time = 24 * 3600;
    r.needsAt = w.time;
    r.energy = 1;
    r.hunger = .1;
    r.thirst = .1;
    r.bladder = .1;
    w.decide(2);
    require(r.task == TaskKind::Home, "Did not go to bed at night");
    w.advance(r.route.back().time - w.time + .01);
    require(r.activity == Activity::Sleeping, "Not on bed after arrival");
    w.decide(2);
    require(r.activity == Activity::Sleeping, "Full energy woke a night sleeper");
  });
  test("Unstaffed kitchen waits without charging; wrong or remote workers cannot serve", [] {
    auto w = small();
    auto &f = w.facilities[0];
    f.staffNeeded = 1;
    f.staffRole = Role::Chef;
    auto &chef = w.residents[2];
    chef.role = Role::Vendor;
    chef.jobSite = 0;
    chef.employer = f.owner;
    chef.routine.shiftStart = 0;
    chef.routine.shiftHours = 24;
    f.employees = {2};
    place(w, 2, f.pos);
    chef.activity = Activity::Working;
    place(w, 0, f.pos);
    Money before = w.accounts[w.residents[0].account].balance;
    w.joinQueue(0, 0);
    require(f.serving.empty() && f.queue.size() == 1, "Wrong profession served food");
    chef.role = Role::Chef;
    place(w, 2, {500, 0, 0});
    chef.activity = Activity::Working;
    w.serve(0);
    require(f.serving.empty(), "Remote staff counted");
    place(w, 2, f.pos);
    w.clockIn(2);
    require(f.serving.size() == 1 && w.accounts[w.residents[0].account].balance == before,
            "No actual arrival or premature charge");
    w.advance(f.serving.front().until - w.time + .01);
    require(w.accounts[w.residents[0].account].balance == before - f.price,
            "Completed service not paid once");
  });
  test("Service pauses when staff leave and resumes once without free progress", [] {
    auto w = small();
    auto &f = w.facilities[0];
    f.staffNeeded = 1;
    f.staffRole = Role::Chef;
    auto &r = w.residents[2];
    r.role = Role::Chef;
    r.jobSite = 0;
    r.employer = f.owner;
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    f.employees = {2};
    place(w, 2, f.pos);
    w.clockIn(2);
    place(w, 0, f.pos);
    w.joinQueue(0, 0);
    w.advance(30);
    w.clockOut(2);
    double remaining = f.serving.front().remaining;
    w.advance(2000);
    require(f.serving.size() == 1 && f.serving.front().until < 0, "Absent chef completed meal");
    w.clockIn(2);
    require(std::abs(f.serving.front().until - w.time - remaining) < .001, "Service progress lost");
    w.advance(remaining + .01);
    require(f.serving.empty(), "Resumed meal did not finish");
    conserved(w);
  });
  test("A worker death immediately suspends the station", [] {
    auto w = small();
    auto &f = w.facilities[0];
    f.staffNeeded = 1;
    f.staffRole = Role::Chef;
    auto &r = w.residents[2];
    r.role = Role::Chef;
    r.jobSite = 0;
    r.employer = f.owner;
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    f.employees = {2};
    place(w, 2, f.pos);
    w.clockIn(2);
    place(w, 0, f.pos);
    w.joinQueue(0, 0);
    w.kill(2);
    require(f.serving.front().until < 0 && w.staffOnDuty(0) == 0, "Dead employee still serving");
  });
  test("Unstaffed self-service toilets work at night, but respect private access", [] {
    auto w = small();
    auto f = w.nearest(0, FacilityKind::Toilet);
    w.time = 3 * 3600;
    w.facilities[f].manager = 1;
    place(w, 0, w.facilities[f].pos);
    w.setFacilityHours(1, f, 0, 24, false);
    rejects([&] { w.joinQueue(0, f); });
    w.facilities[f].whitelist.insert(0);
    w.joinQueue(0, f);
    require(w.facilities[f].serving.size() == 1, "24h self-service required staff");
    rejects([&] { w.setFacilityHours(0, f, 0, 24, true); });
  });
  test("Wages reflect actual attendance instead of a full hour at payday", [] {
    auto w = small();
    auto &r = w.residents[2];
    auto &f = w.facilities[0];
    r.role = Role::Chef;
    r.jobSite = 0;
    r.employer = f.owner;
    r.hourlyWage = 1200;
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    Money before = w.accounts[r.account].balance;
    place(w, 2, f.pos);
    w.clockIn(2);
    w.advance(900);
    w.clockOut(2);
    require(w.accounts[r.account].balance == before + 300, "Wage not proportional to on-site work");
    w.clockOut(2);
    require(w.accounts[r.account].balance == before + 300, "Wages paid twice");
  });
  test("Unpaid wages remain the old employer's debt after a job change", [] {
    auto w = small();
    auto &r = w.residents[2];
    auto &f = w.facilities[0];
    r.role = Role::Chef;
    r.jobSite = 0;
    r.employer = f.owner;
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    r.hourlyWage = 1200;
    w.pay(f.owner, 0, w.accounts[f.owner].balance, "test-withdrawal");
    place(w, 2, f.pos);
    w.clockIn(2);
    w.advance(3600);
    w.clockOut(2);
    Id old = f.owner;
    require(r.routine.wageClaims[old] == 1200, "Unpaid wages disappeared");
    Id office = w.nearest(2, FacilityKind::Office);
    w.chooseJob(2, Role::Programmer, office);
    Money balance = w.accounts[r.employer].balance;
    w.clockOut(2);
    require(w.accounts[r.employer].balance == balance && r.routine.wageClaims[old] == 1200,
            "New employer paid another employer's debt");
  });
  test("Tenants can change linens but not repaint someone else's fixtures", [] {
    auto w = small();
    Id h = w.residentHomes[0];
    auto look = w.housing[h].decor;
    look.linen = (look.linen + 1) % 8;
    w.customizeHome(0, h, look);
    rejects([&] { w.customizeHome(2, h, look); });
    look.frame = (look.frame + 1) % 8;
    rejects([&] { w.customizeHome(0, h, look); });
    w.housing[h].owner = w.residents[0].account;
    w.customizeHome(0, h, look);
  });
  test("Customization cannot modify others' clothes or occupied vehicles", [] {
    auto w = small();
    Id item = w.residents[0].outfit;
    w.customizeClothes(0, item, {3, 5, 2, 1});
    rejects([&] { w.customizeClothes(2, item, {3, 5, 2, 1}); });
    auto &v = w.vehicles[0];
    v.owner = w.residents[0].account;
    w.customizeVehicle(0, v.id, {7, 2, 1, 1});
    v.driver = 0;
    rejects([&] { w.customizeVehicle(0, v.id, {1, 2, 0, 0}); });
    rejects([&] { w.customizeClothes(0, item, {99, 0, 0, 0}); });
  });
  test("Robbery transfers one physical item, preserves provenance and reports the crime", [] {
    auto w = small();
    auto &victim = w.residents[3];
    Id item = victim.items.front();
    // Select a loose device; the worn outfit is not removable by this operation.
    victim.outfit = none;
    place(w, 0, {10, 0, 4});
    place(w, 3, {11, 0, 4});
    victim.surrendered = true;
    auto count = w.items.size();
    auto owner = w.items[item].owner;
    require(w.stealItem(0, 3, item, true), "Surrendered robbery failed");
    require(w.items.size() == count && w.items[item].carrier == 0 &&
                w.items[item].stolenFrom == owner,
            "Theft duplicated or lost provenance");
    require(std::find(victim.items.begin(), victim.items.end(), item) == victim.items.end(),
            "Victim retained stolen item");
    require(w.incidents.back().kind == CrimeKind::Theft && w.incidents.back().threat,
            "Robbery not reported");
    rejects([&] { w.stealItem(0, 3, item, true); });
    conserved(w);
  });
  test("Theft cannot reach through the world or take worn clothing", [] {
    auto w = small();
    auto &r = w.residents[3];
    Id item = r.items.front();
    place(w, 0, {500, 0, 0});
    rejects([&] { w.stealItem(0, 3, item); });
    place(w, 0, w.position(3));
    rejects([&] { w.stealItem(0, 3, r.outfit); });
  });
  test("Unpaid interrupted laundry returns the garment without cleaning it", [] {
    auto w = small();
    Id item = w.residents[0].outfit;
    w.items[item].cleanliness = .1;
    Id f = w.nearest(0, FacilityKind::Laundry);
    place(w, 0, w.facilities[f].pos);
    w.joinQueue(0, f);
    w.pay(w.residents[0].account, 1, w.accounts[w.residents[0].account].balance, "spend-cash");
    w.advance(w.facilities[f].serving[0].until - w.time + .01);
    require(w.items[item].reservedAt == none && w.items[item].carrier == 0 &&
                w.items[item].cleanliness < .11,
            "Unpaid laundry stranded/cleaned clothes");
  });
  test("New schedules, access policies and customizations survive saves", [] {
    auto w = small();
    w.residents[2].routine.shiftStart = 22;
    w.facilities[0].publicAccess = false;
    w.customizeClothes(0, w.residents[0].outfit, {7, 3, 2, 1});
    auto path = std::filesystem::temp_directory_path() / "mega-schedule-style.mdsave";
    w.save(path);
    auto b = World::load(path);
    require(b.residents[2].routine.shiftStart == 22 && !b.facilities[0].publicAccess &&
                b.items[b.residents[0].outfit].look.primary == 7,
            "New data lost");
    std::filesystem::remove(path);
  });
  test("New worlds use 512 m floors and a 640 m entrance, with adjustable dimensions", [] {
    Config c;
    c.population = 40;
    c.floors = 10;
    auto w = World::generate(c);
    require(w.layout(1).halfWidth() == 256 && w.layout(1).halfDepth() == 256,
            "Wrong standard footprint");
    require(w.layout(0).halfWidth() == 320 && w.layout(0).halfDepth() == 320,
            "Wrong entrance footprint");
    c.floorWidth = 560;
    c.floorDepth = 520;
    c.lobbyWidth = 720;
    c.lobbyDepth = 680;
    w = World::generate(c);
    require(w.floorPlans[1].width == 560 && w.floorPlans[0].depth == 680,
            "Custom dimensions ignored");
    c.lobbyWidth = 500;
    rejects([&] { c.validate(); });
  });
  test("Entrance has a reception lobby and essential services without residential beds", [] {
    Config c;
    c.population = 100;
    c.floors = 20;
    auto w = World::generate(c);
    require(std::none_of(w.housing.begin(), w.housing.end(), [](auto &h) { return h.floor == 0; }),
            "Beds occupied the lobby");
    for (auto kind : {FacilityKind::Reception, FacilityKind::Food, FacilityKind::Water,
                      FacilityKind::Toilet, FacilityKind::Shop, FacilityKind::Infirmary,
                      FacilityKind::Terminal, FacilityKind::Laundry})
      require(std::any_of(w.facilities.begin(), w.facilities.end(),
                          [&](auto &f) { return f.floor == 0 && f.kind == kind; }),
              "Missing entrance service");
    for (auto &f : w.facilities)
      for (auto &other : w.facilities)
        if (f.id != other.id && f.floor == other.floor)
          require(length(f.pos - other.pos) > 2, "Service plots overlap");
  });
  test("Capsule sweep stops at beds and cannot tunnel through a counter", [] {
    Config c;
    c.population = 20;
    c.floors = 10;
    auto w = World::generate(c);
    auto bed = w.layout(1).bed(1, 0);
    Vec3 start = bed + Vec3{0, 0, -3};
    Vec3 result = w.moveCapsule(0, start, {0, 0, 8}, false);
    require(result.z < bed.z - 1.5f, "Walked through a bunk");
    auto f = w.facilities[w.nearest(0, FacilityKind::Reception)].pos;
    result = w.moveCapsule(0, f + Vec3{0, 0, 3}, {0, 0, -30}, false);
    require(result.z > f.z + .9f, "Large step tunneled through counter");
  });
  test("Walking routes go around bed frames, counters and the open shaft", [] {
    Config c;
    c.population = 20;
    c.floors = 10;
    auto w = World::generate(c);
    place(w, 0, {-20, 3, -15});
    w.travel(0, {20, 3, -15}, TaskKind::None, none);
    auto &path = w.residents[0].route;
    for (std::size_t i = 1; i < path.size(); ++i) {
      int steps = std::max(1, int(length(path[i].p - path[i - 1].p) * 10));
      for (int n = 0; n <= steps; ++n)
        require(w.clearPosition(path[i - 1].p + (path[i].p - path[i - 1].p) * (float(n) / steps)),
                "Route crosses an obstacle or hole");
    }
  });
  test("Nearby residents pass each other without intersecting", [] {
    Config c;
    c.population = 20;
    c.floors = 10;
    auto w = World::generate(c);
    while (!w.events.empty())
      w.events.pop();
    for (auto &r : w.residents) {
      r.automatic = false;
      r.route.clear();
      r.activity = Activity::Idle;
      r.pos = {100 + float(r.id), 24, 100};
      r.queuedAt = none;
    }
    for (auto &f : w.facilities) {
      f.queue.clear();
      f.serving.clear();
    }
    place(w, 0, {20, 0, 100});
    place(w, 1, {26, 0, 100});
    w.focus(0);
    w.travel(0, {26, 0, 100}, TaskKind::None, none);
    w.travel(1, {20, 0, 100}, TaskKind::None, none);
    for (int i = 0; i < 900; ++i) {
      w.moveCrowd(.02 * 48);
      w.advance(.02 * 48);
      require(length(w.position(0) - w.position(1)) >= .57f, "Residents intersected");
    }
    require(w.position(0).x > 25 && w.position(1).x < 21, "Passing residents deadlocked");
  });
  test("Ladder traversal is smooth, stops at a landing and dismounts onto a solid floor", [] {
    Config c;
    c.population = 20;
    c.floors = 10;
    auto w = World::generate(c);
    place(w, 0, w.layout().landing(0, 0));
    w.grabLadder(0);
    Vec3 prior = w.position(0);
    for (int i = 0; i < 250; ++i) {
      w.climb(0, 1, .02f);
      require(length(w.position(0) - prior) <= 3.f * .02f + .001f,
              "Ladder exceeded its continuous speed limit");
      prior = w.position(0);
    }
    require(std::abs(w.position(0).y - 3) < .001f, "Did not stop at first landing");
    w.releaseLadder(0);
    for (int i = 0; i < 100; ++i)
      w.climb(0, 0, .02f);
    require(w.residents[0].ladder == none && w.layout(1).supported(w.position(0)),
            "Unsafe ladder exit");
    Body body{w.position(0), {}, true};
    w.moveBody(0, body, {}, .1f);
    require(body.grounded && body.position.y == 3, "Dismounted into a hole");
  });
  test("Ladder climbers cannot pass through someone above them", [] {
    Config c;
    c.population = 20;
    c.floors = 10;
    auto w = World::generate(c);
    for (auto &r : w.residents) {
      r.route.clear();
      r.activity = Activity::Idle;
      r.pos = {100 + float(r.id), 24, 100};
    }
    place(w, 0, w.layout().ladder(0, 0));
    place(w, 1, w.layout().ladder(0, 1.8f));
    w.focus(0);
    w.moveCrowd(.01);
    w.grabLadder(0);
    for (int i = 0; i < 100; ++i)
      w.climb(0, 1, .02f);
    require(w.position(0).y <= .11f, "Climbed through another resident");
  });
  test("Gravity supports the large floor and a shaft fall still causes damage", [] {
    Config c;
    c.population = 20;
    c.floors = 10;
    auto w = World::generate(c);
    Body b{{200, 3, 180}, {}, true};
    w.moveBody(0, b, {}, .05f);
    require(b.grounded && b.position.y == 3, "Large floor had no collision support");
    b = {{0, 15, 0}, {}, false};
    float impact = 0;
    for (int i = 0; i < 600; ++i)
      impact = std::max(impact, w.moveBody(0, b, {}, .01f));
    require(b.grounded && b.position.y == 0 && fallDamage(impact) > 50,
            "Shaft fall has no consequence");
  });
  test("Expansion requires authority, funds and actual construction attendance", [] {
    Config c;
    c.population = 40;
    c.floors = 10;
    c.mode = Mode::Manager;
    auto w = World::generate(c);
    while (!w.events.empty())
      w.events.pop();
    w.residents[0].role = Role::Builder;
    w.residents[0].jobSite = w.nearest(0, FacilityKind::Workshop);
    w.residents[0].routine.shiftStart = 0;
    w.residents[0].routine.shiftHours = 24;
    Money before = w.accounts[w.dormAccount].balance, quote = w.expansionQuote(0, 656, 640);
    rejects([&] { w.commissionExpansion(1, 0, 656, 640); });
    Id id = w.commissionExpansion(0, 0, 656, 640);
    require(w.accounts[w.dormAccount].balance == before - quote, "Contract not funded");
    require(w.floorPlans[0].width == 640, "Expansion appeared before construction");
    rejects([&] { w.commissionExpansion(0, 0, 672, 640); });
    w.assignExpansion(0, id);
    w.workExpansion(id, 3600);
    require(w.expansions[id].remaining == w.expansions[id].totalWork,
            "Remote worker built the floor");
    place(w, 0, w.expansions[id].site);
    w.beginExpansionWork(0, id);
    w.time += 10;
    w.workExpansion(id, 3600);
    require(w.expansions[id].totalWork - w.expansions[id].remaining == 10,
            "Unworked hour was credited");
    for (int n = 0; n < 8 && !w.expansions[id].complete; ++n) {
      w.time += 3600;
      w.workExpansion(id, 3600);
    }
    require(w.expansions[id].complete && w.floorPlans[0].width == 656,
            "Construction never expanded floor");
    require(w.accounts[w.expansions[id].escrow].balance == 0, "Escrow not settled");
    conserved(w);
  });
  test("Climbing state, custom floor sizes and funded expansions persist", [] {
    Config c;
    c.population = 20;
    c.floors = 10;
    c.mode = Mode::Manager;
    auto w = World::generate(c);
    w.commissionExpansion(0, 1, 528, 512);
    place(w, 0, w.layout().landing(0, 0));
    w.grabLadder(0);
    for (int i = 0; i < 50; ++i)
      w.climb(0, 1, .02f);
    auto file = std::filesystem::temp_directory_path() / "mega-expanded-floor-test.mdsave";
    w.save(file);
    auto b = World::load(file);
    require(b.config.floorWidth == 512 && b.floorPlans[0].width == 640 &&
                b.expansions.size() == 1 && b.residents[0].ladder == 0,
            "New state was lost");
    require(length(b.position(0) - w.position(0)) < .001f, "Loaded climber moved");
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
  });
  test("New construction reserves a clear plot and gives the payer ownership", [] {
    Config c;
    c.population = 40;
    c.floors = 10;
    c.mode = Mode::Manager;
    auto w = World::generate(c);
    Id p = w.commissionFacility(0, FacilityKind::Toilet, 1, {54, 3, 0}, 2);
    auto &project = w.projects[p];
    auto &f = w.facilities[project.facility];
    require(!f.open && f.owner == w.dormAccount && f.manager == 0 && f.stations == 2,
            "Construction opened early or ownership went to contractor");
    require(w.accounts[project.escrow].balance == project.budget, "Construction not escrowed");
    rejects([&] { w.commissionFacility(0, FacilityKind::Water, 1, {54, 3, 0}, 1); });
    rejects([&] { w.commissionFacility(0, FacilityKind::Toilet, 1, w.layout(1).bed(1, 0), 1); });
    w.validate();
    conserved(w);
  });
  test("Builders must collect and carry materials before building a usable facility", [] {
    Config c;
    c.population = 40;
    c.floors = 10;
    c.mode = Mode::Manager;
    auto w = World::generate(c);
    while (!w.events.empty())
      w.events.pop();
    auto &r = w.residents[2];
    r.role = Role::Builder;
    r.jobSite = w.nearest(2, FacilityKind::Workshop);
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    r.routine.daysOff = 0;
    Id id = w.commissionFacility(0, FacilityKind::Toilet, 1, {54, 3, 0}, 1);
    w.assignBuilder(id, 2);
    rejects([&] { w.deliverMaterials(2, id); });
    w.workProject(id, 3600);
    require(!w.facilities[w.projects[id].facility].open, "Facility built without materials");
    auto source = w.projects[id].source;
    auto stock = w.facilities[source].stock;
    place(w, 2, w.facilities[source].pos + Vec3{0, 0, 1.6f});
    w.collectMaterials(2, id);
    auto item = w.projects[id].materialItem;
    require(r.carrying == item && w.items[item].carrier == 2 &&
                w.facilities[source].stock == stock - w.projects[id].materials,
            "Materials not physically collected");
    require(!w.projects[id].materialsDelivered, "Pickup remotely delivered materials");
    place(w, 2, w.facilities[w.projects[id].facility].pos + Vec3{0, 0, 1.6f});
    w.deliverMaterials(2, id);
    require(r.carrying == none && w.items[item].carrier == none,
            "Material crate remained on worker");
    for (int n = 0; n < 9 && w.projects[id].state != ProjectState::Completed; ++n) {
      w.time += 3600;
      w.workProject(id, 3600);
    }
    Id facility = w.projects[id].facility;
    require(w.projects[id].state == ProjectState::Completed && w.facilities[facility].open &&
                w.items[item].consumed,
            "Finished facility unavailable");
    require(w.facilities[facility].owner == w.dormAccount &&
                w.accounts[w.projects[id].escrow].balance == 0,
            "Contractor took facility or escrow leaked");
    place(w, 1, w.facilities[facility].pos + Vec3{0, 0, 1.6f});
    w.joinQueue(1, facility);
    require(w.residents[1].queuedAt == facility, "Completed facility cannot accept a customer");
    conserved(w);
  });
  test("A dead builder drops recoverable materials without a second purchase", [] {
    Config c;
    c.population = 40;
    c.floors = 10;
    c.mode = Mode::Manager;
    auto w = World::generate(c);
    Id p = w.commissionFacility(0, FacilityKind::Water, 2, {54, 6, 0}, 1);
    for (Id id : {2, 3}) {
      w.residents[id].role = Role::Builder;
      w.residents[id].jobSite = w.nearest(id, FacilityKind::Workshop);
    }
    w.assignBuilder(p, 2);
    place(w, 2, w.facilities[w.projects[p].source].pos + Vec3{0, 0, 1.6f});
    w.collectMaterials(2, p);
    auto item = w.projects[p].materialItem;
    auto funds = w.accounts[w.projects[p].escrow].balance;
    w.kill(2);
    require(w.projects[p].state == ProjectState::Funded && w.items[item].carrier == none,
            "Death lost the site or crate");
    w.assignBuilder(p, 3);
    place(w, 3, w.items[item].pos);
    w.collectMaterials(3, p);
    require(w.residents[3].carrying == item && w.accounts[w.projects[p].escrow].balance == funds,
            "Recovery duplicated or repurchased materials");
    w.validate();
    conserved(w);
  });
  test("A material delivery route and its construction contract survive saving", [] {
    Config c;
    c.population = 40;
    c.floors = 10;
    c.mode = Mode::Manager;
    auto w = World::generate(c);
    auto &r = w.residents[2];
    r.role = Role::Builder;
    r.jobSite = w.nearest(2, FacilityKind::Workshop);
    Id p = w.commissionFacility(0, FacilityKind::Water, 2, {54, 6, 0}, 1);
    w.assignBuilder(p, 2);
    place(w, 2, w.facilities[w.projects[p].source].pos + Vec3{0, 0, 1.6f});
    w.collectMaterials(2, p);
    auto file = std::filesystem::temp_directory_path() / "mega-materials-save.mdsave";
    w.save(file);
    auto b = World::load(file);
    require(b.projects[p].newConstruction && b.residents[2].task == TaskKind::ConstructionDrop &&
                b.residents[2].carrying == b.projects[p].materialItem,
            "Construction delivery lost");
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
  });
  test("Medical subscriptions transfer real money and require a device or a clinic visit", [] {
    auto w = small();
    Id clinic = w.nearest(0, FacilityKind::Infirmary);
    w.pay(0, w.residents[0].account, 100000, "test-capital");
    Money before = w.accounts[w.facilities[clinic].owner].balance;
    w.subscribeMedical(0, clinic, 0);
    require(w.residents[0].medicalUntil == w.time + 30 * 86400, "Coverage duration wrong");
    require(w.accounts[w.facilities[clinic].owner].balance == before + 40000, "Insurer not paid");
    w.subscribeMedical(0, clinic, 0);
    require(w.residents[0].medicalUntil == w.time + 60 * 86400, "Renewal lost coverage");
    for (Id item : w.residents[0].items)
      w.items[item].consumed = true;
    place(w, 0, {15, 21, 12});
    rejects([&] { w.callAmbulance(0); });
    conserved(w);
  });
  test("Ambulance needs an on-site pilot and medic, then physically transports the patient", [] {
    auto w = small();
    Id clinic = w.nearest(0, FacilityKind::Infirmary);
    auto &a = w.ambulances[0];
    w.subscribeMedical(0, clinic, 0);
    place(w, 0, w.accessible({15, 9, 12}));
    for (auto &r : w.residents) {
      r.role = Role::Unemployed;
      r.routine.workSince = -1;
    }
    rejects([&] { w.callAmbulance(0); });
    for (Id who : {2, 3}) {
      w.chooseJob(who, who == 2 ? Role::Medic : Role::Pilot, clinic);
      w.residents[who].routine.shiftStart = 0;
      w.residents[who].routine.shiftHours = 24;
      place(w, who, w.facilities[clinic].pos + Vec3{0, 0, -1.6f});
      w.clockIn(who);
    }
    w.callAmbulance(0);
    Vec3 original = w.position(0);
    bool flew = false, carried = false;
    require(a.state == RescueState::Dispatch && a.readyAt == w.time + 600, "Wrong dispatch tier");
    for (int n = 0; n < 1200 && a.state != RescueState::Idle; ++n) {
      Vec3 before = w.ambulancePosition(a.id);
      w.advance(30);
      w.medicalTick();
      require(length(w.ambulancePosition(a.id) - before) < 10, "Aircraft teleported");
      if (a.state == RescueState::FlyingOut) {
        flew = true;
        require(length(w.position(0) - original) < .01f, "Patient moved before medic arrived");
      }
      if (w.residents[0].carriedBy != none) {
        carried = true;
        require(length(w.position(0) - w.position(2)) < 1.2f, "Patient lost physical stretcher");
      }
    }
    require(flew && carried && a.state == RescueState::Idle, "Medical journey never finished");
    require(w.near(0, w.facilities[clinic].pos, 5) && w.residents[0].ambulance == none &&
                w.residents[0].carriedBy == none,
            "Patient not delivered to clinic");
    require(w.residents[2].medicalDuty == none && w.residents[3].medicalDuty == none,
            "Crew never released");
    w.validate();
    conserved(w);
  });
  test("A death during evacuation returns the aircraft and preserves the remains", [] {
    auto w = small();
    Id clinic = w.nearest(0, FacilityKind::Infirmary);
    auto &a = w.ambulances[0];
    for (auto &r : w.residents) {
      r.role = Role::Unemployed;
      r.routine.workSince = -1;
    }
    for (Id who : {2, 3}) {
      w.chooseJob(who, who == 2 ? Role::Medic : Role::Pilot, clinic);
      w.residents[who].routine.shiftStart = 0;
      w.residents[who].routine.shiftHours = 24;
      place(w, who, w.facilities[clinic].pos + Vec3{0, 0, -1.6f});
    }
    w.subscribeMedical(0, clinic, 0);
    place(w, 0, w.accessible({15, 9, 12}));
    w.callAmbulance(0);
    for (int n = 0; n < 1200 && a.state != RescueState::FlyingHome; ++n) {
      w.advance(30);
      w.medicalTick();
    }
    require(a.state == RescueState::FlyingHome, "Never boarded patient");
    w.kill(0);
    require(w.remains.size() == 1 && w.remains[0].ambulance == a.id, "Passenger body vanished");
    Vec3 before = w.ambulancePosition(a.id);
    w.medicalTick();
    require(length(before - w.ambulancePosition(a.id)) < .01f, "Abort teleported aircraft");
    for (int n = 0; n < 1200 && a.state != RescueState::Idle; ++n) {
      w.advance(30);
      w.medicalTick();
    }
    require(a.state == RescueState::Idle && w.remains[0].ambulance == none && !w.residents[0].alive,
            "Abort stranded aircraft or resurrected patient");
    w.validate();
  });
  test("A cleaner physically takes remains through a morgue and staffed cremation", [] {
    auto w = small();
    Id morgue = w.nearest(2, FacilityKind::Morgue), oven = w.nearest(2, FacilityKind::Crematorium);
    w.facilities[morgue].staffNeeded = 1;
    w.facilities[morgue].staffRole = Role::Cleaner;
    w.chooseJob(2, Role::Cleaner, morgue);
    w.residents[2].routine.shiftStart = 0;
    w.residents[2].routine.shiftHours = 24;
    place(w, 3, {15, 3, 12});
    w.kill(3);
    w.transportBody(2, 0);
    rejects([&] { w.pickupBody(2, 0); });
    place(w, 2, w.remains[0].pos);
    w.pickupBody(2, 0);
    require(w.residents[2].bodyCarrying == 0 && w.remains[0].destination == morgue,
            "No physical body reservation");
    place(w, 2, w.facilities[morgue].pos + Vec3{0, 0, 1.6f});
    w.storeBody(2, morgue);
    require(w.remains[0].storage == morgue && !w.remains[0].cremated,
            "Morgue prematurely burned body");
    w.transportBody(2, 0);
    place(w, 2, w.remains[0].pos);
    w.pickupBody(2, 0);
    place(w, 2, w.facilities[oven].pos + Vec3{0, 0, 1.6f});
    w.storeBody(2, oven);
    w.facilities[oven].staffNeeded = 1;
    w.facilities[oven].staffRole = Role::Cleaner;
    w.medicalTick();
    w.time += 3600;
    w.medicalTick();
    require(!w.remains[0].cremated, "Unstaffed cremation ran");
    w.chooseJob(2, Role::Cleaner, oven);
    w.residents[2].routine.shiftStart = 0;
    w.residents[2].routine.shiftHours = 24;
    w.clockIn(2);
    w.medicalTick();
    w.time += 1800;
    w.medicalTick();
    require(w.remains[0].cremated, "Staffed cremation did not complete");
    conserved(w);
    w.validate();
  });
  test("Medical plans, body reservations and aircraft routes persist without moving", [] {
    auto w = small();
    Id clinic = w.nearest(0, FacilityKind::Infirmary);
    w.subscribeMedical(0, clinic, 0);
    w.kill(4);
    auto &a = w.ambulances[0];
    a.route = {{a.pos, w.time}, {a.pos + Vec3{10, 4, 0}, w.time + 300}};
    w.residents[0].infectedAt = w.time;
    auto file = std::filesystem::temp_directory_path() / "mega-medical-save.mdsave";
    w.save(file);
    auto b = World::load(file);
    require(b.remains.size() == 1 && b.residents[0].medicalTier == 0 &&
                b.residents[0].infectedAt == w.time && b.ambulances[0].route.size() == 2,
            "Medical state lost");
    require(length(b.ambulancePosition(0) - w.ambulancePosition(0)) < .001f,
            "Saved aircraft moved");
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
  });
  test("Food consumption uses actual inventory and player death ends survival", [] {
    auto w = small();
    Item food;
    food.id = Id(w.items.size());
    food.owner = w.residents[0].account;
    food.carrier = 0;
    food.kind = ItemKind::Food;
    w.items.push_back(food);
    w.residents[0].items.push_back(food.id);
    w.residents[0].hunger = .9f;
    w.consumeItem(0, food.id);
    require(w.residents[0].hunger < .31f && w.items[food.id].consumed, "Food had no effect");
    rejects([&] { w.consumeItem(0, food.id); });
    w.config.mode = Mode::Survival;
    w.kill(0);
    require(w.gameOver && w.endReason == "player-died", "Player death did not end survival");
  });
  test("Business restocking spends external money and waits for a physical freight delivery", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Food);
    w.facilities[f].manager = 0;
    w.facilities[f].stock = 12;
    w.pay(0, w.facilities[f].owner, 1000000, "test-capital");
    Money external = w.accounts[0].balance;
    Id s = w.requestStock(0, f, 1000);
    require(w.facilities[f].stock == 12 && w.accounts[0].balance == external + w.freight[s].cost,
            "Supply import was free or instantly stocked");
    rejects([&] { w.requestStock(0, f, 1000); });
    Id shop = w.nearest(2, FacilityKind::Shop);
    w.chooseJob(2, Role::Courier, shop);
    w.residents[2].routine.shiftStart = 0;
    w.residents[2].routine.shiftHours = 24;
    w.assignFreight(2, s);
    rejects([&] { w.collectFreight(2, s); });
    place(w, 2, w.items[w.freight[s].item].pos);
    w.collectFreight(2, s);
    require(w.residents[2].carrying == w.freight[s].item && w.facilities[f].stock == 12,
            "Stock arrived at pickup");
    place(w, 2, w.facilities[f].pos + Vec3{0, 0, 1.6f});
    Money before = w.accounts[w.residents[2].account].balance;
    w.deliverFreight(2, s);
    require(w.facilities[f].stock == 1012 && w.freight[s].state == FreightState::Delivered,
            "Physical freight did not replenish stock");
    require(w.accounts[w.residents[2].account].balance == before + w.freight[s].fee,
            "Courier was not paid");
    w.validate();
    conserved(w);
  });
  test("A deceased courier leaves freight recoverable without another import charge", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Food);
    w.facilities[f].manager = 0;
    Id s = w.requestStock(0, f, 100);
    for (Id id : {2, 3}) {
      w.chooseJob(id, Role::Courier, w.nearest(id, FacilityKind::Shop));
      w.residents[id].routine.shiftStart = 0;
      w.residents[id].routine.shiftHours = 24;
    }
    w.assignFreight(2, s);
    place(w, 2, w.items[w.freight[s].item].pos);
    w.collectFreight(2, s);
    w.kill(2);
    require(w.freight[s].worker == none && w.freight[s].state == FreightState::Waiting,
            "Freight claim not released");
    Money external = w.accounts[0].balance;
    w.assignFreight(3, s);
    place(w, 3, w.items[w.freight[s].item].pos);
    w.collectFreight(3, s);
    require(w.accounts[0].balance == external, "Recovering cargo repurchased it");
    w.validate();
  });
  test("Daily rent preserves leases, records arrears and never creates money", [] {
    auto w = small();
    Id unit = w.residentHomes[0];
    auto &h = w.housing[unit];
    h.paidUntil = w.time + 86400;
    Money before = w.accounts[h.owner].balance;
    w.time += 86400;
    w.settleRent(0);
    require(w.accounts[h.owner].balance == before + h.rent && h.arrears == 0, "Due rent not paid");
    w.pay(w.residents[0].account, 0, w.accounts[w.residents[0].account].balance, "test-spend");
    w.time += 86400;
    w.settleRent(0);
    require(h.arrears == h.rent && h.tenant == 0, "Missed rent silently erased a tenancy");
    w.pay(0, w.residents[0].account, h.rent, "test-income");
    w.settleRent(0);
    require(h.arrears == 0, "Arrears did not settle");
    conserved(w);
  });
  test("Immigrants bring tracked capital, rent an actual vacant bed and retain localized identity",
       [] {
         auto w = small();
         Id unit = none;
         for (Id i = 0; i < Id(w.housing.size()); ++i)
           if (w.housing[i].tenant == none) {
             unit = i;
             break;
           }
         auto before = w.internalMoney();
         Id id = w.immigrate(unit, 80000);
         require(w.internalMoney() == before + 80000 && w.residentHomes[id] == unit &&
                     w.housing[unit].tenant == id,
                 "Migration money or tenancy is wrong");
         require(w.near(id, w.layout(0).entrance(), .1f) && !w.names[id].zh.empty() &&
                     !w.names[id].fr.empty(),
                 "Arrival location or names missing");
         rejects([&] { w.immigrate(unit, 80000); });
         w.validate();
         conserved(w);
       });
  test("Hiring funds real local workers without moving them and service choice avoids dead "
       "businesses",
       [] {
         auto w = small();
         Id f = w.nearest(0, FacilityKind::Food);
         w.facilities[f].manager = 0;
         w.facilities[f].staffRole = Role::Chef;
         w.facilities[f].staffNeeded = 1;
         auto &r = w.residents[3];
         r.role = Role::Unemployed;
         r.automatic = true;
         place(w, 3, {15, 9, 12});
         Vec3 before = w.position(3);
         w.setBusiness(0, f, 450, 1600, true, true);
         w.hireEmployee(0, f, 3, Role::Chef, 6);
         require(r.role == Role::Chef && r.hourlyWage == 1600 &&
                     length(w.position(3) - before) < .001f,
                 "Hiring teleported worker or ignored contract");
         for (auto &x : w.facilities)
           if (x.kind == FacilityKind::Water)
             x.stock = 0;
         require(w.serviceChoice(0, FacilityKind::Water) == none,
                 "Resident chose an empty service");
         rejects([&] { w.setBusiness(4, f, 10, 1000, false, false); });
         w.validate();
       });
  test("Freight, rent and business settings survive a save", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Food);
    w.facilities[f].manager = 0;
    w.requestStock(0, f, 100);
    w.setBusiness(0, f, 550, 1700, false, true);
    w.residents[5].leaving = true;
    w.housing[w.residentHomes[0]].arrears = 500;
    auto file = std::filesystem::temp_directory_path() / "mega-business-save.mdsave";
    w.save(file);
    auto b = World::load(file);
    require(b.freight.size() == 1 && !b.facilities[f].hiring && b.facilities[f].wageOffer == 1700 &&
                b.residents[5].leaving && b.housing[b.residentHomes[0]].arrears == 500,
            "Operational save data missing");
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
  });
  test("A starving and thirsty resident finishes water instead of restarting its queue", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Water);
    auto &r = w.residents[0];
    r.automatic = true;
    r.hunger = 1.4f;
    r.thirst = 1.4f;
    r.bladder = .2f;
    place(w, 0, w.facilities[f].pos + Vec3{0, 0, 1.6f});
    w.joinQueue(0, f);
    w.decide(0);
    require(r.queuedAt == f && r.activity == Activity::Using,
            "Competing needs interrupted essential service");
  });
  test("An on-duty chef buys a staff meal without deadlocking the public queue", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Food);
    auto &r = w.residents[2];
    r.automatic = true;
    w.facilities[f].staffNeeded = 1;
    w.chooseJob(2, Role::Chef, f);
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    r.hunger = 1.1f;
    r.thirst = .2f;
    r.bladder = .2f;
    place(w, 2, w.facilities[f].pos + Vec3{0, 0, -1.6f});
    w.clockIn(2);
    int stock = w.facilities[f].stock;
    w.decide(2);
    require(r.hunger < .6f && r.activity == Activity::Working && r.queuedAt == none &&
                w.facilities[f].stock == stock - 1,
            "Chef abandoned the kitchen to queue for own food");
    conserved(w);
  });
  test("Default scarcity remains survivable over three days while retaining substantial queues",
       [] {
         Config c;
         c.floors = 20;
         c.population = 600;
         c.mode = Mode::Cheat;
         auto w = World::generate(c);
         w.residents[0].automatic = true;
         double end = w.time + 3 * 86400;
         std::size_t peakQueue = 0;
         while (w.time < end && !w.gameOver) {
           std::size_t queued = 0;
           for (auto &f : w.facilities)
             queued += f.queue.size();
           peakQueue = std::max(peakQueue, queued);
           w.advance(std::min(3600., end - w.time));
         }
         require(w.living() >= 590 && !w.gameOver && peakQueue > 20,
                 "The initial service economy collapsed or lost scarcity");
         w.validate();
         conserved(w);
       });
  test("Lift travel includes a paid boarding walk, smooth cabin movement and exit at a real stop",
       [] {
         Config c;
         c.population = 40;
         c.floors = 30;
         c.mode = Mode::Cheat;
         auto w = World::generate(c);
         while (!w.events.empty())
           w.events.pop();
         for (auto &r : w.residents) {
           r.automatic = false;
           r.route.clear();
         }
         auto &car = w.lifts[0];
         Id f = car.facility;
         place(w, 0, w.liftDoor(0));
         Money before = w.accounts[w.residents[0].account].balance;
         w.requestLift(0, car.id, 20);
         require(w.residents[0].lift == none, "Call teleported passenger into cabin");
         bool riding = false;
         Vec3 previous = w.position(0);
         for (int n = 0; n < 600; ++n) {
           w.advance(12);
           w.liftTick();
           Vec3 now = w.position(0);
           require(length(now - previous) < 3.f, "Lift boarding or motion teleported resident");
           previous = now;
           riding |= w.residents[0].lift != none;
           if (riding && w.residents[0].lift == none && !w.liftJourneys.contains(0))
             break;
         }
         require(riding && w.residents[0].lift == none && w.near(0, w.liftDoor(20), .1f),
                 "Passenger never reached destination");
         require(w.accounts[w.residents[0].account].balance == before - w.facilities[f].price,
                 "Fare missing or charged twice");
         w.validate();
         conserved(w);
       });
  test("Lift capacity and free access govern real boarding", [] {
    Config c;
    c.population = 40;
    c.floors = 30;
    c.mode = Mode::Cheat;
    auto w = World::generate(c);
    while (!w.events.empty())
      w.events.pop();
    for (auto &r : w.residents) {
      r.automatic = false;
      r.route.clear();
    }
    auto &car = w.lifts[0];
    auto &f = w.facilities[car.facility];
    f.stations = 1;
    f.whitelist.insert(0);
    place(w, 0, w.liftDoor(0));
    place(w, 1, w.liftDoor(0) + Vec3{0, 0, .8f});
    Money before = w.accounts[w.residents[0].account].balance;
    w.requestLift(0, 0, 10);
    w.requestLift(1, 0, 20);
    bool first = false, second = false;
    for (int n = 0; n < 1200; ++n) {
      w.advance(12);
      w.liftTick();
      require(car.passengers.size() <= 1, "Cabin over capacity");
      first |= w.residents[0].lift != none;
      second |= w.residents[1].lift != none;
      if (first && second && w.residents[1].lift == none)
        break;
    }
    require(first && second && w.near(1, w.liftDoor(20), .1f), "Waiting passenger stranded");
    require(w.accounts[w.residents[0].account].balance == before, "Whitelisted passenger charged");
    w.validate();
  });
  test("A lift journey resumes its original job route and survives a save while in transit", [] {
    Config c;
    c.population = 40;
    c.floors = 30;
    c.mode = Mode::Cheat;
    auto w = World::generate(c);
    while (!w.events.empty())
      w.events.pop();
    for (auto &r : w.residents) {
      r.automatic = false;
      r.route.clear();
    }
    place(w, 0, w.liftDoor(0));
    Vec3 goal{50, 60, 20};
    w.travel(0, goal, TaskKind::None, none);
    require(w.liftJourneys.contains(0), "Long route ignored lift");
    for (int n = 0; n < 200 && !w.lifts[0].moving; ++n) {
      w.advance(12);
      w.liftTick();
    }
    require(w.lifts[0].moving && w.residents[0].lift == 0, "Journey never boarded");
    auto file = std::filesystem::temp_directory_path() / "mega-lift-save.mdsave";
    w.save(file);
    auto loaded = World::load(file);
    require(length(w.position(0) - loaded.position(0)) < .001f && loaded.liftJourneys.contains(0),
            "Save lost cabin position or destination");
    for (int n = 0; n < 1600; ++n) {
      loaded.advance(12);
      loaded.liftTick();
      if (loaded.near(0, goal, .05f) && loaded.residents[0].route.empty())
        break;
    }
    require(loaded.near(0, goal, .05f) && loaded.residents[0].lift == none,
            "Passenger failed to resume walking");
    loaded.validate();
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
  });
  test("Programming contracts execute data operations against multiple real test cases", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    w.chooseJob(0, Role::Programmer, f);
    auto &r = w.residents[0];
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    place(w, 0, w.facilities[f].pos + Vec3{0, 0, -1.6f});
    Id id = w.officeOrder(0);
    auto &o = w.officeOrders[id];
    o.brief = 0;
    require(w.scoreOffice(id, {0, 2, 3}) == 100 && w.scoreOffice(id, {3, 2}) < 100,
            "Program acceptance tests were cosmetic");
    o.brief = 1;
    require(w.scoreOffice(id, {5, 4}) == 100 && w.scoreOffice(id, {4}) < 100,
            "Duplicate handling not tested");
    o.brief = 2;
    require(w.scoreOffice(id, {0, 6}) == 100 && w.scoreOffice(id, {6}) < 100,
            "Counting ignored input values");
    require(w.runProgram({-2, 1, 3, 0, 5}, {0, 2, 3}) == std::vector<int>{18},
            "Execution returned wrong result");
    rejects([&] { w.runProgram({1}, {999}); });
    w.validate();
    conserved(w);
  });
  test("Office revenue requires attendance and quality, with an explicit external customer", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    w.chooseJob(0, Role::Programmer, f);
    auto &r = w.residents[0];
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    place(w, 0, w.facilities[f].pos + Vec3{0, 0, -1.6f});
    w.clockIn(0);
    Id id = w.officeOrder(0);
    w.officeOrders[id].brief = 0;
    rejects([&] { w.submitOffice(id, {0, 2, 3}); });
    w.officeTick();
    for (int n = 0; n < 11; ++n) {
      w.time += 60;
      w.officeTick();
    }
    Money company = w.accounts[w.facilities[f].owner].balance,
          worker = w.accounts[r.account].balance;
    w.submitOffice(id, {0, 2, 3});
    require(w.officeOrders[id].submitted && w.officeOrders[id].quality == 100,
            "Valid project did not settle");
    require(w.accounts[w.facilities[f].owner].balance == company + 19200 &&
                w.accounts[r.account].balance == worker + 4800,
            "Contract revenue/bonus not split correctly");
    rejects([&] { w.submitOffice(id, {0, 2, 3}); });
    w.validate();
    conserved(w);
  });
  test("Abandoned live administration work refunds the dormitory client", [] {
    auto w = small();
    prepareAdmin(w);
    Id kitchen = w.nearest(0, FacilityKind::Food);
    w.facilities[kitchen].stock = 0;
    Id id = w.officeOrder(0);
    auto answer = adminAnswers(w.officeOrders[id]);
    require(w.scoreOffice(id, answer) == 100, "Live request routing did not affect quality");
    Money client = w.accounts[w.dormAccount].balance, budget = w.officeOrders[id].budget;
    w.kill(0);
    w.officeTick();
    require(w.officeOrders[id].submitted && w.officeOrders[id].quality == 0 &&
                w.accounts[w.dormAccount].balance == client + budget,
            "Abandoned administration fee was not refunded");
    conserved(w);
  });
  test("Administration creates funded physical supply and repair jobs without instant completion",
       [] {
         auto w = small();
         Id office = prepareAdmin(w);
         Id kitchen = w.nearest(0, FacilityKind::Food), water = w.nearest(0, FacilityKind::Water);
         w.facilities[kitchen].stock = 1;
         w.facilities[water].condition = .35f;
         Money dorm = w.accounts[w.dormAccount].balance;
         Id order = w.officeOrder(0);
         auto &o = w.officeOrders[order];
         require(o.liveRequests && o.client == w.dormAccount && o.budget == 12000 &&
                     w.accounts[w.dormAccount].balance == dorm - 12000,
                 "Administration was not internally funded");
         auto answer = adminAnswers(o);
         finishAttendance(w);
         w.submitOffice(order, answer);
         require(w.freight.size() == 1 && w.repairs.size() == 1,
                 "Correct routing created no real jobs");
         require(w.facilities[kitchen].stock == 1 && w.facilities[water].condition == .35f,
                 "Routing teleported stock or repaired a facility without workers");
         require(w.freight[0].state == FreightState::Waiting && !w.repairs[0].complete,
                 "Dispatch pretended to finish service");
         Money cash = w.internalMoney();
         rejects([&] { w.submitOffice(order, answer); });
         require(w.internalMoney() == cash && w.freight.size() == 1,
                 "Duplicate submission repeated effects");
         auto file = std::filesystem::temp_directory_path() / "mega-admin-live.mdsave";
         w.save(file);
         auto loaded = World::load(file);
         require(loaded.officeOrders[order].tickets == o.tickets &&
                     loaded.officeOrders[order].dispatched == o.dispatched,
                 "Dispatch links were not saved");
         loaded.validate();
         conserved(w);
         std::filesystem::remove(file);
         std::filesystem::remove(file.string() + ".bak");
         (void)office;
       });
  test("Administration cannot override withdrawn supply permission or an empty treasury", [] {
    auto w = small();
    prepareAdmin(w);
    Id kitchen = w.nearest(0, FacilityKind::Food);
    w.facilities[kitchen].stock = 0;
    Id order = w.officeOrder(0);
    auto answer = adminAnswers(w.officeOrders[order]);
    w.facilities[kitchen].autoRestock = false;
    finishAttendance(w);
    w.submitOffice(order, answer);
    bool blocked = false;
    for (int result : w.officeOrders[order].outcomes)
      blocked |= result == 3;
    require(blocked && w.freight.empty(), "Administrator bypassed owner authorization");
    auto other = small();
    prepareAdmin(other);
    other.facilities[kitchen].stock = 0;
    other.pay(other.dormAccount, 0, other.accounts[other.dormAccount].balance, "treasury-test");
    auto accounts = other.accounts.size();
    rejects([&] { other.officeOrder(0); });
    require(other.accounts.size() == accounts && other.officeOrders.empty(),
            "Unfunded commission created money or escrow");
    conserved(w);
    conserved(other);
  });
  test("Incorrect administration routing pays no bonus and cannot dispatch services", [] {
    auto w = small();
    prepareAdmin(w);
    Id kitchen = w.nearest(0, FacilityKind::Food);
    w.facilities[kitchen].stock = 0;
    Id order = w.officeOrder(0);
    auto answer = adminAnswers(w.officeOrders[order]);
    for (int &department : answer)
      department = (department + 1) % 4;
    finishAttendance(w);
    Money dorm = w.accounts[w.dormAccount].balance;
    w.submitOffice(order, answer);
    require(w.officeOrders[order].quality == 0 && w.freight.empty() &&
                w.accounts[w.dormAccount].balance == dorm + w.officeOrders[order].budget,
            "Misrouted request created services or retained unearned fees");
    w.validate();
    conserved(w);
  });
  test("Administration dispatches real wardens and subscribed medical crews", [] {
    auto w = small();
    prepareAdmin(w);
    Id jail = w.nearest(0, FacilityKind::Jail), clinic = w.nearest(0, FacilityKind::Infirmary);
    w.chooseJob(1, Role::Warden, jail);
    w.residents[1].automatic = true;
    w.residents[1].routine.shiftStart = 0;
    w.residents[1].routine.shiftHours = 24;
    place(w, 1, {15, 0, 10});
    place(w, 4, {12, 0, 10});
    place(w, 5, {10, 0, 10});
    Id incident = w.report(CrimeKind::Assault, 4, 5);
    for (Id id : {2, 3}) {
      w.chooseJob(id, id == 2 ? Role::Medic : Role::Pilot, clinic);
      w.residents[id].routine.shiftStart = 0;
      w.residents[id].routine.shiftHours = 24;
      place(w, id, w.facilities[clinic].pos + Vec3{0, 0, -1.6f});
      w.clockIn(id);
    }
    place(w, 6, w.facilities[clinic].pos + Vec3{0, 0, -1.6f});
    w.subscribeMedical(6, clinic, 0);
    w.residents[6].health = 30;
    place(w, 6, {15, 0, 12});
    // The generated fixture can lack a personal device; give the patient a real owned phone.
    Item phone;
    phone.id = Id(w.items.size());
    phone.kind = ItemKind::Phone;
    phone.owner = w.residents[6].account;
    phone.carrier = 6;
    w.items.push_back(phone);
    w.residents[6].items.push_back(phone.id);
    Id order = w.officeOrder(0);
    auto answer = adminAnswers(w.officeOrders[order]);
    finishAttendance(w);
    w.submitOffice(order, answer);
    require(w.incidents[incident].warden == 1 && w.residents[1].task == TaskKind::Investigate &&
                !w.residents[1].route.empty(),
            "Security routing did not send a real warden");
    bool dispatched = false;
    for (auto &a : w.ambulances)
      dispatched |=
          a.patient == 6 && a.state == RescueState::Dispatch && a.medic != none && a.pilot != none;
    require(dispatched && w.residents[6].health == 30,
            "Medical dispatch lacked crew or healed remotely");
    w.validate();
    conserved(w);
  });
  test("Office attendance pauses away from work and active contracts survive saves", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    w.chooseJob(0, Role::Programmer, f);
    auto &r = w.residents[0];
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    place(w, 0, w.facilities[f].pos + Vec3{0, 0, -1.6f});
    w.clockIn(0);
    Id id = w.officeOrder(0);
    w.officeTick();
    w.time += 60;
    w.officeTick();
    double left = w.officeOrders[id].remaining;
    place(w, 0, {15, 21, 12});
    w.time += 3600;
    w.officeTick();
    require(w.officeOrders[id].remaining == left, "Work completed remotely");
    auto file = std::filesystem::temp_directory_path() / "mega-office-save.mdsave";
    w.save(file);
    auto b = World::load(file);
    require(b.activeOfficeOrders.at(0) == id && b.officeOrders[id].remaining == left,
            "Saved task progress missing");
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
  });
  test("Firearms consume owned ammunition, need line of sight and harm the actual target", [] {
    auto w = small();
    for (auto &r : w.residents)
      place(w, r.id, {15, 24, 12});
    place(w, 0, {0, 0, 2});
    place(w, 1, {0, 0, -8});
    for (ItemKind kind : {ItemKind::Handgun, ItemKind::Ammunition}) {
      Item i;
      i.id = Id(w.items.size());
      i.kind = kind;
      i.owner = w.residents[0].account;
      i.carrier = 0;
      i.quantity = kind == ItemKind::Ammunition ? 2 : 1;
      w.items.push_back(i);
      w.residents[0].items.push_back(i.id);
    }
    Id ammo = w.residents[0].items.back();
    float before = w.residents[1].health;
    w.shoot(0, 1);
    require(w.residents[1].health < before && w.items[ammo].quantity == 1 &&
                w.shots.back().victim == 1,
            "Shot did not hit or consume ammunition");
    rejects([&] { w.shoot(0, 1); });
    w.time += 21;
    w.residents[1].surrendered = false;
    w.shoot(0, 1);
    require(!w.hasGun(0), "Empty ammunition remained usable");
    w.time += 21;
    rejects([&] { w.shoot(0, 1); });
    w.validate();
  });
  test("A bystander intercepts a shot and receives its crime consequences", [] {
    auto w = small();
    for (auto &r : w.residents)
      place(w, r.id, {15, 24, 12});
    place(w, 0, {0, 0, 4});
    place(w, 1, {0, 0, -8});
    place(w, 2, {0, 0, -2});
    for (ItemKind kind : {ItemKind::Handgun, ItemKind::Ammunition}) {
      Item i;
      i.id = Id(w.items.size());
      i.kind = kind;
      i.owner = w.residents[0].account;
      i.carrier = 0;
      w.items.push_back(i);
      w.residents[0].items.push_back(i.id);
    }
    w.shoot(0, 1);
    require(w.residents[1].health == 100 && w.residents[2].health < 100 &&
                w.shots.back().victim == 2,
            "Bullet passed through a bystander");
    require(w.incidents.back().victim == 2, "Crime was attributed to the wrong victim");
  });
  test("Bunk geometry blocks gunfire instead of allowing shots through furniture", [] {
    auto w = small();
    Vec3 bed = w.layout(1).bed(1, 0);
    require(!w.lineOfSight(bed + Vec3{-2, 1.3f, 0}, bed + Vec3{2, 1.3f, 0}),
            "Bunk did not block the shot");
    require(w.lineOfSight({0, 1.3f, 3}, {0, 1.3f, -3}), "Clear corridor blocked shots");
  });
  test("Stopping an active armed attacker differs from executing someone who surrendered", [] {
    auto w = small();
    place(w, 0, {0, 0, 1});
    place(w, 1, {0, 0, 2});
    w.residents[1].lethalThreat = true;
    w.residents[1].activity = Activity::Fighting;
    w.residents[1].opponent = 0;
    w.residents[1].health = 20;
    w.attack(0, 1, true);
    require(!w.residents[1].alive, "Armed threat survived lethal hit");
    require(std::none_of(
                w.incidents.begin(), w.incidents.end(),
                [](const Incident &i) { return i.offender == 0 && i.kind == CrimeKind::Murder; }),
            "Active deadly self-defense became murder");
    place(w, 2, {0, 0, 2});
    w.residents[2].lethalThreat = true;
    w.surrender(2);
    w.time += 21;
    w.attack(0, 2, true);
    require(w.incidents.back().kind == CrimeKind::Murder, "Execution was excused as self-defense");
  });
  test("Cleaner collects actual waste and earns pay only after physical disposal", [] {
    Config c;
    c.population = 60;
    c.floors = 20;
    c.mode = Mode::Cheat;
    auto w = World::generate(c);
    while (!w.events.empty())
      w.events.pop();
    Id source = w.nearest(0, FacilityKind::Food), depot = w.nearest(0, FacilityKind::Garbage);
    w.facilities[source].manager = 0;
    w.facilities[source].waste = 40;
    Id id = w.requestWaste(0, source);
    require(w.facilities[source].waste == 40, "Contract removed waste before pickup");
    w.chooseJob(0, Role::Cleaner, depot);
    w.residents[0].routine.shiftStart = 0;
    w.residents[0].routine.shiftHours = 24;
    w.assignFreight(0, id);
    rejects([&] { w.collectFreight(0, id); });
    place(w, 0, w.items[w.freight[id].item].pos);
    w.collectFreight(0, id);
    require(w.facilities[source].waste == 0 && w.residents[0].carrying == w.freight[id].item,
            "Cleaner did not carry waste");
    Id destination = w.freight[id].facility;
    Money before = w.accounts[w.residents[0].account].balance;
    place(w, 0, w.facilities[destination].pos + Vec3{0, 0, 1.6f});
    w.deliverFreight(0, id);
    require(w.facilities[destination].waste == 40 &&
                w.accounts[w.residents[0].account].balance == before + w.freight[id].fee,
            "Disposal or cleaner pay missing");
    w.validate();
    conserved(w);
  });
  test("A cleaner dying before body pickup does not teleport the uncollected remains", [] {
    auto w = small();
    Id morgue = w.nearest(2, FacilityKind::Morgue);
    w.facilities[morgue].staffNeeded = 1;
    w.chooseJob(2, Role::Cleaner, morgue);
    w.residents[2].routine.shiftStart = 0;
    w.residents[2].routine.shiftHours = 24;
    place(w, 3, {15, 3, 12});
    w.kill(3);
    Vec3 body = w.remains[0].pos;
    place(w, 2, {0, 0, 0});
    w.transportBody(2, 0);
    w.kill(2);
    require(length(w.remains[0].pos - body) < .001f && w.remains[0].worker == none,
            "Death relocated a body that had never been collected");
  });
  test("Dormitory purchase requires a listing and transfers control without consuming its treasury",
       [] {
         auto w = small();
         Id old = w.dormOwner;
         Money treasury = w.accounts[w.dormAccount].balance;
         rejects([&] { w.buyDorm(0); });
         rejects([&] { w.listDorm(0, 50000); });
         w.listDorm(old, 50000);
         auto cash = w.accounts[w.residents[old].account].balance;
         w.buyDorm(0);
         require(w.dormOwner == 0 && w.accounts[w.residents[old].account].balance == cash + 50000 &&
                     w.accounts[w.dormAccount].balance == treasury,
                 "Purchase did not preserve accounts");
         require(w.governance.askingPrice == 0, "Sale listing remained active");
         conserved(w);
       });
  test("Succession requires the nominated person's acceptance and survives saving", [] {
    auto w = small();
    w.nominateSuccessor(w.dormOwner, 0);
    require(w.dormOwner != 0, "Nomination immediately replaced the owner");
    rejects([&] { w.acceptSuccession(3); });
    auto file = std::filesystem::temp_directory_path() / "mega-governance-save.mdsave";
    w.save(file);
    auto b = World::load(file);
    b.acceptSuccession(0);
    require(b.dormOwner == 0 && !b.governance.retiring, "Saved invitation failed");
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
  });
  test("Automatic occupants negotiate compensation but the player retains their own consent", [] {
    auto w = small();
    w.dormOwner = 0;
    w.facilities[0].owner = w.dormAccount;
    for (auto &r : w.residents)
      r.automatic = true;
    Id p = w.proposeProject(0, 0, false, 100000);
    w.offerCompensation(0, p, 100000);
    for (Id id : w.projects[p].affected)
      require(w.projects[p].consented.contains(id) == (id != w.player),
              "NPC or player consent handled incorrectly");
    if (w.projects[p].affected.contains(0))
      w.consentProject(p, 0);
    w.fundProject(p, 0);
    require(w.projects[p].state == ProjectState::Funded, "Negotiated project not funded");
    conserved(w);
  });
  test("Violent takeover requires reception control and preserves criminal consequences", [] {
    Config c;
    c.floors = 10;
    c.population = 40;
    c.mode = Mode::Cheat;
    auto w = World::generate(c);
    Id reception = w.nearest(0, FacilityKind::Reception);
    rejects([&] { w.seizeDorm(0); });
    w.surrender(w.dormOwner);
    place(w, 0, w.facilities[reception].pos + Vec3{0, 0, 1.6f});
    for (auto &r : w.residents)
      if (r.role == Role::Warden)
        w.surrender(r.id);
    w.seizeDorm(0);
    require(w.dormOwner == 0 && w.incidents.back().offender == 0 && !w.incidents.back().resolved,
            "Force bypassed consequences");
    w.validate();
  });
  test("Repair contracts need actual collected parts and on-site shift attendance", [] {
    auto w = small();
    Id site = w.nearest(0, FacilityKind::Food), shop = w.nearest(0, FacilityKind::Workshop);
    w.facilities[site].manager = 0;
    w.facilities[site].condition = .05f;
    require(!w.operational(site), "Broken facility remained usable");
    Id job = w.requestRepair(0, site);
    w.chooseJob(2, Role::Repairer, shop);
    auto &r = w.residents[2];
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    w.assignRepair(2, job);
    w.time += 60;
    w.repairTick();
    require(!w.repairs[job].parts, "Remote parts collection");
    place(w, 2, w.facilities[shop].pos + Vec3{0, 0, 1.6f});
    w.repairTick();
    Id item = w.repairs[job].partItem;
    require(w.repairs[job].parts && r.carrying == item, "Parts not carried");
    w.time += 7200;
    w.repairTick();
    require(w.facilities[site].condition == .05f, "Remote repair completed");
    place(w, 2, w.facilities[site].pos + Vec3{0, 0, 1.6f});
    r.activity = Activity::Working;
    Money cash = w.accounts[r.account].balance;
    for (int n = 0; n < 62; ++n) {
      w.time += 60;
      w.repairTick();
    }
    require(w.repairs[job].complete && w.facilities[site].condition == 1 &&
                w.items[item].consumed && r.carrying == none,
            "Repair did not finish");
    require(w.accounts[r.account].balance == cash + 6000, "Completed repair did not pay worker");
    w.validate();
    conserved(w);
  });
  test("A replacement repairer recovers dropped parts without charging a second purchase", [] {
    auto w = small();
    Id site = w.nearest(0, FacilityKind::Food), shop = w.nearest(0, FacilityKind::Workshop);
    w.facilities[site].manager = 0;
    w.facilities[site].condition = .1f;
    Id job = w.requestRepair(0, site);
    for (Id id : {2, 3}) {
      w.chooseJob(id, Role::Repairer, shop);
      w.residents[id].routine.shiftStart = 0;
      w.residents[id].routine.shiftHours = 24;
    }
    w.assignRepair(2, job);
    place(w, 2, w.facilities[shop].pos + Vec3{0, 0, 1.6f});
    w.repairTick();
    Id item = w.repairs[job].partItem;
    Money balance = w.accounts[w.repairs[job].escrow].balance;
    w.kill(2);
    w.repairTick();
    w.assignRepair(3, job);
    place(w, 3, w.items[item].pos);
    w.repairTick();
    require(w.items[item].carrier == 3 && w.accounts[w.repairs[job].escrow].balance == balance,
            "Parts recovery duplicated purchase");
    auto file = std::filesystem::temp_directory_path() / "mega-repair-save.mdsave";
    w.save(file);
    auto b = World::load(file);
    require(b.repairs[job].partItem == item && b.residents[3].carrying == item,
            "Repair state was lost");
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
  });
  test("Every visible generated bed is a real rentable housing unit", [] {
    Config c;
    c.floors = 20;
    c.population = 60;
    auto w = World::generate(c);
    for (int f = 0; f < c.floors; ++f)
      if (w.layout(f).residential(f, c.floors))
        require(w.floorHomes[f].size() == std::size_t(c.bedsPerFloor),
                "Decorative beds were not registered as housing");
  });
  test("Bed construction requires physical materials and produces an owned collidable home", [] {
    Config c;
    c.floors = 10;
    c.population = 40;
    c.mode = Mode::Manager;
    auto w = World::generate(c);
    while (!w.events.empty())
      w.events.pop();
    Id job = w.commissionBed(0, 1, {60, 3, 10});
    auto &p = w.housingWorks[job];
    Id home = p.home;
    require(!w.housing[home].active, "Bed became rentable before construction");
    w.residents[2].role = Role::Builder;
    w.residents[2].jobSite = p.workshop;
    w.residents[2].routine.shiftStart = 0;
    w.residents[2].routine.shiftHours = 24;
    w.assignHousingWork(2, job);
    place(w, 2, w.facilities[p.workshop].pos + Vec3{0, 0, 1.6f});
    w.housingWorkTick();
    require(w.residents[2].carrying == p.material && p.material != none, "No material cargo");
    place(w, 2, p.site + Vec3{1.5f, 0, 0});
    w.residents[2].activity = Activity::Working;
    for (int n = 0; n < 123; ++n) {
      w.time += 60;
      w.housingWorkTick();
    }
    require(w.housing[home].active && w.housing[home].owner == w.dormAccount &&
                !w.clearPosition(p.site),
            "Completed home has no ownership/collider");
    require(length(w.homePosition(home) - Vec3{60, 3, 10}) < .01f, "Custom home position lost");
    w.rentHome(3, home);
    w.sleepAtHome(3);
    require(w.residents[3].route.size() > 1, "Cannot walk to constructed home");
    w.validate();
    conserved(w);
  });
  test("Occupied bed removal pays compensation and requires actual evacuation", [] {
    auto w = small();
    w.dormOwner = 0;
    Id home = w.residentHomes[3];
    Vec3 old = w.homePosition(home);
    place(w, 3, old);
    w.residents[3].automatic = false;
    Id job = w.proposeHomeRemoval(0, home, 30000);
    rejects([&] { w.fundHomeRemoval(0, job); });
    Money cash = w.accounts[w.residents[3].account].balance;
    w.consentHomeRemoval(3, job);
    w.fundHomeRemoval(0, job);
    require(w.residentHomes[3] != home &&
                w.accounts[w.residents[3].account].balance == cash + 30000,
            "No replacement lease or compensation");
    require(length(w.position(3) - old) < .01f && w.housing[home].active,
            "Relocation teleported tenant or instantly demolished bed");
    w.residents[2].role = Role::Builder;
    w.residents[2].jobSite = w.nearest(2, FacilityKind::Workshop);
    w.residents[2].routine.shiftStart = 0;
    w.residents[2].routine.shiftHours = 24;
    w.assignHousingWork(2, job);
    place(w, 2, old + Vec3{1.5f, 0, 0});
    w.residents[2].activity = Activity::Working;
    place(w, 3, {0, 0, 0});
    for (int n = 0; n < 63; ++n) {
      w.time += 60;
      w.housingWorkTick();
    }
    require(!w.housing[home].active, "Vacated bed was not removed");
    w.validate();
    conserved(w);
  });
  test("A new top floor appears only when its funded physical construction is complete", [] {
    Config c;
    c.floors = 10;
    c.population = 40;
    c.mode = Mode::Manager;
    auto w = World::generate(c);
    w.pay(0, w.dormAccount, 100000000, "test-capital");
    Id job = w.commissionFloor(0, 96, 72);
    auto &p = w.housingWorks[job];
    require(w.config.floors == 10, "Floor created before work");
    w.residents[2].role = Role::Builder;
    w.residents[2].jobSite = p.workshop;
    w.residents[2].routine.shiftStart = 0;
    w.residents[2].routine.shiftHours = 24;
    w.assignHousingWork(2, job);
    place(w, 2, w.facilities[p.workshop].pos + Vec3{0, 0, 1.6f});
    w.housingWorkTick();
    place(w, 2, p.site + Vec3{1.5f, 0, 0});
    w.residents[2].activity = Activity::Working;
    for (int n = 0; n < 210; ++n) {
      w.time += 60;
      w.housingWorkTick();
    }
    require(w.config.floors == 11 && w.floorPlans.back().width == 96 &&
                w.floorPlans.back().depth == 72,
            "Structural extension did not complete");
    while (!w.events.empty())
      w.events.pop();
    auto file = std::filesystem::temp_directory_path() / "mega-housing-work-save.mdsave";
    w.save(file);
    auto b = World::load(file);
    require(b.config.floors == 11 && b.housingWorks[job].state == ProjectState::Completed,
            "Extension lost on reload");
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
    conserved(w);
  });
  test("Conversation requires proximity and respects interests, cooldown and personal trust", [] {
    auto w = small();
    place(w, 0, {0, 0, 0});
    place(w, 1, {0, 0, 1});
    w.residents[1].interests = {0, 0, 0, 0, 0, 1, 0, 0};
    require(w.talk(0, 1, 1) == "talk.interest5", "Resident ignored their interests");
    require(w.trust(0, 1) > 0, "Shared conversation had no relationship effect");
    rejects([&] { w.talk(0, 1, 1); });
    w.time += 301;
    float before = w.trust(0, 1);
    w.talk(0, 1, 3);
    require(w.trust(0, 1) < before, "Insult did not affect relationship");
    place(w, 1, {0, 0, 12});
    w.time += 301;
    rejects([&] { w.talk(0, 1, 0); });
    w.validate();
  });
  test("Personal item resale requires a physical exchange and transfers ownership exactly once",
       [] {
         auto w = small();
         Id item = w.residents[1].items[0];
         require(w.items[item].kind == ItemKind::Phone, "Fixture phone missing");
         w.listItem(1, item, 12000);
         place(w, 0, {0, 0, 0});
         place(w, 1, {0, 0, 12});
         rejects([&] { w.buyItem(0, item); });
         Money buyer = w.accounts[w.residents[0].account].balance,
               seller = w.accounts[w.residents[1].account].balance;
         place(w, 1, {0, 0, 1});
         w.buyItem(0, item);
         require(w.items[item].owner == w.residents[0].account && w.items[item].carrier == 0 &&
                     w.items[item].askingPrice == 0,
                 "Handover did not transfer item");
         require(w.accounts[w.residents[0].account].balance == buyer - 12000 &&
                     w.accounts[w.residents[1].account].balance == seller + 12000,
                 "Resale money transfer wrong");
         rejects([&] { w.buyItem(0, item); });
         w.validate();
         conserved(w);
       });
  test("Worn clothing and stolen property cannot be listed as legitimate personal sales", [] {
    auto w = small();
    rejects([&] { w.listItem(0, w.residents[0].outfit, 1000); });
    Id item = w.residents[0].items[0];
    w.items[item].stolenFrom = w.residents[1].account;
    rejects([&] { w.listItem(0, item, 1000); });
  });
  test("Out-of-order service cancellation cannot assign two residents to the same station", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Water);
    w.facilities[f].stations = 4;
    for (Id id : {0, 1, 2, 3}) {
      place(w, id, w.facilities[f].pos + Vec3{0, 0, 1.6f});
      w.joinQueue(id, f);
    }
    w.leaveQueue(1);
    place(w, 4, w.facilities[f].pos + Vec3{0, 0, 1.6f});
    w.joinQueue(4, f);
    std::set<int> slots;
    for (auto &service : w.facilities[f].serving)
      require(slots.insert(service.station).second, "Two residents share a service station");
    require(w.facilities[f].serving.back().station == 1, "Vacated station was not reused");
    w.validate();
  });
  test("Hungry dehydrated cooks can return to their own kitchen before a toilet break", [] {
    auto w = small();
    Id kitchen = w.nearest(2, FacilityKind::Food);
    w.facilities[kitchen].staffNeeded = 1;
    w.chooseJob(2, Role::Chef, kitchen);
    auto &r = w.residents[2];
    r.automatic = true;
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    r.hunger = 1.2f;
    r.thirst = 1.2f;
    r.bladder = 1;
    r.health = 50;
    r.needsAt = w.time;
    place(w, 2, {0, 3, 14});
    w.decide(2);
    require(r.task == TaskKind::Work && r.target == kitchen,
            "Basic staff survival was displaced by an unrelated urgent need");
    place(w, 2, w.facilities[kitchen].pos + Vec3{0, 0, -1.6f});
    w.clockIn(2);
    w.decide(2);
    require(r.hunger < .8f && r.thirst < .8f, "Cook did not purchase on-site meal and water");
    conserved(w);
  });
  test("Routine service planning does not send a hungry resident hundreds of floors away", [] {
    Config c;
    c.floors = 200;
    c.population = 10000;
    c.mode = Mode::Cheat;
    auto w = World::generate(c);
    for (auto &f : w.facilities)
      if (f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe)
        f.open = false;
    Id far = none;
    for (auto &f : w.facilities)
      if (f.kind == FacilityKind::Food && f.floor == 190) {
        far = f.id;
        f.open = true;
        f.staffNeeded = 0;
      }
    require(far != none, "Missing test kitchen");
    place(w, 0, {10, 3, 0});
    require(w.serviceChoice(0, FacilityKind::Food) == none,
            "Resident planned an all-day meal commute");
  });
  test("People turn back from a service that closed while they were travelling", [] {
    auto w = small();
    Id kitchen = w.nearest(0, FacilityKind::Food);
    auto &r = w.residents[0];
    r.automatic = true;
    r.role = Role::Unemployed;
    r.health = 100;
    r.hunger = .7f;
    r.thirst = .2f;
    r.needsAt = w.time;
    place(w, 0, {0, 21, 14});
    w.travel(0, w.facilities[kitchen].pos, TaskKind::Service, kitchen);
    Vec3 before = w.position(0);
    w.facilities[kitchen].open = false;
    w.decide(0);
    require(!(r.task == TaskKind::Service && r.target == kitchen),
            "Resident still committed to a closed service");
    require(length(w.position(0) - before) < .01f, "Replanning teleported the resident");
  });
  test("Funded demolition clears reservations and stops new customers before work", [] {
    auto w = small();
    w.dormOwner = 0;
    Id f = w.nearest(0, FacilityKind::Water);
    w.facilities[f].owner = w.dormAccount;
    place(w, 2, w.facilities[f].pos + Vec3{0, 0, 1.6f});
    w.joinQueue(2, f);
    Id p = w.proposeProject(0, f, true, 100000);
    for (Id r : w.projects[p].affected)
      w.consentProject(p, r);
    w.fundProject(p, 0);
    require(!w.facilities[f].open && w.facilities[f].serving.empty() &&
                w.residents[2].queuedAt == none && !w.facilities[f].demolished,
            "Construction did not safely clear the live business");
    rejects([&] { w.joinQueue(2, f); });
    w.validate();
  });
  test("High-floor imports fly to a landing before a courier can collect them", [] {
    Config c;
    c.floors = 60;
    c.population = 600;
    c.mode = Mode::Cheat;
    auto w = World::generate(c);
    while (!w.events.empty())
      w.events.pop();
    Id target = none;
    for (auto &f : w.facilities)
      if (f.floor == 52 && f.kind == FacilityKind::Water)
        target = f.id;
    w.facilities[target].manager = 0;
    Id shipment = w.requestStock(0, target, 1000);
    auto &s = w.freight[shipment];
    int stock = w.facilities[target].stock;
    Id shop = w.nearest(2, FacilityKind::Shop);
    w.chooseJob(2, Role::Courier, shop);
    w.residents[2].routine.shiftStart = 0;
    w.residents[2].routine.shiftHours = 24;
    rejects([&] { w.assignFreight(2, shipment); });
    Vec3 start = w.freightPosition(shipment);
    w.time = (w.time + s.readyAt) / 2;
    w.freightTick();
    Vec3 halfway = w.items[s.item].pos;
    require(halfway.y > start.y && halfway.y < s.landing.y + 2,
            "Cargo did not move continuously through the air");
    require(w.facilities[target].stock == stock, "Aerial import teleported into business stock");
    w.time = s.readyAt;
    w.freightTick();
    require(length(w.items[s.item].pos - s.landing) < .01f, "Cargo did not land on its floor");
    w.assignFreight(2, shipment);
    place(w, 2, s.landing);
    w.collectFreight(2, shipment);
    require(w.residents[2].carrying == s.item, "Last-mile courier did not collect cargo");
    conserved(w);
  });
  test("A casualty stays with a moving lift until it reaches a real landing", [] {
    Config c;
    c.population = 40;
    c.floors = 30;
    c.mode = Mode::Cheat;
    auto w = World::generate(c);
    while (!w.events.empty())
      w.events.pop();
    for (auto &r : w.residents) {
      r.automatic = false;
      r.route.clear();
    }
    place(w, 0, w.liftDoor(0));
    w.requestLift(0, 0, 20);
    for (int n = 0; n < 600 && !w.lifts[0].moving; ++n) {
      w.advance(12);
      w.liftTick();
    }
    require(w.lifts[0].moving && w.residents[0].lift == 0, "No passenger aboard");
    w.advance(60);
    w.liftTick();
    w.kill(0);
    auto &body = w.remains.back();
    require(body.lift == 0 && w.residents[0].lift == none, "Body lost cabin association");
    float previous = body.pos.y;
    w.advance(24);
    w.liftTick();
    require(body.pos.y > previous && body.lift == 0, "Body floated behind the moving cabin");
    for (int n = 0; n < 800 && body.lift != none; ++n) {
      w.advance(12);
      w.liftTick();
    }
    require(body.lift == none && std::abs(body.pos.y - 60) < .01f,
            "Body not unloaded at the destination landing");
    w.validate();
  });
  test("Cheat overrides enforce mode, bounds, cash accounting and persistent state", [] {
    auto w = small();
    w.config.mode = Mode::Survival;
    rejects([&] { w.cheatResident(0, "health", 100); });
    w.config.mode = Mode::Cheat;
    rejects([&] { w.cheatResident(0, "hunger", -1); });
    rejects([&] { w.cheatResident(0, "health", 101); });
    rejects([&] { w.cheatResident(0, "unknown", 1); });
    auto money = w.internalMoney();
    auto original = w.accounts[w.residents[0].account].balance;
    w.cheatResident(0, "cash", 300000);
    require(w.internalMoney() == money + 300000 - original, "Cheat money bypassed ledger");
    w.cheatResident(0, "cash", 200000);
    w.cheatResident(0, "health", 42);
    w.cheatResident(0, "hunger", .1);
    w.cheatResident(0, "strength", 95);
    auto file = std::filesystem::temp_directory_path() / "mega-cheat-save.mdsave";
    w.save(file);
    auto loaded = World::load(file);
    require(loaded.residents[0].health == 42 && loaded.residents[0].strength == 95 &&
                loaded.accounts[loaded.residents[0].account].balance == 200000,
            "Overrides lost in save");
    conserved(loaded);
    std::filesystem::remove(file);
    std::filesystem::remove(file.string() + ".bak");
  });
  test("Autonomous conversations require actual proximity and cannot reach sleeping residents", [] {
    auto w = small();
    for (auto &r : w.residents) {
      r.automatic = false;
      r.route.clear();
    }
    place(w, 2, {16, 0, 14});
    place(w, 3, {17, 0, 14});
    place(w, 4, {-16, 0, -14});
    for (Id id : {2, 3, 4}) {
      w.residents[id].automatic = true;
      w.residents[id].activity = Activity::Idle;
      w.residents[id].hunger = w.residents[id].thirst = .3;
    }
    for (int n = 0; n < 60 && w.relationships.empty(); ++n)
      w.socialTick();
    require(w.trust(2, 3) > 0 && w.relationships.size() == 1, "Local residents did not converse");
    float trust = w.trust(2, 3);
    w.time += 3600;
    w.residents[3].activity = Activity::Sleeping;
    for (int n = 0; n < 60; ++n)
      w.socialTick();
    require(w.trust(2, 3) == trust && w.trust(2, 4) == 0, "Talk ignored sleep or distance");
  });
  test("Remote repair orders source parts near the facility, not near its owner", [] {
    Config c;
    c.floors = 200;
    c.population = 600;
    c.mode = Mode::Cheat;
    auto w = World::generate(c);
    Id facility = none;
    for (const auto &f : w.facilities)
      if (f.kind == FacilityKind::Water && f.floor > 170) {
        facility = f.id;
        break;
      }
    require(facility != none, "Missing upper-floor fixture");
    auto &f = w.facilities[facility];
    f.condition = .3f;
    place(w, f.manager, {15, 0, 14});
    Id contract = w.requestRepair(f.manager, facility);
    require(std::abs(w.facilities[w.repairs[contract].workshop].floor - f.floor) <= 10,
            "Remote manager sent replacement parts across the entire building");
    Id worker = none;
    for (auto &r : w.residents)
      if (r.role == Role::Repairer && std::abs(r.home - f.floor) < 20) {
        worker = r.id;
        break;
      }
    require(worker != none, "No local maintenance worker");
    for (auto &r : w.residents)
      r.automatic = false;
    auto &r = w.residents[worker];
    r.automatic = true;
    r.routine.shiftStart = 0;
    r.routine.shiftHours = 24;
    r.routine.daysOff = 0;
    r.health = 100;
    r.energy = r.hygiene = 1;
    r.hunger = r.thirst = r.bladder = .1;
    if (r.outfit != none)
      w.items[r.outfit].cleanliness = 1;
    place(w, worker, f.pos + Vec3{0, 0, 2});
    r.activity = Activity::Idle;
    w.decide(worker);
    require(w.repairs[contract].worker == worker, "Local repair was not assigned");
  });
  test("A dead business manager cannot freeze the entire supply economy", [] {
    Config c;
    c.population = 600;
    c.floors = 30;
    c.mode = Mode::Cheat;
    auto w = World::generate(c);
    std::set<Id> managers;
    for (const auto &f : w.facilities)
      managers.insert(f.manager);
    require(managers.size() > 8, "All businesses still depend on one manager");
    Id target = w.nearestAt({0, 30, 0}, FacilityKind::Food);
    auto &f = w.facilities[target];
    Id previous = f.manager, owner = f.owner;
    w.kill(previous);
    w.refreshManagement();
    require(f.manager != previous && w.residents[f.manager].alive && f.owner == owner,
            "Business lost its ownership or did not appoint a surviving manager");
    f.stock = 10;
    w.requestStock(f.manager, f.id, 1000);
    require(!w.freight.empty() && w.freight.back().facility == target,
            "Replacement manager cannot order essential supplies");
    conserved(w);
  });
  test("A hungry resident can queue for a packed meal when the kitchen is unavailable", [] {
    auto w = small();
    Id shop = w.nearest(0, FacilityKind::Shop);
    for (auto &f : w.facilities) {
      f.queue.clear();
      f.serving.clear();
      if (f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe)
        f.open = false;
    }
    for (auto &r : w.residents) {
      r.automatic = false;
      r.queuedAt = none;
      r.route.clear();
    }
    auto &f = w.facilities[shop];
    f.opensAt = 0;
    f.openHours = 24;
    f.staffNeeded = 0;
    f.price = 800;
    f.duration = 90;
    f.stock = 100;
    auto &r = w.residents[0];
    r.hunger = 1.2;
    place(w, 0, f.pos + Vec3{0, 0, 2});
    require(w.serviceChoice(0, FacilityKind::Food) == shop,
            "Hungry resident ignored available food shops");
    auto cash = w.accounts[r.account].balance;
    auto stock = f.stock;
    w.joinQueue(0, shop);
    w.advance(1800);
    Id meal = none;
    for (Id i : r.items)
      if (w.items[i].kind == ItemKind::Food && !w.items[i].consumed)
        meal = i;
    require(meal != none && w.items[meal].carrier == 0 &&
                w.accounts[r.account].balance == cash - 800 && f.stock == stock - 1,
            "Takeaway was not paid, stocked and physically handed over");
    float before = r.hunger;
    w.consumeItem(0, meal);
    require(r.hunger < before && w.items[meal].consumed, "Packed meal cannot be eaten");
    conserved(w);
  });
  test("Narrow bunk gaps and opposite shaft aisles remain navigable", [] {
    Config c;
    c.floors = 20;
    c.population = 600;
    auto w = World::generate(c);
    for (int floor : {1, 2, 10, 11})
      for (int bed = 0; bed < 16; ++bed) {
        auto p = w.layout(floor).bed(floor, bed) + Vec3{0, 0, 1.7f};
        auto path = w.floorPath(p, w.layout(floor).landing(0, floor * 3.f));
        require(path.size() >= 2, "Missing bunk exit route");
        for (std::size_t i = 1; i < path.size(); ++i) {
          int n = std::max(1, int(std::ceil(length(path[i] - path[i - 1]) / .08f)));
          for (int k = 0; k <= n; ++k)
            require(w.clearPosition(path[i - 1] + (path[i] - path[i - 1]) * (float(k) / n)),
                    "Narrow route intersects furniture");
        }
      }
    auto around = w.floorPath({-7, 3, 0}, {7, 3, 0});
    require(around.size() > 2, "Route crosses the open shaft");
  });
  test("Failed planning retains the resident's existing route", [] {
    auto w = small();
    place(w, 0, {-19, 0, 14});
    w.routeTo(0, {-19, 0, 9});
    auto old = w.residents[0].route;
    auto version = w.residents[0].routeVersion;
    rejects([&] { w.routeTo(0, {1000, 0, 1000}); });
    require(w.residents[0].routeVersion == version && w.residents[0].route.size() == old.size() &&
                length(w.residents[0].route.back().p - old.back().p) < .001f,
            "Failed route destroyed ongoing movement");
  });
  test("Exercise increases persistent fitness and strength with bounded speeds", [] {
    auto w = small();
    auto &r = w.residents[0];
    r.energy = .9f;
    r.hunger = .1f;
    r.thirst = .1f;
    float strength = r.strength, walk = w.walkingSpeed(0), run = w.walkingSpeed(0, true);
    require(walk >= 3.6f && run >= 7.2f, "Large-floor movement is too slow");
    for (int day = 0; day < 30; ++day) {
      r.energy = .9f;
      r.hunger = .1f;
      r.thirst = .1f;
      w.exercise(0, 3600, 3);
    }
    require(r.fitness > 30 && r.fitness <= 100 && r.strength > strength && r.strength <= 100,
            "Training has no bounded progression");
    require(w.walkingSpeed(0) > walk && w.walkingSpeed(0, true) > run &&
                w.walkingSpeed(0, true) <= 9.01f,
            "Fitness does not improve movement");
    w.save("/tmp/mega-fitness-test.mdsave");
    auto b = World::load("/tmp/mega-fitness-test.mdsave");
    require(std::abs(b.residents[0].fitness - r.fitness) < .001f, "Training was lost on reload");
    float trained = r.fitness;
    r.energy = 0;
    w.exercise(0, 3600, 3);
    require(r.fitness == trained && w.walkingSpeed(0, true) == w.walkingSpeed(0),
            "Exhausted resident can farm training or sprint");
  });
  test("Completing gym service trains the resident and pays its owner", [] {
    auto w = small();
    Id fid = w.nearest(0, FacilityKind::Food);
    auto &f = w.facilities[fid];
    f.kind = FacilityKind::Gym;
    f.duration = 1800;
    f.price = 100;
    f.stations = 1;
    auto &r = w.residents[0];
    r.energy = .9f;
    r.hunger = .1f;
    r.thirst = .1f;
    auto cash = w.accounts[r.account].balance;
    place(w, 0, f.pos + Vec3{0, 0, 1.6f});
    w.joinQueue(0, fid);
    w.advance(f.serving.front().until - w.time + .01);
    require(r.fitness > 0 && w.accounts[r.account].balance == cash - 100,
            "Gym does not deliver paid exercise");
  });
  test("Scarcity queues do not spam task interruptions", [] {
    Config c;
    c.floors = 20;
    c.population = 600;
    c.mode = Mode::Cheat;
    auto w = World::generate(c);
    w.residents[0].automatic = true;
    w.advance(7200);
    int failures = 0;
    for (const auto &n : w.news)
      if (n.key == "action.blocked")
        ++failures;
    require(failures < 5, "Routine full queues still interrupt resident tasks");
    require(w.living() == 600, "Route regression caused avoidable deaths");
  });
  test("Crowded service arrivals finish without overlapping or blocking the next customer", [] {
    Config c;
    c.population = 20;
    c.floors = 10;
    auto w = World::generate(c);
    while (!w.events.empty())
      w.events.pop();
    for (auto &f : w.facilities) {
      f.queue.clear();
      f.serving.clear();
    }
    for (auto &r : w.residents) {
      r.automatic = false;
      r.queuedAt = none;
      r.route.clear();
      r.activity = Activity::Idle;
      r.pos = {100 + float(r.id) * 2, 24, 100};
    }
    Id fid = w.nearestAt({0, 0, 260}, FacilityKind::Water);
    auto &f = w.facilities[fid];
    f.staffNeeded = 0;
    f.opensAt = 0;
    f.openHours = 24;
    f.price = 10;
    f.duration = 30;
    f.stations = 1;
    f.stock = 100;
    for (Id id = 0; id < 5; ++id) {
      place(w, id, f.pos + Vec3{float(id - 2), 0, 9});
      w.residents[id].automatic = true;
      w.residents[id].thirst = .8f;
    }
    w.focus(0);
    for (Id id = 0; id < 5; ++id)
      w.travel(id, f.pos, TaskKind::Service, fid);
    for (int step = 0; step < 3000; ++step) {
      w.moveCrowd(.02 * 48);
      w.advance(.02 * 48);
      for (Id a = 0; a < 5; ++a)
        for (Id b = a + 1; b < 5; ++b)
          require(length(w.position(a) - w.position(b)) >= .56f, "Service crowd overlaps");
    }
    for (Id id = 0; id < 5; ++id)
      require(w.residents[id].thirst < .3f, "A customer never reached the station");
    require(f.queue.empty() && f.serving.empty(), "Service flow remained blocked");
  });
  test("Residents can step out of their bed after sleeping without teleporting", [] {
    Config c;
    c.population = 20;
    c.floors = 10;
    auto w = World::generate(c);
    while (!w.events.empty())
      w.events.pop();
    for (auto &r : w.residents) {
      r.automatic = false;
      r.queuedAt = none;
      r.route.clear();
      r.activity = Activity::Idle;
      r.pos = {100 + float(r.id) * 2, 24, 100};
    }
    Id home = w.residentHomes[0];
    auto p = w.homePosition(home);
    place(w, 0, p);
    w.residents[0].activity = Activity::Sleeping;
    Vec3 goal = w.layout(w.housing[home].floor).landing(0, p.y);
    w.travel(0, goal, TaskKind::None, none);
    w.focus(w.housing[home].floor);
    Vec3 previous = w.position(0);
    for (int i = 0; i < 1500; ++i) {
      w.moveCrowd(.02 * 48);
      w.advance(.02 * 48);
      Vec3 next = w.position(0);
      require(length(next - previous) < .08f, "Waking teleported the resident");
      previous = next;
    }
    require(length(w.position(0) - goal) < .2f, "Bed collision trapped a waking resident");
  });
  test("Existing body overlap can separate but cannot deepen", [] {
    Config c;
    c.population = 20;
    c.floors = 10;
    auto w = World::generate(c);
    for (auto &r : w.residents) {
      r.route.clear();
      r.activity = Activity::Idle;
      r.pos = {100 + float(r.id) * 2, 24, 100};
    }
    place(w, 0, {100, 0, 100});
    place(w, 1, {100.2f, 0, 100});
    w.focus(0);
    w.moveCrowd(.001);
    auto away = w.moveCapsule(0, w.position(0), {-.2f, 0, 0});
    auto closer = w.moveCapsule(0, w.position(0), {.1f, 0, 0});
    require(away.x < 99.9f && closer.x == 100.f,
            "Overlap recovery allows deeper penetration or blocks escape");
  });
  test("Company capital and dividends use owned accounts and preserve payroll reserves", [] {
    auto w = small();
    Money before = w.accounts[w.residents[0].account].balance;
    Id cid = w.foundCompany(0, "Shared future", 60000);
    auto &c = w.companies[cid];
    require(w.accounts[c.account].balance == 60000 &&
                w.accounts[w.residents[0].account].balance == before - 60000,
            "Company capital was not funded by its founder");
    rejects([&] { w.investCompany(1, cid, 100); });
    rejects([&] { w.companyDividend(1, cid, 100); });
    rejects([&] { w.foundCompany(0, "", 10000); });
    w.investCompany(0, cid, 10000);
    w.companyDividend(0, cid, 5000);
    require(w.accounts[c.account].balance == 65000, "Company transfers lost money");
    conserved(w);
    w.validate();
  });
  test("Workspace lease separates title, operating funds and employee payroll", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office), landlord = w.facilities[f].owner;
    Id cid = w.foundCompany(0, "Tenant", 60000);
    Money rent = w.workspaceOffers.at(f), before = w.accounts[landlord].balance;
    Id lease = w.leaseWorkspace(0, cid, f);
    require(w.facilities[f].owner == landlord && w.operatingAccount(f) == w.companies[cid].account,
            "Lease transferred legal ownership");
    require(w.accounts[landlord].balance == before + rent &&
                w.accounts[w.workspaceLeases[lease].escrow].balance == 2 * rent,
            "Rent and deposit did not reach separate accounts");
    for (Id r : w.facilities[f].employees)
      require(w.residents[r].employer == w.companies[cid].account,
              "Payroll remained with landlord");
    w.chooseJob(0, Role::Programmer, f);
    require(w.residents[0].employer == w.companies[cid].account, "New hire used wrong employer");
    w.time += 86400;
    w.companyTick();
    require(w.accounts[landlord].balance == before + 2 * rent, "Daily rent was not paid");
    conserved(w);
    w.validate();
  });
  test("Active workplace lease blocks duplicate tenants, property sales and renovation", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    Id landlordOwner = w.companies[w.companyForAccount(w.facilities[f].owner)].owner;
    Id cid = w.foundCompany(0, "Tenant", 60000);
    w.leaseWorkspace(0, cid, f);
    rejects([&] { w.leaseWorkspace(0, cid, f); });
    rejects([&] { w.buyFacility(0, f); });
    rejects([&] { w.acquireWorkspace(0, cid, f); });
    rejects([&] { w.proposeProject(w.dormOwner, f, true, 60000); });
    rejects([&] { w.offerWorkspace(landlordOwner, f, 9000); });
    if (landlordOwner != 0) {
      rejects([&] { w.setBusiness(landlordOwner, f, 10, 900, true, false); });
      rejects([&] { w.setFacilityHours(landlordOwner, f, 0, 0, false); });
    }
    w.setBusiness(0, f, 100, 950, true, false);
    conserved(w);
  });
  test("Company purchase pays the seller and contributed property costs no duplicate price", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office), seller = w.facilities[f].owner;
    w.facilities[f].askingPrice = 45000;
    Id cid = w.foundCompany(0, "Owner", 60000);
    Money previous = w.accounts[seller].balance;
    w.acquireWorkspace(0, cid, f);
    require(w.facilities[f].owner == w.companies[cid].account &&
                w.accounts[seller].balance == previous + 45000 && w.facilities[f].askingPrice == 0,
            "Company purchase did not settle title and price");
    rejects([&] { w.acquireWorkspace(0, cid, f); });
    w.offerWorkspace(0, f, 5000);
    require(w.workspaceOffers.at(f) == 5000, "Company owner could not list property");
    w.offerWorkspace(0, f, 0);
    require(!w.workspaceOffers.contains(f), "Rental offer was not withdrawn");
    auto personal = small();
    f = personal.nearest(0, FacilityKind::Office);
    personal.facilities[f].askingPrice = 30000;
    personal.buyFacility(0, f);
    cid = personal.foundCompany(0, "Contribution", 30000);
    personal.acquireWorkspace(0, cid, f);
    require(personal.accounts[personal.companies[cid].account].balance == 30000,
            "Owner was charged for contributing their own office");
    conserved(w);
    conserved(personal);
    w.validate();
    personal.validate();
  });
  test("Lease default retains only owed deposit and preserves the remaining debt", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    Id cid = w.foundCompany(0, "Default", 60000);
    Id lease = w.leaseWorkspace(0, cid, f);
    Id treasury = w.companies[cid].account;
    w.pay(treasury, 0, w.accounts[treasury].balance, "test-expense");
    w.time += 3 * 86400;
    w.companyTick();
    const auto &l = w.workspaceLeases[lease];
    require(!l.active && !w.leasedWorkspaces.contains(f) && l.arrears == l.dailyRent &&
                w.accounts[l.escrow].balance == 0,
            "Default erased debt or deposit");
    require(w.facilities[f].employees.empty(), "Closed lease kept employees working for tenant");
    w.investCompany(0, cid, 10000);
    w.companyTick();
    require(w.workspaceLeases[lease].arrears == 0, "New capital did not repay the old lease debt");
    conserved(w);
    w.validate();
  });
  test("Returning workspace refunds client escrow and closes the old payroll", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    Id cid = w.foundCompany(0, "Return", 60000);
    Id lease = w.leaseWorkspace(0, cid, f);
    w.chooseJob(0, Role::Programmer, f);
    w.residents[0].routine.shiftStart = 0;
    w.residents[0].routine.shiftHours = 24;
    place(w, 0, w.facilities[f].pos + Vec3{0, 0, -1.6f});
    Id order = w.officeOrder(0);
    w.time += 600;
    w.endWorkspaceLease(0, lease);
    require(w.officeOrders[order].submitted &&
                w.accounts[w.officeOrders[order].escrow].balance == 0 &&
                !w.activeOfficeOrders.contains(0) && w.residents[0].jobSite == none &&
                w.residents[0].routine.workSince == -1,
            "Lease exit left an active commission or payroll");
    require(w.accounts[w.workspaceLeases[lease].escrow].balance == 0, "Deposit not returned");
    conserved(w);
    w.validate();
  });
  test("Team leaders assign real briefs without access to ownership funds", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    Id cid = w.foundCompany(0, "Team", 60000);
    w.leaseWorkspace(0, cid, f);
    w.chooseJob(2, Role::Programmer, f);
    w.chooseJob(3, Role::VideoEditor, f);
    w.appointTeamLeader(0, cid, 2);
    w.assignCompanyWork(2, cid, 3, Role::Programmer, 1);
    require(w.residents[3].role == Role::Programmer && w.companies[cid].assignments.at(3) == 1,
            "Leader assignment was not applied");
    rejects([&] { w.companyDividend(2, cid, 100); });
    rejects([&] { w.endWorkspaceLease(2, w.leasedWorkspaces.at(f)); });
    rejects([&] { w.appointTeamLeader(2, cid, 3); });
    w.chooseJob(2, Role::Courier, w.nearest(2, FacilityKind::Shop));
    rejects([&] { w.assignCompanyWork(2, cid, 3, Role::VideoEditor, 0); });
    w.companyTick();
    require(w.companies[cid].leader == none, "Former employee retained leadership");
    conserved(w);
  });
  test("Company work needs attendance and pays the operating company with a quality bonus", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office), landlord = w.facilities[f].owner;
    Id cid = w.foundCompany(0, "Delivery", 60000);
    w.leaseWorkspace(0, cid, f);
    w.chooseJob(0, Role::Programmer, f);
    w.assignCompanyWork(0, cid, 0, Role::Programmer, 1);
    w.residents[0].routine.shiftStart = 0;
    w.residents[0].routine.shiftHours = 24;
    place(w, 0, w.facilities[f].pos + Vec3{0, 0, -1.6f});
    Id order = w.officeOrder(0);
    require(w.officeOrders[order].brief == 1, "Work ignored the assigned brief");
    rejects([&] { w.assignCompanyWork(0, cid, 0, Role::Programmer, 2); });
    rejects([&] { w.submitOffice(order, {5, 4}); });
    finishAttendance(w);
    w.residents[0].terminal = f;
    w.residents[0].terminalUntil = w.time + 100;
    Money before = w.accounts[w.companies[cid].account].balance,
          titleBefore = w.accounts[landlord].balance;
    w.submitOffice(order, {5, 4});
    require(w.accounts[w.companies[cid].account].balance == before + 19200 &&
                w.accounts[landlord].balance == titleBefore &&
                w.companies[cid].completed.at(0) == 1,
            "Work revenue went to landlord or ignored performance");
    rejects([&] { w.companyDividend(0, cid, w.accounts[w.companies[cid].account].balance); });
    conserved(w);
    w.validate();
  });
  test("Company and active lease state survives a save and reload", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    Id cid = w.foundCompany(0, "Équipe 明天", 60000);
    Id l = w.leaseWorkspace(0, cid, f);
    w.chooseJob(2, Role::Programmer, f);
    w.appointTeamLeader(0, cid, 2);
    w.assignCompanyWork(2, cid, 2, Role::Programmer, 2);
    auto path = std::filesystem::temp_directory_path() / "mega-company-test.mdsave";
    w.save(path);
    auto saved = World::load(path);
    require(saved.companies[cid].name == "Équipe 明天" && saved.companies[cid].leader == 2 &&
                saved.companies[cid].assignments.at(2) == 2 && saved.leasedWorkspaces.at(f) == l &&
                saved.operatingAccount(f) == saved.companies[cid].account,
            "Company or lease state changed during persistence");
    saved.time += 86400;
    saved.companyTick();
    saved.validate();
    conserved(saved);
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
  });
  test("Video office briefs reward their requested story order", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    w.chooseJob(0, Role::VideoEditor, f);
    w.residents[0].routine.shiftStart = 0;
    w.residents[0].routine.shiftHours = 24;
    place(w, 0, w.facilities[f].pos + Vec3{0, 0, -1.6f});
    Id job = w.officeOrder(0);
    w.officeOrders[job].brief = 1;
    require(w.scoreOffice(job, {2, 1, 0, 3, 4, 5}) == 100 &&
                w.scoreOffice(job, {0, 1, 2, 3, 4, 5}) < 100 &&
                w.scoreOffice(job, {2, 2, 0, 3, 4, 5}) == 0,
            "News brief did not assess story structure");
    w.officeOrders[job].brief = 2;
    require(w.scoreOffice(job, {3, 2, 1, 4, 0, 5}) == 100, "Campaign brief scoring is incorrect");
    conserved(w);
  });
  test("Company validation rejects invalid accounts and duplicate property contracts", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    Id cid = w.foundCompany(0, "Validation", 60000);
    Id l = w.leaseWorkspace(0, cid, f);
    auto bad = w;
    bad.companies[cid].account = none;
    rejects([&] { bad.validate(); });
    bad = w;
    bad.workspaceLeases[l].landlord = w.companies[cid].account;
    rejects([&] { bad.validate(); });
    bad = w;
    bad.leasedWorkspaces.clear();
    rejects([&] { bad.validate(); });
    w.validate();
  });

  test("Company construction spends its treasury and reserves ownership before any building work",
       [] {
         Config cfg;
         cfg.population = 40;
         cfg.floors = 10;
         cfg.mode = Mode::Cheat;
         auto w = World::generate(cfg);
         w.pay(0, w.residents[0].account, 1000000, "test-investment");
         Id cid = w.foundCompany(0, "Builders client", 700000);
         Money personal = w.accounts[w.residents[0].account].balance;
         Id project = w.commissionFacility(0, FacilityKind::Office, 1, {54, 3, 0}, 2, cid);
         const auto &p = w.projects[project];
         const auto &f = w.facilities[p.facility];
         require(
             f.owner == w.companies[cid].account && !f.open && p.newConstruction &&
                 w.accounts[p.escrow].balance == 500000 &&
                 w.accounts[w.companies[cid].account].balance == 160000 &&
                 w.accounts[w.residents[0].account].balance == personal,
             "Company construction used personal funds, opened early or assigned the wrong owner");
         rejects([&] { w.commissionFacility(1, FacilityKind::Office, 2, {54, 6, 0}, 1, cid); });
         conserved(w);
         w.validate();
       });
  test("Company hiring requires a real vacancy and enough funds for the whole payroll", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    Id cid = w.foundCompany(0, "Recruitment", 60000);
    w.leaseWorkspace(0, cid, f);
    auto existing = w.facilities[f].employees;
    for (Id worker : existing)
      w.dismissEmployee(0, f, worker);
    auto &candidate = w.residents[2];
    if (candidate.jobSite != none)
      std::erase(w.facilities[candidate.jobSite].employees, 2);
    candidate.jobSite = candidate.employer = none;
    candidate.role = Role::Unemployed;
    candidate.automatic = true;
    w.hireEmployee(0, f, 2, Role::Programmer, 22);
    require(candidate.employer == w.companies[cid].account && candidate.routine.shiftStart == 22 &&
                candidate.routine.sleepStart == 8,
            "Recruited night worker has incorrect payroll or sleep schedule");
    rejects([&] { w.hireEmployee(1, f, 2, Role::Programmer, 6); });
    w.dismissEmployee(0, f, 2);
    Id funds = w.companies[cid].account;
    w.pay(funds, 0, w.accounts[funds].balance, "test-cost");
    rejects([&] { w.hireEmployee(0, f, 2, Role::Programmer, 6); });
    require(candidate.role == Role::Unemployed, "Unfunded hire changed employment");
    conserved(w);
    w.validate();
  });
  test("AI companies promote proven staff and assign the next real brief", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office), cid = w.companyForAccount(w.facilities[f].owner);
    w.chooseJob(2, Role::Programmer, f);
    w.residents[2].automatic = true;
    w.companies[cid].completed[2] = 4;
    require(w.companies[cid].owner != w.player, "Fixture company is player owned");
    w.companyTick();
    require(w.companies[cid].leader == 2 && w.companies[cid].assignments.at(2) == 1,
            "AI director did not recognize completed work or assign the next brief");
    conserved(w);
    w.validate();
  });

  test("Leased private offices give computers to the tenant employees", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    Id cid = w.foundCompany(0, "Private office", 60000);
    w.leaseWorkspace(0, cid, f);
    w.chooseJob(2, Role::Programmer, f);
    w.setFacilityHours(0, f, 0, 24, false);
    place(w, 2, w.facilities[f].pos + Vec3{0, 0, -1.6f});
    require(w.accessAllowed(2, f), "Tenant's employee was barred from private workplace");
    w.useTerminal(2, f, 30);
    require(w.hasDevice(2) && w.residents[2].terminal == f, "Tenant's computer access was denied");
    w.chooseJob(4, Role::Courier, w.nearest(4, FacilityKind::Shop));
    place(w, 4, w.facilities[f].pos + Vec3{0, 0, -1.6f});
    rejects([&] { w.useTerminal(4, f, 30); });
    w.dismissEmployee(0, f, 2);
    require(w.residents[2].terminal == none, "Dismissed employee retained the office computer");
    w.validate();
  });
  test("Office incorporation never treats a personal wallet as company capital", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    w.facilities[f].askingPrice = 10000;
    w.buyFacility(0, f);
    w.companies.clear();
    w.workspaceOffers.clear();
    w.setupCompanies();
    require(w.companyForAccount(w.residents[0].account) == none,
            "Personal wallet became a company treasury");
    w.validate();
  });
  test("An acquired company workplace refunds older unfinished video commissions", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    w.chooseJob(2, Role::VideoEditor, f);
    Id video = w.commissionVideo(2, w.dormAccount, w.residents[2].employer, 24000);
    Id cid = w.foundCompany(0, "New operator", 60000);
    w.leaseWorkspace(0, cid, f);
    require(w.videos[video].submitted && w.videos[video].score == 0 &&
                w.accounts[w.videos[video].escrow].balance == 0,
            "An old video commission stranded its client funds");
    conserved(w);
    w.validate();
  });

  test("Partial rent catch-up reduces the lease default clock", [] {
    auto w = small();
    Id f = w.nearest(0, FacilityKind::Office);
    Id cid = w.foundCompany(0, "Catch-up", 60000);
    Id lease = w.leaseWorkspace(0, cid, f);
    Money rent = w.workspaceLeases[lease].dailyRent;
    Id funds = w.companies[cid].account;
    w.pay(funds, 0, w.accounts[funds].balance - 2 * rent, "test-expenses");
    w.time += 3 * 86400;
    w.companyTick();
    require(w.workspaceLeases[lease].active && w.workspaceLeases[lease].arrears == rent &&
                w.workspaceLeases[lease].missedDays == 1,
            "Catch-up rent failed to clear the oldest unpaid days");
    conserved(w);
    w.validate();
  });

  std::cout << passed << " passed; " << failed << " failed\n";
  return failed ? 1 : 0;
}
