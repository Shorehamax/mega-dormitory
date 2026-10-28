#include "mega/world.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numeric>
namespace mega {
void Config::validate(bool generation) const {
  if (floors < 1 || floors > 10000 || population < 2 || population > 500000)
    throw std::runtime_error("World size outside supported limits (1–10,000 "
                             "floors; 2–500,000 residents)");
  if (!std::isfinite(dayMinutes) || dayMinutes < 5 || dayMinutes > 240 ||
      !std::isfinite(scarcity) || scarcity < .1f || scarcity > 3 || bedsPerFloor < 8 ||
      bedsPerFloor > (layoutVersion == 1 ? 512 : 256) || startingCash < 0 ||
      startingCash > 100000000 || !std::isfinite(aggression) || aggression < 0 || aggression > 1 ||
      !std::isfinite(supplies) || supplies < .1f || supplies > 5 || wardensPerThousand < 0 ||
      wardensPerThousand > 100 || !std::isfinite(accidentRate) || accidentRate < 0 ||
      accidentRate > 10)
    throw std::runtime_error("Invalid generation settings");
  if (mode < Mode::Survival || mode > Mode::Cheat)
    throw std::runtime_error("Invalid game mode");
  if (layoutVersion < 1 || layoutVersion > 2 || floorWidth < 96 || floorDepth < 72 ||
      lobbyWidth < floorWidth || lobbyDepth < floorDepth || lobbyWidth > 2048 || lobbyDepth > 2048)
    throw std::runtime_error("Floor dimensions must be 96–2,048 m wide, 72–2,048 m deep, with an "
                             "entrance at least as large");
  int residential = 0;
  for (int f = 0; f < floors; ++f)
    if (Layout{layoutVersion}.residential(f, floors))
      ++residential;
  if (generation && population > residential * bedsPerFloor)
    throw std::runtime_error("Population exceeds the available residential beds");
}
float World::random() {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return float((rng >> 40) & 0xffffff) / 16777216.f;
}
Id World::account(std::string name, Money opening) {
  if (opening < 0)
    throw std::runtime_error("Negative opening balance");
  Id id = static_cast<Id>(accounts.size());
  accounts.push_back({0, std::move(name)});
  if (opening)
    pay(0, id, opening, "initial-capital");
  return id;
}
void World::pay(Id from, Id to, Money amount, const std::string &reason) {
  if (from < 0 || to < 0 || std::size_t(from) >= accounts.size() ||
      std::size_t(to) >= accounts.size() || amount < 0 || amount > 1000000000000LL)
    throw std::runtime_error("Invalid transfer");
  if (from == to || amount == 0)
    return;
  if (from != 0 && accounts[from].balance < amount)
    throw std::runtime_error("Insufficient funds");
  if (accounts[to].balance > std::numeric_limits<Money>::max() - amount ||
      accounts[from].balance < std::numeric_limits<Money>::min() + amount)
    throw std::runtime_error("Money overflow");
  accounts[from].balance -= amount;
  accounts[to].balance += amount;
  ledger.push_back({time, from, to, amount, reason});
}
void World::schedule(double at, EventKind kind, Id id, Id other, std::uint64_t version) {
  if (!std::isfinite(at) || at < time)
    throw std::runtime_error("Event scheduled in the past");
  events.push({at, sequence++, kind, id, other, version});
}
void World::log(std::string key, Id id, Id other) {
  news.push_back({time, std::move(key), id, other});
  if (news.size() > 2000)
    news.erase(news.begin(), news.begin() + 500);
}
World World::generate(const Config &cfg) {
  cfg.validate();
  World w;
  w.config = cfg;
  for (int f = 0; f < cfg.floors; ++f)
    w.floorPlans.push_back(
        {f == 0 ? cfg.lobbyWidth : cfg.floorWidth, f == 0 ? cfg.lobbyDepth : cfg.floorDepth});
  w.rng = cfg.seed ? cfg.seed : 1;
  w.accounts.push_back({0, "External economy"});
  w.dormAccount = w.account("Dormitory treasury", 50000000);
  w.dormOwner = cfg.mode == Mode::Manager ? 0 : 1;
  auto addFacility = [&](FacilityKind kind, int f, int lane) {
    Facility v;
    v.id = static_cast<Id>(w.facilities.size());
    v.kind = kind;
    v.floor = f;
    v.pos =
        w.layout(f).facility(f, cfg.layoutVersion == 1 ? lane : int(w.floorFacilities[f].size()));
    v.owner = kind == FacilityKind::FireStation
                  ? w.dormAccount
                  : w.account("Business " + std::to_string(v.id + 1), 250000);
    v.manager = none;
    v.stations = std::max(1, int(std::ceil((kind == FacilityKind::Food     ? 10
                                            : kind == FacilityKind::Toilet ? 16
                                                                           : 3) *
                                           cfg.scarcity)));
    v.stock = int(2400 * cfg.supplies);
    v.price = kind == FacilityKind::Water                                  ? 30
              : kind == FacilityKind::Toilet                               ? 15
              : (kind == FacilityKind::Food || kind == FacilityKind::Cafe) ? 450
              : kind == FacilityKind::Shop                                 ? 800
                                                                           : 100;
    v.duration = kind == FacilityKind::Water    ? 40
                 : kind == FacilityKind::Food   ? 210
                 : kind == FacilityKind::Toilet ? 270
                 : kind == FacilityKind::Shop   ? 90
                                                : 180;
    v.askingPrice = 2000000;
    if (kind == FacilityKind::Elevator) {
      v.price = 90;
      v.stations = 8;
      for (int s = f; s < std::min(cfg.floors, f + 100); s += 10)
        v.stops.push_back(s);
    }
    w.floorFacilities[f].push_back(v.id);
    w.facilities.push_back(v);
  };
  for (int f = 0; f < cfg.floors; f += 10) {
    addFacility(FacilityKind::Food, f, 0);
    addFacility(FacilityKind::Water, std::min(f + 2, cfg.floors - 1), 1);
    addFacility(FacilityKind::Toilet, std::min(f + 4, cfg.floors - 1), 2);
    addFacility(FacilityKind::Shop, std::min(f + 9, cfg.floors - 1), 3);
    if (f % 20 == 0) {
      addFacility(FacilityKind::Office, std::min(f + 5, cfg.floors - 1), 4);
      addFacility(FacilityKind::Terminal, std::min(f + 9, cfg.floors - 1), 5);
      addFacility(FacilityKind::Workshop, std::min(f + 5, cfg.floors - 1), 6);
    }
    if (f % 50 == 0) {
      addFacility(FacilityKind::Infirmary, f, 7);
      addFacility(FacilityKind::Jail, f, 8);
      addFacility(FacilityKind::Morgue, f, 10);
      if (f % 100 == 0)
        addFacility(FacilityKind::Crematorium, f, 11);
    }
    if (f % 100 == 0)
      addFacility(FacilityKind::Elevator, f, 9);
  }
  for (int f = 0; f < cfg.floors; f += 20) {
    addFacility(FacilityKind::Laundry, std::min(f + 3, cfg.floors - 1), 4);
    if (cfg.layoutVersion > 1)
      addFacility(FacilityKind::Washbasin, std::min(f + 3, cfg.floors - 1), 12);
    addFacility(FacilityKind::Shower, std::min(f + 6, cfg.floors - 1), 5);
    addFacility(FacilityKind::Leisure, std::min(f + 8, cfg.floors - 1), 6);
    if (f > 0)
      addFacility(FacilityKind::Garbage, std::min(f + 7, cfg.floors - 1), 10);
  }
  for (int f = 0; f < cfg.floors; f += 50) {
    addFacility(FacilityKind::Arcade, std::min(f + 19, cfg.floors - 1), 4);
    addFacility(FacilityKind::Playground, std::min(f + 28, cfg.floors - 1), 5);
    addFacility(FacilityKind::Library, std::min(f + 38, cfg.floors - 1), 6);
  }
  for (int f = 0; f < cfg.floors; f += 100)
    addFacility(FacilityKind::Cinema, std::min(f + 49, cfg.floors - 1), 7);
  for (int f = 0; f < cfg.floors; f += 200)
    addFacility(FacilityKind::Pool, std::min(f + 99, cfg.floors - 1), 6);
  if (cfg.layoutVersion > 1) {
    for (auto kind : {FacilityKind::Reception, FacilityKind::Water, FacilityKind::Toilet,
                      FacilityKind::Toilet, FacilityKind::Shop, FacilityKind::Cafe,
                      FacilityKind::Terminal, FacilityKind::Laundry, FacilityKind::Shower,
                      FacilityKind::Garbage, FacilityKind::Workshop, FacilityKind::Washbasin})
      addFacility(kind, 0, 0);
    for (auto &f : w.facilities)
      if (f.kind == FacilityKind::Reception) {
        f.pos = {0, 0, w.layout(0).halfDepth() - 38};
        f.price = 0;
        f.duration = 60;
      }
  }
  for (int floor = 0; floor < cfg.floors; floor += 80) {
    addFacility(FacilityKind::FireStation, floor, 12);
    w.facilities.back().stock = std::max(1, int(100 * cfg.supplies));
  }
  std::vector<int> residentialFloors;
  for (int f = 0; f < cfg.floors; ++f)
    if (w.layout(f).residential(f, cfg.floors))
      residentialFloors.push_back(f);
  if (residentialFloors.empty())
    residentialFloors.push_back(0);
  static const std::array<const char *, 16> enFirst = {
      "Alex",   "Emma", "Daniel", "Sophie", "Lucas",  "Chloe", "Leo", "Alice",
      "Thomas", "Mia",  "Ethan",  "Nina",   "Victor", "Eva",   "Max", "Lily"};
  static const std::array<const char *, 16> frFirst = {
      "Alexandre", "Emma", "Daniel", "Sophie", "Lucas",  "Chloé", "Léo",    "Alice",
      "Thomas",    "Mia",  "Éthan",  "Nina",   "Victor", "Ève",   "Maxime", "Lilie"};
  static const std::array<const char *, 16> zhFirst = {
      "明",   "雨桐", "浩然", "思雅", "子轩", "若曦", "俊杰", "安然",
      "宇航", "诗涵", "辰逸", "宁",   "志远", "亦菲", "博文", "丽"};
  static const std::array<const char *, 16> enLast = {
      "Chen",    "Li",     "Wang",  "Zhang", "Liu",    "Martin", "Dubois", "Laurent",
      "Bernard", "Moreau", "Smith", "Brown", "Wilson", "Taylor", "Lee",    "Roy"};
  static const std::array<const char *, 16> zhLast = {
      "陈",     "李",   "王",     "张",   "刘",     "马丁", "杜布瓦", "洛朗",
      "贝尔纳", "莫罗", "史密斯", "布朗", "威尔逊", "泰勒", "李",     "罗伊"};
  w.residents.reserve(cfg.population);
  w.names.reserve(cfg.population);
  for (int i = 0; i < cfg.population; ++i) {
    Resident r;
    r.id = i;
    r.account = w.account("Resident " + std::to_string(i + 1),
                          Money(cfg.startingCash * (.35f + w.random() * 1.3f)));
    r.home = residentialFloors[i % residentialFloors.size()];
    r.age = 18 + int(w.random() * 65);
    r.pos = w.layout(r.home).bed(r.home, int(i / residentialFloors.size()) % cfg.bedsPerFloor);
    r.needsAt = w.time;
    r.bladder = .05f + w.random() * .55f;
    r.hygiene = .65f + w.random() * .35f;
    r.hunger = .35f + w.random() * .45f;
    r.thirst = .25f + w.random() * .4f;
    r.strength = 25 + w.random() * 45;
    r.aggression = std::clamp(cfg.aggression * (.25f + w.random() * 1.8f), 0.f, 1.f);
    int first = int(w.random() * 16), last = int(w.random() * 16);
    LocalName name{std::string(enFirst[first]) + " " + enLast[last],
                   std::string(frFirst[first]) + " " + enLast[last],
                   std::string(zhLast[last]) + zhFirst[first]};
    r.name = name.en;
    w.names.push_back(name);
    if (i % 1000 < cfg.wardensPerThousand) {
      r.role = Role::Warden;
      r.employer = w.dormAccount;
      r.strength = 70;
      r.jobSite = w.nearest(0, FacilityKind::Jail);
    } else if (i % 40 == 7)
      r.role = Role::Courier;
    else if (i % 50 == 11)
      r.role = Role::Builder;
    else if (i % 12 == 0)
      r.role = Role::VideoEditor;
    else if (i % 9 == 0)
      r.role = Role::Programmer;
    if (i > 1 && i % 3 == 1 && r.role != Role::Warden)
      r.activity = Activity::Sleeping;
    if (r.activity != Activity::Sleeping) {
      r.pos.x += 1.35f;
      r.pos.z += (int(i / residentialFloors.size()) >= 32 ? .35f : -.35f);
    }
    r.automatic = i != 0;
    w.residents.push_back(r);
    if (i == 0 || i % 3) {
      Item item;
      item.id = static_cast<Id>(w.items.size());
      item.owner = r.account;
      item.carrier = i;
      item.kind = i % 10 == 0 ? ItemKind::Laptop : ItemKind::Phone;
      item.pos = r.pos;
      w.residents.back().items.push_back(item.id);
      w.items.push_back(item);
    }
    for (float &interest : w.residents.back().interests)
      interest = w.random();
    for (int clothing = 0; clothing < (i == 0 ? 2 : 1); ++clothing) {
      Item item;
      item.id = static_cast<Id>(w.items.size());
      item.kind = ItemKind::Clothes;
      item.look = {i % 8, (i / 3) % 8, i % 3, i % 3};
      item.owner = r.account;
      item.carrier = i;
      item.pos = r.pos;
      item.cleanliness = .6f + w.random() * .4f;
      if (clothing == 0)
        w.residents.back().outfit = item.id;
      w.residents.back().items.push_back(item.id);
      w.items.push_back(item);
    }
    w.schedule(w.time + 1 + w.random() * 600, EventKind::Decide, i);
  }
  w.residents[0].role = Role::Unemployed;
  w.residents[0].jobSite = w.residents[0].employer = none;
  w.residents[0].pos =
      cfg.layoutVersion == 1 ? w.layout(0).entrance() : Vec3{0, 0, w.layout(0).halfDepth() - 35};
  w.names[0] = {"Alex Chen", "Alexandre Chen", "陈明"};
  w.residents[0].name = w.names[0].en;
  for (auto &r : w.residents) {
    if (r.role != Role::Unemployed) {
      auto kind = r.role == Role::Courier   ? FacilityKind::Shop
                  : r.role == Role::Builder ? FacilityKind::Workshop
                  : r.role == Role::Warden  ? FacilityKind::Jail
                                            : FacilityKind::Office;
      r.jobSite = w.nearest(r.id, kind);
      r.employer = r.role == Role::Warden ? w.dormAccount : w.facilities[r.jobSite].owner;
    }
  }
  w.residentHomes.assign(cfg.population, none);
  std::map<int, int> counts;
  std::map<int, Money> prices;
  for (int f : residentialFloors)
    prices[f] = w.housingPrice(f);
  for (auto &r : w.residents) {
    HousingUnit h;
    h.floor = r.home;
    h.bed = counts[r.home]++;
    h.owner = w.dormAccount;
    h.tenant = r.id;
    h.paidUntil = w.time + 86400;
    h.decor = {int(r.id % 8), int((r.id / 2) % 8), int((r.id / 3) % 8),
               int(r.id % 8), r.id % 4 == 0,       false};
    h.price = prices[r.home];
    h.rent = h.price / 100;
    w.residentHomes[r.id] = static_cast<Id>(w.housing.size());
    w.housing.push_back(h);
  }
  for (int f : residentialFloors)
    for (int b = counts[f]; b < cfg.bedsPerFloor; ++b) {
      HousingUnit h;
      h.floor = f;
      h.bed = b;
      h.owner = w.dormAccount;
      h.price = prices[f];
      h.rent = h.price / 100;
      w.housing.push_back(h);
    }
  for (Id i = 0; i < Id(w.housing.size()); ++i)
    w.floorHomes[w.housing[i].floor].push_back(i);
  // Existing lines are part of the initial state, not a fake visual crowd.
  w.generateVehicles();
  w.setupRoutines();
  w.refreshManagement();
  for (auto &f : w.facilities)
    if (f.kind == FacilityKind::Workshop) {
      int assigned = 0;
      for (auto &r : w.residents)
        if (r.id > 1 && r.role == Role::Unemployed && std::abs(r.home - f.floor) < 12) {
          w.chooseJob(r.id, Role::Repairer, f.id);
          r.routine.shiftStart = 6 + assigned * 8;
          r.routine.sleepStart = std::fmod(r.routine.shiftStart + 14, 24.0);
          if (++assigned == 3)
            break;
        }
    }
  w.setupOffices();
  w.setupCompanies();
  w.generateMedical();
  w.generateLifts();
  for (auto &r : w.residents)
    if (r.role == Role::Warden)
      for (ItemKind kind : {ItemKind::Handgun, ItemKind::Ammunition}) {
        Item item;
        item.id = Id(w.items.size());
        item.kind = kind;
        item.owner = r.account;
        item.carrier = r.id;
        item.quantity = kind == ItemKind::Ammunition ? 24 : 1;
        w.items.push_back(item);
        r.items.push_back(item.id);
      }
  for (auto &r : w.residents)
    if (!w.sleepTime(r.id) && r.id > 1 && r.id % 5 == 0 && r.role == Role::Unemployed) {
      Id f = w.nearest(r.id, FacilityKind::Food);
      if (w.facilities[f].queue.size() < 60) {
        r.pos = w.facilities[f].pos + Vec3{0, 0, 1.6f};
        r.activity = Activity::Idle;
        w.joinQueue(r.id, f);
      }
    }
  for (auto &f : w.facilities) {
    w.layoutQueue(f.id);
    for (Id id : f.queue) {
      auto &r = w.residents[id];
      if (!r.route.empty()) {
        r.pos = r.route.back().p;
        r.route.clear();
      }
    }
  }
  for (auto &r : w.residents) {
    if (r.activity == Activity::Using && !r.route.empty()) {
      r.pos = r.route.back().p;
      r.route.clear();
    }
    if (r.activity != Activity::Sleeping && r.route.empty())
      r.pos = w.accessible(r.pos);
  }
  w.schedule(w.time + 60, EventKind::FacilityTick, 0);
  w.schedule(w.time + 3600, EventKind::Payroll, 0);
  w.log("world.created");
  w.validate();
  return w;
}
bool World::near(Id id, Vec3 p, float distance) const {
  return length(position(id) - p) <= distance;
}
Id World::nearest(Id id, FacilityKind kind) const {
  Vec3 p = id >= 0 && std::size_t(id) < residents.size() ? position(id) : Vec3{};
  return nearestAt(p, kind);
}
Id World::nearestAt(Vec3 p, FacilityKind kind) const {
  Id best = none;
  float score = 1e20f;
  for (auto &f : facilities)
    if (f.kind == kind && f.open) {
      float d = length(f.pos - p) + float(f.queue.size()) * .15f;
      if (d < score) {
        score = d;
        best = f.id;
      }
    }
  return best;
}
void World::travel(Id id, Vec3 dest, TaskKind task, Id target) {
  auto &r = residents.at(id);
  if (!r.alive || !r.present || r.vehicle != none || r.lift != none || r.ambulance != none ||
      r.carriedBy != none || r.activity == Activity::Jailed || r.activity == Activity::Restrained)
    throw std::runtime_error("Resident cannot travel");
  if (looseBodies.contains(id))
    throw std::runtime_error("physics.error.airborne");
  cancelLift(id);
  clockOut(id);
  leaveQueue(id);
  r.ladder = -1;
  r.ladderExit = false;
  if (target >= 0 && std::size_t(target) < facilities.size() &&
      (task == TaskKind::Service || task == TaskKind::Work))
    dest = facilities[target].pos + Vec3{0, 0, task == TaskKind::Work ? -1.6f : 1.6f};
  if (task != TaskKind::LiftWait && planLift(id, dest, task, target)) {
    stopPersonalUse(id);
    return;
  }
  routeTo(id, dest);
  stopPersonalUse(id);
  double at = r.route.back().time;
  r.task = task;
  r.target = target;
  r.activity = task == TaskKind::Escort ? Activity::Escorted : Activity::Walking;
  ++r.routeVersion;
  schedule(std::max(at, time + .01), EventKind::Arrive, id, none, r.routeVersion);
}
void World::needs(Id id) {
  auto &r = residents.at(id);
  double hours = (time - r.needsAt) / 3600;
  if (hours <= 0 || !r.alive || !r.present)
    return;
  r.needsAt = time;
  r.hunger = std::clamp(r.hunger + float(hours) * .045f, 0.f, 1.4f);
  r.thirst = std::clamp(r.thirst + float(hours) * .065f, 0.f, 1.4f);
  r.energy = std::clamp(r.energy - float(hours) * .035f, 0.f, 1.f);
  r.hygiene = std::max(0.f, r.hygiene - float(hours) * .035f);
  r.bladder = std::min(1.f, r.bladder + float(hours) * .13f);
  int floor = std::clamp(int(std::floor(position(id).y / 3)), 0, config.floors - 1);
  auto local = floorFacilities.find(floor);
  int refuse = 0;
  if (local != floorFacilities.end())
    for (Id fid : local->second)
      refuse += facilities[fid].waste;
  if (refuse > 100)
    r.hygiene = std::max(0.f, r.hygiene - float(hours) * .01f * std::min(5, refuse / 100));
  if (r.outfit != none) {
    items[r.outfit].cleanliness = std::max(0.f, items[r.outfit].cleanliness - float(hours) * .025f);
    items[r.outfit].durability = std::max(0.f, items[r.outfit].durability - float(hours) * .0005f);
  }
  if (r.activity == Activity::Walking && !crowd.contains(id))
    exercise(id, hours * 3600, .2f);
  if (r.activity == Activity::Sleeping)
    r.energy = std::min(1.f, r.energy + float(hours) * .22f);
  r.health -= float(hours) * (std::max(0.f, r.hunger - 1) * 3 + std::max(0.f, r.thirst - 1) * 8);
  if (r.hunger < .8f && r.thirst < .8f && r.infectedAt < 0 && r.activity != Activity::Fighting)
    r.health =
        std::min(100.f, r.health + float(hours) * (r.activity == Activity::Sleeping ? .8f : .2f));
  float satisfaction = std::clamp(.95f - r.hunger * .24f - r.thirst * .2f - (1 - r.energy) * .15f -
                                      (r.activity == Activity::Queuing ? .15f : 0.f),
                                  0.f, 1.f);
  r.happiness += (satisfaction - r.happiness) * std::min(1.f, float(hours) * .2f);
  if (r.health <= 0)
    kill(id);
}
void World::decide(Id id) {
  auto &r = residents.at(id);
  needs(id);
  if (!r.alive || !r.present)
    return;
  schedule(time + 180 + random() * 120, EventKind::Decide, id);
  if (looseBodies.contains(id))
    return;
  if (!r.automatic || r.vehicle != none || r.lift != none || r.ambulance != none ||
      r.carriedBy != none || r.medicalDuty != none || r.activity == Activity::Jailed ||
      r.activity == Activity::Restrained || r.activity == Activity::Escorted || r.surrendered)
    return;
  if (liftJourneys.contains(id))
    return;
  if (fireSprays.contains(id) || r.task == TaskKind::FireResponse)
    return;
  if (r.task == TaskKind::Evacuate && !r.route.empty())
    return;
  if (dangerAt(position(id)) > .02f && evacuate(id))
    return;
  if (r.activity == Activity::Walking && r.task == TaskKind::Service && r.target != none &&
      !operational(r.target)) {
    r.pos = position(id);
    r.route.clear();
    ++r.routeVersion;
    r.task = TaskKind::None;
    r.target = none;
    r.activity = Activity::Idle;
    crowd.erase(id);
  }
  if (r.leaving) {
    if (r.route.empty())
      travel(id, layout(0).entrance(), TaskKind::Emigrate, id);
    return;
  }
  if (r.activity == Activity::Fighting) {
    if (r.opponent != none && residents[r.opponent].alive && !residents[r.opponent].surrendered &&
        near(id, position(r.opponent), 2.5f)) {
      if (time >= r.nextHit) {
        if (r.lethalThreat && hasGun(id)) {
          try {
            shoot(id, r.opponent);
          } catch (const std::runtime_error &) {
            attack(id, r.opponent, false);
          }
        } else
          attack(id, r.opponent, false);
      }
    } else {
      r.activity = Activity::Idle;
      r.opponent = none;
    }
    return;
  }
  if ((r.activity == Activity::Queuing || r.activity == Activity::Using) && r.queuedAt != none) {
    const auto &f = facilities[r.queuedAt];
    FacilityKind priority = r.thirst > 1 ? FacilityKind::Water : FacilityKind::Food;
    bool different = priority == FacilityKind::Water
                         ? f.kind != FacilityKind::Water
                         : (f.kind != FacilityKind::Food && f.kind != FacilityKind::Cafe &&
                            f.kind != FacilityKind::Shop);
    bool urgent = r.activity == Activity::Queuing && (r.thirst > 1 || r.hunger > 1.1f) &&
                  different && serviceChoice(id, priority) != none;
    if (urgent || (!operational(f.id) && time - r.waitSince > 1200) || time - r.waitSince > 7200) {
      leaveQueue(id);
      r.activity = Activity::Idle;
    }
  }
  if (r.role == Role::Chef && onShift(id) && r.queuedAt == r.jobSite && r.jobSite != none &&
      near(id, facilities[r.jobSite].pos, 3)) {
    leaveQueue(id);
    clockIn(id);
  }
  if (r.activity == Activity::Queuing && r.aggression > .35f && random() < .0002f) {
    auto &q = facilities[r.queuedAt].queue;
    for (Id victim : q)
      if (victim != id && near(id, position(victim), 1.3f)) {
        auto possessions = residents[victim].items;
        for (Id item : possessions)
          if (items[item].kind == ItemKind::Phone && items[item].carrier == victim) {
            try {
              stealItem(id, victim, item);
            } catch (const std::runtime_error &) {
            }
            break;
          }
        break;
      }
  }
  if (r.activity != Activity::Idle && r.activity != Activity::Working &&
      r.activity != Activity::Sleeping)
    return;
  if (r.health < 85)
    for (Id item : r.items)
      if (items[item].kind == ItemKind::Medicine && items[item].owner == r.account &&
          items[item].carrier == id && !items[item].consumed && items[item].reservedAt == none) {
        consumeItem(id, item);
        break;
      }
  if (r.hunger > .55f) {
    Id food = none, utensil = none;
    for (Id item : r.items)
      if (!items[item].consumed && items[item].carrier == id && items[item].reservedAt == none) {
        if (items[item].kind == ItemKind::Food)
          food = item;
        if ((items[item].kind == ItemKind::Tableware || items[item].kind == ItemKind::RiceCooker) &&
            items[item].durability > 0)
          utensil = item;
      }
    if (food != none && utensil != none && r.thirst < .95f) {
      try {
        usePersonalItem(id, utensil);
        return;
      } catch (const std::runtime_error &) {
      }
    }
    if (food != none)
      consumeItem(id, food);
  }
  if (r.energy < .65f && r.energy > .3f && !onShift(id) && !sleepTime(id))
    for (Id item : r.items)
      if (items[item].kind == ItemKind::Fan && !items[item].consumed && items[item].carrier == id &&
          items[item].durability > 0 && residentHomes[id] != none &&
          near(id, homePosition(residentHomes[id]), .75f)) {
        usePersonalItem(id, item);
        return;
      }
  if (r.health < 65 && r.hunger < .95f && r.thirst < .95f && r.ambulance == none &&
      r.carriedBy == none) {
    Id f = serviceChoice(id, FacilityKind::Infirmary);
    if (f != none && operational(f) && accounts[r.account].balance >= facilities[f].price) {
      travel(id, facilities[f].pos, TaskKind::Service, f);
      return;
    }
  }
  bool urgent = r.thirst > 1.0f || r.hunger > 1.05f || r.bladder > .98f;
  if (r.activity == Activity::Sleeping && sleepTime(id) && !urgent)
    return;
  if (r.activity == Activity::Sleeping) {
    r.pos = accessible(r.pos);
    r.activity = Activity::Idle;
  }
  if (sleepTime(id) && !urgent && !onShift(id)) {
    auto h = residentHomes[id];
    if (h >= 0 && dangerAt(homePosition(h)) > .02f)
      return;
    travel(id, h >= 0 ? homePosition(h) : layout(r.home).landing(0, r.home * 3.f), TaskKind::Home,
           id);
    return;
  }
  bool workWindow = onShift(id) && r.jobSite != none && !activeFires.contains(r.jobSite) &&
                    (r.role == Role::Firefighter || dangerAt(facilities[r.jobSite].pos) <= .02f);
  if (r.role == Role::Chef && r.jobSite != none && facilities[r.jobSite].open &&
      !activeFires.contains(r.jobSite) && dangerAt(facilities[r.jobSite].pos) <= .02f &&
      facilities[r.jobSite].stock > 0 && accounts[r.account].balance >= 480 &&
      (r.hunger > .55f || r.thirst > .6f)) {
    if (near(id, facilities[r.jobSite].pos, 3))
      clockIn(id);
    else {
      travel(id, facilities[r.jobSite].pos, TaskKind::Work, r.jobSite);
      return;
    }
  }
  // On-site staff can buy a prepared ration from their own stocked kitchen/shop,
  // and a bottle from workplace supplies. This avoids a chef waiting forever
  // in the queue they are the sole person qualified to serve.
  if (r.jobSite != none && near(id, facilities[r.jobSite].pos, 3) &&
      ((workWindow && r.activity == Activity::Working) || r.role == Role::Chef)) {
    auto &f = facilities[r.jobSite];
    if (r.hunger > .55f &&
        (f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe ||
         f.kind == FacilityKind::Shop) &&
        f.stock > 0 && accounts[r.account].balance >= 450) {
      pay(r.account, f.owner, 450, "staff-meal");
      --f.stock;
      r.hunger = std::max(0.f, r.hunger - .55f);
      ++f.waste;
    }
    if (r.thirst > .6f && f.stock > 0 && accounts[r.account].balance >= 30) {
      pay(r.account, f.owner, 30, "staff-water");
      --f.stock;
      r.thirst = std::max(0.f, r.thirst - .7f);
    }
  }
  if (r.activity == Activity::Working && !workWindow)
    clockOut(id);
  FacilityKind kind = r.thirst > .65f ? FacilityKind::Water : FacilityKind::Food;
  if (r.hunger > (workWindow ? .85f : .57f) || r.thirst > (workWindow ? .9f : .65f)) {
    Id f = serviceChoice(id, kind);
    if (f != none && accounts[r.account].balance >= facilities[f].price) {
      travel(id, facilities[f].pos, TaskKind::Service, f);
      return;
    }
  }
  if (r.personalWaste >= 3) {
    Id f = serviceChoice(id, FacilityKind::Garbage);
    if (f != none) {
      travel(id, facilities[f].pos, TaskKind::Service, f);
      return;
    }
  }
  if (r.bladder > .75f || r.hygiene < .35f ||
      (r.outfit != none && items[r.outfit].cleanliness < .25f)) {
    auto service = r.bladder > .75f   ? FacilityKind::Toilet
                   : r.hygiene < .35f ? FacilityKind::Shower
                                      : FacilityKind::Laundry;
    Id f = serviceChoice(id, service);
    if (f != none && accounts[r.account].balance >= facilities[f].price) {
      travel(id, facilities[f].pos, TaskKind::Service, f);
      return;
    }
  }
  if (!workWindow && r.hunger < .85f && r.thirst < .65f && r.energy > .25f) {
    auto tools = washableItems(id);
    if (std::any_of(tools.begin(), tools.end(),
                    [&](Id item) { return items[item].cleanliness < .4f; })) {
      Id basin = serviceChoice(id, FacilityKind::Washbasin);
      if (basin != none) {
        travel(id, facilities[basin].pos, TaskKind::Service, basin);
        return;
      }
    }
  }
  if (!workWindow && r.activity == Activity::Idle && r.route.empty() && r.hunger < .85f &&
      r.thirst < .65f && r.bladder < .75f && r.hygiene >= .35f && r.energy > .3f &&
      autonomyDecision(id))
    return;
  if (r.role == Role::Warden && workWindow) {
    wardenRespond(id);
    if (r.task == TaskKind::Investigate || r.task == TaskKind::Escort ||
        r.activity == Activity::Fighting)
      return;
  }
  if (!workWindow && r.happiness < .6f && random() < .2f) {
    std::array<FacilityKind, 8> leisure = {
        FacilityKind::Cafe, FacilityKind::Leisure, FacilityKind::Arcade,  FacilityKind::Cinema,
        FacilityKind::Pool, FacilityKind::Shop,    FacilityKind::Library, FacilityKind::Playground};
    auto interest = std::max_element(r.interests.begin(), r.interests.end()) - r.interests.begin();
    auto choice = interest == 4 && random() < .5f ? FacilityKind::Gym : leisure[interest];
    Id f = serviceChoice(id, choice);
    if (f != none && accounts[r.account].balance > facilities[f].price + 1500) {
      travel(id, facilities[f].pos, TaskKind::Service, f);
      return;
    }
  }
  if (r.energy < .25f) {
    auto h = residentHomes[id];
    if (h >= 0 && dangerAt(homePosition(h)) > .02f)
      return;
    travel(id, h >= 0 ? homePosition(h) : layout(r.home).landing(0, r.home * 3.f), TaskKind::Home,
           id);
    return;
  }
  if (workWindow && r.role == Role::Cleaner) {
    if (r.bodyCarrying != none) {
      auto &b = remains[r.bodyCarrying];
      if (b.destination != none) {
        travel(id, facilities[b.destination].pos + Vec3{0, 0, 1.6f}, TaskKind::BodyDrop,
               b.destination);
        return;
      }
    }
    Id sanitation = none;
    float sanitationDistance = 1e20f;
    for (const auto &shipment : freight)
      if (shipment.waste && shipment.state != FreightState::Delivered) {
        if (shipment.worker == id) {
          sanitation = shipment.id;
          break;
        }
        Vec3 pickup = items[shipment.item].pos;
        float d = length(position(id) - pickup);
        if (shipment.worker == none && std::abs(position(id).y - pickup.y) <= 60 &&
            d < sanitationDistance) {
          sanitation = shipment.id;
          sanitationDistance = d;
        }
      }
    if (sanitation != none) {
      auto &shipment = freight[sanitation];
      if (shipment.worker == none)
        assignFreight(id, shipment.id);
      else if (shipment.state == FreightState::Carrying)
        travel(id, facilities[shipment.facility].pos + Vec3{0, 0, 1.6f}, TaskKind::FreightDrop,
               shipment.id);
      else
        travel(id, items[shipment.item].pos, TaskKind::FreightPickup, shipment.id);
      return;
    }
    for (Id b = 0; b < Id(remains.size()); ++b) {
      auto &body = remains[b];
      if (!body.cremated && !body.lost && !airborneRemains.contains(Id(&body - remains.data())) &&
          body.carrier == none && body.ambulance == none &&
          (body.worker == none || body.worker == id) &&
          (body.storage == none || facilities[body.storage].kind == FacilityKind::Morgue) &&
          length(position(id) - body.pos) < 200) {
        transportBody(id, b);
        return;
      }
    }
  }
  if (workWindow && r.role == Role::Courier) {
    if (r.carrying != none && items[r.carrying].order != none) {
      auto &parcel = orders[items[r.carrying].order];
      if (parcel.courier == id && parcel.state == OrderState::ToCustomer &&
          residents[parcel.buyer].alive && residents[parcel.buyer].present) {
        routeDelivery(parcel.id);
        return;
      }
    }
    for (auto &shipment : freight)
      if (!shipment.waste && shipment.worker == id && shipment.state != FreightState::Delivered) {
        if (shipment.state == FreightState::Carrying)
          travel(id, facilities[shipment.facility].pos + Vec3{0, 0, 1.6f}, TaskKind::FreightDrop,
                 shipment.id);
        else
          travel(id, items[shipment.item].pos, TaskKind::FreightPickup, shipment.id);
        return;
      }
    Id localFreight = none;
    float freightDistance = 1e20f;
    for (auto &shipment : freight)
      if (!shipment.waste && shipment.state == FreightState::Waiting && shipment.readyAt <= 0) {
        Vec3 pickup = items[shipment.item].pos;
        float d = length(position(id) - pickup);
        if (std::abs(position(id).y - pickup.y) <= 60 && d < freightDistance) {
          localFreight = shipment.id;
          freightDistance = d;
        }
      }
    if (localFreight != none) {
      assignFreight(id, localFreight);
      return;
    }
    for (auto &o : orders)
      if (o.courier == id && o.state == OrderState::ToPickup) {
        if (operational(o.shop))
          travel(id, facilities[o.shop].pos + Vec3{1.6f, 0, 0}, TaskKind::DeliveryPickup, o.id);
        return;
      }
    Id localOrder = none;
    float orderDistance = 1e20f;
    for (auto &o : orders)
      if (o.state == OrderState::Waiting) {
        Vec3 pickup = facilities[o.shop].pos;
        float d = length(position(id) - pickup);
        bool ownWorkplace = o.shop == r.jobSite;
        bool localPickup = std::abs(position(id).y - pickup.y) <= 12 &&
                           std::hypot(position(id).x - pickup.x, position(id).z - pickup.z) < 80;
        if ((ownWorkplace || localPickup) && d < orderDistance) {
          localOrder = o.id;
          orderDistance = d;
        }
      }
    if (localOrder != none) {
      assignDelivery(localOrder, id);
      return;
    }
  }
  if (workWindow && r.role == Role::Builder) {
    if (structuralDecision(id))
      return;
    for (auto &p : housingWorks)
      if ((p.state == ProjectState::Funded || p.state == ProjectState::Working) &&
          (p.worker == id || (p.worker == none && std::abs(position(id).y - p.site.y) <= 60))) {
        if (p.worker == none)
          assignHousingWork(id, p.id);
        Vec3 destination = housingWorkDestination(p.id);
        if (near(id, destination, 2))
          r.activity = Activity::Working;
        else if (r.route.empty())
          travel(id, destination, TaskKind::None, none);
        return;
      }
    for (auto &p : expansions)
      if (!p.complete &&
          (p.worker == id || (p.worker == none && std::abs(position(id).y - p.site.y) <= 60))) {
        if (p.worker == none)
          assignExpansion(id, p.id);
        if (near(id, p.site, 2))
          beginExpansionWork(id, p.id);
        else if (r.route.empty())
          travel(id, p.site, TaskKind::None, none);
        return;
      }
    for (auto &p : projects)
      if (p.state == ProjectState::Working && p.worker == id) {
        if (p.newConstruction && !p.materialsDelivered) {
          if (r.carrying == p.materialItem && p.materialItem != none)
            travel(id, facilities[p.facility].pos + Vec3{0, 0, 1.6f}, TaskKind::ConstructionDrop,
                   p.id);
          else
            travel(id,
                   p.materialItem == none ? facilities[p.source].pos + Vec3{0, 0, 1.6f}
                                          : items[p.materialItem].pos,
                   TaskKind::ConstructionPickup, p.id);
          return;
        }
        if (near(id, facilities[p.facility].pos))
          beginProjectWork(id, p.id);
        else
          travel(id, facilities[p.facility].pos, TaskKind::Work, p.facility);
        return;
      }
    for (auto &p : projects)
      if (p.state == ProjectState::Funded && p.worker == none &&
          std::abs(position(id).y - facilities[p.facility].pos.y) <= 60) {
        assignBuilder(p.id, id);
        return;
      }
  }
  if (r.role == Role::Repairer && workWindow) {
    Id maintenance = none;
    float repairDistance = 1e20f;
    for (const auto &p : repairs)
      if (!p.complete) {
        if (p.worker == id) {
          maintenance = p.id;
          break;
        }
        Vec3 site = facilities[p.facility].pos;
        float d = length(position(id) - site);
        if (p.worker == none && std::abs(position(id).y - site.y) <= 60 && d < repairDistance) {
          maintenance = p.id;
          repairDistance = d;
        }
      }
    if (maintenance != none) {
      auto &p = repairs[maintenance];
      if (p.worker == none)
        assignRepair(id, p.id);
      Vec3 destination = !p.parts                          ? facilities[p.workshop].pos
                         : items[p.partItem].carrier == id ? facilities[p.facility].pos
                                                           : items[p.partItem].pos;
      if (near(id, destination, 3))
        r.activity = Activity::Working;
      else if (r.route.empty())
        travel(id, destination, TaskKind::None, none);
      return;
    }
  }
  if (r.jobSite != none &&
      (workWindow || inHours(time, std::fmod(r.routine.shiftStart + 23, 24), 1))) {
    if (near(id, facilities[r.jobSite].pos, 3))
      clockIn(id);
    else
      travel(id, facilities[r.jobSite].pos, TaskKind::Work, r.jobSite);
  } else
    r.activity = Activity::Idle;
}
void World::advance(double seconds, std::size_t budget) {
  if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400 * 366)
    throw std::runtime_error("Invalid time advance");
  if (gameOver)
    return;
  double target = time + seconds;
  std::size_t n = 0;
  while (!events.empty() && events.top().time <= target && n++ < budget) {
    auto e = events.top();
    events.pop();
    time = e.time;
    dispatch(e);
    if (gameOver)
      break;
  }
  if (!gameOver && (events.empty() || events.top().time > target))
    time = target;
}
void World::dispatch(const Event &e) {
  if (e.kind == EventKind::PhysicsTick) {
    if (e.time == nextPhysicsAt)
      physicsTick();
    return;
  }
  if (e.kind == EventKind::Decide) {
    try {
      decide(e.id);
    } catch (const std::runtime_error &error) {
      if (std::getenv("MEGA_TRACE"))
        std::cerr << "Decision " << e.id << ": " << error.what() << "\n";
      residents[e.id].activity = Activity::Idle;
      log("action.blocked", e.id);
    }
    return;
  }
  if (e.kind == EventKind::Arrive) {
    auto arrivalTask = residents[e.id].task;
    if (residents[e.id].routeVersion == e.version && residents[e.id].alive &&
        residents[e.id].present)
      try {
        arrive(e.id);
      } catch (const std::runtime_error &error) {
        if (std::getenv("MEGA_TRACE"))
          std::cerr << "Arrival " << e.id << " (task " << int(arrivalTask) << "): " << error.what()
                    << "\n";
        // A destination may close or change while the resident is travelling.
        residents[e.id].activity = Activity::Idle;
        log("action.blocked", e.id);
      }
    return;
  }
  if (e.kind == EventKind::FacilityTick) {
    for (auto &p : expansions)
      if (!p.complete && p.worker != none)
        workExpansion(p.id, 60);
    for (auto &p : projects)
      if (p.newConstruction && p.state == ProjectState::Working)
        workProject(p.id, 60);
    personalUseTick();
    orderTick();
    emergencyTick();
    refreshServices();
    medicalTick();
    liftTick();
    freightTick();
    officeTick();
    repairTick();
    housingWorkTick();
    structuralTick();
    schedule(time + 60, EventKind::FacilityTick, 0);
    return;
  }
  if (e.kind == EventKind::FinishService) {
    auto &f = facilities[e.id];
    if (!operational(f.id)) {
      serve(f.id);
      return;
    }
    auto it = std::find_if(f.serving.begin(), f.serving.end(), [&](const Service &s) {
      return s.resident == e.other && s.until == e.time;
    });
    if (it == f.serving.end())
      return;
    if (crowd.contains(e.other) && !residents[e.other].route.empty() &&
        length(position(e.other) - residents[e.other].route.back().p) > .08f) {
      it->until = time + f.duration;
      schedule(it->until, EventKind::FinishService, e.id, e.other);
      return;
    }
    Money price = it->price;
    f.serving.erase(it);
    auto &r = residents[e.other];
    r.queuedAt = none;
    needs(r.id);
    if (!r.alive || !r.present || accounts[r.account].balance < price) {
      ++f.stock;
      r.queuedAt = f.id;
      leaveQueue(r.id); // Release reserved clothing even if payment/death interrupts completion.
      serve(f.id);
      return;
    }
    if (r.alive && r.present) {
      pay(r.account, operatingAccount(f.id), price, "facility-service");
      f.condition = std::max(0.f, f.condition - .00002f);
      if (f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe ||
          f.kind == FacilityKind::Infirmary)
        ++f.waste;
      r.pos = position(r.id);
      r.route.clear();
      if (f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe)
        r.hunger = std::max(0.f, r.hunger - .55f);
      if (f.kind == FacilityKind::Water)
        r.thirst = std::max(0.f, r.thirst - .7f);
      if (f.kind == FacilityKind::Infirmary)
        r.health = std::min(100.f, r.health + 30);
      if (f.kind == FacilityKind::Shop) {
        Item meal;
        meal.id = Id(items.size());
        meal.kind = ItemKind::Food;
        meal.owner = r.account;
        meal.carrier = r.id;
        meal.pos = r.pos;
        items.push_back(meal);
        r.items.push_back(meal.id);
        log("shop.takeaway", r.id, f.id);
      }
      if (f.kind == FacilityKind::Garbage) {
        f.waste += r.personalWaste;
        r.personalWaste = 0;
      }
      if (f.kind == FacilityKind::Toilet)
        r.bladder = 0;
      if (f.kind == FacilityKind::Gym || f.kind == FacilityKind::Pool)
        exercise(r.id, f.duration, f.kind == FacilityKind::Gym ? 3.f : 2.f);
      if (f.kind == FacilityKind::Shower)
        r.hygiene = 1;
      if (f.kind == FacilityKind::Washbasin)
        finishWashing(r.id, f.id);
      if (f.kind == FacilityKind::Laundry)
        for (Id item : r.items)
          if (items[item].reservedAt == f.id) {
            items[item].cleanliness = 1;
            items[item].reservedAt = none;
            items[item].carrier = r.id;
            if (r.outfit == none)
              r.outfit = item;
          }
      std::array<FacilityKind, 8> preferences = {FacilityKind::Cafe,    FacilityKind::Leisure,
                                                 FacilityKind::Arcade,  FacilityKind::Cinema,
                                                 FacilityKind::Pool,    FacilityKind::Shop,
                                                 FacilityKind::Library, FacilityKind::Playground};
      auto preference = std::find(preferences.begin(), preferences.end(), f.kind);
      if (preference != preferences.end())
        r.happiness = std::min(1.f, r.happiness + .05f +
                                        r.interests[preference - preferences.begin()] * .25f);
      r.activity = Activity::Idle;
      r.queuedAt = none;
    }
    if (r.automatic && r.alive && r.present) {
      try {
        travel(r.id, f.pos + Vec3{5, 0, 1.5f}, TaskKind::None, none);
      } catch (
          const std::runtime_error &) { /* The ordinary decision tick retries a changed layout. */
      }
    }
    serve(f.id);
    return;
  }
  if (e.kind == EventKind::Release) {
    auto &r = residents[e.id];
    if (r.alive && r.present && r.activity == Activity::Jailed && r.releaseAt <= time) {
      r.activity = Activity::Idle;
      r.surrendered = false;
      r.escort = none;
      log("custody.released", r.id);
    }
    return;
  }
  if (e.kind == EventKind::Payroll) {
    for (auto &p : projects)
      if (p.state == ProjectState::Working && !p.newConstruction)
        workProject(p.id, 3600);
    for (auto &r : residents) {
      if (std::any_of(expansions.begin(), expansions.end(),
                      [&](const Expansion &p) { return p.worker == r.id && !p.complete; }))
        continue;
      if (std::any_of(projects.begin(), projects.end(), [&](const Project &p) {
            return p.worker == r.id && p.newConstruction && p.state == ProjectState::Working;
          }))
        continue;
      if (std::any_of(constructionTeams.begin(), constructionTeams.end(),
                      [&](const ConstructionTeam &t) {
                        return t.work != none && t.members.contains(r.id);
                      }))
        continue;
      if (r.medicalDuty != none)
        continue;
      if (std::any_of(housingWorks.begin(), housingWorks.end(), [&](const HousingWork &p) {
            return p.worker == r.id && p.state == ProjectState::Working;
          }))
        continue;
      if (std::any_of(repairs.begin(), repairs.end(),
                      [&](const Repair &p) { return p.worker == r.id && !p.complete; }))
        continue;
      bool working = r.activity == Activity::Working;
      clockOut(r.id);
      if (working)
        clockIn(r.id);
    }
    healthTick();
    socialTick();
    economyTick();
    governanceTick();
    schedule(time + 3600, EventKind::Payroll, 0);
  }
}
void World::arrive(Id id) {
  auto &r = residents.at(id);
  float arrivalRadius = r.task == TaskKind::Service || r.task == TaskKind::Work ? .68f : .08f;
  bool closeHandover =
      r.task == TaskKind::DeliveryPickup ? near(id, items[orders[r.target].item].pos)
      : r.task == TaskKind::DeliveryDrop ? near(id, position(orders[r.target].buyer))
                                         : false;
  if (!closeHandover && !r.route.empty() &&
      (r.route.back().time > time + .01 ||
       (crowd.contains(id) && length(position(id) - r.route.back().p) > arrivalRadius))) {
    schedule(std::max(time + 12, r.route.back().time), EventKind::Arrive, id, none, r.routeVersion);
    return;
  }
  r.pos = position(id);
  r.route.clear();
  auto task = r.task;
  Id target = r.target;
  r.task = TaskKind::None;
  r.activity = Activity::Idle;
  if (task == TaskKind::Service) {
    auto &f = facilities[target];
    if (f.open && f.queue.size() >= 64 && r.automatic) {
      Id alternative = serviceChoice(id, f.kind);
      if (alternative != none && alternative != target)
        travel(id, facilities[alternative].pos, TaskKind::Service, alternative);
      // Crowded service is an expected condition, not an interrupted task.
      // The next decision chooses a service again after the queue has advanced.
    } else if (f.open)
      joinQueue(id, target);
  } else if (task == TaskKind::TradeMeet)
    finishAutonomyTrade(id);
  else if (task == TaskKind::ShoppingTerminal) {
    // Availability can change while walking. A busy computer is normal, not a failed task.
    try {
      useTerminal(id, target, 5);
      residentPlans[id].nextShopping = time;
      residentPlans[id].action = AutonomyAction::NeedConnection;
      schedule(time + 1, EventKind::Decide, id);
    } catch (const std::runtime_error &) {
      residentPlans[id].nextShopping = time + 900;
      residentPlans[id].action = AutonomyAction::NeedConnection;
    }
  } else if (task == TaskKind::FireResponse)
    beginExtinguish(id, target);
  else if (task == TaskKind::MedicalReach)
    medicalReached(id, target);
  else if (task == TaskKind::MedicalBoard)
    medicalBoard(id, target);
  else if (task == TaskKind::MedicalClinic)
    medicalClinic(id, target);
  else if (task == TaskKind::BodyPickup)
    pickupBody(id, target);
  else if (task == TaskKind::BodyDrop)
    storeBody(id, target);
  else if (task == TaskKind::FreightPickup)
    collectFreight(id, target);
  else if (task == TaskKind::FreightDrop)
    deliverFreight(id, target);
  else if (task == TaskKind::Emigrate)
    depart(id);
  else if (task == TaskKind::LiftWait) {
    auto j = liftJourneys.at(id);
    try {
      requestLift(id, j.lift, j.destination);
    } catch (const std::runtime_error &) {
      liftJourneys.erase(id);
      routeTo(id, j.goal);
      r.task = j.task;
      r.target = j.target;
      r.activity = Activity::Walking;
      schedule(std::max(time + .01, r.route.back().time), EventKind::Arrive, id, none,
               r.routeVersion);
    }
  } else if (task == TaskKind::ConstructionPickup)
    collectMaterials(id, target);
  else if (task == TaskKind::ConstructionDrop)
    deliverMaterials(id, target);
  else if (task == TaskKind::DeliveryPickup)
    pickup(target, id);
  else if (task == TaskKind::DeliveryDrop) {
    auto &o = orders[target];
    if (residents[o.buyer].alive && residents[o.buyer].present)
      routeDelivery(target);
    else {
      o.state = OrderState::Lost;
      log("delivery.recipient_missing", o.buyer);
    }
  } else if (task == TaskKind::Home) {
    r.activity = Activity::Sleeping;
    if (residentHomes[id] != none)
      r.pos = homePosition(residentHomes[id]);
    crowd.erase(id);
  } else if (task == TaskKind::Work) {
    bool construction =
        r.role == Role::Builder &&
        std::any_of(projects.begin(), projects.end(), [&](const Project &p) {
          return p.worker == id && p.facility == target && p.state == ProjectState::Working;
        });
    if (construction) {
      for (auto &p : projects)
        if (p.worker == id && p.facility == target && p.state == ProjectState::Working)
          beginProjectWork(id, p.id);
    } else
      clockIn(id);
  } else if (task == TaskKind::Investigate) {
    auto &c = incidents[target];
    if (residents[c.offender].surrendered)
      apprehend(id, c.offender);
    else if (near(id, position(c.offender), 2.5f)) {
      if (residents[c.offender].strength < r.strength * 1.2f) {
        surrender(c.offender);
        apprehend(id, c.offender);
      } else
        attack(id, c.offender, false);
    } else
      travel(id, position(c.offender), task, target);
  } else if (task == TaskKind::Escort) {
    if (r.escort != none) {
      auto &suspect = residents[r.escort];
      if (near(suspect.id, r.pos, 3)) {
        suspect.pos = accessible(r.pos + Vec3{1.2f, 0, 0});
        if (crowd.contains(suspect.id))
          crowd[suspect.id].pos = suspect.pos;
        suspect.route.clear();
        suspect.activity = Activity::Jailed;
        suspect.releaseAt = time + 8 * 3600;
        schedule(suspect.releaseAt, EventKind::Release, suspect.id);
        log("custody.jailed", suspect.id, id);
      }
      r.escort = none;
    }
  }
}
void World::joinQueue(Id id, Id fId) {
  auto &r = residents.at(id);
  auto &f = facilities.at(fId);
  if (looseBodies.contains(id))
    throw std::runtime_error("physics.error.airborne");
  if (!r.alive || !r.present || !f.open || !accessAllowed(id, fId) ||
      !inHours(time, f.opensAt, f.openHours) || !near(id, f.pos, 3))
    throw std::runtime_error("Reach the open facility first");
  if (r.queuedAt == fId)
    return;
  if (r.queuedAt != none)
    leaveQueue(id);
  if (accounts[r.account].balance < f.price)
    throw std::runtime_error("Cannot afford service");
  if (f.queue.size() >= 64)
    throw std::runtime_error("The waiting area is full");
  clockOut(id);
  stopPersonalUse(id);
  r.pos = position(id);
  r.route.clear();
  ++r.routeVersion;
  r.queuedAt = fId;
  r.waitSince = time;
  r.activity = Activity::Queuing;
  f.queue.push_back(id);
  serve(fId);
}
void World::leaveQueue(Id id) {
  auto &r = residents.at(id);
  if (r.queuedAt == none)
    return;
  auto &f = facilities[r.queuedAt];
  r.pos = position(id);
  r.route.clear();
  ++r.routeVersion;
  std::erase(f.queue, id);
  f.stock += int(std::erase_if(f.serving, [&](auto &s) { return s.resident == id; }));
  for (Id item : r.items)
    if (items[item].reservedAt == f.id) {
      items[item].reservedAt = none;
      items[item].carrier = r.alive && r.present ? id : none;
      items[item].pos = r.pos;
      if (r.alive && r.outfit == none && items[item].kind == ItemKind::Clothes)
        r.outfit = item;
    }
  r.queuedAt = none;
  if (r.activity == Activity::Queuing || r.activity == Activity::Using)
    r.activity = Activity::Idle;
  serve(f.id);
}
Vec3 World::servicePosition(Id fid, int station) const {
  const auto &f = facilities.at(fid);
  if (f.kind == FacilityKind::Washbasin) {
    int columns = std::min(8, f.stations);
    return f.pos + Vec3{(float(station % columns) - (columns - 1) * .5f) * .8f, 0,
                        1.6f + float(station / columns) * 2.2f};
  }
  return f.pos + Vec3{(float(station % 4) - 1.5f) * .72f, 0, 1.6f + float(station / 4) * .8f};
}
void World::serve(Id fId) {
  auto &f = facilities.at(fId);
  std::set<int> used;
  for (auto &s : f.serving) {
    if (s.station < 0) {
      int slot = 0;
      while (used.contains(slot))
        ++slot;
      s.station = slot;
    }
    used.insert(s.station);
  }
  bool available = operational(fId);
  for (auto &s : f.serving) {
    if (!available && s.until >= 0) {
      s.remaining = std::max(.01, s.until - time);
      s.until = -1; // Invalidates the old completion event without losing progress.
    } else if (available && s.until < 0) {
      s.until = time + s.remaining;
      schedule(s.until, EventKind::FinishService, fId, s.resident);
    }
  }
  while (available && f.stock > 0 && int(f.serving.size()) < f.stations && !f.queue.empty()) {
    Id id = f.queue.front();
    auto &r = residents[id];
    if (!r.alive || !r.present || !accessAllowed(id, fId) ||
        accounts[r.account].balance < f.price) {
      f.queue.erase(f.queue.begin());
      r.queuedAt = none;
      if (r.alive)
        r.activity = Activity::Idle;
      continue;
    }
    int station = 0;
    while (std::any_of(f.serving.begin(), f.serving.end(),
                       [&](const Service &s) { return s.station == station; }))
      ++station;
    Vec3 dest = servicePosition(fId, station);
    try {
      routeTo(id, dest);
    } catch (const std::runtime_error &) {
      break;
    } // Keep FIFO and reserved stock unchanged.
    f.queue.erase(f.queue.begin());
    --f.stock;
    r.activity = Activity::Using;
    if (f.kind == FacilityKind::Washbasin)
      reserveWashing(id, fId);
    if (f.kind == FacilityKind::Laundry) {
      std::vector<Id> garments;
      if (r.outfit != none && items[r.outfit].owner == r.account && items[r.outfit].carrier == id &&
          items[r.outfit].reservedAt == none)
        garments.push_back(r.outfit);
      for (Id item : r.items)
        if (item != r.outfit && items[item].kind == ItemKind::Clothes &&
            items[item].owner == r.account && items[item].carrier == id &&
            items[item].reservedAt == none && !items[item].consumed &&
            items[item].cleanliness < .95f && garments.size() < 4)
          garments.push_back(item);
      for (Id item : garments) {
        items[item].reservedAt = fId;
        items[item].carrier = none;
        items[item].pos = f.pos;
        items[item].askingPrice = 0;
        items[item].seller = none;
        if (r.outfit == item)
          r.outfit = none;
      }
    }

    double arrival = r.route.back().time;
    double until = arrival + f.duration;
    f.serving.push_back({id, until, 0, f.price, station});
    schedule(until, EventKind::FinishService, fId, id);
  }
  layoutQueue(fId);
}
void World::layoutQueue(Id fid) {
  auto &f = facilities.at(fid);
  std::vector<Vec3> slots;
  for (std::size_t i = 0; i < f.queue.size(); ++i) {
    auto &r = residents[f.queue[i]];
    int row = int(i / 8), col = int(i % 8);
    if (row % 2)
      col = 7 - col;
    Vec3 dest =
        config.layoutVersion == 1
            ? Vec3{(f.pos.x < 0 ? -1.f : 1.f) * (19.f - col * .75f), f.floor * 3.f,
                   std::clamp(f.pos.z, -12.f, 6.f) + 1.8f + row * .8f}
            : f.pos + Vec3{-3.5f + col, 0,
                           std::max(3.2f, 2.4f + float((f.stations + 3) / 4) * .8f) + row * .85f};
    auto vacant = [&](Vec3 p) {
      return clearPosition(p) &&
             std::none_of(slots.begin(), slots.end(), [&](Vec3 q) { return length(p - q) < .7f; });
    };
    if (!vacant(dest)) {
      bool found = false;
      for (float radius = .4f; radius < 12 && !found; radius += .4f)
        for (int a = 0; a < 32; ++a) {
          Vec3 p = dest + Vec3{std::sin(a * .19635f) * radius, 0, std::cos(a * .19635f) * radius};
          if (vacant(p)) {
            dest = p;
            found = true;
            break;
          }
        }
      if (!found)
        throw std::runtime_error("No free queue standing space");
    }
    slots.push_back(dest);
    if (!r.route.empty() && length(r.route.back().p - dest) < .01f)
      continue;
    routeTo(r.id, dest);
  }
}
std::size_t World::living() const {
  return std::count_if(residents.begin(), residents.end(),
                       [](auto &r) { return r.alive && r.present; });
}
float World::happiness() const {
  double total = 0;
  std::size_t n = 0;
  for (auto &r : residents)
    if (r.alive && r.present) {
      total += r.happiness;
      ++n;
    }
  return n ? float(total / n) : 0;
}
Money World::internalMoney() const {
  Money n = 0;
  for (std::size_t i = 1; i < accounts.size(); ++i)
    n += accounts[i].balance;
  return n;
}
std::string World::displayName(Id id, const std::string &lang) const {
  const auto &n = names.at(id);
  return lang == "zh" ? n.zh : lang == "fr" ? n.fr : n.en;
}
std::string roleKey(Role r) {
  static const char *k[] = {
      "role.unemployed", "role.courier",    "role.warden",  "role.builder", "role.video",
      "role.programmer", "role.medic",      "role.pilot",   "role.admin",   "role.repair",
      "role.cleaner",    "role.vendor",     "role.teacher", "role.chef",    "role.lifeguard",
      "role.attendant",  "role.firefighter"};
  return k[int(r)];
}
std::string activityKey(Activity a) {
  static const char *k[] = {"activity.idle",    "activity.walking",   "activity.queue",
                            "activity.using",   "activity.sleep",     "activity.work",
                            "activity.fight",   "activity.surrender", "activity.restrained",
                            "activity.escort",  "activity.jailed",    "activity.dead",
                            "activity.departed"};
  return k[int(a)];
}
std::string facilityKey(FacilityKind f) {
  static const char *k[] = {
      "facility.food",         "facility.water",     "facility.toilet",     "facility.shop",
      "facility.office",       "facility.infirmary", "facility.jail",       "facility.terminal",
      "facility.workshop",     "facility.garbage",   "facility.lift",       "facility.crematorium",
      "facility.school",       "facility.laundry",   "facility.shower",     "facility.leisure",
      "facility.pool",         "facility.arcade",    "facility.playground", "facility.cinema",
      "facility.garden",       "facility.gym",       "facility.library",    "facility.cafe",
      "facility.dealership",   "facility.parking",   "facility.reception",  "facility.morgue",
      "facility.fire_station", "facility.washbasin"};
  return k[int(f)];
}
} // namespace mega
