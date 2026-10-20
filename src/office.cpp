#include "mega/world.hpp"
#include <algorithm>
#include <numeric>
namespace mega {
void World::setupOffices() {
  for (auto &f : facilities)
    if (f.kind == FacilityKind::Office) {
      std::vector<Id> kept;
      for (Id id : f.employees) {
        auto &r = residents[id];
        if (kept.size() >= std::size_t(f.stations * 3)) {
          clockOut(id);
          r.role = Role::Unemployed;
          r.jobSite = r.employer = none;
          continue;
        }
        r.routine.shiftStart = 6 + (kept.size() % 3) * 8;
        r.routine.sleepStart =
            std::fmod(r.routine.shiftStart + (r.routine.shiftStart >= 20 ? 10 : 14), 24);
        r.routine.daysOff = 0;
        kept.push_back(id);
      }
      f.employees = std::move(kept);
    }
}
Id World::officeOrder(Id id) {
  auto &r = residents.at(id);
  if (activeOfficeOrders.contains(id))
    return activeOfficeOrders.at(id);
  if (!r.alive || !r.present || r.jobSite == none ||
      facilities[r.jobSite].kind != FacilityKind::Office || !operational(r.jobSite) ||
      !onShift(id) || r.employer != operatingAccount(r.jobSite) ||
      !near(id, facilities[r.jobSite].pos, 3) ||
      (r.role != Role::Programmer && r.role != Role::Administrator && r.role != Role::VideoEditor))
    throw std::runtime_error(
        "Work contracts require attendance at an open office during your shift");
  int count = 0;
  for (auto &[worker, order] : activeOfficeOrders)
    if (officeOrders[order].facility == r.jobSite)
      ++count;
  if (count >= facilities[r.jobSite].stations)
    throw std::runtime_error("All company workstations are allocated");
  OfficeOrder o;
  o.id = Id(officeOrders.size());
  o.worker = id;
  o.facility = r.jobSite;
  o.role = r.role;
  o.employer = r.employer;
  Id company = companyForAccount(r.employer);
  o.brief = company != none && companies[company].assignments.contains(id)
                ? companies[company].assignments.at(id)
                : int(random() * 3);
  o.due = time + 2 * 86400;
  if (o.role == Role::Administrator)
    prepareAdministration(o);
  o.escrow = account("Office client work escrow " + std::to_string(o.id));
  pay(o.client, o.escrow, o.budget,
      o.liveRequests ? "administration-commission" : "outside-work-commission");
  officeOrders.push_back(o);
  activeOfficeOrders[id] = o.id;
  clockIn(id);
  log("office.assigned", id, o.id);
  return o.id;
}
std::vector<int> World::runProgram(const std::vector<int> &input,
                                   const std::vector<int> &ops) const {
  if (input.size() > 100 || ops.size() > 8)
    throw std::runtime_error("Program limits exceeded");
  auto out = input;
  for (int op : ops)
    switch (op) {
    case 0:
      std::erase_if(out, [](int n) { return n <= 0; });
      break;
    case 1:
      std::erase_if(out, [](int n) { return n % 2 != 0; });
      break;
    case 2:
      for (int &n : out) {
        if (std::abs(double(n)) > 1000000)
          throw std::runtime_error("Program overflow");
        n *= 2;
      }
      break;
    case 3: {
      int total = 0;
      for (int n : out) {
        if (std::abs(double(n)) > 1000000)
          throw std::runtime_error("Program overflow");
        total += n;
      }
      out = {total};
      break;
    }
    case 4:
      std::sort(out.begin(), out.end());
      break;
    case 5: {
      std::set<int> seen;
      std::erase_if(out, [&](int n) { return !seen.insert(n).second; });
      break;
    }
    case 6:
      out = {int(out.size())};
      break;
    default:
      throw std::runtime_error("Unknown program operation");
    }
  return out;
}
float World::scoreOffice(Id id, const std::vector<int> &answer) const {
  const auto &o = officeOrders.at(id);
  if (o.role == Role::VideoEditor) {
    static const int sequences[3][6] = {{0, 1, 2, 3, 4, 5}, {2, 1, 0, 3, 4, 5}, {3, 2, 1, 4, 0, 5}};
    std::vector<int> order;
    for (int clip : answer) {
      if (clip < 0 || clip > 5)
        return 0;
      auto first = sequences[o.brief];
      order.push_back(int(std::find(first, first + 6, clip) - first));
    }
    return scoreVideo(order);
  }
  if (o.role == Role::Administrator) {
    if (answer.size() != 4)
      return 0;
    int correct = 0, count = 0;
    for (int n = 0; n < 4; ++n)
      if (!o.liveRequests || o.tickets[n] != none) {
        ++count;
        if (answer[n] == (n + o.brief) % 4)
          ++correct;
      }
    return count ? correct * 100.f / count : 0;
  }
  std::vector<std::vector<int>> cases = {
      {-2, 1, 3, 0, 5}, {4, 4, -1, 2}, {0, -4, -2}, {1, 2, 2, 3, 7}};
  int passed = 0;
  for (auto &input : cases) {
    std::vector<int> expected;
    if (o.brief == 0) {
      int sum = 0;
      for (int n : input)
        if (n > 0)
          sum += n * 2;
      expected = {sum};
    } else if (o.brief == 1) {
      std::set<int> unique(input.begin(), input.end());
      expected.assign(unique.begin(), unique.end());
    } else {
      int count = 0;
      for (int n : input)
        if (n > 0)
          ++count;
      expected = {count};
    }
    try {
      if (runProgram(input, answer) == expected)
        ++passed;
    } catch (const std::runtime_error &) {
    }
  }
  return passed * 25.f;
}
void World::submitOffice(Id id, const std::vector<int> &answer) {
  auto &o = officeOrders.at(id);
  auto &r = residents.at(o.worker);
  auto &f = facilities[o.facility];
  if (o.submitted || !r.alive || !r.present || r.jobSite != o.facility ||
      r.employer != operatingAccount(f.id) || r.employer != o.employer || !f.open ||
      !hasDevice(r.id) || !near(r.id, f.pos, 3) || o.total - o.remaining < 600)
    throw std::runtime_error(
        "Complete ten minutes of actual work and submit from your workplace computer");
  o.quality = scoreOffice(id, answer) * (time > o.due ? .65f : 1.f);
  Money revenue = Money(o.budget * o.quality / 100), bonus = revenue / 5;
  pay(o.escrow, o.employer, revenue - bonus, "office-export-revenue");
  pay(o.escrow, r.account, bonus, "office-quality-bonus");
  pay(o.escrow, o.client, o.budget - revenue, "client-refund");
  if (o.liveRequests)
    dispatchAdministration(o, answer);
  Id company = companyForAccount(o.employer);
  if (company != none) {
    companies[company].revenue += revenue - bonus;
    if (o.quality >= 75)
      ++companies[company].completed[r.id];
  }
  o.submission = answer;
  o.submitted = true;
  activeOfficeOrders.erase(r.id);
  log("office.completed", r.id, o.id);
}
void World::officeTick() {
  for (auto &f : facilities)
    if (f.kind == FacilityKind::Office && operational(f.id)) {
      for (Id id : f.employees) {
        auto &r = residents[id];
        if (r.automatic && r.alive && r.present && r.activity == Activity::Working && onShift(id) &&
            near(id, f.pos, 3) && !activeOfficeOrders.contains(id) && time >= officeRetryAt[id])
          try {
            officeOrder(id);
          } catch (const std::runtime_error &) {
            officeRetryAt[id] = time + 1800;
          }
      }
    }
  std::vector<Id> completed;
  for (auto &[worker, id] : activeOfficeOrders) {
    auto &o = officeOrders[id];
    auto &r = residents[worker];
    auto &f = facilities[o.facility];
    if (!r.alive || !r.present || r.jobSite != o.facility || r.employer != operatingAccount(f.id) ||
        r.employer != o.employer || time > o.due + 86400) {
      pay(o.escrow, o.client, accounts[o.escrow].balance, "abandoned-work-refund");
      o.submitted = true;
      o.quality = 0;
      completed.push_back(worker);
      continue;
    }
    if (r.activity != Activity::Working || !onShift(worker) || !near(worker, f.pos, 3) ||
        !operational(f.id)) {
      o.workAt = -1;
      continue;
    }
    if (o.workAt < 0)
      o.workAt = time;
    o.remaining = std::max(0.0, o.remaining - std::min(60.0, time - o.workAt));
    o.workAt = time;
    if (o.remaining == 0 && r.automatic)
      completed.push_back(worker);
  }
  for (Id worker : completed) {
    auto it = activeOfficeOrders.find(worker);
    if (it == activeOfficeOrders.end())
      continue;
    Id id = it->second;
    auto &o = officeOrders[id];
    if (o.submitted) {
      activeOfficeOrders.erase(worker);
      continue;
    }
    std::vector<int> answer;
    if (o.role == Role::Programmer)
      answer = o.brief == 0   ? std::vector<int>{0, 2, 3}
               : o.brief == 1 ? std::vector<int>{5, 4}
                              : std::vector<int>{0, 6};
    else if (o.role == Role::VideoEditor)
      answer = o.brief == 0   ? std::vector<int>{0, 1, 2, 3, 4, 5}
               : o.brief == 1 ? std::vector<int>{2, 1, 0, 3, 4, 5}
                              : std::vector<int>{3, 2, 1, 4, 0, 5};
    else
      for (int n = 0; n < 4; ++n)
        answer.push_back((n + o.brief) % 4);
    // Ordinary staff use their assigned desk rather than requiring a personal laptop.
    residents[worker].terminal = o.facility;
    residents[worker].terminalUntil = time + 300;
    try {
      submitOffice(id, answer);
    } catch (const std::runtime_error &) {
    }
  }
}
} // namespace mega
