#include "mega/world.hpp"
#include <algorithm>

namespace mega {
namespace {
bool willing(const Resident &r) {
  return r.alive && r.present && !r.leaving && r.age >= 18 && !r.surrendered &&
         r.activity != Activity::Fighting && r.activity != Activity::Sleeping &&
         r.activity != Activity::Jailed && r.activity != Activity::Restrained &&
         r.activity != Activity::Escorted && r.lift == none && r.ambulance == none &&
         r.carriedBy == none;
}
int meals(const World &w, Id who) {
  const auto &r = w.residents.at(who);
  return int(std::count_if(r.items.begin(), r.items.end(), [&](Id id) {
    const auto &i = w.items[id];
    return i.kind == ItemKind::Food && !i.consumed && i.durability > .2f && i.owner == r.account &&
           i.carrier == who && i.stolenFrom == none && i.reservedAt == none && !w.itemInUse(id);
  }));
}
bool conversation(const World &w, Id a, Id b) {
  return willing(w.residents.at(a)) && willing(w.residents.at(b)) && !w.looseBodies.contains(a) &&
         !w.looseBodies.contains(b) && w.near(a, w.position(b), 2.5f) &&
         w.lineOfSight(w.position(a) + Vec3{0, 1.4f, 0}, w.position(b) + Vec3{0, 1.4f, 0});
}
} // namespace
Id World::householdOf(Id actor) const {
  auto it = householdMembership.find(actor);
  return it == householdMembership.end() ? none : it->second;
}
Id World::createHousehold(Id actor) {
  if (!willing(residents.at(actor)) || householdOf(actor) != none || looseBodies.contains(actor))
    throw std::runtime_error("household.error.membership");
  Household h;
  h.id = Id(households.size());
  h.founder = actor;
  h.members.insert(actor);
  households.push_back(h);
  householdMembership[actor] = h.id;
  log("household.created", actor, h.id);
  return h.id;
}
void World::inviteHousehold(Id actor, Id other) {
  Id group = householdOf(actor);
  if (actor == other || group == none || householdOf(other) != none ||
      !conversation(*this, actor, other) || trust(actor, other) < .5f)
    throw std::runtime_error("household.error.invite");
  auto &h = households.at(group);
  if (h.members.size() + h.invitations.size() >= 6 && !h.invitations.contains(other))
    throw std::runtime_error("household.error.full");
  h.invitations[other] = time + 86400;
  log("household.invited", actor, other);
}
void World::acceptHousehold(Id actor, Id group) {
  auto &h = households.at(group);
  auto invite = h.invitations.find(actor);
  if (householdOf(actor) != none || invite == h.invitations.end() || invite->second < time ||
      h.members.empty() || h.members.size() >= 6 || !willing(residents.at(actor)))
    throw std::runtime_error("household.error.membership");
  bool heard = false;
  for (Id member : h.members)
    if (conversation(*this, actor, member) && trust(actor, member) >= .5f)
      heard = true;
  if (!heard)
    throw std::runtime_error("household.error.invite");
  h.members.insert(actor);
  h.invitations.erase(actor);
  householdMembership[actor] = group;
  for (auto &other : households)
    other.invitations.erase(actor);
  log("household.joined", actor, group);
}
void World::leaveHousehold(Id actor) {
  Id group = householdOf(actor);
  if (group == none)
    return;
  auto &h = households[group];
  h.members.erase(actor);
  householdMembership.erase(actor);
  if (h.founder == actor)
    h.founder = h.members.empty() ? none : *h.members.begin();
  if (h.members.empty())
    h.invitations.clear();
  auto &r = residents.at(actor);
  if (r.task == TaskKind::HouseholdMeet) {
    r.pos = position(actor);
    r.route.clear();
    ++r.routeVersion;
    r.task = TaskKind::None;
    r.target = none;
    if (r.alive && r.present)
      r.activity = Activity::Idle;
  }
  if (residentPlans.contains(actor))
    residentPlans[actor].helpTarget = none;
  log("household.left", actor, group);
}
void World::rebuildHouseholds() {
  householdMembership.clear();
  for (const auto &h : households)
    for (Id person : h.members)
      if (!householdMembership.emplace(person, h.id).second)
        throw std::runtime_error("Duplicate household membership");
}
void World::householdTick() {
  std::vector<Id> departed;
  for (const auto &[person, group] : householdMembership) {
    const auto &r = residents.at(person);
    const auto &h = households.at(group);
    bool hostile = person != h.founder && trust(person, h.founder) < -.2f;
    if (!r.alive || !r.present || r.leaving || (r.automatic && hostile))
      departed.push_back(person);
  }
  for (Id person : departed)
    leaveHousehold(person);
  for (auto &h : households)
    std::erase_if(h.invitations, [&](const auto &invite) {
      const auto &r = residents.at(invite.first);
      return invite.second < time || !r.alive || !r.present || r.leaving ||
             householdOf(r.id) != none;
    });
}
void World::householdSocial(Id a, Id b) {
  if (!residents[a].automatic || !residents[b].automatic || !conversation(*this, a, b) ||
      residents[a].aggression > .6f || residents[b].aggression > .6f || trust(a, b) < .65f)
    return;
  auto key = (std::uint64_t(std::min(a, b)) << 32) | std::uint32_t(std::max(a, b));
  if (relationships.at(key).conversations < 8)
    return;
  Id ga = householdOf(a), gb = householdOf(b);
  if (ga != none && gb != none)
    return;
  if (ga == none && gb != none)
    std::swap(a, b);
  // Shared routines need reachable neighbors, rather than a remote invitation.
  if (residentHomes[a] == none || residentHomes[b] == none ||
      std::abs(housing.at(residentHomes[a]).floor - housing.at(residentHomes[b]).floor) > 4)
    return;
  try {
    if (householdOf(a) == none)
      createHousehold(a);
    inviteHousehold(a, b);
    acceptHousehold(b, householdOf(a));
  } catch (const std::runtime_error &) {
  }
}
int World::householdMealDeficit(Id actor) const {
  Id group = householdOf(actor);
  if (group == none)
    return 0;
  int need = 0, surplus = 0;
  const auto &h = households.at(group);
  for (Id member : h.members) {
    const auto &r = residents[member];
    if (!r.alive || !r.present || r.leaving)
      continue;
    int held = meals(*this, member);
    need += r.hunger > .35f && !held;
    surplus += std::max(0, held - 1);
  }
  for (const auto &o : orders)
    if (h.members.contains(o.buyer) && o.product == ItemKind::Food &&
        (o.state == OrderState::Waiting || o.state == OrderState::ToPickup ||
         o.state == OrderState::ToCustomer))
      ++surplus;
  return std::max(0, need - surplus);
}
bool World::householdDecision(Id actor) {
  auto &p = residentPlans[actor];
  auto &r = residents[actor];
  if (p.helpTarget != none) {
    finishHouseholdVisit(actor);
    return !r.route.empty();
  }
  Id group = householdOf(actor);
  if (group == none || time < p.nextHousehold || r.hunger > .55f || meals(*this, actor) < 2)
    return false;
  p.nextHousehold = time + 1800;
  Id best = none;
  float hunger = .55f;
  for (Id member : households[group].members) {
    const auto &other = residents[member];
    auto at = position(member);
    auto key =
        (std::uint64_t(std::min(actor, member)) << 32) | std::uint32_t(std::max(actor, member));
    auto bond = relationships.find(key);
    if (member != actor && willing(other) && !looseBodies.contains(member) &&
        other.hunger > hunger && meals(*this, member) == 0 && trust(actor, member) >= .35f &&
        bond != relationships.end() && time - bond->second.lastAid >= 21600 &&
        std::abs(position(actor).y - at.y) <= 12 &&
        std::hypot(position(actor).x - at.x, position(actor).z - at.z) <= 80) {
      best = member;
      hunger = other.hunger;
    }
  }
  if (best == none)
    return false;
  p.helpTarget = best;
  p.helpDeadline = time + 1800;
  p.helpRetries = 0;
  finishHouseholdVisit(actor);
  return !r.route.empty();
}
void World::finishHouseholdVisit(Id actor) {
  auto &p = residentPlans.at(actor);
  Id target = p.helpTarget, group = householdOf(actor);
  auto cancel = [&] {
    p.helpTarget = none;
    p.helpRetries = 0;
  };
  if (target == none || group == none || householdOf(target) != group ||
      !residents[actor].automatic || !willing(residents[actor]) || !willing(residents[target]) ||
      looseBodies.contains(actor) || looseBodies.contains(target) || time > p.helpDeadline ||
      meals(*this, actor) < 2 || meals(*this, target) > 0 || residents[target].hunger < .55f ||
      residents[actor].hunger > .65f) {
    cancel();
    return;
  }
  if (conversation(*this, actor, target)) {
    shareFood(actor, target);
    cancel();
    return;
  }
  if (p.helpRetries >= 3) {
    cancel();
    return;
  }
  ++p.helpRetries;
  try {
    travel(actor, position(target), TaskKind::HouseholdMeet, target);
    p.action = AutonomyAction::HouseholdErrand;
  } catch (const std::runtime_error &) {
    cancel();
  }
}
void World::validateHouseholds() const {
  std::map<Id, Id> membership;
  auto valid = [&](Id id) { return id >= 0 && std::size_t(id) < residents.size(); };
  for (std::size_t i = 0; i < households.size(); ++i) {
    const auto &h = households[i];
    if (h.id != Id(i) || h.members.size() + h.invitations.size() > 6 ||
        (h.members.empty() ? h.founder != none || !h.invitations.empty()
                           : !h.members.contains(h.founder)))
      throw std::runtime_error("Invalid household");
    for (Id member : h.members)
      if (!valid(member) || !membership.emplace(member, h.id).second)
        throw std::runtime_error("Invalid household member");
    for (auto [invite, expires] : h.invitations)
      if (!valid(invite) || h.members.contains(invite) || !std::isfinite(expires) || expires < 0)
        throw std::runtime_error("Invalid household invitation");
  }
  if (membership != householdMembership)
    throw std::runtime_error("Invalid household index");
}
} // namespace mega
