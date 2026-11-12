#include "mega/world.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
namespace mega {
namespace {
bool shareable(ItemKind k) {
  return k == ItemKind::Food || k == ItemKind::Medicine || k == ItemKind::Clothes;
}
bool awake(const World &w, Id id) {
  const auto &r = w.residents.at(id);
  return r.alive && r.present && !r.leaving && !r.surrendered && !w.inCustody(id) &&
         r.activity != Activity::Sleeping && r.activity != Activity::Fighting && r.lift == none &&
         r.vehicle == none && r.ambulance == none && r.carriedBy == none &&
         !w.looseBodies.contains(id);
}
bool free(const World &w, Id id) {
  const auto &r = w.residents.at(id);
  return awake(w, id) && r.activity == Activity::Idle && r.route.empty() && r.queuedAt == none &&
         r.ladder < 0 && r.carrying == none && r.bodyCarrying == none && r.medicalDuty == none &&
         !w.personalUses.contains(id);
}
bool pending(const Order &o) {
  return o.state == OrderState::Waiting || o.state == OrderState::ToPickup ||
         o.state == OrderState::ToCustomer;
}
int held(const World &w, Id id, ItemKind kind) {
  const auto &r = w.residents[id];
  int n = 0;
  for (Id item : r.items) {
    const auto &i = w.items[item];
    if (i.kind == kind && i.owner == r.account && i.carrier == id && !i.consumed &&
        i.durability > .2f && i.reservedAt == none && i.stolenFrom == none && !w.itemInUse(item) &&
        (kind != ItemKind::Clothes || i.cleanliness >= .6f))
      ++n;
  }
  return n;
}
bool needed(const World &w, Id id, ItemKind kind) {
  const auto &r = w.residents[id];
  if (!r.alive || !r.present || r.leaving || held(w, id, kind))
    return false;
  if (kind == ItemKind::Food && w.personalUses.contains(id)) {
    auto ingredient = w.personalUses.at(id).ingredient;
    if (ingredient != none && w.items[ingredient].kind == kind)
      return false;
  }
  if (r.activity == Activity::Using && r.queuedAt != none) {
    auto service = w.facilities[r.queuedAt].kind;
    if ((kind == ItemKind::Food &&
         (service == FacilityKind::Food || service == FacilityKind::Cafe)) ||
        (kind == ItemKind::Medicine && service == FacilityKind::Infirmary) ||
        (kind == ItemKind::Clothes && service == FacilityKind::Laundry))
      return false;
  }
  for (const auto &o : w.orders)
    if (o.buyer == id && o.product == kind && pending(o))
      return false;
  if (kind == ItemKind::Food)
    return r.hunger > .35f;
  if (kind == ItemKind::Medicine)
    return r.health < 80;
  if (kind == ItemKind::Clothes)
    return r.outfit == none || w.items[r.outfit].durability < .3f ||
           w.items[r.outfit].cleanliness < .35f;
  return false;
}
bool contact(const World &w, Id a, Id b) {
  return awake(w, a) && awake(w, b) && w.near(a, w.position(b), 2.5f) &&
         w.lineOfSight(w.position(a) + Vec3{0, 1.3f, 0}, w.position(b) + Vec3{0, 1.3f, 0});
}
bool local(const World &w, Id a, Vec3 p) {
  auto d = p - w.position(a);
  return std::abs(d.y) <= 36 && std::hypot(d.x, d.z) <= 140;
}
bool willing(const World &w, Id a, Id b) {
  if (!w.considerChoice(a, b, ChoiceKind::Cooperate).willing ||
      !w.considerChoice(b, a, ChoiceKind::Cooperate).willing)
    return false;
  const auto &r = w.residents[a];
  if (!r.automatic)
    return true; // The player's explicit volunteer action is consent.
  return r.personality.generosity * .65f + w.trust(a, b) * .7f - r.aggression * .3f >= .5f;
}
void reason(HouseholdSupply &s, const char *key, double retry) {
  s.reason = key;
  s.nextAttempt = retry;
}
} // namespace
bool World::householdSupplyActive(Id id) const {
  return id >= 0 && std::size_t(id) < householdSupplies.size() &&
         householdSupplies[id].state != SupplyState::Complete &&
         householdSupplies[id].state != SupplyState::Cancelled;
}
bool World::householdSupplyReserved(Id item) const { return householdItemClaims.contains(item); }
void World::rebuildHouseholdSupplies() {
  householdSupplyIndex.clear();
  householdItemClaims.clear();
  for (const auto &s : householdSupplies) {
    householdSupplyIndex[s.household].push_back(s.id);
    if (householdSupplyActive(s.id) && s.item != none &&
        !householdItemClaims.emplace(s.item, s.id).second)
      throw std::runtime_error("Invalid duplicate household reservation");
  }
}
void World::addHouseholdDependent(Id actor, Id child) {
  auto &a = residents.at(actor);
  auto &b = residents.at(child);
  Id group = householdOf(actor);
  if (group == none || a.age < 18 || child == actor || b.age >= 18 || !b.automatic ||
      householdOf(child) != none ||
      households[group].members.size() + households[group].invitations.size() >= 6 ||
      !contact(*this, actor, child) || trust(actor, child) < .5f || trust(child, actor) < .5f ||
      !willing(*this, actor, child))
    throw std::runtime_error("coord.dependent_error");
  auto &h = households[group];
  h.members.insert(child);
  h.dependents[child] = actor;
  householdMembership[child] = group;
  for (auto &other : households)
    other.invitations.erase(child);
  log("coord.dependent_added", actor, child);
}
Id World::requestHouseholdSupply(Id actor, ItemKind kind) {
  Id group = householdOf(actor);
  if (group == none || !awake(*this, actor) || !shareable(kind) || !needed(*this, actor, kind))
    throw std::runtime_error("coord.request_error");
  auto requests = householdSupplyIndex.find(group);
  if (requests != householdSupplyIndex.end())
    for (Id id : requests->second) {
      const auto &s = householdSupplies[id];
      if (s.recipient == actor && s.kind == kind && householdSupplyActive(id))
        return id;
    }
  if (householdSupplies.size() >= 1000000)
    throw std::runtime_error("coord.limit");
  HouseholdSupply s;
  s.id = Id(householdSupplies.size());
  s.household = group;
  s.recipient = actor;
  s.kind = kind;
  s.where = position(actor);
  s.created = time;
  s.expires = time + 43200;
  householdSupplies.push_back(s);
  householdSupplyIndex[group].push_back(s.id);
  log("coord.posted", actor);
  return s.id;
}
void World::volunteerHouseholdSupply(Id actor, Id request, Money budget) {
  auto &s = householdSupplies.at(request);
  auto &r = residents.at(actor);
  if (s.state != SupplyState::Requested || householdOf(actor) != s.household ||
      householdOf(s.recipient) != s.household || actor == s.recipient || r.age < 18 ||
      !free(*this, actor) || !householdSupplyVisible(actor, request) ||
      !needed(*this, s.recipient, s.kind) || !willing(*this, actor, s.recipient) || budget < 0 ||
      budget > 20000 ||
      (residentPlans.contains(actor) &&
       (residentPlans.at(actor).supply != none || residentPlans.at(actor).tradeItem != none ||
        residentPlans.at(actor).helpTarget != none)))
    throw std::runtime_error("coord.volunteer_error");
  Id item = none;
  int keep = s.kind == ItemKind::Food ? 1 : s.kind == ItemKind::Medicine && r.health < 85 ? 1 : 0;
  if (held(*this, actor, s.kind) > keep)
    for (Id id : r.items) {
      const auto &i = items[id];
      if (i.kind == s.kind && i.owner == r.account && i.carrier == actor && !i.consumed &&
          i.durability > .2f && i.stolenFrom == none && i.reservedAt == none && i.id != r.outfit &&
          !itemInUse(id) && (s.kind != ItemKind::Clothes || i.cleanliness >= .6f)) {
        item = id;
        break;
      }
    }
  if (item == none) {
    auto shelves = storageByHousehold.find(s.household);
    if (shelves != storageByHousehold.end())
      for (Id lease : shelves->second) {
        const auto &l = storageLeases[lease];
        if (!l.active || l.released || l.arrears || !local(*this, actor, storagePosition(lease)))
          continue;
        for (Id id : l.items) {
          const auto &i = items[id];
          if (i.kind == s.kind && storageItemAllowed(actor, id) && !i.consumed &&
              i.durability > .2f && i.stolenFrom == none && !itemInUse(id) &&
              (s.kind != ItemKind::Clothes || i.cleanliness >= .6f)) {
            item = id;
            break;
          }
        }
        if (item != none)
          break;
      }
  }
  if (item == none &&
      (budget < productPrice(s.kind) + 200 ||
       accounts[r.account].balance < householdReserve(actor) + productPrice(s.kind) + 200))
    throw std::runtime_error("coord.funds");
  s.buyer = actor;
  s.item = item;
  s.budget = budget;
  s.state = item == none ? SupplyState::Assigned : SupplyState::Ready;
  s.reason = item == none ? "coord.shopping" : "coord.reserved";
  residentPlans[actor].supply = s.id;
  if (item != none)
    householdItemClaims[item] = s.id;
  chooseSocial(actor, s.recipient, ChoiceKind::Cooperate);
  log("coord.accepted", actor, s.recipient);
}
void World::cancelHouseholdSupply(Id actor, Id request) {
  auto &s = householdSupplies.at(request);
  bool owner = s.item != none && items[s.item].owner == residents.at(actor).account;
  if (!householdSupplyActive(request) || (actor != s.recipient && actor != s.buyer && !owner))
    throw std::runtime_error("coord.cancel_error");
  if (s.item != none)
    householdItemClaims.erase(s.item);
  if (s.buyer != none) {
    auto &p = residentPlans[s.buyer];
    if (p.supply == s.id) {
      auto &r = residents[s.buyer];
      if (r.task == TaskKind::HouseholdMeet && r.target == s.recipient) {
        r.pos = position(r.id);
        r.route.clear();
        ++r.routeVersion;
        r.task = TaskKind::None;
        r.target = none;
        if (r.alive && r.present && r.activity == Activity::Walking)
          r.activity = Activity::Idle;
      }
      p.supply = none;
    }
    if (s.order != none && (orders[s.order].state == OrderState::Waiting ||
                            orders[s.order].state == OrderState::ToPickup))
      cancelOrder(s.order, s.buyer);
  }
  if (s.order != none && orders[s.order].state == OrderState::Cancelled)
    s.spent = std::max<Money>(0, s.spent - orders[s.order].price - orders[s.order].deliveryFee);
  s.state = SupplyState::Cancelled;
  s.reason = "coord.cancelled";
  log("coord.cancelled", actor, s.recipient);
}
void World::householdSupplyNeeds(Id actor) {
  const auto &r = residents.at(actor);
  Id group = householdOf(actor);
  if (group == none || !r.automatic || !awake(*this, actor))
    return;
  auto post = [&](Id id) {
    for (auto kind : {ItemKind::Food, ItemKind::Medicine, ItemKind::Clothes})
      if (needed(*this, id, kind))
        try {
          requestHouseholdSupply(id, kind);
        } catch (const std::runtime_error &) {
        }
  };
  auto plan = residentPlans.find(actor);
  if (plan != residentPlans.end() && householdSupplyActive(plan->second.supply) &&
      (r.hunger > .9f || r.thirst > .9f || r.health < 50))
    cancelHouseholdSupply(actor, plan->second.supply);
  post(actor);
  if (r.age >= 18 && r.personality.generosity > .6f && households[group].members.size() < 6)
    for (Id child : knownContacts(actor))
      if (residents[child].age < 18 && residents[child].automatic && householdOf(child) == none &&
          trust(actor, child) >= .65f && trust(child, actor) >= .65f &&
          contact(*this, actor, child)) {
        try {
          addHouseholdDependent(actor, child);
        } catch (const std::runtime_error &) {
        }
        if (households[group].members.size() == 6)
          break;
      }
  for (const auto &[child, carer] : households[group].dependents)
    if (carer == actor && contact(*this, actor, child))
      post(child);
}
void World::advanceHouseholdSupply(Id actor) {
  auto found = residentPlans.find(actor);
  if (found == residentPlans.end() || !householdSupplyActive(found->second.supply))
    return;
  auto &s = householdSupplies[found->second.supply];
  auto &r = residents[actor];
  if (s.buyer != actor || !free(*this, actor) || (r.automatic && time < s.nextAttempt))
    return;
  if (householdOf(actor) != s.household || householdOf(s.recipient) != s.household ||
      !needed(*this, s.recipient, s.kind) || time > s.expires ||
      !willing(*this, actor, s.recipient)) {
    cancelHouseholdSupply(actor, s.id);
    return;
  }
  auto &p = found->second;
  if (p.day != int(time / 86400)) {
    p.day = int(time / 86400);
    p.spentToday = 0;
  }
  if (s.order != none) {
    const auto &o = orders[s.order];
    if (pending(o)) {
      reason(s, "coord.delivery", time + 180);
      return;
    }
    if (o.state != OrderState::Delivered) {
      cancelHouseholdSupply(actor, s.id);
      return;
    }
    if (s.item == none) {
      s.item = o.item;
      householdItemClaims[s.item] = s.id;
    }
    s.state = SupplyState::Ready;
  }
  if (s.item != none) {
    auto &i = items[s.item];
    if (i.consumed || i.durability <= .2f || i.stolenFrom != none || i.kind != s.kind ||
        householdItemClaims.find(i.id) == householdItemClaims.end() ||
        householdItemClaims.at(i.id) != s.id) {
      cancelHouseholdSupply(actor, s.id);
      return;
    }
    if (i.storedAt != none) {
      Id lease = i.storedAt;
      if (!storageItemAllowed(actor, i.id)) {
        cancelHouseholdSupply(actor, s.id);
        return;
      }
      if (near(actor, storagePosition(lease), 2.2f)) {
        bool wasOpen = storageLeases[lease].open;
        try {
          setStorageOpen(actor, lease, true);
          withdrawStorage(actor, i.id);
          setStorageOpen(actor, lease, wasOpen);
        } catch (const std::runtime_error &) {
          reason(s, "coord.pantry", time + 300);
          return;
        }
      } else {
        try {
          travel(actor, storagePosition(lease), TaskKind::None, none);
          s.reason = "coord.pantry";
        } catch (const std::runtime_error &) {
          reason(s, "coord.pantry", time + 300);
        }
        return;
      }
    }
    if (i.owner != r.account || i.carrier != actor || i.reservedAt != none) {
      cancelHouseholdSupply(actor, s.id);
      return;
    }
    if (!contact(*this, actor, s.recipient)) {
      if (hasDevice(actor) && awake(*this, s.recipient) && hasDevice(s.recipient))
        s.where = position(s.recipient);
      if (!awake(*this, s.recipient) || !local(*this, actor, s.where) ||
          near(actor, s.where, 1.f)) {
        reason(s, "coord.contact", time + 300);
        return;
      }
      if (s.attempts >= 6) {
        cancelHouseholdSupply(actor, s.id);
        return;
      }
      ++s.attempts;
      try {
        travel(actor, s.where, TaskKind::HouseholdMeet, s.recipient);
        s.reason = "coord.meeting";
      } catch (const std::runtime_error &) {
        reason(s, "coord.contact", time + 300);
      }
      return;
    }
    if (!chooseSocial(s.recipient, actor, ChoiceKind::Cooperate).willing) {
      cancelHouseholdSupply(actor, s.id);
      return;
    }
    auto &recipient = residents[s.recipient];
    if (recipient.automatic && recipient.queuedAt != none) {
      auto kind = facilities[recipient.queuedAt].kind;
      if ((s.kind == ItemKind::Food &&
           (kind == FacilityKind::Food || kind == FacilityKind::Cafe)) ||
          (s.kind == ItemKind::Medicine && kind == FacilityKind::Infirmary) ||
          (s.kind == ItemKind::Clothes && kind == FacilityKind::Laundry))
        leaveQueue(recipient.id);
    }
    householdItemClaims.erase(i.id);
    std::erase(r.items, i.id);
    recipient.items.push_back(i.id);
    i.owner = recipient.account;
    i.carrier = recipient.id;
    i.pos = position(recipient.id);
    i.askingPrice = 0;
    i.seller = none;
    itemOffers.erase(i.id);
    s.state = SupplyState::Complete;
    s.reason = "coord.complete";
    p.supply = none;
    rememberEncounter(actor, recipient.id,
                      s.kind == ItemKind::Food ? EncounterKind::Meal : EncounterKind::Trade, .03f,
                      .08f);
    log("coord.handed_over", actor, recipient.id);
    // Basic feeding/medicine assistance is an actual handover then owned consumption.
    auto guardian = households[s.household].dependents.find(recipient.id);
    if (guardian != households[s.household].dependents.end() && guardian->second == actor &&
        recipient.age < 6) {
      if (s.kind == ItemKind::Food || s.kind == ItemKind::Medicine)
        consumeItem(recipient.id, i.id);
      else
        changeClothes(recipient.id, i.id);
    }
    return;
  }
  Money available = std::min({s.budget - s.spent, 20000 - p.spentToday,
                              accounts[r.account].balance - householdReserve(actor)});
  if (available < productPrice(s.kind) + 200) {
    reason(s, "coord.funds", time + 600);
    return;
  }
  if (!hasDevice(actor)) {
    Id terminal = serviceChoice(actor, FacilityKind::Terminal);
    if (terminal == none || !local(*this, actor, facilities[terminal].pos) ||
        available < productPrice(s.kind) + 200 + facilities[terminal].price * 5) {
      reason(s, "coord.connection", time + 600);
      return;
    }
    try {
      if (!near(actor, facilities[terminal].pos, 1.8f)) {
        travel(actor, accessible(facilities[terminal].pos + Vec3{0, 0, 1.6f}), TaskKind::None,
               none);
        s.reason = "coord.connection";
        return;
      }
      Money cost = facilities[terminal].price * 5;
      useTerminal(actor, terminal, 5);
      s.spent += cost;
      p.spentToday += cost;
      available -= cost;
    } catch (const std::runtime_error &) {
      reason(s, "coord.connection", time + 600);
      return;
    }
  }
  Id shop = none;
  Money quote = std::numeric_limits<Money>::max();
  int floor = int(std::round(position(actor).y / 3));
  for (auto row = floorFacilities.lower_bound(floor - 12);
       row != floorFacilities.end() && row->first <= floor + 12; ++row)
    for (Id fid : row->second) {
      const auto &f = facilities[fid];
      if (f.kind != FacilityKind::Shop || f.stock <= 0 || !operational(fid) ||
          !accessAllowed(actor, fid) || !local(*this, actor, f.pos))
        continue;
      int couriers = 0, backlog = 0;
      for (Id person : f.employees) {
        const auto &c = residents[person];
        if (c.role == Role::Courier && c.automatic && c.alive && c.present && !c.surrendered &&
            onShift(person))
          ++couriers;
      }
      for (const auto &o : orders)
        if (o.shop == fid && pending(o))
          ++backlog;
      Money price = productPrice(s.kind) + deliveryPrice(actor, fid);
      if (couriers && backlog < couriers * 4 && price <= available && price < quote) {
        shop = fid;
        quote = price;
      }
    }
  if (shop == none) {
    reason(s, "coord.stock", time + 600);
    return;
  }
  try {
    s.order = order(actor, shop, s.kind);
    s.item = orders[s.order].item;
    householdItemClaims[s.item] = s.id;
    s.spent += quote;
    p.spentToday += quote;
    s.state = SupplyState::Ordered;
    s.reason = "coord.delivery";
  } catch (const std::runtime_error &) {
    reason(s, "coord.stock", time + 600);
  }
}
bool World::householdSupplyDecision(Id actor) {
  auto &r = residents.at(actor);
  Id group = householdOf(actor);
  if (group == none || !r.automatic || r.age < 18 || !free(*this, actor) || onShift(actor) ||
      sleepTime(actor))
    return false;
  auto &p = residentPlans[actor];
  if (householdSupplyActive(p.supply)) {
    if (r.hunger > .9f || r.thirst > .9f || r.health < 50) {
      cancelHouseholdSupply(actor, p.supply);
      return false;
    }
    advanceHouseholdSupply(actor);
    return householdSupplyActive(p.supply) || !r.route.empty();
  }
  p.supply = none;
  if (r.hunger > .55f || r.thirst > .65f || r.health < 65 ||
      dailyPlan(actor).commitment - time < 1800)
    return false;
  auto requests = householdSupplyIndex.find(group);
  if (requests == householdSupplyIndex.end())
    return false;
  for (Id request : requests->second) {
    const auto &s = householdSupplies[request];
    if (s.state != SupplyState::Requested || s.recipient == actor || time > s.expires ||
        !householdSupplyVisible(actor, request) || !needed(*this, s.recipient, s.kind) ||
        !willing(*this, actor, s.recipient))
      continue;
    try {
      volunteerHouseholdSupply(actor, request);
      advanceHouseholdSupply(actor);
      return true;
    } catch (const std::runtime_error &) {
    }
  }
  return false;
}
void World::householdSupplyTick() {
  for (auto &h : households)
    std::erase_if(h.dependents,
                  [&](const auto &entry) { return residents[entry.first].age >= 18; });
  for (auto &s : householdSupplies)
    if (householdSupplyActive(s.id)) {
      bool gone = householdOf(s.recipient) != s.household || !residents[s.recipient].alive ||
                  !residents[s.recipient].present || residents[s.recipient].leaving;
      if (s.buyer != none)
        gone = gone || householdOf(s.buyer) != s.household || !residents[s.buyer].alive ||
               !residents[s.buyer].present || residents[s.buyer].leaving;
      if (s.buyer != none && s.item != none && items[s.item].storedAt != none &&
          !storageItemAllowed(s.buyer, s.item))
        gone = true;
      bool lost = s.order != none && (orders[s.order].state == OrderState::Cancelled ||
                                      orders[s.order].state == OrderState::Lost);
      if (gone || lost || time > s.expires || !needed(*this, s.recipient, s.kind)) {
        Id actor = s.buyer == none ? s.recipient : s.buyer;
        cancelHouseholdSupply(actor, s.id);
      }
    }
}
void World::validateHouseholdSupplies() const {
  if (householdSupplies.size() > 1000000)
    throw std::runtime_error("Invalid household request count");
  std::map<Id, std::vector<Id>> index;
  std::map<Id, Id> claims;
  std::set<std::tuple<Id, Id, ItemKind>> active;
  std::set<Id> buyers;
  auto ref = [](Id id, std::size_t n) { return id >= 0 && std::size_t(id) < n; };
  for (std::size_t id = 0; id < householdSupplies.size(); ++id) {
    const auto &s = householdSupplies[id];
    if (s.id != Id(id) || !ref(s.household, households.size()) ||
        !ref(s.recipient, residents.size()) ||
        (s.buyer != none && !ref(s.buyer, residents.size())) ||
        (s.item != none && !ref(s.item, items.size())) ||
        (s.order != none && (!ref(s.order, orders.size()) || orders[s.order].buyer != s.buyer ||
                             orders[s.order].product != s.kind)) ||
        !shareable(s.kind) || int(s.state) < 0 || int(s.state) > int(SupplyState::Cancelled) ||
        !std::isfinite(s.created) || s.created < 0 || s.created > time ||
        !std::isfinite(s.expires) || s.expires < s.created || !std::isfinite(s.nextAttempt) ||
        s.nextAttempt < 0 || s.budget < 0 || s.budget > 20000 || s.spent < 0 ||
        s.spent > s.budget || s.attempts < 0 || s.attempts > 6 ||
        (s.buyer == none && s.state != SupplyState::Requested && s.state != SupplyState::Cancelled))
      throw std::runtime_error("Invalid household supply request");
    index[s.household].push_back(s.id);
    if (householdSupplyActive(s.id)) {
      if (s.buyer != none && (!buyers.insert(s.buyer).second || !residentPlans.contains(s.buyer) ||
                              residentPlans.at(s.buyer).supply != s.id))
        throw std::runtime_error("Invalid household volunteer assignment");
      if (!active.emplace(s.household, s.recipient, s.kind).second)
        throw std::runtime_error("Duplicate household request");
      if (s.item != none && !claims.emplace(s.item, s.id).second)
        throw std::runtime_error("Duplicate household item claim");
    }
  }
  if (index != householdSupplyIndex || claims != householdItemClaims)
    throw std::runtime_error("Invalid household supply index");
  for (const auto &h : households)
    for (const auto &[child, carer] : h.dependents)
      if (!h.members.contains(child) || !h.members.contains(carer) || child == carer ||
          residents[child].age >= 18 || residents[carer].age < 18)
        throw std::runtime_error("Invalid household caregiver");
}
bool World::householdSupplyVisible(Id actor, Id request) const {
  const auto &s = householdSupplies.at(request);
  if (householdOf(actor) != s.household)
    return false;
  if (actor == s.recipient || actor == s.buyer ||
      (s.item != none && items[s.item].owner == residents.at(actor).account))
    return true;
  return contact(*this, actor, s.recipient) || (awake(*this, actor) && awake(*this, s.recipient) &&
                                                hasDevice(actor) && hasDevice(s.recipient));
}
} // namespace mega
