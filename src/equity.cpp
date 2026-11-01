#include "mega/world.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>

namespace mega {
namespace {
constexpr int capitalUnits = 10000;
bool liveActor(const World &w, Id id) {
  return id >= 0 && std::size_t(id) < w.residents.size() && w.residents[id].alive &&
         w.residents[id].present && !w.residents[id].leaving &&
         w.residents[id].activity != Activity::Jailed &&
         w.residents[id].activity != Activity::Restrained;
}
Id personFor(const World &w, Id account) {
  if (w.equityPopulation != w.residents.size()) {
    w.equityPeople.clear();
    for (const auto &r : w.residents)
      w.equityPeople[r.account] = r.id;
    w.equityPopulation = w.residents.size();
  }
  auto it = w.equityPeople.find(account);
  return it == w.equityPeople.end() ? none : it->second;
}
bool marketAccess(const World &w, Id actor, Id cid) {
  if (w.hasDevice(actor))
    return true;
  for (const auto &f : w.facilities)
    if (f.kind == FacilityKind::Office && f.open && !f.demolished &&
        w.operatingAccount(f.id) == w.companies[cid].account && w.near(actor, f.pos, 3))
      return true;
  return false;
}
void checkAcquisitionGraph(const World &w, Id buyer, Id target) {
  std::vector<int> state(w.companies.size()), height(w.companies.size());
  std::function<int(Id)> visit = [&](Id cid) {
    if (state[cid] == 1)
      throw std::runtime_error("capital.error.cycle");
    if (state[cid] == 2)
      return height[cid];
    state[cid] = 1;
    int depth = 1;
    if (cid == target)
      depth = 1 + visit(buyer);
    else
      for (auto [account, units] : w.companies[cid].shares) {
        Id next = w.companyForAccount(account);
        if (next != none)
          depth = std::max(depth, 1 + visit(next));
      }
    if (depth > 128)
      throw std::runtime_error("capital.error.cycle");
    state[cid] = 2;
    return height[cid] = depth;
  };
  for (Id c = 0; c < Id(w.companies.size()); ++c)
    visit(c);
}
} // namespace
Id World::companyController(Id cid) const {
  if (cid < 0 || std::size_t(cid) >= companies.size())
    return none;
  std::set<Id> visiting;
  std::function<Id(Id)> controller = [&](Id id) -> Id {
    if (visiting.size() >= 128 || !visiting.insert(id).second)
      return none;
    const auto &c = companies[id];
    Id best = none;
    std::map<Id, int> votes;
    for (auto [acct, units] : c.shares) {
      Id parent = companyForAccount(acct);
      Id candidate = parent == none ? personFor(*this, acct) : controller(parent);
      if (liveActor(*this, candidate))
        votes[candidate] += units;
    }
    int largest = -1;
    for (auto [candidate, units] : votes)
      if (units > largest || (units == largest && candidate == c.owner)) {
        best = candidate;
        largest = units;
      }
    visiting.erase(id);
    return best;
  };
  return controller(cid);
}
bool World::controlsAccount(Id actor, Id acct) const {
  if (!liveActor(*this, actor))
    return false;
  if (residents[actor].account == acct)
    return true;
  Id c = companyForAccount(acct);
  return c != none && companyController(c) == actor;
}
void World::refreshCompanyControl(Id cid) {
  auto &c = companies.at(cid);
  Id prior = c.owner, now = companyController(cid);
  c.parent = none;
  for (auto [acct, units] : c.shares)
    if (units > capitalUnits / 2) {
      Id parent = companyForAccount(acct);
      if (parent != none)
        c.parent = parent;
    }
  if (now != none)
    c.owner = now;
  if (c.owner != prior) {
    for (auto &f : facilities)
      if (operatingAccount(f.id) == c.account && f.manager == prior)
        f.manager = c.owner;
    for (auto &p : projects)
      if (p.newConstruction && p.state != ProjectState::Completed &&
          facilities[p.facility].owner == c.account && p.owner == prior)
        p.owner = c.owner;
    for (auto &l : workspaceLeases)
      if (l.active && l.landlord == c.account && l.manager == prior)
        l.manager = c.owner;
    log("capital.control_changed", c.owner, cid);
  }
}
int World::heldShares(Id actor, Id cid) const {
  const auto &c = companies.at(cid);
  Id acct = residents.at(actor).account;
  auto it = c.shares.find(acct);
  return it == c.shares.end() ? 0 : it->second;
}
int World::reservedShares(Id acct, Id cid) const {
  int count = 0;
  for (const auto &o : shareOffers)
    if (o.active && o.company == cid && o.seller == acct)
      count += o.units;
  return count;
}
Money World::companyValue(Id cid) const {
  std::set<Id> seen;
  std::function<Money(Id)> value = [&](Id id) -> Money {
    if (seen.size() >= 128 || !seen.insert(id).second)
      return 0;
    const auto &c = companies.at(id);
    constexpr Money ceiling = 10000000000000LL;
    Money v = std::clamp<Money>(accounts[c.account].balance - companyReserve(id), 0, ceiling);
    auto add = [&](Money amount) { v = amount >= ceiling - v ? ceiling : v + amount; };
    add(std::min<Money>(1000000, c.revenue));
    for (const auto &f : facilities)
      if (!f.demolished && f.owner == c.account)
        add(300000 + Money(f.stations) * 100000);
    for (const auto &vehicle : vehicles)
      if (vehicle.owner == c.account)
        add(Money(
            std::min<double>(ceiling, vehicle.originalPrice * double(vehicle.condition) * .6)));
    for (const auto &child : companies) {
      auto it = child.shares.find(c.account);
      if (it != child.shares.end()) {
        Money worth = value(child.id);
        add(worth / capitalUnits * it->second + worth % capitalUnits * it->second / capitalUnits);
      }
    }
    seen.erase(id);
    return v;
  };
  return std::max<Money>(10000, value(cid));
}
Id World::listShares(Id actor, Id cid, int units, Money price, bool capital) {
  auto &c = companies.at(cid);
  if (!liveActor(*this, actor) || units < 1 || units > capitalUnits || price < 1 ||
      price > 1000000000)
    throw std::runtime_error("capital.error.offer");
  Id seller = residents[actor].account;
  if (!c.shares.contains(seller) && c.parent != none && directsCompany(actor, c.parent, true))
    seller = companies[c.parent].account;
  if (!controlsAccount(actor, seller) || !c.shares.contains(seller) ||
      c.shares.at(seller) - reservedShares(seller, cid) < units ||
      (capital && !directsCompany(actor, cid, true)))
    throw std::runtime_error("capital.error.offer");
  ShareOffer o;
  o.id = Id(shareOffers.size());
  o.company = cid;
  o.seller = seller;
  o.units = units;
  o.unitPrice = price;
  o.capital = capital;
  shareOffers.push_back(o);
  return o.id;
}
void World::cancelShareOffer(Id actor, Id id) {
  auto &o = shareOffers.at(id);
  if (!o.active || !controlsAccount(actor, o.seller))
    throw std::runtime_error("capital.error.offer");
  o.active = false;
}
void World::buyShares(Id buyer, Id id, int units) {
  auto &o = shareOffers.at(id);
  auto &r = residents.at(buyer);
  auto &c = companies.at(o.company);
  if (!liveActor(*this, buyer) || r.age < 18 || !o.active || units < 1 || units > o.units ||
      o.seller == r.account || !marketAccess(*this, buyer, c.id) || !c.shares.contains(o.seller) ||
      c.shares.at(o.seller) < units)
    throw std::runtime_error("capital.error.trade");
  Id recipient = o.capital ? c.account : o.seller;
  Id person = personFor(*this, recipient);
  if (person != none && !residents[person].present)
    recipient = 0;
  pay(r.account, recipient, Money(units) * o.unitPrice,
      o.capital ? "company-share-capital" : "company-share-sale");
  c.shares[o.seller] -= units;
  if (c.shares[o.seller] == 0)
    c.shares.erase(o.seller);
  c.shares[r.account] += units;
  o.units -= units;
  if (o.units == 0)
    o.active = false;
  for (Id cid = 0; cid < Id(companies.size()); ++cid)
    refreshCompanyControl(cid);
  log("capital.traded", buyer, c.id);
}
void World::distributeEquity(Id source, Id cid, Money amount, const std::string &reason) {
  const auto &c = companies.at(cid);
  if (amount < 0 || amount > 10000000000000LL || accounts.at(source).balance < amount)
    throw std::runtime_error("capital.error.funds");
  std::vector<std::pair<Id, Money>> portions;
  Money left = amount;
  Id largest = none;
  int maxUnits = 0;
  for (auto [acct, units] : c.shares) {
    Money part = amount * units / capitalUnits;
    portions.emplace_back(acct, part);
    left -= part;
    if (units > maxUnits) {
      largest = acct;
      maxUnits = units;
    }
  }
  for (auto [acct, portion] : portions) {
    if (acct == largest)
      portion += left;
    Id resident = personFor(*this, acct);
    Id payee = resident != none && !residents[resident].present ? 0 : acct;
    pay(source, payee, portion, reason);
  }
}
void World::seedCapitalMarkets() {
  for (const auto &c : companies)
    if (c.owner != player && liveActor(*this, c.owner))
      listShares(c.owner, c.id, 1500,
                 std::max<Money>(1, (companyValue(c.id) + 9999) / capitalUnits), true);
}
Id World::proposeAcquisition(Id actor, Id buyer, Id target, Money price) {
  if (buyer == target || buyer < 0 || target < 0 || std::size_t(buyer) >= companies.size() ||
      std::size_t(target) >= companies.size() || !directsCompany(actor, buyer, true) ||
      price < 10000 || price > 10000000000000LL)
    throw std::runtime_error("capital.error.acquire");
  checkAcquisitionGraph(*this, buyer, target);
  for (const auto &a : acquisitions)
    if (a.state == AcquisitionState::Pending && a.target == target)
      throw std::runtime_error("capital.error.acquire");
  auto &b = companies[buyer];
  if (accounts[b.account].balance < companyReserve(buyer) + price)
    throw std::runtime_error("capital.error.funds");
  Acquisition a;
  a.id = Id(acquisitions.size());
  a.buyer = buyer;
  a.target = target;
  a.proposer = actor;
  a.price = price;
  a.expires = time + 3 * 86400;
  a.escrow = account("Company acquisition escrow");
  pay(b.account, a.escrow, price, "company-acquisition-funding");
  acquisitions.push_back(a);
  return a.id;
}
void World::voteAcquisition(Id actor, Id id, bool approve) {
  auto &a = acquisitions.at(id);
  if (!liveActor(*this, actor) || a.state != AcquisitionState::Pending || time >= a.expires)
    throw std::runtime_error("capital.error.vote");
  bool eligible = false;
  for (auto [acct, units] : companies[a.target].shares)
    if (controlsAccount(actor, acct)) {
      a.ballots[acct] = approve;
      eligible = true;
    }
  if (!eligible)
    throw std::runtime_error("capital.error.vote");
  int yes = 0;
  for (auto [acct, units] : companies[a.target].shares)
    if (a.ballots.contains(acct) && a.ballots.at(acct))
      yes += units;
  if (yes > capitalUnits / 2)
    completeAcquisition(id);
}
void World::completeAcquisition(Id id) {
  auto &a = acquisitions.at(id);
  if (a.state != AcquisitionState::Pending || time >= a.expires ||
      !directsCompany(a.proposer, a.buyer, true))
    throw std::runtime_error("capital.error.acquire");
  int yes = 0;
  for (auto [acct, units] : companies[a.target].shares)
    if (a.ballots.contains(acct) && a.ballots.at(acct))
      yes += units;
  if (yes <= capitalUnits / 2)
    throw std::runtime_error("capital.error.vote");
  checkAcquisitionGraph(*this, a.buyer, a.target);
  if (accounts[a.escrow].balance != a.price)
    throw std::runtime_error("capital.error.funds");
  distributeEquity(a.escrow, a.target, a.price, "company-acquisition-buyout");
  auto &t = companies[a.target];
  for (auto &o : shareOffers)
    if (o.company == a.target)
      o.active = false;
  t.shares.clear();
  t.shares[companies[a.buyer].account] = capitalUnits;
  t.parent = a.buyer;
  for (Id cid = 0; cid < Id(companies.size()); ++cid)
    refreshCompanyControl(cid);
  a.state = AcquisitionState::Complete;
  log("capital.acquired", a.proposer, a.target);
}
void World::cancelAcquisition(Id actor, Id id) {
  auto &a = acquisitions.at(id);
  if (a.state != AcquisitionState::Pending ||
      (!directsCompany(actor, a.buyer, true) && !(actor == none && time >= a.expires)))
    throw std::runtime_error("capital.error.acquire");
  pay(a.escrow, companies[a.buyer].account, accounts[a.escrow].balance,
      "company-acquisition-refund");
  a.state = AcquisitionState::Cancelled;
}
void World::nominateEquityHeir(Id actor, Id heir) {
  if (!liveActor(*this, actor) ||
      (heir != none && (!liveActor(*this, heir) || heir == actor || residents[heir].age < 18)))
    throw std::runtime_error("capital.error.heir");
  if (heir == none)
    equityHeirs.erase(actor);
  else
    equityHeirs[actor] = heir;
}
void World::equityTick() {
  std::map<Id, Id> people;
  for (const auto &r : residents)
    people[r.account] = r.id;
  for (auto &c : companies) {
    std::vector<std::pair<Id, Id>> inherit;
    for (auto [acct, units] : c.shares) {
      auto who = people.find(acct);
      if (who == people.end() || residents[who->second].alive)
        continue;
      Id heir = equityHeirs.contains(who->second) ? equityHeirs.at(who->second) : none;
      if (!liveActor(*this, heir) && residents[who->second].automatic) {
        if (liveActor(*this, c.leader) && residents[c.leader].age >= 18)
          heir = c.leader;
        else
          for (const auto &f : facilities)
            if (f.kind == FacilityKind::Office && operatingAccount(f.id) == c.account)
              for (Id employee : f.employees)
                if (liveActor(*this, employee) && residents[employee].age >= 18) {
                  heir = employee;
                  break;
                }
      }
      if (liveActor(*this, heir)) {
        inherit.emplace_back(acct, residents[heir].account);
        log("capital.inherited", heir, c.id);
      }
    }
    for (auto [from, to] : inherit) {
      for (auto &o : shareOffers)
        if (o.company == c.id && o.seller == from)
          o.active = false;
      c.shares[to] += c.shares[from];
      c.shares.erase(from);
    }
  }
  for (Id cid = 0; cid < Id(companies.size()); ++cid)
    refreshCompanyControl(cid);
  for (auto &a : acquisitions)
    if (a.state == AcquisitionState::Pending) {
      if (time >= a.expires) {
        cancelAcquisition(none, a.id);
        continue;
      }
      if (!directsCompany(a.proposer, a.buyer, true)) {
        pay(a.escrow, companies[a.buyer].account, accounts[a.escrow].balance,
            "company-control-change-refund");
        a.state = AcquisitionState::Cancelled;
        continue;
      }
      if (a.price < companyValue(a.target))
        continue;
      auto holders = companies[a.target].shares;
      for (auto [acct, units] : holders) {
        Id voter =
            people.contains(acct) ? people[acct] : companyController(companyForAccount(acct));
        if (voter != player && liveActor(*this, voter) && residents[voter].automatic &&
            !a.ballots.contains(acct)) {
          try {
            voteAcquisition(voter, a.id, true);
          } catch (const std::runtime_error &) {
          }
          if (a.state != AcquisitionState::Pending)
            break;
        }
      }
    }
  for (auto &o : shareOffers)
    if (o.active && random() < .08f) {
      if (o.unitPrice > std::max<Money>(1, companyValue(o.company) / capitalUnits))
        continue;
      std::size_t first = std::size_t(random() * residents.size());
      for (std::size_t n = 0; n < std::min<std::size_t>(64, residents.size()); ++n) {
        auto &r = residents[(first + n) % residents.size()];
        if (r.id == player || !r.automatic || !liveActor(*this, r.id) || r.age < 18 ||
            r.account == o.seller || !hasDevice(r.id))
          continue;
        Money spare = accounts[r.account].balance - householdReserve(r.id);
        if (spare < 120000)
          continue;
        int units = int(std::min<Money>({250, o.units, spare / 8 / o.unitPrice}));
        if (units < 1)
          continue;
        try {
          buyShares(r.id, o.id, units);
        } catch (const std::runtime_error &) {
        }
        break;
      }
    }
}
void World::setCompanyExpansion(Id actor, Id cid, bool enabled) {
  if (!directsCompany(actor, cid, true))
    throw std::runtime_error("capital.error.permission");
  companies[cid].autoExpand = enabled;
}
Id World::expandCompany(Id actor, Id cid, bool unoccupiedOnly) {
  auto &c = companies.at(cid);
  if (!directsCompany(actor, cid, true))
    throw std::runtime_error("capital.error.permission");
  int spaces = 0, anchor = residents[actor].home;
  for (const auto &f : facilities)
    if (f.kind == FacilityKind::Office && !f.demolished && operatingAccount(f.id) == c.account) {
      ++spaces;
      anchor = f.floor;
      if (!f.open)
        throw std::runtime_error("capital.error.pending");
    }
  if (spaces >= 4)
    throw std::runtime_error("capital.error.expansion");
  Money available = accounts[c.account].balance - companyReserve(cid);
  Id best = none;
  Money cost = std::numeric_limits<Money>::max();
  for (auto [fid, rent] : workspaceOffers) {
    const auto &f = facilities[fid];
    if (!f.open || f.demolished || leasedWorkspaces.contains(fid) || f.owner == c.account ||
        std::abs(f.floor - anchor) > 16 || activeFires.contains(fid) ||
        (unoccupiedOnly && !f.employees.empty()))
      continue;
    Money need = rent * 10 + Money(f.employees.size() + 3) * f.wageOffer * 8;
    if (available >= need && need < cost) {
      best = fid;
      cost = need;
    }
  }
  if (best != none) {
    leaseWorkspace(actor, cid, best);
    c.nextExpansionAt = time + 86400;
    log("capital.expanded", actor, best);
    return best;
  }
  if (available < 640000)
    throw std::runtime_error("capital.error.funds");
  for (float x : {80.f, -80.f, 110.f, -110.f})
    for (float z : {130.f, 160.f, 190.f}) {
      try {
        Id project =
            commissionFacility(actor, FacilityKind::Office, anchor, {x, anchor * 3.f, z}, 2, cid);
        c.nextExpansionAt = time + 86400;
        log("capital.expanded", actor, projects[project].facility);
        return projects[project].facility;
      } catch (const std::runtime_error &) {
      }
    }
  throw std::runtime_error("capital.error.expansion");
}
void World::expansionTick(Id cid) {
  auto &c = companies.at(cid);
  if (!c.autoExpand || time < c.nextExpansionAt || !directsCompany(c.owner, cid, true))
    return;
  c.nextExpansionAt = time + 21600;
  int capacity = 0, staff = 0, completed = 0;
  for (auto [worker, count] : c.completed)
    completed += count;
  for (const auto &f : facilities)
    if (f.kind == FacilityKind::Office && !f.demolished && operatingAccount(f.id) == c.account) {
      capacity += f.stations;
      staff += int(f.employees.size());
      if (!f.open)
        return;
    }
  if (capacity == 0 || staff < capacity * 2 || completed < 8 || c.revenue < 200000)
    return;
  try {
    expandCompany(c.owner, cid, true);
  } catch (const std::runtime_error &) {
  }
}
void World::validateEquity() const {
  auto acct = [&](Id id) { return id > 0 && std::size_t(id) < accounts.size(); };
  auto person = [&](Id id) { return id >= 0 && std::size_t(id) < residents.size(); };
  auto firm = [&](Id id) { return id >= 0 && std::size_t(id) < companies.size(); };
  for (std::size_t index = 0; index < companies.size(); ++index) {
    const auto &c = companies[index];
    std::int64_t total = 0;
    if (c.id != Id(index) || !person(c.owner) || !acct(c.account) || c.shares.empty() ||
        c.shares.size() > residents.size() + companies.size() ||
        !std::isfinite(c.nextExpansionAt) || c.nextExpansionAt < 0 ||
        (c.parent != none && (!firm(c.parent) || c.parent == c.id)))
      throw std::runtime_error("Invalid company equity");
    Id parent = none;
    for (auto [a, units] : c.shares) {
      if (!acct(a) || units < 1 || units > capitalUnits ||
          (personFor(*this, a) == none && companyForAccount(a) == none))
        throw std::runtime_error("Invalid shareholder");
      total += units;
      if (units > capitalUnits / 2)
        parent = companyForAccount(a);
    }
    if (total != capitalUnits || c.parent != parent)
      throw std::runtime_error("Invalid share total or controlling parent");
    std::set<Id> visiting, done;
    std::function<void(Id)> walk = [&](Id id) {
      if (done.contains(id))
        return;
      if (visiting.size() >= 128 || !visiting.insert(id).second)
        throw std::runtime_error("Circular company equity");
      for (auto [a, u] : companies[id].shares) {
        Id next = companyForAccount(a);
        if (next != none)
          walk(next);
      }
      visiting.erase(id);
      done.insert(id);
    };
    walk(c.id);
  }
  std::map<std::pair<Id, Id>, std::int64_t> reserved;
  for (std::size_t i = 0; i < shareOffers.size(); ++i) {
    const auto &o = shareOffers[i];
    if (o.id != Id(i) || !firm(o.company) || !acct(o.seller) || o.units < 0 ||
        o.units > capitalUnits || o.unitPrice < 1 || o.unitPrice > 1000000000 ||
        (o.active && o.units == 0))
      throw std::runtime_error("Invalid share offer");
    if (o.active)
      reserved[{o.company, o.seller}] += o.units;
  }
  for (auto [key, units] : reserved)
    if (!companies[key.first].shares.contains(key.second) ||
        units > companies[key.first].shares.at(key.second))
      throw std::runtime_error("Oversold company shares");
  std::set<Id> pending, escrows;
  for (std::size_t i = 0; i < acquisitions.size(); ++i) {
    const auto &a = acquisitions[i];
    if (a.id != Id(i) || !firm(a.buyer) || !firm(a.target) || a.buyer == a.target ||
        !person(a.proposer) || !acct(a.escrow) || companyForAccount(a.escrow) != none ||
        personFor(*this, a.escrow) != none || !escrows.insert(a.escrow).second || a.price < 10000 ||
        a.price > 10000000000000LL || !std::isfinite(a.expires) || a.expires < 0 ||
        int(a.state) < 0 || int(a.state) > int(AcquisitionState::Cancelled) ||
        accounts[a.escrow].balance != (a.state == AcquisitionState::Pending ? a.price : 0) ||
        (a.state == AcquisitionState::Pending && !pending.insert(a.target).second))
      throw std::runtime_error("Invalid company acquisition");
    for (auto [a, v] : a.ballots)
      if (!acct(a))
        throw std::runtime_error("Invalid acquisition ballot");
  }
  for (auto [from, to] : equityHeirs)
    if (!person(from) || !person(to) || from == to)
      throw std::runtime_error("Invalid equity heir");
}
} // namespace mega
