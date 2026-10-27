#include "mega/world.hpp"
#include <algorithm>
#include <cmath>
namespace mega {
bool World::inHours(double seconds, double start, double duration) {
  double hour = std::fmod(seconds / 3600 - start + 48, 24);
  return duration > 0 && (duration >= 24 || hour < duration);
}
bool World::onShift(Id id) const {
  const auto &r = residents.at(id);
  if (r.role == Role::Unemployed || r.jobSite == none)
    return false;
  // After midnight an overnight shift belongs to the day it started.
  int day = int(std::floor(time / 86400));
  if (std::fmod(time / 3600, 24) < r.routine.shiftStart)
    --day;
  day = (day % 7 + 7) % 7;
  return !(r.routine.daysOff & (1 << day)) &&
         inHours(time, r.routine.shiftStart, r.routine.shiftHours);
}
bool World::sleepTime(Id id) const {
  const auto &r = residents.at(id);
  return inHours(time, r.routine.sleepStart, r.routine.sleepHours);
}
int World::staffOnDuty(Id fid) const {
  const auto &f = facilities.at(fid);
  int count = 0;
  for (Id id : f.employees) {
    const auto &r = residents.at(id);
    if (r.alive && r.present && r.jobSite == fid && r.employer == operatingAccount(fid) &&
        r.role == f.staffRole && r.activity == Activity::Working && onShift(id) &&
        near(id, f.pos, 3))
      ++count;
  }
  return count;
}
bool World::accessAllowed(Id id, Id fid) const {
  const auto &r = residents.at(id);
  const auto &f = facilities.at(fid);
  return r.alive && r.present &&
         (f.publicAccess || r.account == operatingAccount(fid) || id == f.manager ||
          (r.jobSite == fid && r.employer == operatingAccount(fid)) ||
          directsCompany(id, companyForAccount(operatingAccount(fid))) || f.whitelist.contains(id));
}
std::string World::availability(Id fid) const {
  const auto &f = facilities.at(fid);
  if (f.demolished || !f.open || !inHours(time, f.opensAt, f.openHours))
    return "facility.closed";
  if (activeFires.contains(fid) || (f.kind != FacilityKind::FireStation && dangerAt(f.pos) > .02f))
    return "facility.fire";
  if (f.condition < .1f)
    return "facility.broken";
  if (staffOnDuty(fid) < f.staffNeeded)
    return "facility.unstaffed";
  return "facility.ready";
}
bool World::operational(Id fid) const { return availability(fid) == "facility.ready"; }
void World::clockIn(Id id) {
  auto &r = residents.at(id);
  if (looseBodies.contains(id))
    return;
  if (!r.alive || !r.present || r.medicalDuty != none || r.ambulance != none ||
      r.carriedBy != none || r.jobSite == none || !onShift(id) ||
      !near(id, facilities[r.jobSite].pos, 3))
    return;
  if (r.routine.workSince >= 0 && r.activity == Activity::Working)
    return;
  if (facilities[r.jobSite].kind == FacilityKind::Office) {
    int count = 0;
    for (Id id : facilities[r.jobSite].employees)
      if (id != r.id && residents[id].alive && residents[id].present &&
          residents[id].activity == Activity::Working && onShift(id) &&
          near(id, facilities[r.jobSite].pos, 3))
        ++count;
    if (count >= facilities[r.jobSite].stations)
      return;
  }
  leaveQueue(id);
  r.activity = Activity::Working;
  r.routine.workSince = time;
  serve(r.jobSite);
}
void World::clockOut(Id id) {
  auto &r = residents.at(id);
  if (r.employer != none && r.routine.unpaidWages > 0) {
    r.routine.wageClaims[r.employer] += r.routine.unpaidWages;
    r.routine.unpaidWages = 0;
  }
  if (r.routine.workSince >= 0) {
    if (r.employer != none)
      r.routine.wageClaims[r.employer] +=
          std::max(0.0, time - r.routine.workSince) / 3600 * r.hourlyWage;
    r.routine.workSince = -1;
  }
  for (auto &[employer, claim] : r.routine.wageClaims) {
    Money amount = std::min(Money(claim), accounts[employer].balance);
    if (amount > 0) {
      pay(employer, r.account, amount, "work-wages");
      claim -= amount;
    }
  }
  bool working = r.activity == Activity::Working;
  if (working)
    r.activity = Activity::Idle;
  if (working && r.jobSite != none)
    serve(r.jobSite);
}
void World::setFacilityHours(Id actor, Id fid, double start, double hours, bool publicAccess) {
  auto &r = residents.at(actor);
  auto &f = facilities.at(fid);
  if (!r.alive || !r.present ||
      (r.account != operatingAccount(fid) && actor != f.manager &&
       !directsCompany(actor, companyForAccount(operatingAccount(fid)))) ||
      !std::isfinite(start) || !std::isfinite(hours) || start < 0 || start >= 24 || hours < 0 ||
      hours > 24)
    throw std::runtime_error(
        "Only the owner or appointed manager can set access and opening hours");
  f.opensAt = start;
  f.openHours = hours;
  f.publicAccess = publicAccess;
  serve(fid);
}
void World::refreshServices() {
  for (auto &r : residents)
    if (r.activity == Activity::Working && r.routine.workSince >= 0 &&
        (!onShift(r.id) || r.jobSite == none || !near(r.id, facilities[r.jobSite].pos, 3)))
      clockOut(r.id);
  for (auto &f : facilities)
    serve(f.id);
}
void World::setupRoutines() {
  std::map<int, std::vector<Id>> local;
  for (auto &r : residents) {
    if (r.alive && r.present && r.age >= 18)
      local[r.home].push_back(r.id);
    r.routine.sleepStart = 22 + (r.id % 5) * .5;
    if (r.routine.sleepStart >= 24)
      r.routine.sleepStart -= 24;
    r.routine.sleepHours = 7 + (r.id % 3) * .5;
    if (r.role == Role::Warden || r.role == Role::Courier || r.role == Role::Builder) {
      r.routine.shiftStart = 6 + (r.id % 3) * 8;
      r.routine.sleepStart =
          std::fmod(r.routine.shiftStart + (r.routine.shiftStart == 22 ? 10 : 14), 24);
    } else if (r.role != Role::Unemployed) {
      r.routine.shiftStart = 8 + (r.id % 3) * .5;
      r.routine.daysOff = (1 << 5) | (1 << 6);
    }
  }
  for (auto &f : facilities) {
    switch (f.kind) {
    case FacilityKind::Reception:
      f.staffRole = Role::Administrator;
      break;
    case FacilityKind::Food:
    case FacilityKind::Cafe:
      f.staffRole = Role::Chef;
      break;
    case FacilityKind::Garbage:
    case FacilityKind::Morgue:
    case FacilityKind::Crematorium:
      f.staffRole = Role::Cleaner;
      break;
    case FacilityKind::Infirmary:
      f.staffRole = Role::Medic;
      break;
    case FacilityKind::Shop:
    case FacilityKind::Dealership:
      f.staffRole = Role::Vendor;
      f.opensAt = 8;
      f.openHours = 14;
      break;
    case FacilityKind::Pool:
      f.staffRole = Role::Lifeguard;
      f.opensAt = 8;
      f.openHours = 12;
      break;
    case FacilityKind::Cinema:
      f.staffRole = Role::Attendant;
      f.opensAt = 12;
      f.openHours = 12;
      break;
    case FacilityKind::FireStation:
      f.staffRole = Role::Firefighter;
      f.openHours = 24;
      f.wageOffer = 1800;
      break;
    case FacilityKind::School:
      f.staffRole = Role::Teacher;
      f.opensAt = 8;
      f.openHours = 8;
      break;
    default:
      break;
    }
    if (f.staffRole == Role::Unemployed)
      continue;
    f.staffNeeded = f.kind == FacilityKind::Garbage || f.kind == FacilityKind::FireStation ? 0 : 1;
    int shifts = f.openHours == 24 ? 3 : f.openHours > 8 ? 2 : 1;
    for (int assignment = 0;
         assignment <
         shifts * ((f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe) ? 2 : 1);
         ++assignment) {
      int shift = assignment % shifts;
      Id chosen = none;
      for (int distance = 0; distance < 16 && chosen == none; ++distance)
        for (int sign : {-1, 1}) {
          auto it = local.find(f.floor + distance * sign);
          if (it == local.end())
            continue;
          for (Id id : it->second)
            if (id > 1 && residents[id].role == Role::Unemployed) {
              chosen = id;
              break;
            }
          if (chosen != none)
            break;
        }
      if (chosen == none)
        continue; // Real vacancy: no phantom staff.
      auto &r = residents[chosen];
      r.role = f.staffRole;
      if (f.kind == FacilityKind::FireStation)
        r.hourlyWage = f.wageOffer;
      r.employer = f.owner;
      r.jobSite = f.id;
      r.routine.shiftStart =
          f.openHours == 24 ? 6 + shift * 8 : f.opensAt + shift * (f.openHours - 8);
      r.routine.sleepStart =
          std::fmod(r.routine.shiftStart + (r.routine.shiftStart >= 20 ? 10 : 14), 24);
      r.routine.sleepHours = 8;
      r.routine.shiftHours = f.openHours == 24 ? 8.75 : 8;
      f.employees.push_back(chosen);
    }
  }
  for (auto &r : residents) {
    if (!r.alive || !r.present)
      continue;
    if (r.jobSite != none &&
        std::find(facilities[r.jobSite].employees.begin(), facilities[r.jobSite].employees.end(),
                  r.id) == facilities[r.jobSite].employees.end())
      facilities[r.jobSite].employees.push_back(r.id);
    if (r.id == player)
      continue;
    if (onShift(r.id) && r.jobSite != none && facilities[r.jobSite].staffNeeded) {
      const auto &staff = facilities[r.jobSite].employees;
      auto index = std::find(staff.begin(), staff.end(), r.id) - staff.begin();
      r.pos = facilities[r.jobSite].pos + Vec3{(float(index % 5) - 2) * .65f, 0, -1.6f};
      clockIn(r.id);
    } else if (sleepTime(r.id) && residentHomes[r.id] != none) {
      r.pos = homePosition(residentHomes[r.id]);
      r.activity = Activity::Sleeping;
    } else
      r.activity = Activity::Idle;
  }
}
} // namespace mega
