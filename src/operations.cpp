#include "mega/world.hpp"
#include <algorithm>
#include <cmath>
namespace mega {
Id World::serviceChoice(Id id, FacilityKind kind) const {
  const auto &r = residents.at(id);
  Vec3 p = position(id);
  Id best = none;
  double score = 1e30;
  int currentFloor = std::clamp(int(std::round(p.y / 3)), 0, config.floors - 1);
  for (auto floor = floorFacilities.lower_bound(currentFloor - 16);
       floor != floorFacilities.end() && floor->first <= currentFloor + 16; ++floor)
    for (Id fid : floor->second)
      if (const auto &f = facilities[fid];
          (f.kind == kind ||
           (kind == FacilityKind::Food &&
            (f.kind == FacilityKind::Cafe || (f.kind == FacilityKind::Shop && r.hunger > .9f)))) &&
          f.open && f.stock > 0 && accessAllowed(id, f.id) &&
          accounts[r.account].balance >= f.price && f.queue.size() < 64 && operational(f.id)) {
        // Compare the walking/climbing time and expected queue, not just straight-line distance.
        double seconds = (std::hypot(p.x - f.pos.x, p.z - f.pos.z) / walkingSpeed(id) +
                          std::abs(p.y - f.pos.y) / 3.0) *
                         (1440 / config.dayMinutes);
        // Routine needs must remain local. A kitchen opening briefly hundreds of
        // floors away is not a sensible meal plan, even when nearby chefs are on break.
        if (seconds > 7200 || std::abs(f.floor - int(std::round(p.y / 3))) > 16)
          continue;
        seconds += (f.queue.size() + f.serving.size()) * f.duration / f.stations;
        if (kind == FacilityKind::Food && f.kind == FacilityKind::Shop)
          seconds += 600;
        if (seconds < score) {
          score = seconds;
          best = f.id;
        }
      }
  return best;
}
static bool manages(const World &w, Id actor, const Facility &f) {
  return actor >= 0 && std::size_t(actor) < w.residents.size() && w.residents[actor].alive &&
         w.residents[actor].present &&
         (w.residents[actor].account == w.operatingAccount(f.id) || actor == f.manager ||
          w.directsCompany(actor, w.companyForAccount(w.operatingAccount(f.id))) ||
          (actor == w.dormOwner && w.operatingAccount(f.id) == w.dormAccount));
}
void World::setBusiness(Id actor, Id id, Money price, Money wage, bool hiring, bool restock) {
  auto &f = facilities.at(id);
  if (!manages(*this, actor, f) || price < 0 || price > 1000000 || wage < 100 || wage > 100000)
    throw std::runtime_error(
        "Only the business owner or manager can set affordable operating terms");
  for (Id worker : f.employees)
    clockOut(worker);
  f.price = price;
  f.wageOffer = wage;
  f.hiring = hiring;
  f.autoRestock = restock;
  for (Id worker : f.employees)
    residents[worker].hourlyWage = wage;
  log("business.updated", actor, id);
}
void World::hireEmployee(Id actor, Id id, Id worker, Role role, double shift) {
  auto &f = facilities.at(id);
  auto &r = residents.at(worker);
  if (!manages(*this, actor, f) || !f.open || !f.hiring || !r.alive || !r.present || !r.automatic ||
      r.leaving || r.role != Role::Unemployed || !std::isfinite(shift) || shift < 0 ||
      shift >= 24 || accounts[operatingAccount(id)].balance < f.wageOffer * 8)
    throw std::runtime_error("Hiring requires a willing available resident and a funded shift");
  Id company = companyForAccount(operatingAccount(id));
  if (f.kind == FacilityKind::Office && company != none &&
      (f.employees.size() >= std::size_t(f.stations * 3) ||
       accounts[companies[company].account].balance < companyReserve(company) + f.wageOffer * 8))
    throw std::runtime_error(
        "Office hiring needs a free shift position and a funded payroll reserve");
  chooseJob(worker, role, id);
  r.hourlyWage = f.wageOffer;
  r.routine.shiftStart = shift;
  r.routine.sleepStart = std::fmod(shift + (shift >= 20 ? 10 : 14), 24);
  log("business.hired", worker, id);
}
Id World::requestStock(Id actor, Id id, int quantity) {
  auto &f = facilities.at(id);
  if (!manages(*this, actor, f) || !f.open || quantity < 100 || quantity > 10000)
    throw std::runtime_error("Only the owner or manager can place a funded stock order");
  for (auto &s : freight)
    if (!s.waste && s.facility == id && s.state != FreightState::Delivered)
      throw std::runtime_error("A shipment is already outstanding");
  Money unit = f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe ? 140
               : f.kind == FacilityKind::Water                              ? 3
               : f.kind == FacilityKind::Shop                               ? 180
               : f.kind == FacilityKind::Workshop                           ? 3500
               : f.kind == FacilityKind::Toilet                             ? 2
                                                                            : 10;
  Money cost =
            quantity * unit + (config.layoutVersion > 1 && f.floor >= 25 ? 1000 + f.floor * 2 : 0),
        fee = 1500 + f.floor * 5;
  if (accounts[operatingAccount(id)].balance < cost + fee)
    throw std::runtime_error("The business cannot afford supplies and freight");
  Freight s;
  s.id = Id(freight.size());
  s.facility = id;
  s.quantity = quantity;
  s.cost = cost;
  s.fee = fee;
  s.placed = time;
  s.escrow = account("Freight wages " + std::to_string(s.id));
  pay(operatingAccount(id), 0, cost, "outside-supply-import");
  pay(operatingAccount(id), s.escrow, fee, "freight-escrow");
  Item crate;
  crate.id = Id(items.size());
  crate.kind = ItemKind::Materials;
  crate.owner = operatingAccount(id);
  crate.reservedAt = id;
  crate.pos =
      layout(0).entrance() + Vec3{float((s.id % 8) - 4) * 1.4f, 0, -4.f - float(s.id % 4) * 1.2f};
  crate.pos = accessible(crate.pos);
  if (config.layoutVersion > 1 && f.floor >= 25) {
    s.landing = {layout(f.floor).halfWidth() - 4, f.floor * 3.f, -40.f - float(s.id % 4) * 2.f};
    s.landing = accessible(s.landing);
    s.origin = {layout(f.floor).halfWidth() + 25, 3, s.landing.z};
    s.readyAt = time + (length(s.landing - s.origin) / 45 + 15) * (1440 / config.dayMinutes);
    crate.pos = s.origin;
  }
  s.item = crate.id;
  items.push_back(crate);
  freight.push_back(s);
  log("business.stock_ordered", actor, s.id);
  return s.id;
}
Vec3 World::freightPosition(Id id) const {
  const auto &s = freight.at(id);
  if (s.readyAt <= 0)
    return items[s.item].pos;
  float t = std::clamp(float((time - s.placed) / std::max(1.0, s.readyAt - s.placed)), 0.f, 1.f);
  if (t < .85f) {
    Vec3 peak{s.origin.x, s.landing.y + 2, s.origin.z};
    return s.origin + (peak - s.origin) * (t / .85f);
  }
  Vec3 peak{s.origin.x, s.landing.y + 2, s.origin.z};
  return peak + (s.landing - peak) * ((t - .85f) / .15f);
}
void World::freightTick() {
  for (auto &s : freight)
    if (s.readyAt > 0 && s.state == FreightState::Waiting && items[s.item].carrier == none) {
      items[s.item].pos = freightPosition(s.id);
      if (time >= s.readyAt)
        s.readyAt = 0;
    }
}
void World::assignFreight(Id worker, Id id) {
  auto &r = residents.at(worker);
  auto &s = freight.at(id);
  if (s.readyAt > 0 && time >= s.readyAt) {
    items[s.item].pos = s.landing;
    s.readyAt = 0;
  }
  if (!r.alive || !r.present || (s.waste ? r.role != Role::Cleaner : r.role != Role::Courier) ||
      !onShift(worker) || r.carrying != none || r.bodyCarrying != none || r.medicalDuty != none ||
      s.state != FreightState::Waiting || s.worker != none || time < s.readyAt)
    throw std::runtime_error("Courier or freight unavailable");
  travel(worker, items[s.item].pos, TaskKind::FreightPickup, id);
  s.worker = worker;
  s.state = FreightState::Collecting;
}
void World::collectFreight(Id worker, Id id) {
  auto &r = residents.at(worker);
  auto &s = freight.at(id);
  auto &i = items.at(s.item);
  if (!r.alive || !r.present || s.worker != worker || s.state != FreightState::Collecting ||
      time < s.readyAt || !near(worker, i.pos) || r.carrying != none || i.carrier != none ||
      i.consumed)
    throw std::runtime_error("Reach the supply crate with an empty trolley");
  if (s.waste && s.source != none) {
    facilities[s.source].waste = std::max(0, facilities[s.source].waste - s.quantity);
    s.source = none;
  }
  r.carrying = i.id;
  i.carrier = worker;
  s.state = FreightState::Carrying;
  travel(worker, facilities[s.facility].pos + Vec3{0, 0, 1.6f}, TaskKind::FreightDrop, id);
}
void World::deliverFreight(Id worker, Id id) {
  auto &r = residents.at(worker);
  auto &s = freight.at(id);
  auto &f = facilities.at(s.facility);
  auto &i = items.at(s.item);
  if (!r.alive || !r.present || s.worker != worker || s.state != FreightState::Carrying ||
      r.carrying != i.id || i.carrier != worker || !near(worker, f.pos, 2.5f))
    throw std::runtime_error("Bring the supplies to the actual business");
  if (s.waste) {
    f.waste += s.quantity;
    pay(s.escrow, f.owner, s.cost, "waste-disposal");
  } else
    f.stock += s.quantity;
  r.carrying = none;
  i.carrier = none;
  i.consumed = true;
  i.reservedAt = none;
  s.state = FreightState::Delivered;
  pay(s.escrow, r.account, s.fee, "freight-delivered");
  r.activity = Activity::Idle;
  serve(f.id);
  log("business.stock_arrived", worker, f.id);
}
void World::listHome(Id actor, Id home, Money price) {
  auto &h = housing.at(home);
  auto &r = residents.at(actor);
  if (!r.alive || !r.present || !h.active || h.owner != r.account || price < 100 ||
      price > 1000000000)
    throw std::runtime_error("Only the owner may list this home");
  h.price = price;
  h.listed = true;
  log("housing.listed", actor, home);
}
void World::settleRent(Id id) {
  auto &r = residents.at(id);
  Id home = residentHomes.at(id);
  if (home == none)
    return;
  auto &h = housing[home];
  if (h.owner == r.account) {
    h.arrears = 0;
    h.paidUntil = time + 86400;
    return;
  }
  if (h.paidUntil == 0)
    h.paidUntil = time + 86400;
  while (h.paidUntil <= time) {
    h.arrears += h.rent;
    h.paidUntil += 86400;
  }
  Money amount = std::min(h.arrears, accounts[r.account].balance);
  if (amount) {
    pay(r.account, h.owner, amount, "daily-rent");
    h.arrears -= amount;
  }
  if (h.arrears > 0)
    r.happiness = std::max(0.f, r.happiness - .025f);
}
Id World::immigrate(Id unit, Money capital) {
  auto &h = housing.at(unit);
  if (!h.active || h.tenant != none || residents.size() >= 500000 || capital < h.rent ||
      capital > 10000000)
    throw std::runtime_error("A vacant bed and sufficient incoming capital are required");
  Resident r;
  r.id = Id(residents.size());
  r.account = account("New resident " + std::to_string(r.id + 1), capital);
  r.home = h.floor;
  r.pos = layout(0).entrance();
  r.needsAt = time;
  r.age = 18 + int(random() * 50);
  r.aggression = config.aggression * random();
  r.routine.sleepStart = 22 + random() * 2;
  r.routine.sleepHours = 7 + random();
  for (auto &interest : r.interests)
    interest = random();
  const char *en[] = {"Camille Laurent", "Jules Martin", "Mei Zhang", "Lin Chen",
                      "Sofia Wilson",    "Hugo Bernard", "Lucie Roy", "David Li"};
  const char *fr[] = {"Camille Laurent", "Jules Martin", "Mei Zhang", "Lin Chen",
                      "Sofia Wilson",    "Hugo Bernard", "Lucie Roy", "David Li"};
  const char *zh[] = {"卡米耶·洛朗",   "朱尔·马丁",   "张梅",      "陈林",
                      "索菲亚·威尔逊", "雨果·贝尔纳", "露西·罗伊", "李大卫"};
  int name = int(random() * 8);
  r.name = en[name];
  names.push_back({en[name], fr[name], zh[name]});
  residents.push_back(r);
  residentHomes.push_back(none);
  for (ItemKind kind : {ItemKind::Phone, ItemKind::Clothes}) {
    Item item;
    item.id = Id(items.size());
    item.owner = r.account;
    item.carrier = r.id;
    item.kind = kind;
    item.pos = r.pos;
    item.look.primary = int(random() * 8);
    items.push_back(item);
    residents.back().items.push_back(item.id);
    if (kind == ItemKind::Clothes)
      residents.back().outfit = item.id;
  }
  rentHome(r.id, unit);
  schedule(time + 30, EventKind::Decide, r.id);
  log("resident.arrived", r.id, unit);
  return r.id;
}
void World::refreshManagement() {
  std::map<Id, Id> accountHolders;
  std::map<int, std::vector<Id>> local;
  for (const auto &r : residents)
    if (r.alive && r.present && !r.leaving && r.activity != Activity::Jailed) {
      accountHolders[r.account] = r.id;
      if (r.id != player)
        local[r.home].push_back(r.id);
    }
  for (auto &f : facilities) {
    auto eligible = [&](Id id) {
      return id >= 0 && std::size_t(id) < residents.size() && residents[id].alive &&
             residents[id].present && !residents[id].leaving &&
             residents[id].activity != Activity::Jailed;
    };
    if (eligible(f.manager) || f.demolished)
      continue;
    Id appointed = none;
    Id company = companyForAccount(operatingAccount(f.id));
    if (company != none && eligible(companies[company].owner))
      appointed = companies[company].owner;
    else if (operatingAccount(f.id) == dormAccount && eligible(dormOwner))
      appointed = dormOwner;
    else if (accountHolders.contains(operatingAccount(f.id)))
      appointed = accountHolders.at(operatingAccount(f.id));
    else {
      // An independent business keeps its operating account when its manager dies.
      // Promote surviving local staff before recruiting a neighborhood caretaker.
      for (Id employee : f.employees)
        if (eligible(employee)) {
          appointed = employee;
          break;
        }
      if (appointed == none && !local.empty()) {
        auto next = local.lower_bound(f.floor);
        auto best = next;
        if (next != local.begin()) {
          auto previous = std::prev(next);
          if (next == local.end() || f.floor - previous->first <= next->first - f.floor)
            best = previous;
        }
        if (best != local.end())
          appointed = best->second[std::size_t(f.id) % best->second.size()];
      }
    }
    if (appointed != none) {
      f.manager = appointed;
      if (time > 6 * 3600)
        log("business.manager_changed", appointed, f.id);
    }
  }
}
void World::economyTick() {
  companyTick();
  refreshManagement();
  for (auto &f : facilities)
    if (f.open && f.condition < .5f && f.manager != none)
      try {
        requestRepair(f.manager, f.id);
      } catch (const std::runtime_error &) {
      }
  // Owner-funded supply imports wait as real crates at the entrance.
  for (auto &f : facilities)
    if (f.open && f.autoRestock && f.stock < 600 && f.manager != none) {
      try {
        requestStock(f.manager, f.id, f.kind == FacilityKind::Workshop ? 100 : 2000);
      } catch (const std::runtime_error &) {
      }
    }
  for (auto &f : facilities)
    if (f.open && f.waste >= 30 && !(f.kind == FacilityKind::Garbage && f.floor == 0) &&
        f.manager != none) {
      try {
        requestWaste(f.manager, f.id);
      } catch (const std::runtime_error &) {
      }
    }
  // Fill actual shift vacancies from willing local job seekers. No worker is teleported.
  std::map<int, std::vector<Id>> jobseekers;
  for (auto &r : residents)
    if (r.id != player && r.automatic && r.alive && r.present && r.role == Role::Unemployed &&
        !r.leaving)
      jobseekers[r.home].push_back(r.id);
  for (auto &f : facilities)
    if (f.open && f.hiring && (f.staffNeeded > 0 || f.kind == FacilityKind::FireStation) &&
        f.manager != none) {
      int shifts = f.openHours == 24 ? 3 : f.openHours > 8 ? 2 : 1;
      for (int shift = 0; shift < shifts; ++shift) {
        double starts =
            f.openHours == 24 ? 6 + shift * 8 : f.opensAt + shift * std::max(0.0, f.openHours - 8);
        int staff = 0;
        for (Id id : f.employees)
          if (residents[id].alive && residents[id].present && residents[id].role == f.staffRole &&
              std::abs(residents[id].routine.shiftStart - starts) < .1)
            ++staff;
        int target = std::max(1, f.staffNeeded) +
                     (f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe ? 1 : 0);
        if (staff >= target)
          continue;
        Id candidate = none;
        for (int distance = 0; distance < 12 && candidate == none; ++distance)
          for (int sign : {-1, 1}) {
            auto &pool = jobseekers[f.floor + distance * sign];
            while (!pool.empty() && residents[pool.back()].role != Role::Unemployed)
              pool.pop_back();
            if (!pool.empty()) {
              candidate = pool.back();
              pool.pop_back();
              break;
            }
          }
        if (candidate != none)
          try {
            hireEmployee(f.manager, f.id, candidate, f.staffRole, starts);
          } catch (const std::runtime_error &) {
          }
      }
    }
  int day = int(time / 86400);
  if (day <= lastEconomyDay)
    return;
  lastEconomyDay = day;
  for (auto &r : residents)
    if (r.alive && r.present) {
      settleRent(r.id);
      if (r.automatic && r.id != player && r.medicalUntil <= time && hasDevice(r.id) &&
          accounts[r.account].balance > 90000 && random() < .03f) {
        Id provider = nearest(r.id, FacilityKind::Infirmary);
        if (provider != none)
          try {
            subscribeMedical(r.id, provider,
                             accounts[r.account].balance > 500000   ? 2
                             : accounts[r.account].balance > 250000 ? 1
                                                                    : 0);
          } catch (const std::runtime_error &) {
          }
      }
      if (r.id != player && r.id != dormOwner && r.automatic && !r.leaving &&
          r.medicalDuty == none && r.carrying == none && r.ambulance == none &&
          r.carriedBy == none && day >= 2 &&
          (r.happiness < .22f || (accounts[r.account].balance < 500 && r.hunger > 1)) &&
          random() < .35f) {
        r.leaving = true;
        log("resident.leaving", r.id);
      }
      // Employed office teams sell completed work to outside clients. Earnings require logged
      // attendance.
    }
  for (auto &h : housing)
    if (h.tenant != none && (!residents[h.tenant].alive || !residents[h.tenant].present)) {
      residentHomes[h.tenant] = none;
      h.tenant = none;
      h.arrears = 0;
    }
  for (auto &f : facilities)
    if (f.kind == FacilityKind::Garbage && f.floor == 0 && f.waste > 0) {
      int quantity = std::min(f.waste, int(accounts[f.owner].balance / 5));
      pay(f.owner, 0, quantity * 5, "outside-waste-collection");
      f.waste -= quantity;
    }
  for (auto &f : facilities)
    if (f.open && f.owner != dormAccount) {
      Money rent = f.kind == FacilityKind::Office ? 2500 : 500;
      pay(f.owner, dormAccount, std::min(rent, accounts[f.owner].balance), "workspace-ground-rent");
    }
  float mood = happiness();
  if (mood > .5f) {
    std::vector<Id> empty;
    for (Id i = 0; i < Id(housing.size()); ++i)
      if (housing[i].active && housing[i].tenant == none)
        empty.push_back(i);
    int count = std::min({int(empty.size()), std::max(1, int(living() / 2000)), 100});
    for (int n = 0; n < count; ++n) {
      int which = int(random() * empty.size());
      Id unit = empty[which];
      empty.erase(empty.begin() + which);
      immigrate(unit, Money(config.startingCash * (.7f + random())));
    }
  }
}
} // namespace mega
namespace mega {
Id World::requestWaste(Id actor, Id fid) {
  auto &f = facilities.at(fid);
  if (!manages(*this, actor, f) || !f.open || f.waste < 1)
    throw std::runtime_error("No authorized waste collection");
  for (auto &s : freight)
    if (s.waste && s.source == fid && s.state != FreightState::Delivered)
      throw std::runtime_error("Waste pickup already assigned");
  Id destination = none;
  float best = 1e20f;
  for (auto &candidate : facilities)
    if (candidate.kind == FacilityKind::Garbage && candidate.open && candidate.id != fid &&
        (f.kind != FacilityKind::Garbage || candidate.floor < f.floor)) {
      float d = length(candidate.pos - f.pos);
      if (d < best) {
        best = d;
        destination = candidate.id;
      }
    }
  if (destination == none)
    throw std::runtime_error("No reachable waste depot");
  Freight s;
  s.id = Id(freight.size());
  s.facility = destination;
  s.source = fid;
  s.waste = true;
  s.quantity = std::min(200, f.waste);
  s.cost = s.quantity * 10;
  s.fee = 600 + std::abs(f.floor - facilities[destination].floor) * 5;
  s.placed = time;
  if (accounts[operatingAccount(fid)].balance < s.cost + s.fee)
    throw std::runtime_error("Business cannot fund waste removal");
  s.escrow = account("Sanitation contract");
  pay(operatingAccount(fid), s.escrow, s.cost + s.fee, "waste-collection-escrow");
  Item bag;
  bag.id = Id(items.size());
  bag.kind = ItemKind::Waste;
  bag.owner = operatingAccount(fid);
  bag.pos = accessible(f.pos + Vec3{0, 0, 1.6f});
  bag.reservedAt = fid;
  s.item = bag.id;
  items.push_back(bag);
  freight.push_back(s);
  log("business.waste_ordered", actor, s.id);
  return s.id;
}
} // namespace mega
