#include "mega/world.hpp"
#include <algorithm>
#include <cmath>

namespace mega {
static void refundLegacyVideos(World &world, Id worker) {
  for (auto &v : world.videos)
    if (v.worker == worker && !v.submitted) {
      world.pay(v.escrow, v.client, world.accounts[v.escrow].balance,
                "company-transition-video-refund");
      v.submitted = true;
      v.score = 0;
    }
}
Id World::operatingAccount(Id fid) const {
  auto it = leasedWorkspaces.find(fid);
  return it == leasedWorkspaces.end()
             ? facilities.at(fid).owner
             : companies.at(workspaceLeases.at(it->second).company).account;
}
Id World::companyForAccount(Id account) const {
  for (const auto &c : companies)
    if (c.account == account)
      return c.id;
  return none;
}
bool World::directsCompany(Id actor, Id cid, bool ownerOnly) const {
  if (actor < 0 || std::size_t(actor) >= residents.size() || cid < 0 ||
      std::size_t(cid) >= companies.size())
    return false;
  const auto &r = residents[actor];
  const auto &c = companies[cid];
  return r.alive && r.present && !r.leaving && r.activity != Activity::Jailed &&
         (actor == c.owner || (!ownerOnly && actor == c.leader && r.employer == c.account));
}
void World::setupCompanies() {
  std::set<Id> personalAccounts{dormAccount};
  for (const auto &r : residents)
    personalAccounts.insert(r.account);
  for (auto &f : facilities)
    if (f.kind == FacilityKind::Office && f.manager != none &&
        !personalAccounts.contains(f.owner) && companyForAccount(f.owner) == none) {
      Company c;
      c.id = Id(companies.size());
      c.owner = f.manager;
      c.account = f.owner;
      c.name = "Studio " + std::to_string(c.id + 1);
      companies.push_back(c);
      workspaceOffers[f.id] = 2500 + f.stations * 500;
    }
}
Id World::foundCompany(Id actor, const std::string &name, Money capital) {
  const auto &r = residents.at(actor);
  if (!r.alive || !r.present || r.leaving || r.activity == Activity::Jailed || name.empty() ||
      name.size() > 80 || name.find_first_of("\r\n\t") != std::string::npos || capital < 10000 ||
      capital > accounts[r.account].balance)
    throw std::runtime_error("A company needs a name and at least 100 yuan of your own capital");
  Company c;
  c.id = Id(companies.size());
  c.owner = actor;
  c.name = name;
  c.account = account(name);
  pay(r.account, c.account, capital, "company-capital");
  companies.push_back(c);
  log("company.founded", actor, c.id);
  return c.id;
}
void World::investCompany(Id actor, Id cid, Money amount) {
  auto &c = companies.at(cid);
  if (!directsCompany(actor, cid, true) || amount <= 0)
    throw std::runtime_error("Only the company owner may contribute capital");
  pay(residents[actor].account, c.account, amount, "company-capital");
}
Money World::companyReserve(Id cid) const {
  const auto &c = companies.at(cid);
  Money reserve = 0;
  for (const auto &r : residents) {
    auto claim = r.routine.wageClaims.find(c.account);
    if (claim != r.routine.wageClaims.end())
      reserve += Money(std::ceil(claim->second));
    if (r.employer == c.account) {
      reserve += Money(std::ceil(r.routine.unpaidWages));
      if (r.routine.workSince >= 0)
        reserve +=
            Money(std::ceil(std::max(0.0, time - r.routine.workSince) / 3600 * r.hourlyWage));
      if (r.alive && r.present)
        reserve += r.hourlyWage * 8;
    }
  }
  for (const auto &l : workspaceLeases)
    if (l.company == cid) {
      reserve += l.arrears;
      if (l.active) {
        reserve += l.dailyRent * 3;
        if (time >= l.nextDue)
          reserve += l.dailyRent * (1 + Money((time - l.nextDue) / 86400));
      }
    }
  return reserve;
}
void World::companyDividend(Id actor, Id cid, Money amount) {
  const auto &c = companies.at(cid);
  if (!directsCompany(actor, cid, true) || amount <= 0 ||
      amount > accounts[c.account].balance - companyReserve(cid))
    throw std::runtime_error(
        "Dividends must leave accrued wages, one payroll shift and three days of rent");
  pay(c.account, residents[actor].account, amount, "company-dividend");
}
void World::changeOfficeEmployer(Id fid, Id employer, bool dismiss) {
  auto &f = facilities.at(fid);
  // Complete the old payroll and refund clients before transferring operating rights.
  for (Id id : f.employees) {
    auto &r = residents[id];
    clockOut(id);
    refundLegacyVideos(*this, id);
    auto job = activeOfficeOrders.find(id);
    if (job != activeOfficeOrders.end()) {
      auto &o = officeOrders[job->second];
      pay(o.escrow, o.client, accounts[o.escrow].balance, "company-transition-refund");
      o.submitted = true;
      o.quality = 0;
      activeOfficeOrders.erase(job);
    }
    Id oldCompany = companyForAccount(r.employer);
    if (oldCompany != none) {
      companies[oldCompany].assignments.erase(id);
      if (companies[oldCompany].leader == id)
        companies[oldCompany].leader = none;
    }
    r.employer = dismiss ? none : employer;
    if (dismiss) {
      if (r.terminal == fid) {
        r.terminal = none;
        r.terminalUntil = 0;
      }
      r.jobSite = none;
      r.role = Role::Unemployed;
      if (r.task == TaskKind::Work) {
        r.pos = position(id);
        r.route.clear();
        ++r.routeVersion;
        r.task = TaskKind::None;
        r.activity = Activity::Idle;
      }
      if (r.automatic)
        schedule(time + 1, EventKind::Decide, id);
    }
  }
  if (dismiss)
    f.employees.clear();
}
static void availableWorkspace(const World &w, Id fid) {
  const auto &f = w.facilities.at(fid);
  if (f.kind != FacilityKind::Office || !f.open || f.demolished || w.activeFires.contains(fid) ||
      w.leasedWorkspaces.contains(fid))
    throw std::runtime_error("An open, unleased office is required");
  for (const auto &p : w.projects)
    if (p.facility == fid && p.state != ProjectState::Completed)
      throw std::runtime_error("Resolve the existing construction contract first");
}
void World::acquireWorkspace(Id actor, Id cid, Id fid) {
  auto &c = companies.at(cid);
  auto &f = facilities.at(fid);
  if (!directsCompany(actor, cid, true))
    throw std::runtime_error("Only the company owner can acquire property");
  availableWorkspace(*this, fid);
  if (f.owner == c.account || (f.owner != residents[actor].account && f.askingPrice <= 0))
    throw std::runtime_error("This office is not offered for sale");
  if (f.owner != residents[actor].account)
    pay(c.account, f.owner, f.askingPrice, "company-workspace-purchase");
  changeOfficeEmployer(fid, c.account);
  f.owner = c.account;
  f.manager = actor;
  f.askingPrice = 0;
  workspaceOffers.erase(fid);
  log("company.acquired", actor, fid);
}
void World::offerWorkspace(Id actor, Id fid, Money dailyRent) {
  const auto &f = facilities.at(fid);
  Id cid = companyForAccount(f.owner);
  if (actor < 0 || std::size_t(actor) >= residents.size() || !residents[actor].alive ||
      !residents[actor].present ||
      !(residents[actor].account == f.owner || (actor == dormOwner && f.owner == dormAccount) ||
        (cid != none && directsCompany(actor, cid, true))) ||
      dailyRent < 0 || dailyRent > 1000000)
    throw std::runtime_error("Only the property owner can publish lease terms");
  availableWorkspace(*this, fid);
  if (dailyRent == 0)
    workspaceOffers.erase(fid);
  else
    workspaceOffers[fid] = dailyRent;
}
Id World::leaseWorkspace(Id actor, Id cid, Id fid) {
  auto &c = companies.at(cid);
  auto &f = facilities.at(fid);
  if (!directsCompany(actor, cid, true))
    throw std::runtime_error("Only the company owner may sign a lease");
  availableWorkspace(*this, fid);
  auto offer = workspaceOffers.find(fid);
  if (offer == workspaceOffers.end() || f.owner == c.account ||
      accounts[c.account].balance < offer->second * 3)
    throw std::runtime_error(
        "A listed workspace needs the first day of rent and a two-day deposit");
  WorkspaceLease l;
  l.id = Id(workspaceLeases.size());
  l.company = cid;
  l.facility = fid;
  l.landlord = f.owner;
  l.manager = f.manager;
  l.dailyRent = offer->second;
  l.deposit = l.dailyRent * 2;
  l.escrow = account("Workspace deposit " + std::to_string(l.id));
  l.nextDue = time + 86400;
  pay(c.account, l.landlord, l.dailyRent, "workspace-rent");
  pay(c.account, l.escrow, l.deposit, "workspace-deposit");
  changeOfficeEmployer(fid, c.account);
  workspaceLeases.push_back(l);
  leasedWorkspaces[fid] = l.id;
  workspaceOffers.erase(fid);
  f.manager = actor;
  log("company.leased", actor, fid);
  return l.id;
}
void World::settleWorkspaceLease(Id id) {
  auto &l = workspaceLeases.at(id);
  if (l.active && time >= l.nextDue) {
    double elapsed = (time - l.nextDue) / 86400;
    if (elapsed > 1000000)
      throw std::runtime_error("Workspace accounting horizon exceeded");
    int days = 1 + int(elapsed);
    l.arrears += days * l.dailyRent;
    l.missedDays += days;
    l.nextDue += days * 86400;
  }
  Money payment = std::min(l.arrears, accounts[companies[l.company].account].balance);
  pay(companies[l.company].account, l.landlord, payment, "workspace-rent");
  l.arrears -= payment;
  // Payments clear the oldest unpaid days first; a partial catch-up must
  // reduce the default clock instead of evicting a nearly current tenant.
  l.missedDays = int((l.arrears + l.dailyRent - 1) / l.dailyRent);
}
void World::closeWorkspaceLease(Id id) {
  auto &l = workspaceLeases.at(id);
  if (!l.active)
    return;
  changeOfficeEmployer(l.facility, l.landlord, true);
  Money retained = std::min(l.arrears, accounts[l.escrow].balance);
  pay(l.escrow, l.landlord, retained, "workspace-deposit-arrears");
  l.arrears -= retained;
  pay(l.escrow, companies[l.company].account, accounts[l.escrow].balance,
      "workspace-deposit-return");
  facilities[l.facility].manager = l.manager;
  l.active = false;
  leasedWorkspaces.erase(l.facility);
  workspaceOffers[l.facility] = l.dailyRent;
  log("company.lease_ended", companies[l.company].owner, l.facility);
}
void World::endWorkspaceLease(Id actor, Id id) {
  auto &l = workspaceLeases.at(id);
  if (!l.active || !directsCompany(actor, l.company, true))
    throw std::runtime_error("Only the tenant company owner may return the workspace");
  settleWorkspaceLease(id);
  closeWorkspaceLease(id);
}
void World::appointTeamLeader(Id actor, Id cid, Id worker) {
  auto &c = companies.at(cid);
  auto &r = residents.at(worker);
  if (!directsCompany(actor, cid, true) || !r.alive || !r.present || r.leaving ||
      r.employer != c.account || r.jobSite == none ||
      facilities[r.jobSite].kind != FacilityKind::Office)
    throw std::runtime_error("A team leader must be a current employee of this company");
  clockOut(worker);
  c.leader = worker;
  r.hourlyWage = std::max(r.hourlyWage, facilities[r.jobSite].wageOffer * 5 / 4);
  log("company.promoted", worker, cid);
}
void World::assignCompanyWork(Id actor, Id cid, Id worker, Role role, int brief) {
  auto &c = companies.at(cid);
  auto &r = residents.at(worker);
  if (!directsCompany(actor, cid) || !r.alive || !r.present || r.leaving ||
      r.employer != c.account || r.jobSite == none ||
      facilities[r.jobSite].kind != FacilityKind::Office || activeOfficeOrders.contains(worker) ||
      brief < 0 || brief > 2 ||
      (role != Role::Programmer && role != Role::VideoEditor && role != Role::Administrator))
    throw std::runtime_error(
        "Assign available office employees after their current contract is submitted");
  clockOut(worker);
  r.role = role;
  c.assignments[worker] = brief;
  officeRetryAt.erase(worker);
  log("company.assigned", worker, cid);
}
void World::dismissEmployee(Id actor, Id fid, Id worker) {
  auto &f = facilities.at(fid);
  auto &r = residents.at(worker);
  Id cid = companyForAccount(operatingAccount(fid));
  if (!directsCompany(actor, cid) || r.jobSite != fid || r.employer != companies[cid].account)
    throw std::runtime_error("Only the employee's company owner or team leader may end this job");
  clockOut(worker);
  refundLegacyVideos(*this, worker);
  auto job = activeOfficeOrders.find(worker);
  if (job != activeOfficeOrders.end()) {
    auto &o = officeOrders[job->second];
    pay(o.escrow, o.client, accounts[o.escrow].balance, "dismissed-work-refund");
    o.submitted = true;
    o.quality = 0;
    activeOfficeOrders.erase(job);
  }
  companies[cid].assignments.erase(worker);
  if (companies[cid].leader == worker)
    companies[cid].leader = none;
  std::erase(f.employees, worker);
  if (r.terminal == fid) {
    r.terminal = none;
    r.terminalUntil = 0;
  }
  r.employer = r.jobSite = none;
  r.role = Role::Unemployed;
  if (r.task == TaskKind::Work) {
    r.pos = position(worker);
    r.route.clear();
    ++r.routeVersion;
    r.task = TaskKind::None;
    r.activity = Activity::Idle;
  }
  if (r.automatic)
    schedule(time + 1, EventKind::Decide, worker);
  log("company.dismissed", worker, cid);
}
void World::companyTick() {
  for (auto &l : workspaceLeases) {
    settleWorkspaceLease(l.id);
    if (l.active && (l.missedDays >= 3 || facilities[l.facility].demolished))
      closeWorkspaceLease(l.id);
  }
  std::map<int, std::vector<Id>> candidates;
  for (const auto &r : residents)
    if (r.automatic && r.id != player && r.alive && r.present && !r.leaving &&
        r.role == Role::Unemployed && r.carrying == none)
      candidates[r.home].push_back(r.id);
  for (auto &c : companies) {
    if (c.owner != player && directsCompany(c.owner, c.id, true)) {
      int recruited = 0;
      for (auto &f : facilities)
        if (f.kind == FacilityKind::Office && f.open && f.hiring &&
            operatingAccount(f.id) == c.account &&
            f.employees.size() < std::size_t(f.stations * 3) && recruited < 2) {
          Id candidate = none;
          for (auto at = candidates.lower_bound(f.floor - 8);
               at != candidates.end() && at->first <= f.floor + 8; ++at) {
            while (!at->second.empty() && residents[at->second.back()].role != Role::Unemployed)
              at->second.pop_back();
            if (!at->second.empty()) {
              candidate = at->second.back();
              at->second.pop_back();
              break;
            }
          }
          if (candidate != none) {
            int shift = int(f.employees.size()) % 3;
            try {
              hireEmployee(c.owner, f.id, candidate,
                           candidate % 2 ? Role::Programmer : Role::VideoEditor, 6 + shift * 8);
              residents[candidate].routine.daysOff = 0;
              ++recruited;
            } catch (const std::runtime_error &) {
            }
          }
        }
    }
    if (c.leader != none && (!residents[c.leader].alive || !residents[c.leader].present ||
                             residents[c.leader].employer != c.account))
      c.leader = none;
    Id supervisor = c.leader != none ? c.leader : c.owner;
    if (supervisor != player && directsCompany(supervisor, c.id))
      for (auto &f : facilities)
        if (f.kind == FacilityKind::Office && operatingAccount(f.id) == c.account)
          for (Id worker : f.employees) {
            auto &r = residents[worker];
            if (r.automatic && r.alive && r.present && r.employer == c.account &&
                !activeOfficeOrders.contains(worker)) {
              int next =
                  c.completed.contains(worker) ? c.completed.at(worker) % 3 : int(worker % 3);
              if (!c.assignments.contains(worker) || c.assignments[worker] != next)
                c.assignments[worker] = next;
            }
          }
    // AI directors promote a proven worker; a player owner chooses their own leader.
    if (c.owner != player && directsCompany(c.owner, c.id, true) && c.leader == none)
      for (auto [worker, count] : c.completed)
        if (count >= 3 && residents[worker].alive && residents[worker].present &&
            residents[worker].employer == c.account) {
          appointTeamLeader(c.owner, c.id, worker);
          break;
        }
  }
  // Old wage claims survive dismissal, moving jobs and lease default.
  std::set<Id> employers;
  for (const auto &c : companies)
    employers.insert(c.account);
  for (auto &r : residents)
    for (auto &[employer, claim] : r.routine.wageClaims)
      if (employers.contains(employer) && claim >= 1) {
        Money amount = std::min(Money(claim), accounts[employer].balance);
        pay(employer, r.account, amount, "company-wage-arrears");
        claim -= amount;
      }
}
void World::validateCompanies() const {
  auto resident = [&](Id id) { return id >= 0 && std::size_t(id) < residents.size(); };
  auto account = [&](Id id) { return id > 0 && std::size_t(id) < accounts.size(); };
  auto facility = [&](Id id) {
    return id >= 0 && std::size_t(id) < facilities.size() &&
           facilities[id].kind == FacilityKind::Office;
  };
  std::set<Id> companyAccounts, personalAccounts{dormAccount};
  for (const auto &r : residents)
    personalAccounts.insert(r.account);
  for (std::size_t i = 0; i < companies.size(); ++i) {
    const auto &c = companies[i];
    if (c.id != Id(i) || !resident(c.owner) || !account(c.account) ||
        personalAccounts.contains(c.account) || !companyAccounts.insert(c.account).second ||
        (c.leader != none && !resident(c.leader)) || c.name.empty() || c.name.size() > 80 ||
        c.revenue < 0)
      throw std::runtime_error("Invalid company");
    for (auto [worker, brief] : c.assignments)
      if (!resident(worker) || brief < 0 || brief > 2)
        throw std::runtime_error("Invalid company assignment");
    for (auto [worker, count] : c.completed)
      if (!resident(worker) || count < 0)
        throw std::runtime_error("Invalid employee performance record");
  }
  std::map<Id, Id> active;
  for (std::size_t i = 0; i < workspaceLeases.size(); ++i) {
    const auto &l = workspaceLeases[i];
    if (l.id != Id(i) || l.company < 0 || std::size_t(l.company) >= companies.size() ||
        !facility(l.facility) || !account(l.landlord) || !account(l.escrow) ||
        (l.manager != none && !resident(l.manager)) || l.dailyRent <= 0 || l.dailyRent > 1000000 ||
        l.deposit != l.dailyRent * 2 || l.arrears < 0 || l.missedDays < 0 ||
        !std::isfinite(l.nextDue) || l.nextDue < 0 ||
        accounts[l.escrow].balance != (l.active ? l.deposit : 0))
      throw std::runtime_error("Invalid workspace lease");
    if (l.active &&
        (facilities[l.facility].owner != l.landlord || !active.emplace(l.facility, l.id).second))
      throw std::runtime_error("Conflicting workspace leases");
  }
  if (active != leasedWorkspaces)
    throw std::runtime_error("Invalid active workspace index");
  for (auto [fid, rent] : workspaceOffers)
    if (!facility(fid) || rent <= 0 || rent > 1000000 || active.contains(fid))
      throw std::runtime_error("Invalid workspace offer");
}
} // namespace mega
