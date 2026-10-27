#include "json.hpp"
#include "mega/world.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>
#include <zlib.h>
namespace mega {
using nlohmann::json;
namespace {
constexpr std::size_t rawLimit = 512 * 1024 * 1024, compressedLimit = 100 * 1024 * 1024;
std::string readExport(const std::filesystem::path &path) {
  std::error_code error;
  auto size = std::filesystem::file_size(path, error);
  if (error || !size || size > rawLimit)
    throw std::runtime_error("import.error.file");
  std::ifstream file(path, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(file)), {});
  if (!file.eof() && file.fail())
    throw std::runtime_error("import.error.file");
  if (bytes.size() != size)
    throw std::runtime_error("import.error.file");
  if (static_cast<unsigned char>(bytes[0]) != 0x1f)
    return bytes;
  if (bytes.size() > compressedLimit)
    throw std::runtime_error("import.error.file");
  z_stream stream{};
  if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
    throw std::runtime_error("import.error.gzip");
  struct End {
    z_stream &stream;
    ~End() { inflateEnd(&stream); }
  } end{stream};
  stream.next_in = reinterpret_cast<Bytef *>(bytes.data());
  stream.avail_in = static_cast<uInt>(bytes.size());
  std::array<char, 65536> block;
  std::string raw;
  int result = Z_OK;
  while (result == Z_OK) {
    stream.next_out = reinterpret_cast<Bytef *>(block.data());
    stream.avail_out = static_cast<uInt>(block.size());
    result = inflate(&stream, Z_NO_FLUSH);
    auto written = block.size() - stream.avail_out;
    if (raw.size() > rawLimit - written)
      throw std::runtime_error("import.error.file");
    raw.append(block.data(), written);
  }
  if (result != Z_STREAM_END || stream.avail_in != 0)
    throw std::runtime_error("import.error.gzip");
  return raw;
}
double number(const json &j, double low, double high) {
  if (!j.is_number())
    throw std::runtime_error("import.error.structure");
  double value = j.get<double>();
  if (!std::isfinite(value) || value < low || value > high)
    throw std::runtime_error("import.error.structure");
  return value;
}
std::int64_t whole(const json &j, std::int64_t low, std::int64_t high) {
  double value = number(j, double(low), double(high));
  if (value != std::floor(value))
    throw std::runtime_error("import.error.structure");
  return std::int64_t(value);
}
FacilityKind facilityKind(const std::string &kind) {
  static const std::map<std::string, FacilityKind> kinds = {
      {"food", FacilityKind::Food},       {"water", FacilityKind::Water},
      {"shower", FacilityKind::Shower},   {"clinic", FacilityKind::Infirmary},
      {"leisure", FacilityKind::Leisure}, {"shop", FacilityKind::Shop},
      {"toilet", FacilityKind::Toilet},   {"school", FacilityKind::School},
      {"work", FacilityKind::Office},     {"laundry", FacilityKind::Laundry},
      {"gym", FacilityKind::Gym},         {"library", FacilityKind::Library},
      {"cafe", FacilityKind::Cafe},       {"repair", FacilityKind::Workshop}};
  auto found = kinds.find(kind);
  if (found == kinds.end())
    throw std::runtime_error("import.error.facility");
  return found->second;
}
LocalName browserName(Id id) {
  static const std::array<const char *, 16> last = {"Lin",  "Chen", "Wang", "Li",  "Zhao",  "Zhou",
                                                    "Wu",   "Xu",   "Sun",  "Liu", "Zheng", "Xu",
                                                    "Shen", "Yang", "He",   "Fang"};
  static const std::array<const char *, 16> first = {
      "Zhiyuan",   "Muqing", "Zian",   "Yuning", "Jingxing", "Xiaohe", "Shubai", "Yutong",
      "Mingchuan", "Xingye", "Jingyi", "Zeyu",   "Qinglan",  "Yihang", "Qinghe", "Ruoxi"};
  static const std::array<const char *, 16> lastZh = {"林", "陈", "王", "李", "赵", "周",
                                                      "吴", "徐", "孙", "刘", "郑", "许",
                                                      "沈", "杨", "何", "方"};
  static const std::array<const char *, 16> firstZh = {
      "知远", "沐晴", "子安", "予宁", "景行", "晓禾", "书白", "雨桐",
      "明川", "星野", "静宜", "泽宇", "青岚", "亦航", "清和", "若溪"};
  std::string suffix;
  if (id >= 256) {
    auto n = std::to_string(id + 1);
    suffix = " · " + std::string(std::max(0, 6 - int(n.size())), '0') + n;
  }
  auto roman = std::string(first[(id / 16) % 16]) + " " + last[id % 16] + suffix;
  return {roman, roman, std::string(lastZh[id % 16]) + firstZh[(id / 16) % 16] + suffix};
}
} // namespace
World World::importBrowser(const std::filesystem::path &path, Mode mode) {
  try {
    auto raw = readExport(path);
    auto data = json::parse(raw, [](int depth, json::parse_event_t, json &) {
      if (depth > 64)
        throw std::runtime_error("import.error.structure");
      return true;
    });
    int version = int(whole(data.at("version"), 1, 2));
    int count = int(whole(data.at("count"), 2, 500000));
    int floors = int(whole(data.at("floors"), 1, 2000));
    double minutes = number(data.at("time"), 0, 1e10);
    const auto &arrays = data.at("arrays");
    if (!arrays.is_object())
      throw std::runtime_error("import.error.structure");
    for (const auto &[key, values] : arrays.items()) {
      (void)key;
      if (!values.is_array() || values.size() != std::size_t(count))
        throw std::runtime_error("import.error.structure");
      for (const auto &value : values)
        number(value, -1e15, 1e15);
    }
    auto field = [&](const char *key, int id, double fallback, double low, double high) {
      auto found = arrays.find(key);
      return found == arrays.end() ? fallback : number((*found)[id], low, high);
    };
    for (auto key : {"alive", "present", "home", "bed", "occupation", "activity", "sex"})
      if (arrays.contains(key))
        for (const auto &value : arrays[key]) {
          auto n = number(value, -1e15, 1e15);
          if (n != std::floor(n))
            throw std::runtime_error("import.error.structure");
        }
    for (auto key : {"alive", "home", "bed", "cash", "hunger", "thirst", "energy", "health",
                     "happy", "activity"})
      if (!arrays.contains(key))
        throw std::runtime_error("import.error.structure");
    Config cfg;
    cfg.floors = floors;
    cfg.population = 2; // Native scaffold, not an additional resident population.
    cfg.mode = mode;
    cfg.startingCash = 0;
    cfg.accidentRate = 0;
    cfg.seed = std::uint64_t(whole(data.value("rng", json(1)), 0, 0xffffffff));
    auto scaffold = World::generate(cfg);
    World w;
    w.browserSourcePath = std::filesystem::absolute(path).lexically_normal();
    w.config = cfg;
    w.config.population = count;
    w.config.validate(false);
    w.time = minutes * 60;
    w.rng = cfg.seed ? cfg.seed : 1;
    w.nextAccidentAt = w.time + 3600;
    w.lastEconomyDay = int(w.time / 86400);
    w.floorPlans = scaffold.floorPlans;
    w.accounts.push_back({0, "External economy"});
    w.dormAccount = w.account("Dormitory treasury");
    w.residentHomes.assign(count, none);
    w.residents.reserve(count);
    w.names.reserve(count);
    w.importReport = {{"version", version}, {"residents", count}, {"restarted", 0},
                      {"services", 0},      {"homes", 0},         {"items", 0},
                      {"funds", 0},         {"outsideFunds", 0},  {"infrastructure", 0}};
    auto opening = [&](Id account, Money amount) {
      if (w.importReport["funds"] > 1000000000000000LL - amount)
        throw std::runtime_error("import.error.structure");
      w.pay(0, account, amount, "browser-import-opening-balance");
      w.importReport["funds"] += amount;
    };
    for (int id = 0; id < count; ++id) {
      Resident resident;
      resident.id = id;
      resident.account = w.account("Resident " + std::to_string(id + 1));
      resident.home = int(field("home", id, 0, 0, floors - 1));
      if (field("home", id, 0, 0, floors - 1) != resident.home)
        throw std::runtime_error("import.error.structure");
      resident.alive = field("alive", id, 1, 0, 1) == 1;
      resident.present = field("present", id, 1, 0, 1) == 1;
      resident.sex = int(field("sex", id, id % 2, 0, 1));
      resident.age = int(std::clamp(
          (minutes - field("born", id, minutes - 25 * 525600., -1e10, minutes)) / 525600., 0.,
          150.));
      resident.health = float(field("health", id, 100, 0, 100));
      resident.hunger = float(field("hunger", id, 35, 0, 140) / 100);
      resident.thirst = float(field("thirst", id, 30, 0, 140) / 100);
      resident.energy = float(field("energy", id, 80, 0, 100) / 100);
      resident.hygiene = float(field("hygiene", id, 80, 0, 100) / 100);
      resident.happiness = float(field("happy", id, 50, 0, 100) / 100);
      resident.bladder = float(field("bladder", id, 15, 0, 140) / 100);
      resident.needsAt = w.time;
      resident.aggression = float(field("personality", id, .3, 0, 1) * .65);
      resident.strength = 25 + (id % 30);
      resident.infectedAt = field("disease", id, 0, 0, 10) > 0
                                ? std::max(0., field("infectedAt", id, minutes, -1, minutes) * 60)
                                : -1;
      resident.immuneUntil = std::max(0., field("immuneUntil", id, 0, -1, 1e10) * 60);
      for (std::size_t n = 0; n < resident.interests.size(); ++n)
        resident.interests[n] = float(((id * 37 + n * 13) % 101) / 100.);
      resident.activity = !resident.alive     ? Activity::Dead
                          : !resident.present ? Activity::Departed
                                              : Activity::Idle;
      resident.pos = w.layout(resident.home).landing(id % 4, resident.home * 3.f);
      auto cash = field("cash", id, 0, 0, 1000000000000LL);
      if (cash != std::floor(cash))
        throw std::runtime_error("import.error.structure");
      if (resident.present)
        opening(resident.account, Money(cash));
      else
        w.importReport["outsideFunds"] += Money(cash);
      auto name = browserName(id);
      resident.name = name.en;
      w.residents.push_back(std::move(resident));
      w.names.push_back(std::move(name));
      if (field("activity", id, 0, 0, 11) != 0)
        ++w.importReport["restarted"];
    }
    if (data.contains("customNames")) {
      if (!data["customNames"].is_array() || data["customNames"].size() > std::size_t(count))
        throw std::runtime_error("import.error.structure");
      for (const auto &pair : data["customNames"]) {
        if (!pair.is_array() || pair.size() != 2)
          throw std::runtime_error("import.error.structure");
        Id id = Id(whole(pair[0], 0, count - 1));
        auto text = pair[1].get<std::string>();
        if (text.size() > 256)
          throw std::runtime_error("import.error.structure");
        w.names[id] = {text, text, text};
        w.residents[id].name = text;
      }
    }
    const auto &savedFacilities = data.at("facilities");
    if (!savedFacilities.is_array() || savedFacilities.size() > 100000)
      throw std::runtime_error("import.error.structure");
    for (const auto &old : savedFacilities) {
      Facility facility;
      facility.id = Id(w.facilities.size());
      if (whole(old.at("id"), 0, 100000) != facility.id)
        throw std::runtime_error("import.error.structure");
      facility.kind = facilityKind(old.at("type").get<std::string>());
      facility.floor = int(whole(old.at("floor"), 0, floors - 1));
      facility.owner = w.account("Imported business " + std::to_string(facility.id + 1));
      opening(facility.owner, whole(old.at("cash"), 0, 1000000000000LL));
      facility.pos = w.layout(facility.floor)
                         .facility(facility.floor, int(w.floorFacilities[facility.floor].size()));
      facility.stations = int(whole(old.at("servers"), 1, 256));
      facility.stock = int(whole(old.at("inventory"), 0, 100000000));
      facility.price = whole(old.at("price"), 0, 1000000000);
      facility.duration = number(old.at("duration"), .001, 1440) * 60;
      facility.open = !old.value("closed", false) && !old.value("retiring", false);
      facility.demolished = old.value("retiring", false);
      facility.condition = float(number(old.value("quality", json(.8)), 0, 1));
      facility.opensAt = number(old.value("open", json(0)), 0, 1440) / 60;
      double closes = number(old.value("close", json(1440)), 0, 1440) / 60;
      facility.openHours =
          closes >= facility.opensAt ? closes - facility.opensAt : 24 - facility.opensAt + closes;
      if (facility.opensAt == 24)
        facility.opensAt = 0;
      w.floorFacilities[facility.floor].push_back(facility.id);
      w.facilities.push_back(std::move(facility));
    }
    // These systems did not exist in the browser. Their buildings have no invented
    // startup cash or stock; owners must fund supplies and actual workers.
    for (auto facility : scaffold.facilities)
      if (facility.kind == FacilityKind::Reception || facility.kind == FacilityKind::Garbage ||
          facility.kind == FacilityKind::Jail || facility.kind == FacilityKind::Morgue ||
          facility.kind == FacilityKind::Crematorium ||
          facility.kind == FacilityKind::FireStation) {
        facility.id = Id(w.facilities.size());
        facility.owner = w.dormAccount;
        facility.manager = none;
        facility.queue.clear();
        facility.serving.clear();
        facility.employees.clear();
        facility.stock = 0;
        facility.pos = w.layout(facility.floor)
                           .facility(facility.floor, int(w.floorFacilities[facility.floor].size()));
        w.floorFacilities[facility.floor].push_back(facility.id);
        w.facilities.push_back(std::move(facility));
        ++w.importReport["infrastructure"];
      }
    auto housing = data.value("housing", json::object());
    constexpr int beds = 208;
    const std::size_t units = std::size_t(floors) * beds;
    bool oldHousing = !housing.empty();
    if (oldHousing) {
      for (auto key : {"owners", "tenants", "prices", "rents", "listed"})
        if (!housing.at(key).is_array() || housing.at(key).size() != units)
          throw std::runtime_error("import.error.structure");
      if (housing.contains("available") &&
          (!housing["available"].is_array() || housing["available"].size() != units))
        throw std::runtime_error("import.error.structure");
      opening(w.dormAccount, whole(housing.at("cash"), 0, 1000000000000LL));
    }
    auto ownerAccount = [&](Id id) { return id < 0 ? w.dormAccount : w.residents.at(id).account; };
    std::vector<Id> inferredTenants(oldHousing ? 0 : units, none);
    if (!oldHousing)
      for (const auto &r : w.residents) {
        int bed = int(field("bed", r.id, -1, -1, beds - 1));
        if (r.alive && r.present && bed >= 0) {
          auto unit = r.home * beds + bed;
          if (inferredTenants[unit] != none)
            throw std::runtime_error("import.error.housing");
          inferredTenants[unit] = r.id;
        }
      }
    std::set<Id> occupied;
    for (std::size_t u = 0; u < units; ++u) {
      HousingUnit home;
      home.floor = int(u / beds);
      home.bed = int(u % beds);
      home.owner = w.dormAccount;
      home.price = 30000;
      home.rent = 17;
      home.custom = !w.layout(home.floor).residential(home.floor, floors);
      home.location = home.custom ? Vec3{80.f + (home.bed % 16) * 2.f, home.floor * 3.f,
                                         -45.f + (home.bed / 16) * 7.f}
                                  : w.layout(home.floor).bed(home.floor, home.bed);
      home.paidUntil = (std::floor(w.time / 86400) + 1) * 86400;
      if (oldHousing) {
        home.owner = ownerAccount(Id(whole(housing["owners"][u], -1, count - 1)));
        home.tenant = Id(whole(housing["tenants"][u], -1, count - 1));
        home.price = whole(housing["prices"][u], 0, 2000000000);
        home.rent = (whole(housing["rents"][u], 0, 2000000000) + 29) / 30;
        home.listed = whole(housing["listed"][u], 0, 1) == 1;
        home.active = !housing.contains("available") || whole(housing["available"][u], 0, 1) == 1;
      } else {
        home.tenant = inferredTenants[u];
      }
      if (home.tenant != none) {
        auto &resident = w.residents[home.tenant];
        if (!resident.alive || !resident.present)
          home.tenant = none;
        else {
          if (!home.active || !occupied.insert(resident.id).second || resident.home != home.floor ||
              field("bed", resident.id, -1, -1, beds - 1) != home.bed)
            throw std::runtime_error("import.error.housing");
          w.residentHomes[resident.id] = Id(u);
          resident.pos = home.location;
        }
      }
      if (home.active)
        ++w.importReport["homes"];
      w.floorHomes[home.floor].push_back(Id(u));
      w.housing.push_back(std::move(home));
    }
    Id firstLiving = none;
    for (auto &resident : w.residents)
      if (resident.alive && resident.present) {
        firstLiving = resident.id;
        break;
      }
    w.player = firstLiving == none ? 0 : firstLiving;
    if (data.contains("player") && data["player"].is_object()) {
      Id player = Id(whole(data["player"].at("id"), 0, count - 1));
      if (w.residents[player].alive && w.residents[player].present)
        w.player = player;
    }
    w.dormOwner = mode == Mode::Manager ? w.player : firstLiving;
    if (w.dormOwner == none)
      w.dormOwner = 0;
    if (mode != Mode::Manager)
      for (auto &resident : w.residents)
        if (resident.alive && resident.present && resident.id != w.player) {
          w.dormOwner = resident.id;
          break;
        }
    for (auto &resident : w.residents) {
      resident.automatic = resident.id != w.player;
      int occupation = int(field("occupation", resident.id, 0, 0, 4));
      if (resident.alive && resident.present && resident.age >= 18 && occupation >= 2) {
        Id site = w.nearest(resident.id, FacilityKind::Office);
        if (site != none) {
          resident.role = occupation == 2   ? Role::VideoEditor
                          : occupation == 3 ? Role::Programmer
                                            : Role::Administrator;
          resident.jobSite = site;
          resident.employer = w.facilities[site].owner;
          resident.hourlyWage = Money(field("wage", resident.id, 900, 0, 100000000) / 3);
        }
      }
    }
    w.setupRoutines();
    w.refreshManagement();
    w.setupOffices();
    w.setupCompanies();
    // Supply and emergency providers acquire vehicles through the native system.
    w.generateMedical();
    for (auto &resident : w.residents) {
      resident.routine.workSince = -1;
      resident.routine.unpaidWages = 0;
      if (!resident.alive || !resident.present)
        continue;
      resident.route.clear();
      resident.task = TaskKind::None;
      resident.target = none;
      resident.pos = w.residentHomes[resident.id] != none
                         ? w.homePosition(w.residentHomes[resident.id])
                         : w.layout(resident.home).landing(resident.id % 4, resident.home * 3.f);
      int activity = int(field("activity", resident.id, 0, 0, 11));
      resident.activity = (activity == 6 || activity == 11) && w.residentHomes[resident.id] != none
                              ? Activity::Sleeping
                              : Activity::Idle;
      if (resident.activity != Activity::Sleeping) {
        Id home = w.residentHomes[resident.id];
        float stagger = home != none && !w.housing[home].custom &&
                                w.housing[home].bed >= w.layout(resident.home).tierSize()
                            ? .4f
                            : -.4f;
        resident.pos = w.accessible(resident.pos + Vec3{1.5f, 0, stagger});
      }
    }
    if (oldHousing && housing.contains("itemOwners")) {
      const auto &owners = housing["itemOwners"];
      if (!owners.is_array() || owners.size() > 2000000)
        throw std::runtime_error("import.error.structure");
      for (auto key : {"itemKinds", "itemPrices", "itemListed"})
        if (!housing.at(key).is_array() || housing.at(key).size() != owners.size())
          throw std::runtime_error("import.error.structure");
      if (housing.contains("itemCondition") && (!housing["itemCondition"].is_array() ||
                                                housing["itemCondition"].size() != owners.size()))
        throw std::runtime_error("import.error.structure");
      static constexpr ItemKind kinds[] = {ItemKind::Phone,      ItemKind::Fan,
                                           ItemKind::Clothes,    ItemKind::Tableware,
                                           ItemKind::RiceCooker, ItemKind::Food};
      for (std::size_t n = 0; n < owners.size(); ++n) {
        Id owner = Id(whole(owners[n], -2, count - 1));
        int kind = int(whole(housing["itemKinds"][n], 0, 5));
        Item item;
        item.id = Id(w.items.size());
        item.kind = kinds[kind];
        item.owner = ownerAccount(owner);
        item.durability = housing.contains("itemCondition")
                              ? float(number(housing["itemCondition"][n], 0, 1))
                              : 1;
        item.cleanliness = kind == 2 ? item.durability : 1;
        if (kind == 2)
          item.durability = 1;
        item.consumed = owner < 0 || (owner >= 0 && !w.residents[owner].present);
        if (!item.consumed) {
          item.carrier = w.residents[owner].alive ? owner : none;
          item.pos = w.residents[owner].pos;
          w.residents[owner].items.push_back(item.id);
          if (kind == 2)
            w.residents[owner].outfit = item.id;
          item.askingPrice = whole(housing["itemPrices"][n], 0, 2000000000);
          if (whole(housing["itemListed"][n], 0, 1) == 1)
            item.seller = owner;
          else
            item.askingPrice = 0;
        }
        w.items.push_back(item);
      }
    }
    if (!oldHousing) {
      static constexpr ItemKind legacyKinds[] = {ItemKind::Phone, ItemKind::Fan, ItemKind::Clothes,
                                                 ItemKind::Tableware, ItemKind::RiceCooker};
      for (auto &r : w.residents) {
        Item item;
        item.id = Id(w.items.size());
        item.owner = r.account;
        item.kind = legacyKinds[r.id % 5];
        item.consumed = !r.present;
        item.carrier = r.alive && r.present ? r.id : none;
        item.pos = r.pos;
        if (!item.consumed)
          r.items.push_back(item.id);
        if (item.kind == ItemKind::Clothes && !item.consumed)
          r.outfit = item.id;
        w.items.push_back(item);
      }
    }
    w.importReport["items"] = std::int64_t(w.items.size());
    std::set<Id> reserved;
    for (const auto &old : savedFacilities) {
      Id fid = old["id"].get<Id>();
      auto &facility = w.facilities[fid];
      auto reserve = [&](Id id) {
        auto &r = w.residents[id];
        if (!r.alive || !r.present || !reserved.insert(id).second)
          throw std::runtime_error("import.error.queue");
        r.route.clear();
        ++r.routeVersion;
        r.task = TaskKind::None;
        r.target = none;
        r.queuedAt = fid;
        r.routine.workSince = -1;
        r.activity = Activity::Queuing;
        r.waitSince = w.time;
      };
      const auto &active = old.at("active");
      const auto &queue = old.at("queue");
      if (!active.is_array() || active.size() > std::size_t(facility.stations) ||
          !queue.is_array() || queue.size() > 64)
        throw std::runtime_error("import.error.queue");
      for (const auto &service : active) {
        Id id = Id(whole(service.at("id"), 0, count - 1));
        reserve(id);
        int station = int(facility.serving.size());
        double left = std::max(.01, (number(service.at("end"), 0, 1e10) - minutes) * 60);
        whole(service.at("paid"), 0, 1000000000);
        // The browser has already charged this service. Preserve its remaining
        // progress without a second charge; future services use native settlement.
        facility.serving.push_back({id, -1, left, 0, station});
        w.residents[id].activity = Activity::Using;
        w.residents[id].pos = w.servicePosition(fid, station);
        ++w.importReport["services"];
      }
      for (const auto &entry : queue) {
        Id id = Id(whole(entry.at("id"), 0, count - 1));
        reserve(id);
        w.residents[id].waitSince = std::min(w.time, number(entry.at("arrival"), 0, 1e10) * 60);
        w.residents[id].pos = w.accessible(facility.pos + Vec3{0, 0, 4});
        facility.queue.push_back(id);
      }
      if (!facility.queue.empty()) {
        w.layoutQueue(fid);
        for (Id id : facility.queue) {
          auto &r = w.residents[id];
          r.pos = r.route.back().p;
          r.route.clear();
          ++r.routeVersion;
        }
      }
    }
    if (data.contains("friendships")) {
      const auto &friends = data["friendships"];
      if (!friends.is_array() || friends.size() > 1000000)
        throw std::runtime_error("import.error.structure");
      for (const auto &pair : friends) {
        if (!pair.is_array() || pair.size() != 2)
          throw std::runtime_error("import.error.structure");
        auto key = pair[0].get<std::string>();
        auto colon = key.find(':');
        if (colon == std::string::npos || key.size() > 24)
          throw std::runtime_error("import.error.structure");
        auto parseId = [&](const std::string &s) {
          if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("import.error.structure");
          auto n = std::stoll(s);
          if (n >= count)
            throw std::runtime_error("import.error.structure");
          return Id(n);
        };
        Id a = parseId(key.substr(0, colon)), b = parseId(key.substr(colon + 1));
        if (a == b)
          continue;
        if (a > b)
          std::swap(a, b);
        float trust = float(std::clamp(number(pair[1], 0, 1e9) / 10, 0., 1.));
        w.relationships[(std::uint64_t(a) << 32) | std::uint32_t(b)] = {a, b, trust, w.time, 0};
      }
    }
    json archive = {{"version", version}, {"time", minutes}};
    for (auto key : {"births", "deaths", "marriages", "immigrations", "emigrations", "ledger",
                     "history", "logs", "params", "causes"})
      if (data.contains(key))
        archive[key] = data[key];
    for (auto key : {"partner", "mother", "father", "pregnant", "sex", "born", "died", "social",
                     "earned", "spent", "departed", "queueTolerance", "priceSensitivity"})
      if (arrays.contains(key))
        archive["arrays"][key] = arrays[key];
    w.browserArchive = archive.dump();
    if (w.browserArchive.size() > 64 * 1024 * 1024)
      throw std::runtime_error("import.error.file");
    for (auto &r : w.residents) {
      if (r.alive && r.present)
        w.schedule(w.time + 1 + w.random() * 60, EventKind::Decide, r.id);
      else if (!r.alive) {
        Remains remains;
        remains.resident = r.id;
        remains.pos = r.pos;
        remains.diedAt = std::max(0., field("died", r.id, minutes, -1, minutes) * 60);
        remains.cremated = true;
        w.remains.push_back(remains);
      }
    }
    if (firstLiving == none) {
      w.gameOver = true;
      w.endReason = "population-empty";
    }
    w.schedule(w.time + 60, EventKind::FacilityTick, 0);
    w.schedule(w.time + 3600, EventKind::Payroll, 0);
    w.log("import.completed");
    w.validate();
    return w;
  } catch (const json::exception &) {
    throw std::runtime_error("import.error.format");
  } catch (const std::invalid_argument &) {
    throw std::runtime_error("import.error.structure");
  } catch (const std::out_of_range &) {
    throw std::runtime_error("import.error.structure");
  }
}
} // namespace mega
