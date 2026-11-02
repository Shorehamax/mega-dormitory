#include "mega/world.hpp"
#include <algorithm>
#include <limits>

namespace mega {
namespace {
bool eligible(const World &w, Id id) {
  if (id < 0 || std::size_t(id) >= w.residents.size())
    return false;
  const auto &r = w.residents[id];
  return r.alive && r.present && r.age >= 18 && !r.leaving;
}
Id nominee(const World &w, const Estate &e) {
  auto chosen = w.personalHeirs.find(e.deceased);
  if (chosen != w.personalHeirs.end() && !e.declined.contains(chosen->second) &&
      eligible(w, chosen->second))
    return chosen->second;
  Id best = none;
  float confidence = -2;
  for (Id id : e.candidates)
    if (!e.declined.contains(id) && eligible(w, id)) {
      float score = w.trust(e.deceased, id);
      if (score > confidence) {
        best = id;
        confidence = score;
      }
    }
  return best;
}
void payDebts(World &w, Estate &e, Id account) {
  for (auto &debt : e.debts) {
    if (debt.creditor == account) {
      debt.remaining = 0;
      continue;
    }
    Money amount = std::min(debt.remaining, w.accounts[account].balance);
    if (amount > 0) {
      w.pay(account, debt.creditor, amount, "estate-rent-claim");
      debt.remaining -= amount;
    }
  }
}
} // namespace
void World::nominatePersonalHeir(Id actor, Id heir) {
  if (!eligible(*this, actor) || (heir != none && (!eligible(*this, heir) || heir == actor)))
    throw std::runtime_error("estate.error.nominee");
  if (heir == none)
    personalHeirs.erase(actor);
  else
    personalHeirs[actor] = heir;
}
void World::openEstate(Id who) {
  const auto &r = residents.at(who);
  if (r.alive || std::any_of(estates.begin(), estates.end(),
                             [&](const Estate &e) { return e.deceased == who; }))
    return;
  Estate e;
  e.id = Id(estates.size());
  e.deceased = who;
  e.openedAt = time;
  e.reviewAt = time + 86400;
  Id group = householdOf(who);
  if (group != none)
    for (Id id : households[group].members)
      if (id != who && eligible(*this, id))
        e.candidates.insert(id);
  for (const auto &[key, relation] : relationships) {
    (void)key;
    if (relation.trust < .6f)
      continue;
    Id other = relation.a == who ? relation.b : relation.b == who ? relation.a : none;
    if (eligible(*this, other))
      e.candidates.insert(other);
  }
  auto will = personalHeirs.find(who);
  if (will != personalHeirs.end())
    e.candidates.insert(will->second);
  for (auto &h : housing)
    if (h.tenant == who) {
      if (h.arrears > 0 && h.owner != r.account)
        e.debts.push_back({h.owner, h.arrears});
      h.arrears = 0;
      h.tenant = none;
      h.paidUntil = 0;
    }
  residentHomes[who] = none;
  e.beneficiary = nominee(*this, e);
  estates.push_back(e);
  log("estate.opened", who, e.beneficiary);
}
Money World::estateLiabilities(Id id) const {
  const auto &e = estates.at(id);
  Id account = residents[e.deceased].account;
  Money result = 0;
  for (const auto &d : e.debts)
    result += d.remaining;
  if (e.accepted)
    return result;
  for (const auto &l : storageLeases)
    if (l.holder == account)
      result += l.arrears;
  for (const auto &r : residents) {
    auto claim = r.routine.wageClaims.find(account);
    if (claim != r.routine.wageClaims.end())
      result += Money(std::ceil(claim->second));
    if (r.employer == account) {
      result += Money(std::ceil(r.routine.unpaidWages));
      if (r.routine.workSince >= 0)
        result += Money(std::ceil(std::max(0.0, time - r.routine.workSince) / 3600 * r.hourlyWage));
    }
  }
  return result;
}
Money World::estateValuation(Id id) const {
  const auto &e = estates.at(id);
  Id account = residents[e.deceased].account;
  Money result = accounts[account].balance;
  for (const auto &h : housing)
    if (h.owner == account && h.active)
      result += h.price;
  for (const auto &f : facilities)
    if (f.owner == account && !f.demolished)
      result += 300000 + Money(f.stations) * 100000;
  for (const auto &v : vehicles)
    if (v.owner == account)
      result += Money(v.originalPrice * v.condition);
  return result;
}
void World::declineEstate(Id actor, Id id) {
  auto &e = estates.at(id);
  if (e.accepted || actor != e.beneficiary || !eligible(*this, actor))
    throw std::runtime_error("estate.error.offer");
  e.declined.insert(actor);
  e.beneficiary = nominee(*this, e);
  log("estate.declined", actor, e.deceased);
}
void World::acceptEstate(Id actor, Id id) {
  auto &e = estates.at(id);
  if (e.accepted || time < e.reviewAt || e.beneficiary != actor || !eligible(*this, actor))
    throw std::runtime_error("estate.error.offer");
  const auto &a = residents[actor];
  if (a.activity == Activity::Jailed || a.activity == Activity::Restrained ||
      a.activity == Activity::Escorted || a.activity == Activity::Fighting)
    throw std::runtime_error("estate.error.offer");
  Id source = residents[e.deceased].account, destination = a.account;
  e.inheritedDebt = estateLiabilities(id);
  // Crystallize wages already worked before changing an employer's ownership.
  for (auto &r : residents)
    if (r.employer == source) {
      clockOut(r.id);
      r.employer = destination;
    }
  for (auto &r : residents) {
    auto claim = r.routine.wageClaims.find(source);
    if (claim != r.routine.wageClaims.end()) {
      double amount = claim->second;
      r.routine.wageClaims.erase(claim);
      if (amount > 0 && r.account != destination && r.account != source)
        r.routine.wageClaims[destination] += amount;
    }
  }
  // Receivables remain genuine claims against the same employers.
  auto &dead = residents[e.deceased];
  dead.outfit = none;
  for (auto [employer, amount] : dead.routine.wageClaims)
    if (employer != source && employer != destination)
      residents[actor].routine.wageClaims[employer] += amount;
  dead.routine.wageClaims.clear();
  payDebts(*this, e, source);
  std::set<Id> personalFacilities;
  for (auto &f : facilities)
    if (f.owner == source) {
      personalFacilities.insert(f.id);
      f.owner = destination;
      if (f.manager == e.deceased)
        f.manager = actor;
      f.askingPrice = 0;
    }
  for (auto &h : housing)
    if (h.owner == source) {
      h.owner = destination;
      h.listed = false;
    }
  for (auto &v : vehicles)
    if (v.owner == source) {
      v.owner = destination;
      if (v.dealer == none)
        v.askingPrice = 0;
    }
  for (auto &i : items) {
    if (i.stolenFrom == source)
      i.stolenFrom = destination;
    if (i.owner != source)
      continue;
    if (!i.consumed)
      e.belongings.insert(i.id);
    i.owner = destination;
    i.seller = none;
    i.askingPrice = 0;
    i.sharedWith = none;
    std::erase(dead.items, i.id);
    // Ownership changes; carrier, world position and evidence custody do not.
  }
  for (auto &receipt : evidenceReceipts)
    if (receipt.owner == source)
      receipt.owner = destination;
  for (auto &lease : storageLeases)
    if (lease.holder == source) {
      lease.holder = destination;
      lease.custodian = actor;
      lease.household = none;
      lease.active = false;
      lease.open = false;
      for (Id item : lease.items)
        items[item].sharedWith = none;
    }
  for (auto &lease : workspaceLeases)
    if (lease.landlord == source)
      lease.landlord = destination;
  for (auto &p : projects) {
    if (p.state == ProjectState::AwaitingConsent && p.affected.erase(e.deceased)) {
      if (personalFacilities.contains(p.facility))
        p.affected.insert(actor);
      p.consented.erase(e.deceased);
      p.compensation = Money(p.affected.size()) * p.offer;
    }
    if (p.owner == e.deceased && personalFacilities.contains(p.facility))
      p.owner = actor;
    if (p.contractor == source)
      p.contractor = destination;
  }
  for (auto &p : housingWorks) {
    if (p.payer == source) {
      p.payer = destination;
      if (p.actor == e.deceased)
        p.actor = actor;
    }
    if (p.contractor == source)
      p.contractor = destination;
  }
  for (auto &p : structuralWorks)
    if (p.payer == source) {
      p.payer = destination;
      if (p.actor == e.deceased)
        p.actor = actor;
    }
  for (auto &wall : interiorWalls)
    if (wall.owner == source)
      wall.owner = destination;
  for (auto &p : expansions) {
    if (p.owner == source)
      p.owner = destination;
    if (p.contractor == source)
      p.contractor = destination;
  }
  for (auto &p : vehicleJobs)
    if (p.payer == source)
      p.payer = destination;
  for (auto &order : orders)
    if (order.buyer == e.deceased && order.state != OrderState::Delivered &&
        order.state != OrderState::Lost) {
      if (residentPlans.contains(e.deceased) && residentPlans[e.deceased].order == order.id)
        residentPlans[e.deceased].order = none;
      if (!residentPlans.contains(actor) || residentPlans[actor].order == none) {
        residentPlans[actor].order = order.id;
        residentPlans[actor].wanted = order.product;
      }
      order.buyer = actor;
    }
  Money amount = accounts[source].balance;
  if (amount > 0)
    pay(source, destination, amount, "estate-distribution");
  e.distributed += amount;
  e.accepted = true;
  e.acceptedAt = time;
  payDebts(*this, e, destination);
  rebuildStorage();
  log("estate.accepted", actor, e.deceased);
}
void World::estateTick() {
  for (auto &e : estates) {
    Id source = residents[e.deceased].account;
    // Debts and late refunds continue after the initial title transfer.
    if (e.accepted) {
      payDebts(*this, e, source);
      Money amount = accounts[source].balance;
      Id destination = residents[e.beneficiary].account;
      if (amount > 0) {
        pay(source, residents[e.beneficiary].present ? destination : 0, amount,
            "estate-late-distribution");
        e.distributed += amount;
      }
      if (residents[e.beneficiary].present)
        payDebts(*this, e, destination);
      continue;
    }
    payDebts(*this, e, source);
    if (time < e.reviewAt)
      continue;
    if (!eligible(*this, e.beneficiary))
      e.beneficiary = nominee(*this, e);
    if (e.beneficiary == none || e.beneficiary == player || !residents[e.beneficiary].automatic)
      continue;
    if (estateValuation(e.id) < estateLiabilities(e.id)) {
      declineEstate(e.beneficiary, e.id);
      continue;
    }
    try {
      acceptEstate(e.beneficiary, e.id);
    } catch (const std::runtime_error &) {
    }
  }
}
Vec3 World::belongingApproach(Id actor, Id id) const {
  Vec3 target = items.at(id).pos, start = position(actor), direction = start - target;
  direction.y = 0;
  if (length(direction) < .01f)
    direction = {1, 0, 0};
  else
    direction = normalized(direction);
  float angle = std::atan2(direction.z, direction.x);
  for (int step : {0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6}) {
    float a = angle + step * .5235987756f;
    Vec3 goal = target + Vec3{std::cos(a) * 1.2f, 0, std::sin(a) * 1.2f};
    goal.y = std::round(target.y / 3) * 3;
    if (!clearPosition(goal, .31f))
      continue;
    if (!lineOfSight(goal + Vec3{0, 1.4f, 0}, target + Vec3{0, .3f, 0}) &&
        !lineOfSight(goal + Vec3{0, .35f, 0}, target + Vec3{0, .15f, 0}))
      continue;
    if (std::abs(start.y - goal.y) < .1f)
      try {
        floorPath(start, goal);
      } catch (const std::runtime_error &) {
        continue;
      }
    return goal;
  }
  throw std::runtime_error("estate.error.collect");
}
void World::collectBelonging(Id actor, Id id) {
  auto &r = residents.at(actor);
  auto &item = items.at(id);
  if (!eligible(*this, actor) || r.surrendered || r.activity == Activity::Sleeping ||
      r.activity == Activity::Fighting || r.activity == Activity::Jailed ||
      r.activity == Activity::Restrained || r.activity == Activity::Escorted || r.vehicle != none ||
      r.lift != none || r.ambulance != none || r.carriedBy != none || looseBodies.contains(actor) ||
      item.owner != r.account || item.carrier != none || item.consumed || item.reservedAt != none ||
      item.storedAt != none || item.seller != none || item.kind == ItemKind::Materials ||
      item.kind == ItemKind::Waste || !near(actor, item.pos, 1.8f) ||
      std::abs(position(actor).y - item.pos.y) > .5f ||
      (!lineOfSight(position(actor) + Vec3{0, 1.4f, 0}, item.pos + Vec3{0, .3f, 0}) &&
       !lineOfSight(position(actor) + Vec3{0, .35f, 0}, item.pos + Vec3{0, .15f, 0})) ||
      (item.order != none && orders[item.order].state != OrderState::Delivered &&
       orders[item.order].state != OrderState::Lost))
    throw std::runtime_error("estate.error.collect");
  item.carrier = actor;
  item.pos = position(actor);
  if (std::find(r.items.begin(), r.items.end(), id) == r.items.end())
    r.items.push_back(id);
  log("estate.collected", actor, id);
}
bool World::estateDecision(Id actor) {
  auto &r = residents.at(actor);
  if (!r.automatic || !eligible(*this, actor) || r.hunger > .7f || r.thirst > .65f ||
      r.health < 65 || r.energy < .3f)
    return false;
  if (residentPlans.contains(actor) && time < residentPlans[actor].nextEstate)
    return false;
  Id best = none;
  float distance = 1e9f;
  for (const auto &e : estates)
    if (e.accepted && e.beneficiary == actor)
      for (Id id : e.belongings) {
        const auto &item = items[id];
        if (item.owner != r.account || item.consumed || item.carrier != none ||
            item.storedAt != none || item.reservedAt != none || item.seller != none ||
            item.kind == ItemKind::Materials || item.kind == ItemKind::Waste)
          continue;
        Vec3 delta = item.pos - position(actor);
        if (std::abs(delta.y) > 36 || std::hypot(delta.x, delta.z) > 140)
          continue;
        float d = length(delta);
        if (d < distance) {
          best = id;
          distance = d;
        }
      }
  if (best == none)
    return false;
  residentPlans[actor].nextEstate = time + 1800;
  try {
    if (near(actor, items[best].pos, 1.8f))
      collectBelonging(actor, best);
    else
      travel(actor, belongingApproach(actor, best), TaskKind::BelongingCollect, best);
    return true;
  } catch (const std::runtime_error &) {
    return false;
  }
}
void World::validateEstates() const {
  auto person = [&](Id id) { return id >= 0 && std::size_t(id) < residents.size(); };
  std::set<Id> people;
  for (auto [actor, heir] : personalHeirs)
    if (!person(actor) || !person(heir) || actor == heir)
      throw std::runtime_error("Invalid personal heir");
  for (std::size_t i = 0; i < estates.size(); ++i) {
    const auto &e = estates[i];
    if (e.id != Id(i) || !person(e.deceased) || residents[e.deceased].alive ||
        !people.insert(e.deceased).second ||
        (e.beneficiary != none && (!person(e.beneficiary) || e.beneficiary == e.deceased)) ||
        !std::isfinite(e.openedAt) || !std::isfinite(e.reviewAt) || !std::isfinite(e.acceptedAt) ||
        e.openedAt < 0 || e.openedAt > time || e.reviewAt != e.openedAt + 86400 ||
        e.acceptedAt > time ||
        (e.accepted && (e.acceptedAt < e.reviewAt || e.beneficiary == none)) ||
        (!e.accepted && e.acceptedAt != -1) || e.distributed < 0 || e.inheritedDebt < 0 ||
        (!e.accepted && (e.distributed != 0 || e.inheritedDebt != 0)) ||
        (e.accepted && e.declined.contains(e.beneficiary)))
      throw std::runtime_error("Invalid personal estate");
    for (Id item : e.belongings)
      if (item < 0 || std::size_t(item) >= items.size())
        throw std::runtime_error("Invalid inherited belonging");
    for (Id id : e.candidates)
      if (!person(id) || id == e.deceased)
        throw std::runtime_error("Invalid estate candidate");
    for (Id id : e.declined)
      if (!person(id) || id == e.deceased)
        throw std::runtime_error("Invalid declined inheritance");
    for (const auto &debt : e.debts)
      if (debt.creditor <= 0 || std::size_t(debt.creditor) >= accounts.size() ||
          debt.remaining < 0 || debt.remaining > 1000000000000000LL)
        throw std::runtime_error("Invalid estate debt");
  }
}
} // namespace mega
