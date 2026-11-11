#include "json.hpp"
#include "mega/world.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <zstd.h>
using namespace mega;
#include <algorithm>
#include <chrono>
#include <filesystem>
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
    r.hunger = .1f;
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
static DailyStep step(const World &w, DailyNeed need) {
  auto p = w.dailyPlan(0);
  return *std::find_if(p.steps.begin(), p.steps.end(),
                       [&](const auto &s) { return s.need == need; });
}
static void strip(World &w, ItemKind kind) {
  auto &r = w.residents[0];
  for (Id id : r.items)
    if (w.items[id].kind == kind)
      w.items[id].carrier = none;
  std::erase_if(r.items, [&](Id id) { return w.items[id].carrier != 0; });
}
static Id facility(World &w, FacilityKind kind) {
  for (auto &f : w.facilities)
    if (f.kind == kind && f.floor == 0)
      return f.id;
  for (auto &f : w.facilities)
    if (f.kind == kind) {
      std::erase(w.floorFacilities[f.floor], f.id);
      f.floor = 0;
      f.pos.y = 0;
      w.floorFacilities[0].push_back(f.id);
      w.invalidateGeometry(0);
      return f.id;
    }
  throw std::runtime_error("Missing facility");
}
static void quiet(World &w) {
  auto &r = w.residents[0];
  r.automatic = true;
  r.interests.fill(0);
  r.interests[2] = 1;
  r.routine.sleepStart = 23;
  r.routine.sleepHours = 8;
  auto &p = w.residentPlans[0];
  p.nextShopping = p.nextCareer = p.nextBusiness = p.nextListing = w.time + 86400;
}
static void fund(World &w, Money n) {
  auto a = w.residents[0].account;
  if (w.accounts[a].balance > n)
    w.pay(a, w.dormAccount, w.accounts[a].balance - n, "test-budget");
  else
    w.pay(w.dormAccount, a, n - w.accounts[a].balance, "test-budget");
}
static void legacy(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
  auto size = ZSTD_getFrameContentSize(bytes.data() + 8, bytes.size() - 8);
  std::vector<unsigned char> raw(size);
  require(
      !ZSTD_isError(ZSTD_decompress(raw.data(), raw.size(), bytes.data() + 8, bytes.size() - 8)),
      "Decode failed");
  auto j = nlohmann::json::from_cbor(raw);
  for (auto &e : j["residentPlans"]) {
    e[1].erase("nextDaily");
    e[1].erase("nextRecreation");
    e[1].erase("plannedLeisure");
  }
  raw = nlohmann::json::to_cbor(j);
  std::vector<char> packed(ZSTD_compressBound(raw.size()));
  auto n = ZSTD_compress(packed.data(), packed.size(), raw.data(), raw.size(), 3);
  require(!ZSTD_isError(n), "Encode failed");
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write("MDORM001", 8);
  out.write(packed.data(), n);
}
int main() {
  test("Daily preview is read-only, complete and uses no reservations", [] {
    auto w = small();
    auto events = w.events.size();
    auto money = w.internalMoney();
    auto rng = w.rng;
    auto plans = w.residentPlans.size();
    auto p = w.dailyPlan(0);
    require(p.steps.size() == 10 && events == w.events.size() && w.internalMoney() == money &&
                plans == w.residentPlans.size() && rng == w.rng,
            "Preview executed actions or missed a need");
  });
  test("Food and water deadlines forecast different actual need rates", [] {
    auto w = small();
    strip(w, ItemKind::Food);
    auto food = step(w, DailyNeed::Food), water = step(w, DailyNeed::Water);
    require(std::abs(food.due - w.time - (.57 - .1) / .045 * 3600) < 1 &&
                std::abs(water.due - w.time - (.65 - .1) / .065 * 3600) < 1,
            "Need forecast does not match depletion");
  });
  test("Owned meal is consumed before an upcoming shift without magic delivery", [] {
    auto w = small();
    quiet(w);
    auto &r = w.residents[0];
    strip(w, ItemKind::Food);
    Id meal = add(w, 0, ItemKind::Food);
    r.hunger = .4f;
    r.role = Role::Programmer;
    r.jobSite = facility(w, FacilityKind::Office);
    r.routine.shiftStart = 11;
    r.routine.shiftHours = 8;
    auto money = w.internalMoney();
    require(step(w, DailyNeed::Food).due <= w.time + 3600, "No shift preparation");
    // Departure takes priority only when it is actually time to commute.
    place(w, 0, w.facilities[r.jobSite].pos);
    w.time = 10 * 3600 + 56 * 60;
    r.needsAt = w.time;
    require(w.dailyDecision(0) && w.items[meal].consumed && r.hunger < .4f &&
                w.internalMoney() == money,
            "Preparation did not use owned food");
  });
  test("A stolen carried meal is not treated as owned household food", [] {
    auto w = small();
    quiet(w);
    strip(w, ItemKind::Food);
    auto item = add(w, 0, ItemKind::Food);
    w.items[item].owner = w.residents[1].account;
    w.residents[0].hunger = .7f;
    require(step(w, DailyNeed::Food).item == none, "Foreign property planned as owned meal");
  });
  test("Reserved and consumed food cannot satisfy the plan twice", [] {
    auto w = small();
    strip(w, ItemKind::Food);
    auto id = add(w, 0, ItemKind::Food);
    w.items[id].reservedAt = facility(w, FacilityKind::Shop);
    require(step(w, DailyNeed::Food).item == none, "Reserved food reused");
    w.items[id].reservedAt = none;
    w.items[id].consumed = true;
    require(step(w, DailyNeed::Food).item == none, "Consumed food reused");
  });
  test("Actual owned medicine is consumed for injury", [] {
    auto w = small();
    quiet(w);
    strip(w, ItemKind::Medicine);
    auto id = add(w, 0, ItemKind::Medicine);
    w.residents[0].health = 50;
    auto cash = w.accounts[w.residents[0].account].balance;
    require(w.dailyDecision(0) && w.items[id].consumed && w.residents[0].health > 50 &&
                cash == w.accounts[w.residents[0].account].balance,
            "Medicine was not actually used");
  });
  test("A clean owned spare replaces dirty clothes without buying duplicates", [] {
    auto w = small();
    quiet(w);
    Id outfit = w.residents[0].outfit;
    for (Id old : w.residents[0].items)
      if (w.items[old].kind == ItemKind::Clothes && old != outfit)
        w.items[old].carrier = none;
    std::erase_if(w.residents[0].items, [&](Id old) { return w.items[old].carrier != 0; });
    auto id = add(w, 0, ItemKind::Clothes);
    w.items[id].cleanliness = 1;
    w.items[w.residents[0].outfit].cleanliness = .1f;
    require(step(w, DailyNeed::Clothing).item == id && w.dailyDecision(0) &&
                w.residents[0].outfit == id,
            "Did not change into owned spare");
  });
  test("Dirty spare clothes generate a real laundry destination", [] {
    auto w = small();
    quiet(w);
    auto id = add(w, 0, ItemKind::Clothes);
    w.items[id].cleanliness = .2f;
    auto f = facility(w, FacilityKind::Laundry);
    place(w, 0, w.accessible(w.facilities[f].pos + Vec3{0, 0, 4}));
    auto s = step(w, DailyNeed::Clothing);
    require(s.facility != none && s.state == DailyState::Ready && w.dailyDecision(0) &&
                w.residents[0].task == TaskKind::Service &&
                w.facilities[w.residents[0].target].kind == FacilityKind::Laundry &&
                w.items[id].cleanliness == .2f,
            "Laundry skipped physical trip");
  });
  test("Personal waste and dirty dishes have separate real services", [] {
    auto w = small();
    auto &r = w.residents[0];
    r.personalWaste = 3;
    facility(w, FacilityKind::Garbage);
    auto s = step(w, DailyNeed::Cleaning);
    require(s.facility != none && w.facilities[s.facility].kind == FacilityKind::Garbage,
            "Waste has no disposal plan");
    r.personalWaste = 0;
    auto i = add(w, 0, ItemKind::Tableware);
    w.items[i].cleanliness = .1f;
    facility(w, FacilityKind::Washbasin);
    s = step(w, DailyNeed::Cleaning);
    require(s.facility != none && w.facilities[s.facility].kind == FacilityKind::Washbasin,
            "Dishes have no washing plan");
  });
  test("Queues are included in departure estimates, not reserved", [] {
    auto w = small();
    auto id = facility(w, FacilityKind::Water);
    for (auto &f : w.facilities)
      if (f.kind == FacilityKind::Water && f.id != id)
        f.open = false;
    auto a = step(w, DailyNeed::Water);
    w.facilities[id].queue = {1, 2, 3};
    auto b = step(w, DailyNeed::Water);
    require(b.seconds > a.seconds && b.depart < a.depart && w.facilities[id].queue.size() == 3,
            "Queue ignored or altered by preview");
  });
  test("Closed, unstaffed and empty water services explain waiting", [] {
    auto w = small();
    auto id = facility(w, FacilityKind::Water);
    for (auto &f : w.facilities)
      if (f.kind == FacilityKind::Water && f.id != id)
        f.demolished = true;
    auto &f = w.facilities[id];
    f.open = false;
    require(step(w, DailyNeed::Water).reason == "facility.closed", "Closed reason missing");
    f.open = true;
    f.staffNeeded = 1;
    require(step(w, DailyNeed::Water).reason == "facility.unstaffed", "Staff reason missing");
    f.staffNeeded = 0;
    f.stock = 0;
    require(step(w, DailyNeed::Water).reason == "daily.no_stock", "Stock reason missing");
  });
  test("Toilets remain usable with no workers when their owner permits it", [] {
    auto w = small();
    auto f = facility(w, FacilityKind::Toilet);
    w.facilities[f].staffNeeded = 0;
    w.residents[0].bladder = .8f;
    require(step(w, DailyNeed::Toilet).facility != none, "Unstaffed self-service denied");
  });
  test("Recreation follows personal preference and preserves the essential buffer", [] {
    auto w = small();
    quiet(w);
    w.residents[0].happiness = .4f;
    auto f = facility(w, FacilityKind::Arcade);
    w.facilities[f].price = 400;
    place(w, 0, w.accessible(w.facilities[f].pos + Vec3{0, 0, 4}));
    fund(w, w.householdReserve(0) + 2000);
    require(step(w, DailyNeed::Recreation).facility == f && w.dailyLeisure(0) &&
                w.residentPlans[0].plannedLeisure == f,
            "Preference did not cause real leisure trip");
  });
  test("Leisure cannot use rent, arrears, business wages or storage reserves", [] {
    auto w = small();
    quiet(w);
    w.residents[0].happiness = .4f;
    auto f = facility(w, FacilityKind::Arcade);
    w.facilities[f].price = 400;
    fund(w, w.householdReserve(0) + 399);
    require(step(w, DailyNeed::Recreation).state == DailyState::Waiting &&
                step(w, DailyNeed::Recreation).reason == "daily.savings",
            "Rent buffer spent on leisure");
  });
  test("Urgent water can use essential money even when future rent is underfunded", [] {
    auto w = small();
    quiet(w);
    auto f = facility(w, FacilityKind::Water);
    w.facilities[f].price = 30;
    fund(w, 30);
    w.residents[0].thirst = 1.1f;
    place(w, 0, w.accessible(w.facilities[f].pos + Vec3{0, 0, 4}));
    require(w.dailyDecision(0) && w.facilities[w.residents[0].target].kind == FacilityKind::Water,
            "Savings policy stranded a thirsty resident with water money");
  });
  test("Discretionary checkout rechecks savings but manual choices remain possible", [] {
    auto w = small();
    quiet(w);
    auto f = facility(w, FacilityKind::Arcade);
    w.residentPlans[0].plannedLeisure = f;
    fund(w, w.householdReserve(0) + 50);
    require(!w.dailyServiceAllowed(0, f, 100), "Savings changed but checkout permitted");
    w.residents[0].automatic = false;
    require(w.dailyServiceAllowed(0, f, 100), "Manual decision was blocked by autopilot policy");
  });
  test("A queue cancellation clears its discretionary spending permission", [] {
    auto w = small();
    quiet(w);
    auto f = facility(w, FacilityKind::Arcade);
    w.facilities[f].stock = 0;
    place(w, 0, w.serviceApproach(f));
    w.residentPlans[0].plannedLeisure = f;
    fund(w, w.householdReserve(0) + 5000);
    w.joinQueue(0, f);
    w.leaveQueue(0);
    require(w.residentPlans[0].plannedLeisure == none && w.residentPlans[0].nextRecreation > w.time,
            "Cancelled leisure permission leaked");
  });
  test("Long leisure visits defer to a near work shift", [] {
    auto w = small();
    quiet(w);
    auto &r = w.residents[0];
    auto f = facility(w, FacilityKind::Arcade);
    w.facilities[f].duration = 7200;
    r.happiness = .3f;
    r.role = Role::Programmer;
    r.jobSite = facility(w, FacilityKind::Office);
    r.routine.shiftStart = 10.5;
    r.routine.shiftHours = 8;
    fund(w, w.householdReserve(0) + 10000);
    require(step(w, DailyNeed::Recreation).state == DailyState::Waiting &&
                step(w, DailyNeed::Recreation).reason == "daily.schedule",
            "Leisure overran work");
  });
  test("Night workers prepare for their own shift and sleep in the daytime", [] {
    auto w = small();
    quiet(w);
    auto &r = w.residents[0];
    w.time = 21 * 3600;
    r.needsAt = w.time;
    r.routine.shiftStart = 22;
    r.routine.shiftHours = 8;
    r.routine.sleepStart = 7;
    r.routine.sleepHours = 8;
    r.role = Role::Programmer;
    r.jobSite = facility(w, FacilityKind::Office);
    require(step(w, DailyNeed::Work).due == 22 * 3600 && step(w, DailyNeed::Sleep).due == 31 * 3600,
            "Night schedule used a daytime default");
    w.time = 26 * 3600;
    r.needsAt = w.time;
    require(w.onShift(0) && step(w, DailyNeed::Work).due == w.time,
            "Overnight shift lost its start day");
  });
  test("Days off are observed across midnight", [] {
    auto w = small();
    quiet(w);
    auto &r = w.residents[0];
    r.role = Role::Programmer;
    r.jobSite = facility(w, FacilityKind::Office);
    r.routine.shiftStart = 22;
    r.routine.shiftHours = 8;
    r.routine.daysOff = 1;
    w.time = 26 * 3600;
    r.needsAt = w.time;
    require(!w.onShift(0) && step(w, DailyNeed::Work).due == 46 * 3600,
            "Day-off overnight shift scheduled");
  });
  test("Job loss removes commute plans immediately", [] {
    auto w = small();
    quiet(w);
    auto &r = w.residents[0];
    r.role = Role::Programmer;
    r.jobSite = facility(w, FacilityKind::Office);
    require(step(w, DailyNeed::Work).state != DailyState::Covered, "Missing employed shift");
    r.role = Role::Unemployed;
    r.jobSite = none;
    require(step(w, DailyNeed::Work).state == DailyState::Covered, "Stale shift after job loss");
  });
  test("Manual, restrained and airborne residents do not execute daily plans", [] {
    auto w = small();
    auto &r = w.residents[0];
    r.thirst = 1.2f;
    require(!w.dailyDecision(0), "Manual actor hijacked");
    r.automatic = true;
    r.surrendered = true;
    require(!w.dailyDecision(0), "Surrender hijacked");
    r.surrendered = false;
    r.activity = Activity::Restrained;
    require(!w.dailyDecision(0), "Custody hijacked");
  });
  test("Daily throttle and real recreation cooldown survive a save", [] {
    auto w = small();
    quiet(w);
    auto &p = w.residentPlans[0];
    p.nextDaily = w.time + 180;
    p.nextRecreation = w.time + 28800;
    p.plannedLeisure = facility(w, FacilityKind::Arcade);
    auto path =
        std::filesystem::temp_directory_path() /
        ("mega-daily-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mdsave");
    w.save(path);
    auto loaded = World::load(path);
    std::filesystem::remove(path);
    const auto &restored = loaded.residentPlans.at(0);
    require(restored.nextDaily == p.nextDaily && restored.nextRecreation == p.nextRecreation &&
                restored.plannedLeisure == p.plannedLeisure && !loaded.dailyDecision(0),
            "Plan replayed or lost cooldown");
  });
  test("Older saves obtain empty daily clocks without inventing spending or actions", [] {
    auto w = small();
    quiet(w);
    auto cash = w.accounts[w.residents[0].account].balance;
    auto path =
        std::filesystem::temp_directory_path() /
        ("mega-daily-legacy-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mdsave");
    w.save(path);
    legacy(path);
    auto loaded = World::load(path);
    std::filesystem::remove(path);
    const auto &p = loaded.residentPlans.at(0);
    require(p.nextDaily == 0 && p.nextRecreation == 0 && p.plannedLeisure == none &&
                cash == loaded.accounts[loaded.residents[0].account].balance,
            "Migration invented an action");
  });
  test("Leisure checkout refunds stock when rent money is lost during use", [] {
    auto w = small();
    quiet(w);
    auto f = facility(w, FacilityKind::Arcade);
    auto &venue = w.facilities[f];
    venue.price = 100;
    venue.duration = 60;
    venue.stock = 10;
    place(w, 0, w.serviceApproach(f));
    fund(w, w.householdReserve(0) + 500);
    w.residentPlans[0].plannedLeisure = f;
    w.joinQueue(0, f);
    require(venue.serving.size() == 1 && venue.stock == 9, "Service did not reserve real stock");
    // Spending elsewhere changes affordability while the service's reservation is held.
    fund(w, w.householdReserve(0) + 50);
    auto owner = w.accounts[w.operatingAccount(f)].balance;
    auto until = venue.serving.front().until;
    place(w, 0, w.servicePosition(f, venue.serving.front().station));
    while (!w.events.empty())
      w.events.pop();
    w.time = until;
    w.residents[0].needsAt = w.time;
    w.schedule(until, EventKind::FinishService, f, 0);
    w.advance(.01);
    require(venue.stock == 10 && venue.serving.empty() && w.residents[0].queuedAt == none &&
                w.accounts[w.operatingAccount(f)].balance == owner &&
                w.residentPlans[0].plannedLeisure == none,
            "Discretionary charge consumed rent or leaked a reservation");
  });
  test("Successful leisure charges the real owner and starts a persistent cooldown", [] {
    auto w = small();
    quiet(w);
    auto f = facility(w, FacilityKind::Arcade);
    auto &venue = w.facilities[f];
    venue.price = 100;
    venue.duration = 60;
    place(w, 0, w.serviceApproach(f));
    fund(w, w.householdReserve(0) + 500);
    w.residentPlans[0].plannedLeisure = f;
    auto cash = w.accounts[w.residents[0].account].balance;
    auto owner = w.accounts[w.operatingAccount(f)].balance;
    auto total = w.internalMoney();
    w.joinQueue(0, f);
    auto until = venue.serving.front().until;
    place(w, 0, w.servicePosition(f, venue.serving.front().station));
    while (!w.events.empty())
      w.events.pop();
    w.time = until;
    w.residents[0].needsAt = w.time;
    w.schedule(until, EventKind::FinishService, f, 0);
    w.advance(.01);
    require(w.accounts[w.residents[0].account].balance == cash - 100 &&
                w.accounts[w.operatingAccount(f)].balance == owner + 100 &&
                w.internalMoney() == total && w.residentPlans[0].nextRecreation >= until + 28800 &&
                w.residentPlans[0].plannedLeisure == none,
            "Leisure bypassed real payment or repeated without cooldown");
  });
  test("Urgent thirst takes priority over an imminent shift and recreation", [] {
    auto w = small();
    quiet(w);
    auto &r = w.residents[0];
    auto f = facility(w, FacilityKind::Water);
    r.thirst = 1.1f;
    r.happiness = .2f;
    r.role = Role::Programmer;
    r.jobSite = facility(w, FacilityKind::Office);
    r.routine.shiftStart = 10.01;
    r.routine.shiftHours = 8;
    place(w, 0, w.accessible(w.facilities[f].pos + Vec3{0, 0, 4}));
    require(w.dailyDecision(0) && r.task == TaskKind::Service &&
                w.facilities[r.target].kind == FacilityKind::Water,
            "Urgent water lost to commute/leisure");
  });
  test("No worker requirement is bypassed by the daily planner", [] {
    auto w = small();
    quiet(w);
    w.residents[0].hunger = .8f;
    strip(w, ItemKind::Food);
    for (auto &f : w.facilities)
      if (f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe ||
          f.kind == FacilityKind::Shop)
        f.staffNeeded = 10;
    auto s = step(w, DailyNeed::Food);
    require(s.state == DailyState::Waiting && s.reason == "facility.unstaffed" &&
                s.facility == none,
            "Planner conjured a working cook");
  });
  test("A service-departure decision changes into a clean spare inside the daily cooldown", [] {
    auto w = small();
    quiet(w);
    auto outfit = w.residents[0].outfit;
    for (Id old : w.residents[0].items)
      if (w.items[old].kind == ItemKind::Clothes && old != outfit)
        w.items[old].carrier = none;
    std::erase_if(w.residents[0].items, [&](Id old) { return w.items[old].carrier != 0; });
    auto spare = add(w, 0, ItemKind::Clothes);
    w.items[spare].cleanliness = 1;
    w.items[outfit].cleanliness = .1f;
    w.residentPlans[0].nextDaily = w.time + 180;
    w.decide(0);
    require(w.residents[0].outfit == spare,
            "Cooldown left a resident in dirty clothes beside a clean spare");
  });
  test("Missing clothes are an acquisition need, not a satisfied plan", [] {
    auto w = small();
    quiet(w);
    strip(w, ItemKind::Clothes);
    w.residents[0].outfit = none;
    auto clothing = step(w, DailyNeed::Clothing);
    require(clothing.state == DailyState::Waiting && clothing.reason == "daily.need_clothes" &&
                clothing.facility == none,
            "Planner claimed missing clothes were covered");
  });
  test("A daylight sleeper returns to their real owned or rented bed", [] {
    auto w = small();
    quiet(w);
    auto &r = w.residents[0];
    w.time = 7 * 3600;
    r.needsAt = w.time;
    r.routine.sleepStart = 7;
    r.routine.sleepHours = 8;
    auto bed = w.homePosition(w.residentHomes[0]);
    place(w, 0, bed);
    require(w.dailyDecision(0) && r.task == TaskKind::Home, "No actual home trip");
    w.advance(60);
    require(r.activity == Activity::Sleeping && length(w.position(0) - bed) < .1f,
            "Sleep was not on the actual bed");
  });
  test("School-age residents can use their own supplies without adult career decisions", [] {
    auto w = small();
    quiet(w);
    auto &r = w.residents[0];
    r.age = 12;
    r.hunger = 1.1f;
    strip(w, ItemKind::Food);
    auto meal = add(w, 0, ItemKind::Food);
    require(w.dailyDecision(0) && w.items[meal].consumed && r.role == Role::Unemployed,
            "Child could not use owned food or was assigned an adult job");
  });
  test("A changed budget at station admission clears pending leisure permission", [] {
    auto w = small();
    quiet(w);
    auto f = facility(w, FacilityKind::Arcade);
    auto &venue = w.facilities[f];
    venue.stock = 0;
    venue.price = 100;
    place(w, 0, w.serviceApproach(f));
    fund(w, w.householdReserve(0) + 500);
    w.residentPlans[0].plannedLeisure = f;
    w.joinQueue(0, f);
    fund(w, w.householdReserve(0) + 50);
    venue.stock = 1;
    w.serve(f);
    require(w.residents[0].queuedAt == none && w.residentPlans[0].plannedLeisure == none &&
                w.residentPlans[0].nextRecreation > w.time && venue.stock == 1,
            "Station rejection leaked a discretionary permission or stock");
  });
  test("A due affordable shopping trip precedes discretionary recreation", [] {
    auto w = small();
    quiet(w);
    auto &r = w.residents[0];
    r.happiness = .3f;
    facility(w, FacilityKind::Arcade);
    fund(w, w.householdReserve(0) + 5000);
    w.residentPlans[0].nextShopping = w.time;
    require(step(w, DailyNeed::Recreation).state == DailyState::Waiting &&
                step(w, DailyNeed::Recreation).reason == "daily.provisioning",
            "Leisure displaced due shopping");
  });
  test("A real pending delivery defers leisure until cancellation returns escrow", [] {
    auto w = small();
    quiet(w);
    auto &r = w.residents[0];
    r.happiness = .3f;
    auto shop = facility(w, FacilityKind::Shop);
    facility(w, FacilityKind::Arcade);
    if (!w.hasDevice(0))
      add(w, 0, ItemKind::Phone);
    fund(w, w.householdReserve(0) + 10000);
    auto total = w.internalMoney();
    auto cash = w.accounts[r.account].balance;
    auto order = w.order(0, shop, ItemKind::Food);
    w.residentPlans[0].order = order;
    require(step(w, DailyNeed::Recreation).reason == "daily.provisioning" &&
                w.accounts[r.account].balance < cash && w.internalMoney() == total,
            "Pending paid delivery was displaced or escrow invented money");
    w.cancelOrder(order, 0);
    require(step(w, DailyNeed::Recreation).reason != "daily.provisioning" &&
                w.accounts[r.account].balance == cash && w.internalMoney() == total,
            "Cancellation did not release waiting plan or refund real escrow");
  });
  test("A staffed affordable lesson takes priority over an available preferred arcade", [] {
    auto w = small();
    quiet(w);
    auto &r = w.residents[0];
    r.age = 12;
    r.happiness = .3f;
    auto school = facility(w, FacilityKind::School);
    auto &f = w.facilities[school];
    f.staffNeeded = 1;
    f.stock = 100;
    f.employees.clear();
    w.chooseJob(2, Role::Teacher, school);
    auto &teacher = w.residents[2];
    teacher.routine.shiftStart = 0;
    teacher.routine.shiftHours = 24;
    teacher.routine.daysOff = 0;
    place(w, 2, f.pos + Vec3{0, 0, -1.6f});
    w.clockIn(2);
    require(w.operational(school), "Teacher is not at work");
    facility(w, FacilityKind::Arcade);
    fund(w, w.householdReserve(0) + 10000);
    place(w, 0, w.accessible(f.pos + Vec3{0, 0, 4}));
    require(step(w, DailyNeed::Recreation).state == DailyState::Ready,
            "Fixture has no competing recreation");
    w.decide(0);
    require(r.task == TaskKind::Service && r.target == school &&
                w.residentPlans[0].plannedLeisure == none,
            "Recreation displaced an actual staffed lesson");
  });
  test("An ongoing laundry reservation remains active when it used the last stock", [] {
    auto w = small();
    quiet(w);
    auto f = facility(w, FacilityKind::Laundry);
    w.facilities[f].stock = 1;
    place(w, 0, w.serviceApproach(f));
    auto outfit = w.residents[0].outfit;
    w.items[outfit].cleanliness = .1f;
    w.joinQueue(0, f);
    require(w.facilities[f].stock == 0 && w.items[outfit].reservedAt == f,
            "Laundry did not reserve actual stock and garments");
    auto s = step(w, DailyNeed::Clothing);
    require(s.state == DailyState::Active && s.facility == f && s.reason == "daily.active",
            "Preview treated reserved clothes as missing or reserved stock as unavailable");
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
