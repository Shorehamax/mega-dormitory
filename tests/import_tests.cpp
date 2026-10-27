#include "json.hpp"
#include "mega/world.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <zlib.h>
using namespace mega;
using nlohmann::json;
static int passed = 0, failed = 0;
static void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
template <class F> static void rejects(F fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "Invalid import/action was accepted");
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
static json fixture(int count = 8, int floors = 2) {
  json j = {{"version", 2},
            {"count", count},
            {"floors", floors},
            {"time", 480},
            {"rng", 20261026},
            {"player", {{"id", 0}}},
            {"customNames", json::array({json::array({0, "Élodie 龍"})})},
            {"friendships", json::array({json::array({"0:1", 5})})},
            {"births", 2},
            {"logs", json::array()}};
  auto &a = j["arrays"];
  for (auto [key, value] : std::vector<std::pair<std::string, double>>{
           {"alive", 1},        {"present", 1},    {"home", 0},      {"bed", 0},
           {"cash", 1000},      {"hunger", 60},    {"thirst", 20},   {"energy", 70},
           {"health", 100},     {"happy", 65},     {"activity", 0},  {"born", 480 - 25 * 525600.},
           {"personality", .3}, {"occupation", 0}, {"wage", 9000},   {"partner", -1},
           {"mother", -1},      {"father", -1},    {"pregnant", -1}, {"sex", 1}})
    a[key] = std::vector<double>(count, value);
  for (int i = 0; i < count; ++i) {
    a["home"][i] = i / 208;
    a["bed"][i] = i % 208;
  }
  auto &h = j["housing"];
  int units = floors * 208;
  h = {{"owners", std::vector<int>(units, -1)},
       {"tenants", std::vector<int>(units, -1)},
       {"available", std::vector<int>(units, 1)},
       {"prices", std::vector<int>(units, 30000)},
       {"rents", std::vector<int>(units, 600)},
       {"listed", std::vector<int>(units, 1)},
       {"cash", 1000},
       {"itemOwners", json::array()},
       {"itemKinds", json::array()},
       {"itemPrices", json::array()},
       {"itemListed", json::array()},
       {"itemCondition", json::array()}};
  for (int i = 0; i < count; ++i)
    h["tenants"][i] = i;
  h["owners"][0] = 2;
  for (int i = 0; i < 6; ++i) {
    h["itemOwners"].push_back(i);
    h["itemKinds"].push_back(i);
    h["itemPrices"].push_back(1000 + i);
    h["itemListed"].push_back(i % 2);
    h["itemCondition"].push_back(.9);
  }
  auto &f = j["facilities"];
  f = json::array();
  for (auto type : {"food", "water", "work", "clinic"})
    f.push_back({{"id", f.size()},
                 {"type", type},
                 {"floor", 0},
                 {"servers", 4},
                 {"duration", 8},
                 {"inventory", 100},
                 {"price", 500},
                 {"cash", 100000},
                 {"quality", .9},
                 {"open", 0},
                 {"close", 1440},
                 {"closed", false},
                 {"active", json::array()},
                 {"queue", json::array()}});
  f[0]["active"].push_back({{"id", 0}, {"end", 490}, {"paid", 500}});
  f[0]["queue"].push_back({{"id", 1}, {"arrival", 475}});
  return j;
}
static std::filesystem::path file(const json &j, bool compressed = false) {
  auto path = std::filesystem::temp_directory_path() /
              (compressed ? "mega-import-fixture.gz" : "mega-import-fixture.json");
  auto text = j.dump();
  if (compressed) {
    gzFile out = gzopen(path.c_str(), "wb");
    require(out != nullptr, "gzip fixture open failed");
    require(gzwrite(out, text.data(), unsigned(text.size())) == int(text.size()),
            "gzip fixture write failed");
    gzclose(out);
  } else {
    std::ofstream out(path, std::ios::binary);
    out << text;
  }
  return path;
}
static World imported(json j = fixture()) { return World::importBrowser(file(j)); }
static Id addItem(World &w, Id resident, ItemKind kind) {
  Item i;
  i.id = Id(w.items.size());
  i.owner = w.residents[resident].account;
  i.carrier = resident;
  i.kind = kind;
  i.pos = w.position(resident);
  w.items.push_back(i);
  w.residents[resident].items.push_back(i.id);
  return i.id;
}
static World homeWorld() {
  auto j = fixture();
  j["facilities"][0]["active"] = json::array();
  j["facilities"][0]["queue"] = json::array();
  auto w = imported(j);
  while (!w.events.empty())
    w.events.pop();
  for (auto &r : w.residents) {
    r.automatic = false;
    r.activity = Activity::Idle;
    r.route.clear();
    r.queuedAt = none;
  }
  w.residents[0].pos = w.homePosition(w.residentHomes[0]);
  return w;
}
int main() {
  test("Browser import preserves IDs, localized names, property and exact opening money", [] {
    auto w = imported();
    require(w.residents.size() == 8 && w.names[0].en == "Élodie 龍" && w.names[1].zh == "陈知远" &&
                w.names[1].en == "Zhiyuan Chen",
            "Names or IDs changed");
    require(w.residentHomes[0] == 0 && w.housing[0].owner == w.residents[2].account &&
                w.housing[0].rent == 20,
            "Property title/tenant or monthly-to-daily rent changed");
    require(w.internalMoney() == 409000 && w.importReport.at("funds") == 409000,
            "Import invented or lost money");
    require(w.residents[0].sex == 1 && w.trust(0, 1) == .5f,
            "Stable personal identity/relationship lost");
    require(w.items[1].kind == ItemKind::Fan && w.items[3].kind == ItemKind::Tableware &&
                w.items[4].kind == ItemKind::RiceCooker,
            "Personal possessions changed type");
    require(w.facilities[0].queue == std::vector<Id>{1} && w.residents[1].queuedAt == 0 &&
                w.facilities[0].serving[0].resident == 0,
            "FIFO or active service lost");
    require(w.facilities[0].serving[0].price == 0 && w.facilities[0].serving[0].remaining == 600,
            "Prepaid service would be charged twice or lose progress");
    for (auto &f : w.facilities)
      if (f.id >= 4)
        require(w.accounts[f.owner].balance == 1000 && f.stock == 0,
                "Native scaffold got phantom funding/stock");
    w.validate();
  });
  test("Gzip source is unchanged and matches JSON conversion", [] {
    auto path = file(fixture(), true);
    std::ifstream before(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(before)), {});
    auto w = World::importBrowser(path);
    rejects([&] { w.save(path); });
    std::ifstream after(path, std::ios::binary);
    std::string untouched((std::istreambuf_iterator<char>(after)), {});
    require(bytes == untouched && w.internalMoney() == imported().internalMoney(),
            "Importer modified its source or gzip changed state");
  });
  test("Converted state and historical records survive native save/load", [] {
    auto j = fixture();
    j["arrays"]["pregnant"][2] = 700;
    j["arrays"]["partner"][2] = 3;
    auto w = imported(j);
    auto path = std::filesystem::temp_directory_path() / "mega-import-native.mdsave";
    w.save(path);
    auto loaded = World::load(path);
    require(loaded.browserArchive == w.browserArchive && loaded.importReport == w.importReport &&
                loaded.items.size() == w.items.size(),
            "Native roundtrip lost migration data");
    require(json::parse(loaded.browserArchive)["arrays"]["pregnant"][2] == 700 &&
                loaded.facilities[0].serving[0].price == 0,
            "Archived pregnancy or prepaid marker lost");
    for (auto suffix : {"", ".bak", ".meta.json", ".bak.meta.json"})
      std::filesystem::remove(path.string() + suffix);
  });
  test("Prepaid service completes without taking money again", [] {
    auto w = imported();
    while (!w.events.empty())
      w.events.pop();
    for (auto &r : w.residents)
      r.automatic = false;
    w.leaveQueue(1);
    Id chef = none;
    for (auto &r : w.residents)
      if (r.jobSite == 0 && r.role == Role::Chef && w.onShift(r.id)) {
        chef = r.id;
        break;
      }
    require(chef != none, "No real chef assigned");
    w.residents[chef].pos = w.facilities[0].pos;
    w.residents[chef].activity = Activity::Idle;
    w.clockIn(chef);
    auto wallet = w.accounts[w.residents[0].account].balance,
         owner = w.accounts[w.facilities[0].owner].balance;
    w.refreshServices();
    w.advance(600);
    w.validate();
    require(w.residents[0].queuedAt == none &&
                w.accounts[w.residents[0].account].balance == wallet &&
                w.accounts[w.facilities[0].owner].balance == owner,
            "Prepaid service failed/double charged");
  });
  test("Departed cash stays outside; historical dead/absent residents are not hired", [] {
    auto j = fixture();
    j["arrays"]["alive"][6] = 0;
    j["arrays"]["present"][7] = 0;
    j["arrays"]["cash"][7] = 7000;
    auto w = imported(j);
    require(w.living() == 6 && w.accounts[w.residents[7].account].balance == 0 &&
                w.importReport.at("outsideFunds") == 7000,
            "Outside money counted as internal wealth");
    for (auto &f : w.facilities)
      for (Id id : f.employees)
        require(id != 6 && id != 7, "Dead/departed worker hired");
    require(w.remains.size() == 1 && w.remains[0].cremated,
            "Historical death created a fresh corpse");
    w.validate();
  });
  test("Version-one exports without market state infer distinct homes in linear time", [] {
    auto j = fixture(400);
    j["version"] = 1;
    j.erase("housing");
    j.erase("player");
    j["arrays"].erase("present");
    j["facilities"][0]["active"] = json::array();
    j["facilities"][0]["queue"] = json::array();
    auto w = imported(j);
    require(w.living() == 400 && w.housing[w.residentHomes[399]].tenant == 399 &&
                w.items.size() == 400,
            "Legacy homes/items not recovered");
    w.validate();
    require(w.housing[0].custom && length(w.homePosition(0) - w.homePosition(128)) > 1,
            "Custom two-tier conversion overlapped beds");
  });
  test("Malformed sizes, flags, references, queues and facilities are rejected", [] {
    auto bad = [](const std::function<void(json &)> &change) {
      auto j = fixture();
      change(j);
      rejects([&] { imported(j); });
    };
    bad([](auto &j) { j["version"] = 3; });
    bad([](auto &j) { j["count"] = 500001; });
    bad([](auto &j) { j["arrays"]["cash"] = json::array({0}); });
    bad([](auto &j) { j["arrays"]["alive"][0] = .5; });
    bad([](auto &j) { j["arrays"]["home"][0] = 99; });
    bad([](auto &j) { j["arrays"]["cash"][0] = -1; });
    bad([](auto &j) { j["housing"]["owners"][0] = 99; });
    bad([](auto &j) { j["housing"]["tenants"][1] = 0; });
    bad([](auto &j) { j["facilities"][0]["type"] = "unknown"; });
    bad([](auto &j) { j["facilities"][0]["queue"].push_back({{"id", 0}, {"arrival", 475}}); });
    bad([](auto &j) { j["friendships"] = json::array({json::array({"0:999", 5})}); });
  });
  test("Damaged gzip and oversized files are rejected before conversion", [] {
    auto path = file(fixture(), true);
    auto size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size - 4);
    rejects([&] { World::importBrowser(path); });
    path = std::filesystem::temp_directory_path() / "mega-import-oversized.json";
    {
      std::ofstream out(path);
      out << "{}";
    }
    std::filesystem::resize_file(path, 513ULL * 1024 * 1024);
    rejects([&] { World::importBrowser(path); });
    std::filesystem::remove(path);
  });
  test("Timed cooking locks real food, completes once, creates waste and wears equipment", [] {
    auto w = homeWorld();
    auto cooker = addItem(w, 0, ItemKind::RiceCooker), food = addItem(w, 0, ItemKind::Food);
    auto cash = w.internalMoney();
    w.usePersonalItem(0, cooker);
    require(w.itemInUse(food) && !w.items[food].consumed,
            "Cooking consumed its ingredient before completing");
    rejects([&] { w.listItem(0, food, 500); });
    rejects([&] { w.consumeItem(0, food); });
    w.time += 450;
    w.personalUseTick();
    require(!w.items[food].consumed && w.personalUses.at(0).remaining == 450,
            "Cooking ignored elapsed time");
    w.time += 450;
    w.personalUseTick();
    require(w.items[food].consumed && w.residents[0].personalWaste == 1 &&
                w.items[cooker].durability < 1 && w.personalUses.empty(),
            "Cooking failed real inventory/wear/waste settlement");
    auto waste = w.residents[0].personalWaste;
    w.personalUseTick();
    require(w.residents[0].personalWaste == waste && cash == w.internalMoney(),
            "Cooking rewarded twice or invented money");
    w.validate();
  });
  test("Personal use requires ownership, home access, working tools and a real ingredient", [] {
    auto w = homeWorld();
    auto dish = addItem(w, 0, ItemKind::Tableware);
    rejects([&] { w.usePersonalItem(0, dish); });
    auto food = addItem(w, 0, ItemKind::Food);
    (void)food;
    rejects([&] { w.usePersonalItem(1, dish); });
    w.items[dish].durability = 0;
    rejects([&] { w.usePersonalItem(0, dish); });
    w.items[dish].durability = 1;
    w.residents[0].pos = w.residents[0].pos + Vec3{3, 0, 0};
    rejects([&] { w.usePersonalItem(0, dish); });
  });
  test("Moving, theft, cancellation and death interrupt personal activity without duplicating food",
       [] {
         auto w = homeWorld();
         auto cooker = addItem(w, 0, ItemKind::RiceCooker), food = addItem(w, 0, ItemKind::Food);
         w.usePersonalItem(0, cooker);
         w.stopPersonalUse(0);
         require(!w.items[food].consumed && !w.itemInUse(food), "Canceled cooking lost food");
         w.usePersonalItem(0, cooker);
         w.residents[0].pos = w.residents[0].pos + Vec3{2, 0, 0};
         w.time += 60;
         w.personalUseTick();
         require(w.personalUses.empty() && !w.items[food].consumed,
                 "Moving away still cooked food");
         w.residents[0].pos = w.homePosition(w.residentHomes[0]);
         w.usePersonalItem(0, cooker);
         w.items[food].carrier = 1;
         w.time += 60;
         w.personalUseTick();
         require(w.personalUses.empty() && !w.items[food].consumed,
                 "Lost ingredient was consumed remotely");
         w.items[food].carrier = 0;
         w.usePersonalItem(0, cooker);
         w.kill(0);
         require(w.personalUses.empty() && !w.items[food].consumed,
                 "Death left a paid/rewarding personal task");
         w.validate();
       });
  test("Fan use, native save/load and NPC meals retain timed progress", [] {
    auto w = homeWorld();
    auto fan = addItem(w, 0, ItemKind::Fan);
    w.residents[0].energy = .4f;
    w.usePersonalItem(0, fan);
    w.time += 600;
    w.personalUseTick();
    auto path = std::filesystem::temp_directory_path() / "mega-personal-use.mdsave";
    w.save(path);
    auto b = World::load(path);
    require(b.personalUses.at(0).remaining == 600, "Partial fan rest lost progress");
    b.time += 600;
    b.personalUseTick();
    require(b.personalUses.empty() && b.residents[0].energy > .4f, "Fan rest was not applied");
    auto dish = addItem(b, 1, ItemKind::Tableware);
    auto food = addItem(b, 1, ItemKind::Food);
    b.residents[1].pos = b.homePosition(b.residentHomes[1]);
    b.residents[1].activity = Activity::Idle;
    b.residents[1].automatic = true;
    b.residents[1].hunger = .8f;
    b.residents[1].thirst = .2f;
    b.decide(1);
    require(b.personalUses.contains(1) && b.personalUses.at(1).item == dish &&
                b.personalUses.at(1).ingredient == food,
            "NPC ignored its usable household possessions");
    b.validate();
    for (auto suffix : {"", ".bak", ".meta.json", ".bak.meta.json"})
      std::filesystem::remove(path.string() + suffix);
  });
  test("New household orders require real paid pickup and handover", [] {
    auto j = fixture();
    j["facilities"][0]["active"] = json::array();
    j["facilities"][0]["queue"] = json::array();
    auto shop = j["facilities"][0];
    shop["id"] = 4;
    shop["type"] = "shop";
    j["facilities"].push_back(shop);
    auto w = imported(j);
    for (auto &r : w.residents)
      r.automatic = false;
    w.facilities[4].staffNeeded = 0;
    w.pay(0, w.residents[0].account, 30000, "test-capital");
    w.chooseJob(2, Role::Courier, 4);
    auto before = w.internalMoney();
    for (auto kind : {ItemKind::Fan, ItemKind::Tableware, ItemKind::RiceCooker}) {
      Id order = w.order(0, 4, kind);
      Id item = w.orders[order].item;
      require(w.items[item].carrier == none && w.orders[order].state == OrderState::Waiting,
              "Purchase teleported a gadget into inventory");
      w.assignDelivery(order, 2);
      rejects([&] { w.deliver(order, 2); });
      w.residents[2].route.clear();
      w.residents[2].pos = w.facilities[4].pos;
      w.pickup(order, 2);
      require(w.items[item].carrier == 2, "Courier failed to collect the real gadget");
      w.residents[2].route.clear();
      w.residents[2].pos = w.position(0);
      w.deliver(order, 2);
      require(w.items[item].carrier == 0 && w.items[item].owner == w.residents[0].account &&
                  w.items[item].kind == kind && w.accounts[w.orders[order].escrow].balance == 0,
              "Handover lost ownership or escrow settlement");
    }
    require(w.internalMoney() == before, "Delivery created money");
    w.validate();
  });
  test("Actual robbery releases reserved cooking ingredients before transfer", [] {
    auto w = homeWorld();
    auto cooker = addItem(w, 0, ItemKind::RiceCooker), food = addItem(w, 0, ItemKind::Food);
    w.residents[0].aggression = 0;
    w.residents[0].strength = 10;
    w.residents[1].pos = w.position(0);
    w.residents[1].strength = 100;
    w.usePersonalItem(0, cooker);
    require(w.stealItem(1, 0, food, true), "Overpowering robbery failed");
    require(w.personalUses.empty() && w.items[food].owner == w.residents[1].account &&
                !w.items[food].consumed,
            "Robbery left the victim's recipe active or destroyed the ingredient");
    w.validate();
  });
  test("Malformed personal-use state is rejected", [] {
    auto w = homeWorld();
    auto fan = addItem(w, 0, ItemKind::Fan);
    w.usePersonalItem(0, fan);
    w.personalUses[0].remaining = -1;
    rejects([&] { w.validate(); });
    w.personalUses[0].remaining = 1200;
    w.personalUses[0].item = 999999;
    rejects([&] { w.validate(); });
  });
  for (auto name : {"mega-import-fixture.json", "mega-import-fixture.gz"})
    std::filesystem::remove(std::filesystem::temp_directory_path() / name);
  std::cout << passed << " passed; " << failed << " failed\n";
  return failed ? 1 : 0;
}
