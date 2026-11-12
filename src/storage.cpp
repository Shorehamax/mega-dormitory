#include "mega/world.hpp"
#include <algorithm>

namespace mega {
namespace {
bool freeActor(const World &w, Id who) {
  const auto &r = w.residents.at(who);
  return r.alive && r.present && !r.surrendered && r.activity != Activity::Sleeping &&
         r.activity != Activity::Fighting && r.activity != Activity::Jailed &&
         r.activity != Activity::Restrained && r.activity != Activity::Escorted && r.lift == none &&
         r.ambulance == none && r.carriedBy == none && r.vehicle == none &&
         !w.looseBodies.contains(who);
}
bool reachable(const World &w, Id who, Id lease) {
  auto at = w.storagePosition(lease);
  return freeActor(w, who) && w.near(who, at, 2.2f) && std::abs(w.position(who).y - at.y) < .2f &&
         w.lineOfSight(w.position(who) + Vec3{0, 1.6f, 0}, at + Vec3{0, 1.3f, -.3f});
}
bool shareable(ItemKind kind) {
  return kind == ItemKind::Food || kind == ItemKind::Medicine || kind == ItemKind::Clothes;
}
bool local(const World &w, Id who, Id lease) {
  auto d = w.storagePosition(lease) - w.position(who);
  return std::abs(d.y) <= 36 && std::hypot(d.x, d.z) <= 140;
}
int carried(const World &w, Id actor, ItemKind kind) {
  int count = 0;
  for (Id id : w.residents[actor].items) {
    const auto &i = w.items[id];
    if (i.owner == w.residents[actor].account && i.carrier == actor && i.kind == kind &&
        !i.consumed && i.durability > .2f && i.reservedAt == none && !w.itemInUse(id))
      ++count;
  }
  return count;
}
} // namespace
void World::rebuildStorage() {
  storageByAccount.clear();
  storageByHousehold.clear();
  storageByItemOwner.clear();
  for (const auto &l : storageLeases) {
    storageByAccount[l.holder].insert(l.id);
    if (!l.released && l.household != none)
      storageByHousehold[l.household].insert(l.id);
  }
  for (const auto &i : items)
    if (i.storedAt != none)
      storageByItemOwner[i.owner].insert(i.id);
}
Vec3 World::storagePosition(Id lease) const {
  const auto &l = storageLeases.at(lease);
  return servicePosition(l.facility, l.slot);
}
Vec3 World::storedItemPosition(Id lease, int slot) const {
  return storagePosition(lease) + Vec3{(slot % 3 - 1) * .23f, .18f + (slot / 3) * .23f, -.52f};
}
bool World::storagePermission(Id actor, Id lease) const {
  const auto &l = storageLeases.at(lease);
  const auto &r = residents.at(actor);
  if (l.holder == r.account)
    return true;
  if (l.household != none && householdOf(actor) == l.household &&
      householdOf(l.custodian) == l.household && !l.released)
    return true;
  // A departing housemate can still collect their own stored belongings.
  return std::any_of(l.items.begin(), l.items.end(),
                     [&](Id id) { return items[id].owner == r.account; });
}
bool World::storageItemAllowed(Id actor, Id item) const {
  const auto &i = items.at(item);
  if (i.storedAt == none)
    return false;
  const auto &l = storageLeases.at(i.storedAt);
  if (i.owner == residents.at(actor).account)
    return true;
  if (i.sharedWith == none || !l.active || l.released || i.sharedWith != l.household ||
      householdOf(actor) != l.household || householdOf(l.custodian) != l.household)
    return false;
  for (Id member : households[l.household].members)
    if (residents[member].alive && residents[member].present &&
        residents[member].account == i.owner)
      return true;
  return false;
}
bool World::storageFacilityBusy(Id facility) const {
  return std::any_of(storageLeases.begin(), storageLeases.end(),
                     [&](const StorageLease &l) { return l.facility == facility && !l.released; });
}
Id World::rentStorage(Id actor, Id facility, Id group) {
  auto &f = facilities.at(facility);
  const auto &r = residents.at(actor);
  if (!freeActor(*this, actor) || r.leaving || f.kind != FacilityKind::Locker ||
      !operational(facility) || !accessAllowed(actor, facility) ||
      (!near(actor, f.pos, 3) && !atServiceEntrance(actor, facility)) ||
      (group != none && householdOf(actor) != group) || f.price <= 0 ||
      accounts[r.account].balance < f.price)
    throw std::runtime_error("storage.error.rent");
  for (const auto &p : projects)
    if (p.facility == facility && p.state != ProjectState::Completed)
      throw std::runtime_error("structure.error.busy");
  int slot = 0;
  while (slot < f.stations && std::any_of(
                                  storageLeases.begin(), storageLeases.end(),
                                  [&](const StorageLease &l) {
                                    return l.facility == facility && l.slot == slot && !l.released;
                                  }))
    ++slot;
  if (slot == f.stations)
    throw std::runtime_error("storage.error.full");
  StorageLease l;
  l.id = Id(storageLeases.size());
  l.facility = facility;
  l.slot = slot;
  l.holder = r.account;
  l.custodian = actor;
  l.household = group;
  l.dailyRent = f.price;
  l.paidUntil = time + 86400;
  pay(r.account, operatingAccount(facility), l.dailyRent, "locker-day-rent");
  storageLeases.push_back(l);
  storageByAccount[l.holder].insert(l.id);
  if (group != none)
    storageByHousehold[group].insert(l.id);
  log("storage.rented", actor, l.id);
  return l.id;
}
void World::setStorageOpen(Id actor, Id lease, bool open) {
  auto &l = storageLeases.at(lease);
  bool legalSearch =
      residents.at(actor).role == Role::Warden && hasWarrant(l.custodian) &&
      (residents[l.custodian].surrendered || residents[l.custodian].activity == Activity::Jailed);
  if (l.released || !reachable(*this, actor, lease) ||
      (!storagePermission(actor, lease) && !legalSearch))
    throw std::runtime_error("storage.error.access");
  l.open = open;
}
void World::depositStorage(Id actor, Id lease, Id item, bool share) {
  auto &l = storageLeases.at(lease);
  auto &i = items.at(item);
  auto &r = residents.at(actor);
  const auto &f = facilities[l.facility];
  if (r.leaving || !l.active || l.released || !l.open || l.arrears > 0 || l.items.size() >= 12 ||
      !reachable(*this, actor, lease) || !storagePermission(actor, lease) ||
      activeFires.contains(f.id) || f.demolished || i.owner != r.account || i.carrier != actor ||
      i.consumed || i.storedAt != none || i.reservedAt != none || i.seller != none ||
      (i.order != none && orders.at(i.order).state != OrderState::Delivered) || itemInUse(item) ||
      r.outfit == item || r.carrying == item || i.kind == ItemKind::Waste ||
      i.kind == ItemKind::Materials ||
      (share && (i.stolenFrom != none || l.household == none || householdOf(actor) != l.household ||
                 !shareable(i.kind))))
    throw std::runtime_error("storage.error.deposit");
  std::erase(r.items, item);
  i.carrier = none;
  i.storedAt = lease;
  i.sharedWith = share ? l.household : none;
  int slot = 0;
  while (std::any_of(l.items.begin(), l.items.end(),
                     [&](Id id) { return items[id].storageSlot == slot; }))
    ++slot;
  i.storageSlot = slot;
  i.pos = storedItemPosition(lease, slot);
  l.items.push_back(item);
  storageByItemOwner[i.owner].insert(item);
  log(share ? "storage.shared" : "storage.deposited", actor, item);
}
void World::withdrawStorage(Id actor, Id item, bool steal) {
  auto &i = items.at(item);
  if (i.storedAt == none)
    throw std::runtime_error("storage.error.item");
  auto &l = storageLeases.at(i.storedAt);
  auto &r = residents.at(actor);
  if (l.released || !l.open || !reachable(*this, actor, l.id) || i.consumed || i.carrier != none ||
      i.reservedAt != none)
    throw std::runtime_error("storage.error.access");
  auto claim = householdItemClaims.find(item);
  bool reserved =
      claim != householdItemClaims.end() && householdSupplies[claim->second].buyer != actor;
  if (reserved && (!steal || i.owner == r.account))
    throw std::runtime_error("coord.reserved");
  bool permitted = storageItemAllowed(actor, item) && !reserved;
  if (!permitted && !steal)
    throw std::runtime_error("storage.error.private");
  Id victim = none;
  if (!permitted) {
    for (const auto &owner : residents)
      if (owner.account == i.owner) {
        victim = owner.id;
        break;
      }
    if (victim == none)
      throw std::runtime_error("storage.error.private");
    Id crime = report(CrimeKind::Theft, actor, victim, false, false);
    auto &c = incidents[crime];
    c.pos = storagePosition(l.id);
    c.item = item;
    c.remoteProperty = true;
    observeCrime(crime);
    if (i.stolenFrom == none)
      i.stolenFrom = i.owner;
  }
  storageByItemOwner[i.owner].erase(item);
  std::erase(l.items, item);
  i.storedAt = i.sharedWith = none;
  i.storageSlot = -1;
  i.owner = r.account;
  i.carrier = actor;
  i.pos = position(actor);
  r.items.push_back(item);
  log(permitted ? "storage.collected" : "crime.stolen", actor, permitted ? item : victim);
  bool noticed =
      victim != none && residents[victim].alive && residents[victim].present &&
      residents[victim].activity != Activity::Sleeping && near(victim, position(actor), 4) &&
      lineOfSight(position(victim) + Vec3{0, 1.5f, 0}, position(actor) + Vec3{0, 1.5f, 0});
  if (victim != none)
    rememberEncounter(actor, victim, EncounterKind::Theft, -.025f, -.65f, noticed);
  if (noticed) {
    auto &owner = residents[victim];
    owner.happiness = std::max(0.f, owner.happiness - .2f);
    if (!owner.surrendered && owner.aggression > .55f)
      try {
        attack(victim, actor);
      } catch (const std::runtime_error &) {
      }
  }
}
void World::settleStorageLease(Id lease) {
  auto &l = storageLeases.at(lease);
  if (l.released || !l.active || time < l.paidUntil)
    return;
  Money days = 1 + Money((time - l.paidUntil) / 86400);
  Money owed = l.arrears + days * l.dailyRent;
  Money amount = std::min(accounts[l.holder].balance, owed);
  pay(l.holder, operatingAccount(l.facility), amount, "locker-recurring-rent");
  l.arrears = owed - amount;
  l.paidUntil += days * 86400;
  if (l.arrears >= l.dailyRent * 4) {
    l.active = false;
    l.open = false;
    log("storage.delinquent", l.custodian, l.id);
  }
}
void World::payStorageArrears(Id actor, Id lease) {
  auto &l = storageLeases.at(lease);
  if (!freeActor(*this, actor) || residents[actor].account != l.holder ||
      (!hasDevice(actor) && !reachable(*this, actor, lease)))
    throw std::runtime_error("storage.error.payment");
  Money amount = l.arrears + (!l.active && !l.released ? l.dailyRent : 0);
  if (amount <= 0 || accounts[l.holder].balance < amount)
    throw std::runtime_error("storage.error.payment");
  pay(l.holder, operatingAccount(l.facility), amount, "locker-debt-payment");
  l.arrears = 0;
  if (!l.released && !l.active) {
    l.active = true;
    l.paidUntil = time + 86400;
  }
}
void World::releaseStorage(Id actor, Id lease) {
  auto &l = storageLeases.at(lease);
  if (!freeActor(*this, actor) || l.released || l.holder != residents[actor].account ||
      !l.items.empty() || (!hasDevice(actor) && !reachable(*this, actor, lease)))
    throw std::runtime_error("storage.error.release");
  l.released = true;
  l.open = l.active = false;
  if (l.household != none)
    storageByHousehold[l.household].erase(lease);
  log("storage.released", actor, lease);
}
void World::storageTick() {
  for (Id id = 0; id < Id(storageLeases.size()); ++id) {
    auto &l = storageLeases[id];
    if (!residents[l.custodian].alive || !residents[l.custodian].present) {
      l.active = false;
      l.open = false;
      if (l.items.empty() && !l.released) {
        l.released = true;
        if (l.household != none)
          storageByHousehold[l.household].erase(id);
      }
    } else
      settleStorageLease(id);
  }
}
int World::storedPossessions(Id actor, ItemKind kind) const {
  int count = 0;
  auto found = storageByItemOwner.find(residents.at(actor).account);
  if (found != storageByItemOwner.end())
    for (Id id : found->second) {
      const auto &i = items[id];
      if (local(*this, actor, i.storedAt) && i.kind == kind && !i.consumed && i.durability > .2f)
        ++count;
    }
  return count;
}
int World::sharedStoredMeals(Id group, Id actor) const {
  auto found = storageByHousehold.find(group);
  if (found == storageByHousehold.end())
    return 0;
  int count = 0;
  for (Id id : found->second) {
    const auto &l = storageLeases[id];
    if (!l.active || l.released || householdOf(l.custodian) != group ||
        (actor != none && !local(*this, actor, id)))
      continue;
    for (Id item : l.items) {
      const auto &i = items[item];
      if (i.kind == ItemKind::Food && !i.consumed && i.durability > .2f && i.sharedWith == group &&
          storageItemAllowed(l.custodian, item))
        ++count;
    }
  }
  return count;
}
bool World::storageDecision(Id actor, bool urgent) {
  auto &r = residents.at(actor);
  if (!r.automatic || !freeActor(*this, actor) || !r.route.empty() || r.queuedAt != none ||
      r.carrying != none || r.bodyCarrying != none ||
      (r.activity != Activity::Idle && r.activity != Activity::Working))
    return false;
  if (storageLeases.empty() ||
      (householdOf(actor) == none && !storageByItemOwner.contains(r.account)))
    return false;
  auto &p = residentPlans[actor];
  ItemKind wanted;
  bool need = true;
  if (r.hunger > .55f && carried(*this, actor, ItemKind::Food) == 0)
    wanted = ItemKind::Food;
  else if (r.health < 65 && carried(*this, actor, ItemKind::Medicine) == 0)
    wanted = ItemKind::Medicine;
  else if (!urgent && !hasDevice(actor))
    wanted = ItemKind::Phone;
  else if (!urgent && r.outfit != none && items[r.outfit].cleanliness < .35f)
    wanted = ItemKind::Clothes;
  else
    need = false;
  if (need) {
    std::set<Id> candidates;
    auto possessions = storageByItemOwner.find(r.account);
    if (possessions != storageByItemOwner.end())
      for (Id id : possessions->second)
        candidates.insert(items[id].storedAt);
    auto mine = storageByAccount.find(r.account);
    if (mine != storageByAccount.end())
      candidates.insert(mine->second.begin(), mine->second.end());
    auto group = storageByHousehold.find(householdOf(actor));
    if (group != storageByHousehold.end())
      candidates.insert(group->second.begin(), group->second.end());
    for (Id lease : candidates) {
      if (!local(*this, actor, lease))
        continue;
      for (Id item : storageLeases[lease].items) {
        const auto &i = items[item];
        bool suitable =
            i.kind == wanted || (wanted == ItemKind::Phone && i.kind == ItemKind::Laptop);
        if (!suitable || i.consumed || i.durability <= .2f || householdSupplyReserved(item) ||
            (wanted == ItemKind::Clothes && i.cleanliness < .6f) ||
            !storageItemAllowed(actor, item))
          continue;
        p.storageItem = item;
        p.storageDeadline = time + 7200;
        try {
          if (reachable(*this, actor, lease)) {
            setStorageOpen(actor, lease, true);
            withdrawStorage(actor, item);
            setStorageOpen(actor, lease, false);
            p.storageItem = none;
            return true; // Use the collected inventory on the existing next decision tick.
          }
          travel(actor, storagePosition(lease), TaskKind::StorageRetrieve, lease);
          return true;
        } catch (const std::runtime_error &) {
          p.storageItem = none;
        }
      }
    }
  }
  if (urgent || r.activity != Activity::Idle || time < p.nextStorage)
    return false;
  p.nextStorage = time + 3600;
  Id group = householdOf(actor);
  if (group == none)
    return false;
  auto found = storageByHousehold.find(group);
  if (found != storageByHousehold.end())
    for (Id lease : found->second) {
      const auto &l = storageLeases[lease];
      if (!l.active || l.released || l.arrears || l.items.size() >= 12 ||
          !local(*this, actor, lease))
        continue;
      if (carried(*this, actor, ItemKind::Food) >= 2 &&
          sharedStoredMeals(group) < int(households[group].members.size()))
        for (Id item : r.items) {
          const auto &i = items[item];
          if (i.kind != ItemKind::Food || i.owner != r.account || i.carrier != actor ||
              i.consumed || i.durability <= .2f || i.reservedAt != none || i.seller != none ||
              itemInUse(item))
            continue;
          p.storageItem = item;
          p.storageDeadline = time + 7200;
          try {
            travel(actor, storagePosition(lease), TaskKind::StorageDeposit, lease);
            return true;
          } catch (const std::runtime_error &) {
            p.storageItem = none;
          }
        }
    }
  if (households[group].founder != actor ||
      accounts[r.account].balance < householdReserve(actor) + 30000)
    return false;
  if (found != storageByHousehold.end())
    for (Id lease : found->second)
      if (storageLeases[lease].active && !storageLeases[lease].released)
        return false;
  Id facility = nearest(actor, FacilityKind::Locker);
  if (facility != none && operational(facility) && accessAllowed(actor, facility) &&
      std::abs(position(actor).y - facilities[facility].pos.y) <= 24 &&
      length(position(actor) - facilities[facility].pos) < 140)
    try {
      p.storageGroup = group;
      travel(actor, serviceApproach(facility), TaskKind::StorageRent, facility);
      return true;
    } catch (const std::runtime_error &) {
    }
  return false;
}
bool World::storageDepartureDecision(Id actor) {
  auto found = storageByItemOwner.find(residents.at(actor).account);
  if (found == storageByItemOwner.end() || found->second.empty())
    return false;
  Id item = *found->second.begin(), lease = items[item].storedAt;
  auto &p = residentPlans[actor];
  p.storageItem = item;
  p.storageDeadline = time + 7200;
  if (residents[actor].task == TaskKind::StorageRetrieve && residents[actor].target == lease)
    return true;
  try {
    if (reachable(*this, actor, lease)) {
      setStorageOpen(actor, lease, true);
      auto contents = storageLeases[lease].items;
      for (Id id : contents)
        if (items[id].owner == residents[actor].account)
          withdrawStorage(actor, id);
      setStorageOpen(actor, lease, false);
      p.storageItem = none;
      return false;
    }
    travel(actor, storagePosition(lease), TaskKind::StorageRetrieve, lease);
    return true;
  } catch (const std::runtime_error &) {
    p.storageItem = none;
    return true;
  }
}
void World::finishStorageVisit(Id actor, TaskKind task, Id target) {
  auto &p = residentPlans[actor];
  try {
    if (task == TaskKind::StorageRent) {
      rentStorage(actor, target, p.storageGroup);
      return;
    }
    Id item = p.storageItem;
    if (item == none || time > p.storageDeadline || !reachable(*this, actor, target))
      throw std::runtime_error("storage.error.item");
    setStorageOpen(actor, target, true);
    if (task == TaskKind::StorageRetrieve) {
      if (items[item].storedAt != target)
        throw std::runtime_error("storage.error.item");
      withdrawStorage(actor, item);
      if (residents[actor].leaving) {
        auto contents = storageLeases[target].items;
        for (Id id : contents)
          if (items[id].owner == residents[actor].account)
            withdrawStorage(actor, id);
      }
    } else {
      if (residents[actor].automatic && items[item].kind == ItemKind::Food &&
          carried(*this, actor, ItemKind::Food) < 2)
        throw std::runtime_error("storage.error.deposit");
      depositStorage(actor, target, item, true);
    }
    setStorageOpen(actor, target, false);
  } catch (const std::runtime_error &) {
  }
  p.storageItem = none;
}
void World::validateStorage() const {
  std::set<std::pair<Id, int>> occupied;
  std::set<Id> contents;
  std::set<std::pair<Id, int>> shelves;
  std::map<Id, std::set<Id>> accountsIndex, groupsIndex, itemOwners;
  auto person = [&](Id id) { return id >= 0 && std::size_t(id) < residents.size(); };
  for (std::size_t id = 0; id < storageLeases.size(); ++id) {
    const auto &l = storageLeases[id];
    if (l.id != Id(id) || l.facility < 0 || std::size_t(l.facility) >= facilities.size() ||
        facilities[l.facility].kind != FacilityKind::Locker || l.slot < 0 ||
        l.slot >= facilities[l.facility].stations || l.holder <= 0 ||
        std::size_t(l.holder) >= accounts.size() || !person(l.custodian) ||
        (l.household != none &&
         (l.household < 0 || std::size_t(l.household) >= households.size())) ||
        l.dailyRent <= 0 || l.dailyRent > 100000000 || l.arrears < 0 ||
        l.arrears > 1000000000000LL || !std::isfinite(l.paidUntil) || l.paidUntil < 0 ||
        l.items.size() > 12 || (l.released && (l.active || l.open || !l.items.empty())) ||
        (!l.released && !occupied.emplace(l.facility, l.slot).second))
      throw std::runtime_error("Invalid storage lease");
    accountsIndex[l.holder].insert(l.id);
    if (!l.released && l.household != none)
      groupsIndex[l.household].insert(l.id);
    for (Id item : l.items) {
      if (item >= 0 && std::size_t(item) < items.size())
        itemOwners[items[item].owner].insert(item);
      if (item < 0 || std::size_t(item) >= items.size() || !contents.insert(item).second ||
          items[item].storedAt != l.id || items[item].carrier != none || items[item].consumed ||
          items[item].reservedAt != none || items[item].seller != none ||
          items[item].storageSlot < 0 || items[item].storageSlot >= 12 ||
          !shelves.emplace(l.id, items[item].storageSlot).second ||
          length(items[item].pos - storedItemPosition(l.id, items[item].storageSlot)) > .01f)
        throw std::runtime_error("Invalid storage contents");
    }
  }
  for (const auto &item : items) {
    if (item.storedAt == none && item.storageSlot != -1)
      throw std::runtime_error("Invalid loose storage slot");
    if (item.storedAt != none && !contents.contains(item.id))
      throw std::runtime_error("Missing stored item");
    if (item.sharedWith != none &&
        (item.storedAt == none || !shareable(item.kind) || item.sharedWith < 0 ||
         std::size_t(item.sharedWith) >= households.size()))
      throw std::runtime_error("Invalid shared property");
  }
  // Empty index buckets are harmless results of ordinary lease releases.
  auto compact = [](auto index) {
    std::erase_if(index, [](const auto &p) { return p.second.empty(); });
    return index;
  };
  if (compact(storageByAccount) != accountsIndex || compact(storageByHousehold) != groupsIndex ||
      compact(storageByItemOwner) != itemOwners)
    throw std::runtime_error("Invalid storage index");
}
} // namespace mega
