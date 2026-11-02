#include "mega/world.hpp"
#include <algorithm>
#include <numeric>
#include <tuple>

namespace mega {
namespace {
struct Job {
  Id workshop = none, lead = none, payer = none, escrow = none, contractor = none;
  Vec3 site{};
  double remaining = 0;
  bool finished = false, supplied = false;
};
Job job(const World &w, CrewContractKind kind, Id id, Id workshop = none) {
  Job j;
  switch (kind) {
  case CrewContractKind::Facility: {
    const auto &p = w.projects.at(id);
    j.site = w.facilities.at(p.facility).pos;
    j.workshop = p.source != none ? p.source : w.nearestAt(j.site, FacilityKind::Workshop);
    j.lead = p.worker;
    j.payer = w.residents.at(p.owner).account;
    j.escrow = p.escrow;
    j.finished = p.state != ProjectState::Funded && p.state != ProjectState::Working;
    j.supplied = !p.newConstruction || p.materialsDelivered;
    j.remaining = p.remaining;
    break;
  }
  case CrewContractKind::Housing: {
    const auto &p = w.housingWorks.at(id);
    j.site = p.site;
    j.workshop = p.workshop;
    j.lead = p.worker;
    j.payer = p.payer;
    j.escrow = p.escrow;
    j.finished =
        p.cancelled || (p.state != ProjectState::Funded && p.state != ProjectState::Working);
    j.supplied = p.supplied;
    j.remaining = p.remaining;
    break;
  }
  case CrewContractKind::Expansion: {
    const auto &p = w.expansions.at(id);
    j.site = p.site;
    j.workshop = p.workshop != none ? p.workshop : w.nearestAt(j.site, FacilityKind::Workshop);
    j.lead = p.worker;
    j.payer = p.owner;
    j.escrow = p.escrow;
    j.contractor = p.contractor;
    j.finished = p.complete;
    j.supplied = p.supplied;
    j.remaining = p.remaining;
    break;
  }
  case CrewContractKind::Repair: {
    const auto &p = w.repairs.at(id);
    j.site = w.facilities.at(p.facility).pos;
    j.workshop = p.workshop;
    j.lead = p.worker;
    j.payer = w.operatingAccount(p.facility);
    j.escrow = p.escrow;
    j.finished = p.complete;
    j.remaining = p.remaining;
    if (p.parts && p.partItem != none) {
      const auto &part = w.items.at(p.partItem);
      Vec3 at = part.carrier == none ? part.pos : w.position(part.carrier);
      j.supplied = !part.consumed && std::abs(at.y - j.site.y) < .5f && length(at - j.site) < 4;
    }
    break;
  }
  }
  if (workshop != none)
    j.workshop = workshop;
  return j;
}
bool controls(const World &w, Id actor, Id shop) {
  const auto &f = w.facilities.at(shop);
  return w.residents.at(actor).account == f.owner ||
         w.directsCompany(actor, w.companyForAccount(f.owner), true);
}
bool free(const World &w, Id id) {
  const auto &r = w.residents.at(id);
  return r.alive && r.present && r.age >= 18 && r.role == Role::Builder && !r.surrendered &&
         r.carrying == none && r.bodyCarrying == none && r.lift == none && r.vehicle == none &&
         r.ambulance == none && r.carriedBy == none && r.medicalDuty == none &&
         !w.looseBodies.contains(id) && r.activity != Activity::Jailed &&
         r.activity != Activity::Restrained && r.activity != Activity::Escorted &&
         r.activity != Activity::Fighting && r.activity != Activity::Sleeping;
}
void stop(World &w, Id id, Id contract) {
  auto &r = w.residents.at(id);
  if (r.task != TaskKind::CrewWork || r.target != contract)
    return;
  r.pos = w.position(id);
  r.route.clear();
  ++r.routeVersion;
  r.task = TaskKind::None;
  r.target = none;
  if (r.activity == Activity::Walking || r.activity == Activity::Working)
    r.activity = Activity::Idle;
}
void reduce(World &w, const CrewContract &c, double dt) {
  switch (c.kind) {
  case CrewContractKind::Facility:
    w.projects[c.work].remaining -= dt;
    break;
  case CrewContractKind::Housing:
    w.housingWorks[c.work].remaining -= dt;
    break;
  case CrewContractKind::Expansion:
    w.expansions[c.work].remaining -= dt;
    break;
  case CrewContractKind::Repair:
    w.repairs[c.work].remaining -= dt;
    break;
  }
}
void assignLead(World &w, CrewContractKind kind, Id work, Id worker) {
  switch (kind) {
  case CrewContractKind::Facility:
    w.assignBuilder(work, worker);
    break;
  case CrewContractKind::Housing:
    w.assignHousingWork(worker, work);
    break;
  case CrewContractKind::Expansion:
    w.assignExpansion(worker, work);
    break;
  case CrewContractKind::Repair:
    throw std::runtime_error("crew.error.repair_lead");
  }
}
} // namespace

Id World::assignCrewContract(Id actor, Id team, CrewContractKind kind, Id work) {
  auto &t = constructionTeams.at(team);
  auto j = job(*this, kind, work);
  const auto &a = residents.at(actor);
  if (!a.alive || !a.present || t.foreman == none || t.work != none || t.contract != none ||
      j.finished || j.workshop != t.workshop || t.members.empty() ||
      (actor != t.foreman && !controls(*this, actor, t.workshop) && a.account != j.payer &&
       actor != dormOwner) ||
      (actor != t.foreman && !residents[t.foreman].automatic) ||
      (j.lead != none && actor != j.lead && !residents[j.lead].automatic))
    throw std::runtime_error("crew.error.assignment");
  for (const auto &c : crewContracts)
    if (c.active && c.kind == kind && c.work == work)
      throw std::runtime_error("crew.error.assignment");
  for (Id id : t.members)
    if ((!free(*this, id) && id != j.lead) || (builderCommitted(id) && id != j.lead))
      throw std::runtime_error("crew.error.assignment");
  if (j.lead == none)
    assignLead(*this, kind, work, t.foreman);
  CrewContract c;
  c.id = Id(crewContracts.size());
  c.team = team;
  c.kind = kind;
  c.work = work;
  c.workshop = j.workshop;
  crewContracts.push_back(c);
  t.contract = c.id;
  return c.id;
}
void World::cancelCrewContract(Id actor, Id id) {
  auto &c = crewContracts.at(id);
  auto &t = constructionTeams.at(c.team);
  auto j = job(*this, c.kind, c.work, c.workshop);
  const auto &r = residents.at(actor);
  if (!r.alive || !r.present || !c.active ||
      (actor != t.foreman && !controls(*this, actor, t.workshop) && r.account != j.payer &&
       actor != dormOwner))
    throw std::runtime_error("crew.error.assignment");
  c.active = false;
  c.attendance.clear();
  t.contract = none;
  for (Id worker : t.members)
    stop(*this, worker, id);
}
Id World::crewContractFor(Id worker) const {
  for (const auto &t : constructionTeams)
    if (t.members.contains(worker) && t.contract != none && crewContracts.at(t.contract).active)
      return t.contract;
  return none;
}
Vec3 World::crewContractDestination(Id worker, Id id) const {
  const auto &c = crewContracts.at(id);
  const auto &t = constructionTeams.at(c.team);
  if (!c.active || !t.members.contains(worker))
    throw std::runtime_error("crew.error.assignment");
  auto j = job(*this, c.kind, c.work, c.workshop);
  if (worker == j.lead)
    return j.site;
  int index = 0;
  for (Id member : t.members) {
    if (member == worker)
      break;
    ++index;
  }
  float angle = float(index) * .7853981634f;
  return accessible(j.site + Vec3{std::cos(angle) * 2.2f, 0, std::sin(angle) * 2.2f});
}
void World::beginCrewContractWork(Id worker, Id id) {
  auto &c = crewContracts.at(id);
  auto j = job(*this, c.kind, c.work, c.workshop);
  if (worker == j.lead)
    throw std::runtime_error("crew.error.lead");
  if (!c.active || j.finished || !constructionTeams[c.team].members.contains(worker) ||
      !free(*this, worker) || !onShift(worker) ||
      !near(worker, crewContractDestination(worker, id), .8f))
    throw std::runtime_error("structure.error.attend");
  clockOut(worker);
  auto &r = residents[worker];
  r.activity = Activity::Working;
  r.task = TaskKind::CrewWork;
  r.target = id;
  c.attendance[worker] = time;
}
bool World::crewContractDecision(Id worker) {
  Id id = crewContractFor(worker);
  if (id == none && residents.at(worker).automatic) {
    for (const auto &t : constructionTeams) {
      if (t.foreman != worker || t.work != none || t.contract != none)
        continue;
      auto accept = [&](CrewContractKind kind, Id work) {
        auto candidate = job(*this, kind, work);
        if (candidate.finished || candidate.workshop != t.workshop ||
            (kind == CrewContractKind::Repair && candidate.lead == none))
          return false;
        try {
          id = assignCrewContract(worker, t.id, kind, work);
          return true;
        } catch (const std::runtime_error &) {
          return false;
        }
      };
      for (const auto &p : projects)
        if (accept(CrewContractKind::Facility, p.id))
          break;
      if (id == none)
        for (const auto &p : housingWorks)
          if (accept(CrewContractKind::Housing, p.id))
            break;
      if (id == none)
        for (const auto &p : expansions)
          if (accept(CrewContractKind::Expansion, p.id))
            break;
      if (id == none)
        for (const auto &p : repairs)
          if (accept(CrewContractKind::Repair, p.id))
            break;
      break;
    }
  }
  if (id == none)
    return false;
  auto &c = crewContracts[id];
  auto j = job(*this, c.kind, c.work, c.workshop);
  if (worker == j.lead || j.finished)
    return false;
  if (!free(*this, worker))
    return true;
  try {
    Vec3 goal = crewContractDestination(worker, id);
    if (near(worker, goal, .7f)) {
      if (residents[worker].activity != Activity::Working ||
          residents[worker].task != TaskKind::CrewWork)
        beginCrewContractWork(worker, id);
    } else if (residents[worker].route.empty())
      travel(worker, goal, TaskKind::CrewWork, id);
  } catch (const std::runtime_error &) {
    c.attendance.erase(worker);
  }
  return true;
}
void World::crewContractTick() {
  for (auto &c : crewContracts) {
    if (!c.active)
      continue;
    auto &t = constructionTeams[c.team];
    auto j = job(*this, c.kind, c.work, c.workshop);
    if (j.finished || t.members.empty()) {
      c.active = false;
      c.attendance.clear();
      t.contract = none;
      for (Id worker : t.members)
        stop(*this, worker, c.id);
      continue;
    }
    std::erase_if(t.members, [&](Id id) {
      if (residents[id].alive && residents[id].present && residents[id].role == Role::Builder)
        return false;
      c.attendance.erase(id);
      stop(*this, id, c.id);
      return true;
    });
    if (!t.members.contains(t.foreman))
      t.foreman = t.members.empty() ? none : *t.members.begin();
    if (j.lead == none && t.foreman != none && c.kind != CrewContractKind::Repair &&
        residents[t.foreman].automatic) {
      // The original contract retains materials, escrow, final inspection and settlement.
      t.contract = none;
      try {
        assignLead(*this, c.kind, c.work, t.foreman);
      } catch (const std::runtime_error &) {
      }
      t.contract = c.id;
      j = job(*this, c.kind, c.work, c.workshop);
    }
    std::map<Id, double> labor;
    for (Id worker : t.members) {
      auto &r = residents[worker];
      if (worker == j.lead || !free(*this, worker) || !onShift(worker) ||
          r.activity != Activity::Working || r.task != TaskKind::CrewWork || r.target != c.id ||
          !near(worker, crewContractDestination(worker, c.id), .85f)) {
        c.attendance.erase(worker);
        continue;
      }
      auto at = c.attendance.find(worker);
      double dt = at == c.attendance.end() ? 0 : std::clamp(time - at->second, 0.0, 60.0);
      c.attendance[worker] = time;
      if (j.supplied && j.lead != none && j.remaining > 1 && dt > 0)
        labor[worker] = dt;
    }
    if (labor.empty())
      continue;
    bool supervision = t.foreman != none && residents[t.foreman].activity == Activity::Working &&
                       onShift(t.foreman) && near(t.foreman, j.site, 4);
    double bonus = supervision ? 1.1 : 1;
    double total = std::accumulate(labor.begin(), labor.end(), 0.0,
                                   [](double n, const auto &v) { return n + v.second; });
    double scale = std::min(1.0, (j.remaining - 1) / (total * bonus));
    Money cost = 0;
    std::map<Id, Money> wages;
    for (auto [id, dt] : labor) {
      wages[id] = Money(std::ceil(dt * scale / 3600 * residents[id].hourlyWage));
      cost += wages[id];
    }
    Money reserve = c.kind == CrewContractKind::Repair ? 6000 : 1;
    Money available = accounts[j.escrow].balance;
    if (c.kind == CrewContractKind::Expansion) {
      auto &p = expansions[c.work];
      Money earned = Money(p.budget * (1 - (p.remaining - total * scale * bonus) / p.totalWork));
      Money payout = std::max(Money(0), earned - p.paid);
      if (accounts[p.contractor].balance + payout - cost < reserve)
        continue;
      pay(p.escrow, p.contractor, payout, "construction-progress");
      p.paid += payout;
      available = accounts[p.contractor].balance;
    }
    if (available - cost < reserve)
      continue;
    Id payer = c.kind == CrewContractKind::Expansion ? j.contractor : j.escrow;
    for (auto [id, wage] : wages)
      pay(payer, residents[id].account, wage, "construction-crew-wage");
    if (c.kind == CrewContractKind::Facility)
      projects[c.work].spent += cost;
    // Keep the final second for the original lead's physical inspection/finalizer.
    reduce(*this, c, total * scale * bonus);
  }
}
void World::validateCrewContracts() const {
  auto ref = [](Id id, std::size_t size) { return id >= 0 && std::size_t(id) < size; };
  std::set<std::pair<int, Id>> assignments;
  for (const auto &t : constructionTeams)
    if (t.contract != none &&
        (!ref(t.contract, crewContracts.size()) || t.work != none ||
         !crewContracts[t.contract].active || crewContracts[t.contract].team != t.id))
      throw std::runtime_error("Invalid crew assignment");
  for (std::size_t i = 0; i < crewContracts.size(); ++i) {
    const auto &c = crewContracts[i];
    if (c.id != Id(i) || !ref(c.workshop, facilities.size()) ||
        facilities[c.workshop].kind != FacilityKind::Workshop ||
        !ref(c.team, constructionTeams.size()) || int(c.kind) < 0 ||
        int(c.kind) > int(CrewContractKind::Repair))
      throw std::runtime_error("Invalid crew contract");
    auto j = job(*this, c.kind, c.work, c.workshop);
    const auto &t = constructionTeams[c.team];
    if (c.active && (t.contract != c.id || t.workshop != j.workshop ||
                     !assignments.insert({int(c.kind), c.work}).second))
      throw std::runtime_error("Invalid duplicate crew contract");
    for (auto [worker, at] : c.attendance)
      if (!c.active || !t.members.contains(worker) || !std::isfinite(at) || at < 0 || at > time)
        throw std::runtime_error("Invalid crew attendance");
  }
}
} // namespace mega
