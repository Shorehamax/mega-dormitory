#include "mega/world.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
namespace mega {
namespace {
bool canPlan(const World &w, Id id) {
  const auto &r = w.residents.at(id);
  return !w.gameOver && r.automatic && r.alive && r.present && !r.leaving && r.age >= 18 &&
         !r.surrendered && r.activity == Activity::Idle && r.route.empty() && r.queuedAt == none &&
         r.carrying == none && r.bodyCarrying == none && r.ladder < 0 && r.vehicle == none &&
         r.lift == none && r.ambulance == none && r.carriedBy == none && r.medicalDuty == none &&
         !w.looseBodies.contains(id) && !w.personalUses.contains(id);
}
bool active(const Order &o) {
  return o.state == OrderState::Waiting || o.state == OrderState::ToPickup ||
         o.state == OrderState::ToCustomer;
}
int held(const World &w, Id id, ItemKind kind) {
  const auto &r = w.residents[id];
  int n = 0;
  for (Id item : r.items) {
    const auto &i = w.items[item];
    if (i.owner == r.account && i.carrier == id && i.kind == kind && !i.consumed &&
        i.reservedAt == none && i.durability > .2f && !w.itemInUse(item))
      ++n;
  }
  return n;
}
Money wage(Role role, const Facility &f) {
  if (role == f.staffRole)
    return f.wageOffer;
  return role == Role::Programmer || role == Role::Firefighter ? 1800
         : role == Role::Warden                                ? 1400
                                                               : 900;
}
bool local(const World &w, Id id, Vec3 p, int floors = 12, float distance = 140) {
  auto here = w.position(id);
  return std::abs(here.y - p.y) <= floors * 3 && std::hypot(here.x - p.x, here.z - p.z) <= distance;
}
void clearTrade(ResidentPlan &p, AutonomyAction action) {
  p.tradeItem = p.seller = none;
  p.tradePrice = 0;
  p.tradeDeadline = 0;
  p.tradeRetries = 0;
  p.action = action;
}
} // namespace
Money World::productPrice(ItemKind kind) {
  switch (kind) {
  case ItemKind::Food:
    return 800;
  case ItemKind::Phone:
    return 15000;
  case ItemKind::Laptop:
    return 65000;
  case ItemKind::Clothes:
    return 2500;
  case ItemKind::Medicine:
    return 2000;
  case ItemKind::Handgun:
    return 120000;
  case ItemKind::Ammunition:
    return 1500;
  case ItemKind::Fan:
    return 5000;
  case ItemKind::Tableware:
    return 1000;
  case ItemKind::RiceCooker:
    return 8000;
  case ItemKind::Tools:
  case ItemKind::Extinguisher:
    return 4500;
  default:
    throw std::runtime_error("Product unavailable");
  }
}
Money World::deliveryPrice(Id buyer, Id shop) const {
  return 200 + Money(std::abs(position(buyer).y - facilities.at(shop).pos.y)) * 2;
}
Money World::householdReserve(Id id) const {
  const auto &r = residents.at(id);
  long double reserve = 6000; // food, water, sanitation and a small medical margin
  Id home = residentHomes.at(id);
  if (home != none && housing[home].owner != r.account)
    reserve += static_cast<long double>(housing[home].rent) * 3 + housing[home].arrears;
  for (const auto &f : facilities)
    if (f.owner == r.account && f.open && !f.demolished)
      reserve += 30000 + static_cast<long double>(f.wageOffer) * std::max(0, f.staffNeeded) * 8;
  return Money(std::clamp(reserve, 6000.L, 1000000000000.L));
}
void World::householdShopping(Id id) {
  if (!canPlan(*this, id))
    return;
  auto &r = residents[id];
  auto &p = residentPlans[id];
  if (time < p.nextShopping)
    return;
  p.nextShopping = time + 7200 + (id % 7) * 300;
  p.reserve = householdReserve(id);
  int day = int(time / 86400);
  if (p.day != day) {
    p.day = day;
    p.spentToday = 0;
  }
  if (p.order != none && std::size_t(p.order) < orders.size() && active(orders[p.order])) {
    p.action = AutonomyAction::WaitingDelivery;
    return;
  }
  p.order = none;
  // Existing player-issued orders still count when an NPC is put on autopilot.
  for (const auto &o : orders)
    if (o.buyer == id && active(o)) {
      p.order = o.id;
      p.action = AutonomyAction::WaitingDelivery;
      return;
    }
  Money cash = accounts[r.account].balance;
  Money available = std::min<Money>(cash - p.reserve, 20000 - p.spentToday);
  if (available < 1000) {
    p.action = AutonomyAction::Essentials;
    return;
  }
  std::optional<ItemKind> desired;
  if (r.health < 80 && !held(*this, id, ItemKind::Medicine))
    desired = ItemKind::Medicine;
  else if (!held(*this, id, ItemKind::Food) && r.hunger > .3f)
    desired = ItemKind::Food;
  else if (!held(*this, id, ItemKind::Phone) && !held(*this, id, ItemKind::Laptop))
    desired = ItemKind::Phone;
  else if (held(*this, id, ItemKind::Clothes) < 2 &&
           (r.outfit == none || items[r.outfit].cleanliness < .65f ||
            items[r.outfit].durability < .3f))
    desired = ItemKind::Clothes;
  else if (held(*this, id, ItemKind::Food) && !held(*this, id, ItemKind::Tableware))
    desired = ItemKind::Tableware;
  else if (!held(*this, id, ItemKind::Fan) && r.interests[1] > .6f && available > 10000)
    desired = ItemKind::Fan;
  else if (!held(*this, id, ItemKind::RiceCooker) && held(*this, id, ItemKind::Tableware) &&
           r.interests[0] > .7f && cash > p.reserve + 40000)
    desired = ItemKind::RiceCooker;
  else if (held(*this, id, ItemKind::Food) < 2)
    desired = ItemKind::Food;
  else if ((r.role == Role::Programmer || r.role == Role::VideoEditor) &&
           !held(*this, id, ItemKind::Laptop) && cash > p.reserve + 120000 && p.spentToday == 0) {
    desired = ItemKind::Laptop;
    available = cash - p.reserve - 40000;
  }
  if (!desired) {
    p.action = AutonomyAction::Saving;
    return;
  }
  p.wanted = *desired;
  Money retail = productPrice(*desired);
  // Real second-hand stock is compared before paying for a new parcel.
  Id offer = none;
  Money best = std::min(available, retail * 9 / 10);
  for (Id offerId : itemOffers) {
    const auto &i = items[offerId];
    if (i.kind == *desired && i.askingPrice > 0 && i.askingPrice <= best && i.seller != none &&
        i.seller != id && !i.consumed && i.reservedAt == none && i.durability >= .35f &&
        i.stolenFrom == none && i.owner == residents[i.seller].account && i.carrier == i.seller &&
        residents[i.seller].alive && residents[i.seller].present && !itemInUse(i.id) &&
        residents[i.seller].outfit != i.id && local(*this, id, position(i.seller), 4, 80)) {
      offer = i.id;
      best = i.askingPrice;
    }
  }
  if (offer != none) {
    p.tradeItem = offer;
    p.seller = items[offer].seller;
    p.tradePrice = best;
    p.tradeDeadline = time + 1800;
    p.tradeRetries = 0;
    p.action = AutonomyAction::Trading;
    try {
      if (near(id, position(p.seller), 2.5f))
        finishAutonomyTrade(id);
      else
        travel(id, position(p.seller), TaskKind::TradeMeet, p.seller);
    } catch (const std::runtime_error &) {
      clearTrade(p, AutonomyAction::TradeCancelled);
    }
    return;
  }
  if (!hasDevice(id)) {
    Id terminal = serviceChoice(id, FacilityKind::Terminal);
    if (terminal != none && available >= retail + 500 + facilities[terminal].price * 5 &&
        local(*this, id, facilities[terminal].pos)) {
      p.action = AutonomyAction::NeedConnection;
      try {
        travel(id, facilities[terminal].pos, TaskKind::ShoppingTerminal, terminal);
      } catch (const std::runtime_error &) {
        p.action = AutonomyAction::NoSupply;
      }
    } else
      p.action = AutonomyAction::NeedConnection;
    return;
  }
  Id shop = none;
  Money quote = std::numeric_limits<Money>::max();
  int floor = int(std::round(position(id).y / 3));
  for (auto row = floorFacilities.lower_bound(floor - 12);
       row != floorFacilities.end() && row->first <= floor + 12; ++row)
    for (Id fid : row->second) {
      const auto &f = facilities[fid];
      if (f.kind != FacilityKind::Shop || f.stock <= 0 || !operational(fid) ||
          !accessAllowed(id, fid) || !local(*this, id, f.pos))
        continue;
      int couriers = 0, backlog = 0;
      for (Id worker : f.employees) {
        const auto &c = residents[worker];
        if (c.alive && c.present && c.role == Role::Courier && c.jobSite == fid && c.automatic &&
            onShift(worker) && !c.surrendered && c.activity != Activity::Jailed)
          ++couriers;
      }
      for (const auto &o : orders)
        if (o.shop == fid && active(o))
          ++backlog;
      if (!couriers || backlog >= couriers * 4)
        continue;
      Money total = retail + deliveryPrice(id, fid);
      if (total <= available && total < quote) {
        quote = total;
        shop = fid;
      }
    }
  if (shop == none) {
    p.action = available < retail + 200 ? AutonomyAction::Essentials : AutonomyAction::NoSupply;
    return;
  }
  try {
    p.order = order(id, shop, *desired);
    p.spentToday += quote;
    p.action = AutonomyAction::Ordered;
  } catch (const std::runtime_error &) {
    p.action = AutonomyAction::NoSupply;
  }
}
void World::finishAutonomyTrade(Id id) {
  auto found = residentPlans.find(id);
  if (found == residentPlans.end())
    return;
  auto &p = found->second;
  auto &r = residents.at(id);
  if (!r.automatic || !r.alive || !r.present || p.tradeItem == none || p.seller == none ||
      time > p.tradeDeadline || std::size_t(p.tradeItem) >= items.size() ||
      !residents[p.seller].alive || !residents[p.seller].present) {
    clearTrade(p, AutonomyAction::TradeCancelled);
    return;
  }
  auto &i = items[p.tradeItem];
  if (i.seller != p.seller || i.askingPrice != p.tradePrice || i.askingPrice <= 0 ||
      i.askingPrice > accounts[r.account].balance - householdReserve(id) || i.reservedAt != none ||
      i.carrier != p.seller || i.owner != residents[p.seller].account || i.consumed) {
    clearTrade(p, AutonomyAction::TradeCancelled);
    return;
  }
  if (!near(id, position(p.seller), 2.5f)) {
    if (++p.tradeRetries > 3 || !local(*this, id, position(p.seller), 4, 80)) {
      clearTrade(p, AutonomyAction::TradeCancelled);
      return;
    }
    try {
      travel(id, position(p.seller), TaskKind::TradeMeet, p.seller);
    } catch (const std::runtime_error &) {
      clearTrade(p, AutonomyAction::TradeCancelled);
    }
    return;
  }
  try {
    buyItem(id, p.tradeItem);
    p.spentToday += p.tradePrice;
    clearTrade(p, AutonomyAction::Received);
  } catch (const std::runtime_error &) {
    clearTrade(p, AutonomyAction::TradeCancelled);
  }
}
void World::careerDecision(Id id) {
  if (!canPlan(*this, id))
    return;
  auto &r = residents[id];
  auto &p = residentPlans[id];
  // Physical contracts need a completed handover before choosing unrelated work.
  bool committed =
      std::any_of(projects.begin(), projects.end(),
                  [&](const Project &job) {
                    return job.worker == id && job.state == ProjectState::Working;
                  }) ||
      std::any_of(expansions.begin(), expansions.end(),
                  [&](const Expansion &job) { return job.worker == id && !job.complete; }) ||
      std::any_of(housingWorks.begin(), housingWorks.end(),
                  [&](const HousingWork &job) {
                    return job.worker == id && (job.state == ProjectState::Funded ||
                                                job.state == ProjectState::Working);
                  }) ||
      std::any_of(repairs.begin(), repairs.end(),
                  [&](const Repair &job) { return job.worker == id && !job.complete; });
  if (committed)
    return;
  if (time < p.nextCareer || time - p.lastCareer < 86400)
    return;
  p.nextCareer = time + 21600 + id % 1800;
  auto claim = r.routine.wageClaims.find(r.employer);
  double owed = r.routine.unpaidWages + (claim == r.routine.wageClaims.end() ? 0 : claim->second);
  bool unhappy = r.jobSite == none || r.role == Role::Unemployed || !facilities[r.jobSite].open ||
                 owed > r.hourlyWage * 8;
  Id best = none;
  Role role = Role::Unemployed;
  double score = 0;
  double starts = 0;
  int floor = int(std::round(position(id).y / 3));
  for (auto row = floorFacilities.lower_bound(floor - 12);
       row != floorFacilities.end() && row->first <= floor + 12; ++row)
    for (Id fid : row->second) {
      const auto &f = facilities[fid];
      if (!f.open || f.demolished || !f.hiring || activeFires.contains(fid) || f.id == r.jobSite ||
          !local(*this, id, f.pos))
        continue;
      std::vector<Role> roles;
      if ((f.staffNeeded || f.kind == FacilityKind::Garbage ||
           f.kind == FacilityKind::FireStation) &&
          f.staffRole != Role::Unemployed)
        roles.push_back(f.staffRole);
      if (f.kind == FacilityKind::Jail)
        roles.push_back(Role::Warden);
      if (f.kind == FacilityKind::Infirmary) {
        roles.push_back(Role::Pilot);
        roles.push_back(Role::Warden);
      }
      if (f.kind == FacilityKind::Shop)
        roles.push_back(Role::Courier);
      if (f.kind == FacilityKind::Workshop) {
        roles.push_back(Role::Builder);
        roles.push_back(Role::Repairer);
      }
      if (f.kind == FacilityKind::Office) {
        roles.push_back(Role::Programmer);
        roles.push_back(Role::VideoEditor);
        roles.push_back(Role::Administrator);
      }
      for (Role candidate : roles) {
        int shifts = f.openHours >= 24 ? 3 : f.openHours > 8 ? 2 : 1;
        for (int shift = 0; shift < shifts; ++shift) {
          double start =
              f.openHours >= 24 ? 6 + shift * 8 : f.opensAt + shift * std::max(0., f.openHours - 8);
          int occupied = 0;
          for (Id employee : f.employees) {
            const auto &other = residents[employee];
            if (other.alive && other.present && other.role == candidate && other.jobSite == fid &&
                std::abs(other.routine.shiftStart - start) < .1)
              ++occupied;
          }
          int capacity =
              candidate == f.staffRole ? std::max(1, f.staffNeeded) : std::max(1, f.stations);
          if (f.kind == FacilityKind::Office && f.employees.size() >= std::size_t(f.stations * 3))
            continue;
          Money offer = wage(candidate, f);
          if (occupied >= capacity || accounts[operatingAccount(fid)].balance < offer * 8 ||
              (!unhappy && offer < Money(r.hourlyWage * 1.35)))
            continue;
          Id company = companyForAccount(operatingAccount(fid));
          if (company != none &&
              accounts[operatingAccount(fid)].balance < companyReserve(company) + offer * 8)
            continue;
          double commute = length(position(id) - f.pos) / walkingSpeed(id);
          int interest = candidate == Role::Programmer    ? 6
                         : candidate == Role::VideoEditor ? 3
                         : candidate == Role::Courier     ? 5
                         : candidate == Role::Chef        ? 0
                                                          : 1;
          double fit = offer * (1 + .25 * r.interests[interest]) - commute * 5;
          if (fit > score) {
            best = fid;
            role = candidate;
            score = fit;
            starts = start;
          }
        }
      }
    }
  if (best == none) {
    if (unhappy)
      p.action = AutonomyAction::JobSeeking;
    return;
  }
  try {
    chooseJob(id, role, best);
    r.hourlyWage = wage(role, facilities[best]);
    r.routine.shiftStart = starts;
    r.routine.sleepStart = std::fmod(starts + (starts >= 20 ? 10 : 14), 24);
    r.routine.daysOff = facilities[best].kind == FacilityKind::Office ? 96 : 0;
    p.lastCareer = time;
    p.action = AutonomyAction::JobAccepted;
    log("autonomy.job_changed", id, best);
  } catch (const std::runtime_error &) {
    p.action = AutonomyAction::JobSeeking;
  }
}
void World::businessDecision(Id id) {
  if (!canPlan(*this, id))
    return;
  auto &r = residents[id];
  auto &p = residentPlans[id];
  if (time < p.nextBusiness)
    return;
  p.nextBusiness = time + 86400 + (id % 7) * 3600;
  Money cash = accounts[r.account].balance, reserve = householdReserve(id);
  if (cash < reserve + 500000)
    return;
  Id best = none;
  double score = 0;
  for (const auto &f : facilities) {
    bool consumer = f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe ||
                    f.kind == FacilityKind::Water || f.kind == FacilityKind::Toilet ||
                    f.kind == FacilityKind::Shop || f.kind == FacilityKind::Laundry ||
                    f.kind == FacilityKind::Shower || f.kind == FacilityKind::Washbasin;
    if (!consumer || !f.open || f.demolished || f.askingPrice <= 0 || f.owner == r.account ||
        !local(*this, id, f.pos, 8, 180) || f.condition < .5f || f.stock < 20 ||
        activeFires.contains(f.id) || (f.queue.empty() && f.serving.empty()))
      continue;
    Money operating = 30000 + f.wageOffer * std::max(0, f.staffNeeded) * 8;
    if (cash - reserve < f.askingPrice + operating)
      continue;
    double demand = double(f.queue.size() + f.serving.size());
    double estimate = f.price * std::min(demand * 24, f.stations * 86400. / f.duration);
    double ratio = estimate / f.askingPrice;
    if (ratio > score) {
      best = f.id;
      score = ratio;
    }
  }
  if (best != none)
    try {
      buyFacility(id, best);
      p.lastBusiness = best;
      p.action = AutonomyAction::Invested;
      log("autonomy.invested", id, best);
    } catch (const std::runtime_error &) {
      p.action = AutonomyAction::Saving;
    }
}
bool World::autonomyDecision(Id id) {
  if (!canPlan(*this, id))
    return false;
  auto [found, inserted] = residentPlans.try_emplace(id);
  auto &p = found->second;
  if (inserted) {
    p.nextShopping = time + (std::uint64_t(id) * 137 % 21600);
    p.nextCareer = time + (std::uint64_t(id) * 113 % 3600);
    p.nextBusiness = time + (std::uint64_t(id) * 251 % 86400);
    p.nextListing = time + id % 1800;
  }
  auto &r = residents[id];
  p.reserve = householdReserve(id);
  if (p.tradeItem != none) {
    finishAutonomyTrade(id);
    if (!r.route.empty())
      return true;
  }
  // Change into an actually owned clean spare and send dirty spare garments through laundry.
  if (r.outfit != none && items[r.outfit].cleanliness < .3f)
    for (Id item : r.items)
      if (items[item].kind == ItemKind::Clothes && items[item].cleanliness > .6f &&
          items[item].owner == r.account && items[item].carrier == id && !items[item].consumed &&
          items[item].reservedAt == none && items[item].durability > .2f) {
        changeClothes(id, item);
        break;
      }
  bool dirtySpare = false;
  for (Id item : r.items)
    if (item != r.outfit && items[item].kind == ItemKind::Clothes && items[item].carrier == id &&
        items[item].owner == r.account && !items[item].consumed && items[item].cleanliness < .3f)
      dirtySpare = true;
  if (dirtySpare) {
    Id laundry = serviceChoice(id, FacilityKind::Laundry);
    if (laundry != none)
      try {
        travel(id, facilities[laundry].pos, TaskKind::Service, laundry);
        return true;
      } catch (const std::runtime_error &) {
      }
  }
  if (time >= p.nextListing) {
    p.nextListing = time + 86400;
    for (Id item : r.items) {
      const auto &i = items[item];
      bool retail = i.kind == ItemKind::Phone || i.kind == ItemKind::Laptop ||
                    i.kind == ItemKind::Fan || i.kind == ItemKind::Tableware ||
                    i.kind == ItemKind::RiceCooker || i.kind == ItemKind::Clothes;
      if (retail && i.owner == r.account && i.carrier == id && !i.consumed &&
          i.reservedAt == none && i.askingPrice == 0 && i.durability > .35f && i.id != r.outfit &&
          i.stolenFrom == none && !itemInUse(item) &&
          held(*this, id, i.kind) > (i.kind == ItemKind::Clothes ? 2 : 1)) {
        try {
          listItem(id, item,
                   std::max<Money>(100, Money(productPrice(i.kind) * (.4 + .2 * r.interests[5]) *
                                              i.durability)));
        } catch (const std::runtime_error &) {
        }
        break;
      }
    }
  }
  householdShopping(id);
  if (!r.route.empty())
    return true;
  if (p.order != none && active(orders[p.order]) && orders[p.order].courier != none &&
      time - orders[p.order].placed < 1800 &&
      local(*this, id, position(orders[p.order].courier), 4, 60)) {
    // Remain available for a nearby handover, without delaying work, sleep or urgent needs.
    p.action = AutonomyAction::WaitingDelivery;
    return true;
  }
  careerDecision(id);
  businessDecision(id);
  return !r.route.empty();
}
bool World::routeDelivery(Id oid, bool handover) {
  auto &o = orders.at(oid);
  if (o.state != OrderState::ToCustomer || o.courier == none)
    return false;
  auto &courier = residents[o.courier];
  const auto &buyer = residents[o.buyer];
  if (!courier.alive || !buyer.alive || !buyer.present || courier.carrying != o.item ||
      courier.surrendered || courier.activity == Activity::Jailed ||
      courier.activity == Activity::Restrained || courier.activity == Activity::Escorted ||
      courier.lift != none || courier.ambulance != none || courier.carriedBy != none ||
      courier.medicalDuty != none || courier.vehicle != none || looseBodies.contains(courier.id))
    return false;
  Vec3 goal = position(o.buyer);
  bool reachable = buyer.lift == none && buyer.ambulance == none && buyer.carriedBy == none &&
                   !looseBodies.contains(o.buyer) &&
                   std::abs(goal.y - std::round(goal.y / 3) * 3) < .15f;
  if (reachable) {
    if (handover && near(courier.id, goal)) {
      deliver(oid, courier.id);
      return true;
    }
    try {
      travel(courier.id, goal, TaskKind::DeliveryDrop, oid);
      return true;
    } catch (const std::runtime_error &) { /* Wait for an accessible rendezvous. */
    }
  }
  courier.pos = position(courier.id);
  courier.route.clear();
  ++courier.routeVersion;
  courier.task = TaskKind::None;
  courier.target = none;
  courier.activity = Activity::Idle;
  return false;
}
void World::dispatchOrder(Id oid) {
  auto &o = orders.at(oid);
  if (o.state != OrderState::Waiting)
    return;
  std::vector<std::pair<float, Id>> available;
  for (Id id : facilities[o.shop].employees) {
    const auto &r = residents[id];
    if (id != o.buyer && r.automatic && r.alive && r.present && !r.leaving &&
        r.role == Role::Courier && r.jobSite == o.shop && onShift(id) && r.carrying == none &&
        r.bodyCarrying == none && r.route.empty() && r.queuedAt == none && !r.surrendered &&
        r.medicalDuty == none && r.ambulance == none && r.carriedBy == none && r.lift == none &&
        r.vehicle == none && !looseBodies.contains(id) && r.hunger < .85f && r.thirst < .85f &&
        r.energy > .25f && (r.activity == Activity::Idle || r.activity == Activity::Working))
      available.push_back({length(position(id) - facilities[o.shop].pos), id});
  }
  std::sort(available.begin(), available.end());
  for (auto [distance, id] : available) {
    (void)distance;
    try {
      assignDelivery(oid, id);
      return;
    } catch (const std::runtime_error &) {
    }
  }
}
void World::orderTick() {
  for (auto &o : orders) {
    if ((o.state == OrderState::Waiting || o.state == OrderState::ToPickup) &&
        (!residents[o.buyer].alive || !residents[o.buyer].present ||
         facilities[o.shop].demolished || time - o.placed > 86400)) {
      cancelOrder(o.id, o.buyer);
      continue;
    }
    if (o.state == OrderState::ToPickup && o.courier != none &&
        (!residents[o.courier].alive || !residents[o.courier].present ||
         residents[o.courier].role != Role::Courier)) {
      auto &old = residents[o.courier];
      if (old.task == TaskKind::DeliveryPickup && old.target == o.id) {
        old.pos = position(old.id);
        old.route.clear();
        ++old.routeVersion;
        old.task = TaskKind::None;
        old.target = none;
        if (old.alive)
          old.activity = Activity::Idle;
      }
      o.courier = none;
      o.state = OrderState::Waiting;
    }
    if (o.state == OrderState::ToCustomer) {
      bool missing = o.courier == none || !residents[o.courier].alive ||
                     !residents[o.courier].present || items[o.item].carrier != o.courier ||
                     residents[o.courier].carrying != o.item || !residents[o.buyer].alive ||
                     !residents[o.buyer].present || time - o.placed > 172800;
      if (missing) {
        if (o.courier != none && residents[o.courier].carrying == o.item) {
          auto &courier = residents[o.courier];
          items[o.item].pos = position(courier.id);
          items[o.item].carrier = none;
          courier.carrying = none;
          if (courier.task == TaskKind::DeliveryDrop) {
            courier.pos = position(courier.id);
            courier.route.clear();
            ++courier.routeVersion;
            courier.task = TaskKind::None;
            if (courier.alive)
              courier.activity = Activity::Idle;
          }
        }
        o.state = OrderState::Lost;
      }
    }
    if (o.state == OrderState::Lost && accounts[o.escrow].balance > 0) {
      // The shipped goods still belong to the buyer; only the unearned delivery fee is refunded.
      Money goods = std::min(o.price, accounts[o.escrow].balance);
      pay(o.escrow, operatingAccount(o.shop), goods, "lost-parcel-product-settlement");
      pay(o.escrow, residents[o.buyer].present ? residents[o.buyer].account : 0,
          accounts[o.escrow].balance, "lost-parcel-delivery-refund");
      if (auto plan = residentPlans.find(o.buyer);
          plan != residentPlans.end() && plan->second.order == o.id)
        plan->second.action = AutonomyAction::ParcelLost;
      log("autonomy.parcel_lost", o.courier, o.buyer);
    }
  }
}
std::string autonomyKey(AutonomyAction action) {
  static const char *keys[] = {
      "autonomy.saving",      "autonomy.essentials",   "autonomy.ordered",
      "autonomy.waiting",     "autonomy.connection",   "autonomy.trading",
      "autonomy.job_seeking", "autonomy.job_accepted", "autonomy.invested",
      "autonomy.no_supply",   "autonomy.shared",       "autonomy.trade_cancelled",
      "autonomy.received",    "autonomy.cancelled",    "autonomy.parcel_lost"};
  static_assert(std::size(keys) == std::size_t(AutonomyAction::ParcelLost) + 1);
  if (int(action) < 0 || std::size_t(action) >= std::size(keys))
    throw std::runtime_error("Invalid resident decision");
  return keys[int(action)];
}
void World::validateAutonomy() const {
  for (const auto &[id, p] : residentPlans) {
    auto ref = [&](Id x, std::size_t count) {
      return x == none || (x >= 0 && std::size_t(x) < count);
    };
    if (id < 0 || std::size_t(id) >= residents.size() || !ref(p.order, orders.size()) ||
        !ref(p.tradeItem, items.size()) || !ref(p.seller, residents.size()) ||
        !ref(p.lastBusiness, facilities.size()) || p.reserve < 0 || p.reserve > 1000000000000LL ||
        p.spentToday < 0 || p.spentToday > 1000000000 || p.tradePrice < 0 ||
        p.tradePrice > 100000000 || p.day < -1 || p.day > int(time / 86400) || p.tradeRetries < 0 ||
        p.tradeRetries > 3 || int(p.wanted) < 0 || int(p.wanted) > int(ItemKind::RiceCooker) ||
        int(p.action) < 0 || int(p.action) > int(AutonomyAction::ParcelLost) ||
        !std::isfinite(p.nextShopping) || p.nextShopping < 0 || !std::isfinite(p.nextCareer) ||
        p.nextCareer < 0 || !std::isfinite(p.nextBusiness) || p.nextBusiness < 0 ||
        !std::isfinite(p.nextListing) || p.nextListing < 0 || !std::isfinite(p.tradeDeadline) ||
        p.tradeDeadline < 0 || !std::isfinite(p.lastCareer) || p.lastCareer < -86400 ||
        p.lastCareer > time || (p.order != none && orders[p.order].buyer != id) ||
        ((p.tradeItem == none) != (p.seller == none)))
      throw std::runtime_error("Invalid resident plan");
  }
}
} // namespace mega
