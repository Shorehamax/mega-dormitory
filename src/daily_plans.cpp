#include "mega/world.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
namespace mega {
namespace {
constexpr double day = 86400;
struct Window {
  double start, duration;
};
Window nextShift(const World &w, Id id) {
  const auto &r = w.residents.at(id);
  if (r.role == Role::Unemployed || r.jobSite == none || r.routine.shiftHours <= 0)
    return {w.time + 8 * day, 0};
  double today = std::floor(w.time / day) * day;
  for (int offset = -1; offset <= 7; ++offset) {
    double start = today + offset * day + r.routine.shiftStart * 3600;
    int weekday = (int(std::floor(start / day)) % 7 + 7) % 7;
    if (!(r.routine.daysOff & (1 << weekday)) && start + r.routine.shiftHours * 3600 > w.time)
      return {start, r.routine.shiftHours * 3600};
  }
  return {w.time + 8 * day, 0};
}
Window nextSleep(const World &w, Id id) {
  const auto &r = w.residents.at(id);
  if (r.routine.sleepHours <= 0)
    return {w.time + 8 * day, 0};
  double start = std::floor(w.time / day) * day + r.routine.sleepStart * 3600;
  if (start > w.time && start - day + r.routine.sleepHours * 3600 > w.time)
    start -= day;
  if (start + r.routine.sleepHours * 3600 <= w.time)
    start += day;
  // A night worker's shift takes priority over an overlapping sleep window.
  if (w.onShift(id) && start <= w.time)
    start += day;
  return {start, r.routine.sleepHours * 3600};
}
double journey(const World &w, Id id, Vec3 from, Vec3 to) {
  return (std::hypot(from.x - to.x, from.z - to.z) / w.walkingSpeed(id) +
          std::abs(from.y - to.y) / 3.) *
         (1440 / w.config.dayMinutes);
}
bool freeToPlan(const World &w, Id id) {
  const auto &r = w.residents.at(id);
  return r.automatic && r.alive && r.present && !r.leaving && !r.surrendered && r.age >= 6 &&
         r.activity == Activity::Idle && r.route.empty() && r.queuedAt == none &&
         r.carrying == none && r.bodyCarrying == none && r.ladder < 0 && r.vehicle == none &&
         r.lift == none && r.ambulance == none && r.carriedBy == none && r.medicalDuty == none &&
         !w.inCustody(id) && !w.looseBodies.contains(id) && !w.personalUses.contains(id) &&
         !w.gameOver;
}
} // namespace
std::string dailyKey(DailyNeed need) {
  static const char *keys[] = {
      "daily.food",     "daily.water",    "daily.medicine",   "daily.toilet", "daily.hygiene",
      "daily.clothing", "daily.cleaning", "daily.recreation", "daily.work",   "daily.sleep"};
  if (int(need) < 0 || int(need) >= int(std::size(keys)))
    throw std::runtime_error("Invalid daily need");
  return keys[int(need)];
}
DailyPlan World::dailyPlan(Id id) const {
  const auto &r = residents.at(id);
  DailyPlan plan;
  plan.reserve = householdReserve(id);
  Money cash = accounts.at(r.account).balance;
  plan.spendable = std::max<Money>(0, cash - plan.reserve);
  auto shift = nextShift(*this, id), sleep = nextSleep(*this, id);
  bool shiftNext = shift.start <= sleep.start;
  Window busy = shiftNext ? shift : sleep;
  plan.commitment = std::max(time, busy.start);
  Vec3 here = position(id), commitmentPlace = here;
  if (shiftNext && r.jobSite != none)
    commitmentPlace = facilities[r.jobSite].pos;
  else if (residentHomes[id] != none)
    commitmentPlace = homePosition(residentHomes[id]);
  double commute = journey(*this, id, here, commitmentPlace);
  double freeUntil = plan.commitment - commute - 300;
  auto found = residentPlans.find(id);
  const ResidentPlan *stored = found == residentPlans.end() ? nullptr : &found->second;
  auto own = [&](ItemKind kind, bool clean = false) {
    for (Id item : r.items) {
      const auto &i = items[item];
      if (i.kind == kind && i.owner == r.account && i.carrier == id && !i.consumed &&
          i.reservedAt == none && i.durability > .2f && !itemInUse(item) &&
          (!clean || (i.cleanliness > .6f && item != r.outfit)))
        return item;
    }
    return none;
  };
  auto deadline = [&](double value, double threshold, double rate, double minimum) {
    double due = time + std::max(0., (threshold - value) / rate * 3600);
    if (busy.start > time && busy.start - time < 7200 && value >= minimum &&
        due < busy.start + busy.duration)
      due = std::min(due, freeUntil);
    return std::max(time, due);
  };
  auto add = [&](DailyNeed need, double due, FacilityKind kind, Id item = none,
                 bool covered = false, bool optional = false) {
    DailyStep s;
    s.need = need;
    s.due = due;
    s.depart = due;
    s.item = item;
    // A current customer owns their reservation even when it is the last stock.
    // Preview that actual trip/queue/service before searching for another venue.
    Id current = r.queuedAt != none                                ? r.queuedAt
                 : r.task == TaskKind::Service && !r.route.empty() ? r.target
                                                                   : none;
    if (current >= 0 && std::size_t(current) < facilities.size()) {
      const auto &f = facilities[current];
      if (f.kind == kind || (kind == FacilityKind::Food &&
                             (f.kind == FacilityKind::Cafe || f.kind == FacilityKind::Shop))) {
        s.facility = current;
        s.state = DailyState::Active;
        s.reason = "daily.active";
        s.price = f.price;
        if (!operational(current))
          s.reason = availability(current);
        else if (r.activity == Activity::Queuing && f.stock == 0)
          s.reason = "daily.no_stock";
        plan.steps.push_back(s);
        return;
      }
    }
    if (covered) {
      plan.steps.push_back(s);
      return;
    }
    s.state = DailyState::Planned;
    s.reason = "daily.later";
    if (item != none) {
      s.seconds = 1;
      s.depart = due;
      s.reason = "daily.owned";
    } else {
      double best = std::numeric_limits<double>::infinity(), closest = best;
      std::string unavailable = "daily.no_facility";
      int floor = std::clamp(int(std::round(here.y / 3)), 0, config.floors - 1);
      for (auto fi = floorFacilities.lower_bound(floor - 16);
           fi != floorFacilities.end() && fi->first <= floor + 16; ++fi)
        for (Id fid : fi->second) {
          const auto &f = facilities[fid];
          if (f.kind != kind && !(kind == FacilityKind::Food &&
                                  (f.kind == FacilityKind::Cafe || f.kind == FacilityKind::Shop)))
            continue;
          double travel = journey(*this, id, here, f.pos);
          if (travel > 7200 || f.demolished)
            continue;
          std::string why = !accessAllowed(id, fid) ? "daily.private" : availability(fid);
          if (why == "facility.ready" && f.stock <= 0)
            why = "daily.no_stock";
          if (why == "facility.ready" && f.queue.size() >= 64)
            why = "daily.full_queue";
          if (why == "facility.ready" && cash < f.price)
            why = "daily.no_money";
          if (why == "facility.ready" && optional && plan.spendable < f.price)
            why = "daily.savings";
          double seconds =
              travel + (f.queue.size() + f.serving.size()) * f.duration / f.stations + f.duration;
          if (why == "facility.ready" && optional &&
              time + seconds + journey(*this, id, f.pos, commitmentPlace) + 300 > plan.commitment)
            why = "daily.schedule";
          if (travel < closest) {
            closest = travel;
            unavailable = why;
          }
          if (why != "facility.ready" || seconds >= best)
            continue;
          best = seconds;
          s.facility = fid;
          s.seconds = seconds;
          s.price = f.price;
        }
      if (s.facility == none) {
        s.state = DailyState::Waiting;
        s.reason = unavailable;
      } else {
        s.depart = std::max(time, due - s.seconds - 300);
        s.reason = "daily.travel_queue";
      }
    }
    if (s.state != DailyState::Waiting && s.depart <= time) {
      s.state = DailyState::Ready;
      if (s.item != none)
        s.reason = "daily.owned";
    }
    if (s.facility != none &&
        (r.queuedAt == s.facility ||
         (r.task == TaskKind::Service && r.target == s.facility && !r.route.empty()))) {
      s.state = DailyState::Active;
      s.reason = "daily.active";
    }
    plan.steps.push_back(s);
  };
  // Need clocks use the same rates as needs(); previews do not advance those clocks.
  double elapsed = std::max(0., time - r.needsAt) / 3600;
  double hunger = r.hunger + elapsed * .045, thirst = r.thirst + elapsed * .065;
  add(DailyNeed::Food, deadline(hunger, .57, .045, .3), FacilityKind::Food, own(ItemKind::Food));
  add(DailyNeed::Water, deadline(thirst, .65, .065, .25), FacilityKind::Water);
  add(DailyNeed::Medicine, time, FacilityKind::Infirmary, own(ItemKind::Medicine), r.health >= 85);
  add(DailyNeed::Toilet, deadline(r.bladder + elapsed * .13, .75, .13, .35), FacilityKind::Toilet);
  add(DailyNeed::Hygiene, deadline(1 - r.hygiene + elapsed * .035, .65, .035, .35),
      FacilityKind::Shower);
  double dirty = r.outfit == none ? 1 : 1 - items[r.outfit].cleanliness + elapsed * .025;
  bool spareDirty = false;
  for (Id item : r.items) {
    const auto &i = items[item];
    if (i.kind == ItemKind::Clothes && i.owner == r.account && i.carrier == id && !i.consumed &&
        i.reservedAt == none && i.durability > .2f && i.cleanliness < .4f)
      spareDirty = true;
  }
  Id spare = own(ItemKind::Clothes, true);
  if (spare == r.outfit)
    spare = none;
  add(DailyNeed::Clothing, spareDirty ? time : deadline(dirty, .6, .025, .35),
      FacilityKind::Laundry, dirty >= .6 ? spare : none);
  if (r.outfit == none && spare == none && !spareDirty &&
      plan.steps.back().state != DailyState::Active) {
    auto &clothing = plan.steps.back();
    clothing.facility = none;
    clothing.state = DailyState::Waiting;
    clothing.reason = "daily.need_clothes";
  }
  bool toolsDirty = false;
  for (Id item : washableItems(id))
    if (items[item].cleanliness < .4f)
      toolsDirty = true;
  add(DailyNeed::Cleaning, time,
      r.personalWaste >= 3 ? FacilityKind::Garbage : FacilityKind::Washbasin, none,
      r.personalWaste < 3 && !toolsDirty);
  static constexpr std::array<FacilityKind, 8> leisure = {
      FacilityKind::Cafe, FacilityKind::Leisure, FacilityKind::Arcade,  FacilityKind::Cinema,
      FacilityKind::Pool, FacilityKind::Shop,    FacilityKind::Library, FacilityKind::Playground};
  auto preference = std::max_element(r.interests.begin(), r.interests.end()) - r.interests.begin();
  auto recreation =
      preference == 4 && (id + int(time / day)) % 2 == 0 ? FacilityKind::Gym : leisure[preference];
  add(DailyNeed::Recreation, time, recreation, none,
      r.happiness >= .7f || (stored && time < stored->nextRecreation), true);
  bool provisioning = false;
  if (stored) {
    provisioning = stored->supply != none || stored->tradeItem != none ||
                   stored->helpTarget != none || stored->storageItem != none ||
                   (r.age >= 18 && time >= stored->nextShopping && plan.spendable >= 1000);
    if (stored->order >= 0 && std::size_t(stored->order) < orders.size()) {
      auto state = orders[stored->order].state;
      provisioning = provisioning || state == OrderState::Waiting ||
                     state == OrderState::ToPickup || state == OrderState::ToCustomer;
    }
  }
  if (provisioning && plan.steps.back().state != DailyState::Covered &&
      plan.steps.back().state != DailyState::Active) {
    plan.steps.back().state = DailyState::Waiting;
    plan.steps.back().reason = "daily.provisioning";
  }
  for (auto need : {DailyNeed::Work, DailyNeed::Sleep}) {
    DailyStep s;
    s.need = need;
    Window window = need == DailyNeed::Work ? shift : sleep;
    s.due = std::max(time, window.start);
    Vec3 target = here;
    bool exists = need == DailyNeed::Work
                      ? r.jobSite != none && r.role != Role::Unemployed && window.duration > 0
                      : residentHomes[id] != none && window.duration > 0;
    if (exists)
      target =
          need == DailyNeed::Work ? facilities[r.jobSite].pos : homePosition(residentHomes[id]);
    s.facility = need == DailyNeed::Work ? r.jobSite : none;
    s.seconds = journey(*this, id, here, target);
    s.depart = std::max(time, s.due - s.seconds);
    s.state = !exists            ? DailyState::Covered
              : s.depart <= time ? DailyState::Ready
                                 : DailyState::Planned;
    s.reason = !exists ? "daily.covered" : "daily.schedule";
    if (exists && ((need == DailyNeed::Work && r.activity == Activity::Working) ||
                   (need == DailyNeed::Sleep && r.activity == Activity::Sleeping))) {
      s.state = DailyState::Active;
      s.reason = "daily.active";
    }
    plan.steps.push_back(s);
  }
  return plan;
}
bool World::dailyServiceAllowed(Id id, Id fid, Money price) const {
  auto p = residentPlans.find(id);
  if (!residents.at(id).automatic || p == residentPlans.end() || p->second.plannedLeisure != fid)
    return true; // Player-directed expenses remain a deliberate choice.
  return accounts[residents[id].account].balance - price >= householdReserve(id) && !onShift(id) &&
         !sleepTime(id);
}
bool World::dailyDecision(Id id) {
  if (!freeToPlan(*this, id) || onShift(id))
    return false;
  auto &r = residents[id];
  auto [found, inserted] = residentPlans.try_emplace(id);
  auto &p = found->second;
  if (inserted) {
    p.nextShopping = time + (std::uint64_t(id) * 137 % 21600);
    p.nextCareer = time + (std::uint64_t(id) * 113 % 3600);
    p.nextBusiness = time + (std::uint64_t(id) * 251 % 86400);
    p.nextListing = time + id % 1800;
  }
  if (time < p.nextDaily)
    return false;
  p.nextDaily = time + 180;
  // An interrupted leisure trip is never a permanent spending permission.
  p.plannedLeisure = none;
  const auto plan = dailyPlan(id);
  p.reserve = plan.reserve;
  std::vector<DailyStep> ready;
  for (const auto &step : plan.steps)
    if (step.state == DailyState::Ready && step.need != DailyNeed::Recreation)
      ready.push_back(step);
  auto priority = [&](const DailyStep &s) {
    if ((s.need == DailyNeed::Water && r.thirst > .95f) ||
        (s.need == DailyNeed::Food && r.hunger > .95f) ||
        (s.need == DailyNeed::Medicine && r.health < 65) ||
        (s.need == DailyNeed::Toilet && r.bladder > .95f))
      return -1.;
    if (s.need == DailyNeed::Work || s.need == DailyNeed::Sleep)
      return -.5;
    return s.due;
  };
  std::stable_sort(ready.begin(), ready.end(),
                   [&](const auto &a, const auto &b) { return priority(a) < priority(b); });
  for (const auto &s : ready) {
    bool commitment = s.need == DailyNeed::Work || s.need == DailyNeed::Sleep;
    bool urgent = priority(s) < 0;
    if (!commitment && !urgent && (onShift(id) || sleepTime(id)))
      continue;
    if (!commitment && !urgent && s.need != DailyNeed::Recreation && s.due > time &&
        time + s.seconds + 300 > plan.commitment)
      continue;
    try {
      if (s.item != none) {
        if (s.need == DailyNeed::Clothing)
          changeClothes(id, s.item);
        else if (s.need == DailyNeed::Food) {
          Id utensil = none;
          for (Id item : r.items)
            if ((items[item].kind == ItemKind::Tableware ||
                 items[item].kind == ItemKind::RiceCooker) &&
                items[item].owner == r.account && items[item].carrier == id &&
                !items[item].consumed && items[item].reservedAt == none &&
                items[item].cleanliness >= .25f && items[item].durability > .2f)
              utensil = item;
          bool usingUtensil = false;
          if (utensil != none && time + 900 < plan.commitment) {
            try {
              usePersonalItem(id, utensil);
              usingUtensil = true;
            } catch (const std::runtime_error &) {
            }
          }
          if (!usingUtensil)
            consumeItem(id, s.item);
        } else
          consumeItem(id, s.item);
        return true;
      }
      if (s.need == DailyNeed::Work) {
        if (near(id, facilities[s.facility].pos, 3)) {
          if (onShift(id))
            clockIn(id);
          return true; // Wait at the real workplace until the shift opens.
        }
        travel(id, facilities[s.facility].pos, TaskKind::Work, s.facility);
      } else if (s.need == DailyNeed::Sleep) {
        Id home = residentHomes[id];
        if (home == none || dangerAt(homePosition(home)) > .02f || onShift(id))
          continue;
        travel(id, homePosition(home), TaskKind::Home, id);
      } else if (s.facility != none) {
        travel(id, facilities[s.facility].pos, TaskKind::Service, s.facility);
      } else
        continue;
      return true;
    } catch (const std::runtime_error &) {
      p.plannedLeisure = none; // A normal failed estimate is not a world-journal error.
    }
  }
  return false;
}
bool World::dailyLeisure(Id id) {
  if (!freeToPlan(*this, id) || onShift(id) || sleepTime(id))
    return false;
  const auto &r = residents[id];
  if (r.hunger > .85f || r.thirst > .65f || r.bladder > .75f || r.hygiene < .35f || r.energy < .3f)
    return false;
  const auto plan = dailyPlan(id);
  const auto &step = plan.steps[int(DailyNeed::Recreation)];
  if (step.state != DailyState::Ready || step.facility == none)
    return false;
  auto &p = residentPlans[id];
  try {
    p.plannedLeisure = step.facility;
    travel(id, facilities[step.facility].pos, TaskKind::Service, step.facility);
    return true;
  } catch (const std::runtime_error &) {
    p.plannedLeisure = none;
    p.nextRecreation = time + 900;
    return false;
  }
}
} // namespace mega
