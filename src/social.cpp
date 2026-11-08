#include "mega/world.hpp"
#include <algorithm>
#include <tuple>
namespace mega {
static std::uint64_t pairKey(Id a, Id b) {
  return (std::uint64_t(std::min(a, b)) << 32) | std::uint32_t(std::max(a, b));
}
float World::trust(Id a, Id b) const {
  auto it = relationships.find(pairKey(a, b));
  if (it == relationships.end())
    return 0;
  const auto &bond = it->second;
  return std::clamp(bond.trust + (a == bond.a ? bond.regardA : bond.regardB), -1.f, 1.f);
}
Relationship *World::ensureRelationship(Id a, Id b) {
  if (a == b)
    return nullptr;
  auto key = pairKey(a, b);
  auto found = relationships.find(key);
  if (found != relationships.end())
    return &found->second;
  if (relationships.size() >= 100000) {
    auto candidate = relationships.end();
    double oldest = time;
    for (auto it = relationships.begin(); it != relationships.end(); ++it) {
      const auto &bond = it->second;
      // Retain real friendships, grievances and household ties before weak contacts.
      if (trust(bond.a, bond.b) >= .5f || trust(bond.b, bond.a) >= .5f ||
          trust(bond.a, bond.b) <= -.35f || trust(bond.b, bond.a) <= -.35f ||
          (householdOf(bond.a) != none && householdOf(bond.a) == householdOf(bond.b)))
        continue;
      double recent = bond.memories.empty() ? bond.lastTalk : bond.memories.back().time;
      if (recent < oldest) {
        oldest = recent;
        candidate = it;
      }
    }
    if (candidate == relationships.end())
      return nullptr;
    relationships.erase(candidate);
  }
  auto &bond = relationships[key];
  bond.a = std::min(a, b);
  bond.b = std::max(a, b);
  return &bond;
}
std::vector<EncounterMemory> World::relationshipHistory(Id observer, Id other) const {
  std::vector<EncounterMemory> result;
  auto found = relationships.find(pairKey(observer, other));
  if (found == relationships.end())
    return result;
  const auto &bond = found->second;
  for (const auto &memory : bond.memories)
    if (observer == bond.a ? memory.knownToA : memory.knownToB)
      result.push_back(memory);
  return result;
}
RelationshipStatus World::relationshipStatus(Id observer, Id other) const {
  float opinion = trust(observer, other);
  if (opinion >= .5f)
    return RelationshipStatus::Friend;
  if (opinion <= -.35f)
    return RelationshipStatus::Rival;
  auto found = relationships.find(pairKey(observer, other));
  bool known = found != relationships.end() &&
               (found->second.conversations > 0 || !relationshipHistory(observer, other).empty());
  return known ? RelationshipStatus::Acquaintance : RelationshipStatus::Stranger;
}
void World::rememberEncounter(Id actor, Id other, EncounterKind kind, float actorChange,
                              float otherChange, bool otherAware) {
  auto person = [&](Id who) { return who >= 0 && std::size_t(who) < residents.size(); };
  if (!person(actor) || !person(other) || actor == other || int(kind) < 0 ||
      int(kind) > int(EncounterKind::QueueCut) || !std::isfinite(actorChange) ||
      !std::isfinite(otherChange))
    throw std::runtime_error("Invalid social encounter");
  auto *bond = ensureRelationship(actor, other);
  if (!bond)
    return;
  EncounterMemory memory;
  memory.kind = kind;
  memory.actor = actor;
  memory.time = time;
  memory.knownToA = actor == bond->a || otherAware;
  memory.knownToB = actor == bond->b || otherAware;
  auto adjust = [&](float &regard, float change) {
    float before = std::clamp(bond->trust + regard, -1.f, 1.f);
    float after = std::clamp(before + change, -1.f, 1.f);
    regard = after - bond->trust;
    return after - before;
  };
  float changeA = actor == bond->a ? actorChange : (otherAware ? otherChange : 0);
  float changeB = actor == bond->b ? actorChange : (otherAware ? otherChange : 0);
  memory.changeA = adjust(bond->regardA, changeA);
  memory.changeB = adjust(bond->regardB, changeB);
  bond->memories.push_back(memory);
  if (bond->memories.size() > 16)
    bond->memories.erase(bond->memories.begin());
}
std::string World::talk(Id speaker, Id listener, int topic) {
  auto &a = residents.at(speaker);
  auto &b = residents.at(listener);
  if (speaker == listener || !a.alive || !a.present || !b.alive || !b.present || topic < 0 ||
      topic > 5 || !near(speaker, position(listener), 2.5f) || b.activity == Activity::Sleeping ||
      b.activity == Activity::Fighting || a.activity == Activity::Fighting ||
      a.activity == Activity::Sleeping ||
      !lineOfSight(position(speaker) + Vec3{0, 1.4f, 0}, position(listener) + Vec3{0, 1.4f, 0}))
    throw std::runtime_error("Reach an awake resident who can hear you");
  auto key = pairKey(speaker, listener);
  auto it = relationships.find(key);
  if (it != relationships.end() && time - it->second.lastTalk < 300)
    throw std::runtime_error("Give this person time before talking again");
  auto *contact = ensureRelationship(speaker, listener);
  if (!contact)
    throw std::runtime_error("Too many active social commitments");
  auto &bond = *contact;
  bond.lastTalk = time;
  bond.conversations = std::min(1000000, bond.conversations + 1);
  int interest =
      int(std::max_element(b.interests.begin(), b.interests.end()) - b.interests.begin());
  bond.sharedInterest = interest;
  std::string reply;
  if (topic == 0) {
    float change = .05f * (1 - b.aggression);
    rememberEncounter(speaker, listener, EncounterKind::Greeting, change, change);
    reply = b.aggression > .6f ? "talk.guarded" : "talk.greet_reply";
  }
  if (topic == 1) {
    float affinity = 1 - std::abs(a.interests[interest] - b.interests[interest]);
    float change = .1f * affinity - .02f;
    rememberEncounter(speaker, listener, EncounterKind::Interests, change, change);
    reply = "talk.interest" + std::to_string(interest);
    a.happiness = std::min(1.f, a.happiness + .015f * affinity);
    b.happiness = std::min(1.f, b.happiness + .015f * affinity);
  }
  if (topic == 2) {
    rememberEncounter(speaker, listener, EncounterKind::Work, 0, 0);
    reply = b.role == Role::Unemployed ? "talk.job_seeking"
            : onShift(listener)        ? "talk.job_busy"
                                       : "talk.job_rest";
  }
  if (topic == 3) {
    rememberEncounter(speaker, listener, EncounterKind::Insult, -.05f, -.3f);
    b.happiness = std::max(0.f, b.happiness - .03f);
    reply = "talk.insult_reply";
    if (b.aggression > .65f && b.strength > a.strength * .75f && time >= b.nextHit &&
        !b.surrendered) {
      try {
        attack(listener, speaker);
      } catch (const std::runtime_error &) {
      }
      reply = "talk.fight_reply";
    }
  }
  if (topic == 4) {
    float repair = std::clamp(.1f - trust(listener, speaker), 0.f, .12f);
    rememberEncounter(speaker, listener, EncounterKind::Apology, std::min(.02f, repair), repair);
    reply = "talk.apology_reply";
  }
  if (topic == 5)
    reply = shareFood(listener, speaker) ? "talk.meal_shared" : "talk.no_spare";
  householdSocial(speaker, listener);
  log(reply, listener, speaker);
  return reply;
}
void World::rebuildItemOffers() {
  itemOffers.clear();
  for (const auto &item : items)
    if (item.askingPrice > 0 && item.seller != none && !item.consumed)
      itemOffers.insert(item.id);
}
void World::listItem(Id seller, Id id, Money price) {
  auto &r = residents.at(seller);
  auto &i = items.at(id);
  if (!r.alive || !r.present || i.owner != r.account || i.carrier != seller || i.consumed ||
      i.reservedAt != none || itemInUse(id) || i.stolenFrom != none || r.outfit == id ||
      price < 0 || price > 100000000 || i.kind == ItemKind::Waste || i.kind == ItemKind::Materials)
    throw std::runtime_error("Only an owned, unreserved, unworn item may be offered for sale");
  i.askingPrice = price;
  i.seller = price ? seller : none;
  if (price)
    itemOffers.insert(id);
  else
    itemOffers.erase(id);
  log("trade.listed", seller, id);
}
void World::buyItem(Id buyer, Id id) {
  auto &r = residents.at(buyer);
  auto &i = items.at(id);
  if (!r.alive || !r.present || i.askingPrice <= 0 || i.seller == none || i.seller == buyer ||
      i.consumed || i.reservedAt != none || itemInUse(id) || i.stolenFrom != none)
    throw std::runtime_error("This item is not available for purchase");
  auto &seller = residents.at(i.seller);
  if (!seller.alive || !seller.present || i.owner != seller.account || i.carrier != seller.id ||
      seller.outfit == id || !near(buyer, position(seller.id), 2.5f) ||
      !lineOfSight(position(buyer) + Vec3{0, 1.3f, 0}, position(seller.id) + Vec3{0, 1.3f, 0}))
    throw std::runtime_error("Meet the seller for a physical handover");
  pay(r.account, seller.account, i.askingPrice, "personal-item-resale");
  std::erase(seller.items, id);
  r.items.push_back(id);
  i.owner = r.account;
  i.carrier = buyer;
  i.pos = position(buyer);
  i.seller = none;
  i.askingPrice = 0;
  itemOffers.erase(id);
  rememberEncounter(buyer, seller.id, EncounterKind::Trade, .025f, .025f);
  log("trade.completed", buyer, id);
}
bool World::shareFood(Id donor, Id recipient) {
  auto &a = residents.at(donor);
  auto &b = residents.at(recipient);
  auto key = pairKey(donor, recipient);
  auto bond = relationships.find(key);
  if (donor == recipient || !a.alive || !a.present || !b.alive || !b.present ||
      a.aggression > .6f || b.hunger < .55f || a.surrendered || a.activity == Activity::Fighting ||
      b.activity == Activity::Fighting || bond == relationships.end() ||
      trust(donor, recipient) < .35f || trust(recipient, donor) < .35f ||
      time - bond->second.lastAid < 21600 || !near(donor, position(recipient), 2.5f) ||
      !lineOfSight(position(donor) + Vec3{0, 1.3f, 0}, position(recipient) + Vec3{0, 1.3f, 0}))
    return false;
  std::vector<Id> meals;
  for (Id item : a.items) {
    const auto &i = items[item];
    if (i.kind == ItemKind::Food && i.owner == a.account && i.carrier == donor && !i.consumed &&
        i.reservedAt == none && i.stolenFrom == none && !itemInUse(item))
      meals.push_back(item);
  }
  if (meals.size() < 2)
    return false;
  Id item = meals.back();
  std::erase(a.items, item);
  b.items.push_back(item);
  auto &meal = items[item];
  meal.owner = b.account;
  meal.carrier = recipient;
  meal.pos = position(recipient);
  meal.askingPrice = 0;
  meal.seller = none;
  itemOffers.erase(item);
  bond->second.lastAid = time;
  rememberEncounter(donor, recipient, EncounterKind::Meal, .04f, .12f);
  a.happiness = std::min(1.f, a.happiness + .025f);
  b.happiness = std::min(1.f, b.happiness + .04f);
  if (residentPlans.contains(donor))
    residentPlans[donor].action = AutonomyAction::SharedMeal;
  log("autonomy.meal_shared", donor, recipient);
  return true;
}
void World::socialTick() {
  communicationTick();
  using Cell = std::tuple<int, int, int>;
  std::map<Cell, std::vector<Id>> neighbors;
  auto available = [&](const Resident &r) {
    return r.alive && r.present && r.automatic && !r.leaving && r.hunger < 1 && r.thirst < 1 &&
           !r.surrendered && r.lift == none && r.ambulance == none &&
           (r.activity == Activity::Idle || r.activity == Activity::Queuing);
  };
  for (const auto &r : residents)
    if (available(r)) {
      auto p = position(r.id);
      neighbors[{int(std::round(p.y / 3)), int(std::floor(p.x / 3)), int(std::floor(p.z / 3))}]
          .push_back(r.id);
    }
  std::set<Id> spoke;
  for (const auto &r : residents)
    if (available(r) && !spoke.contains(r.id) && random() < .25f) {
      auto p = position(r.id);
      int floor = int(std::round(p.y / 3)), x = int(std::floor(p.x / 3)),
          z = int(std::floor(p.z / 3));
      bool done = false;
      for (int dx = -1; dx <= 1 && !done; ++dx)
        for (int dz = -1; dz <= 1 && !done; ++dz) {
          auto cell = neighbors.find({floor, x + dx, z + dz});
          if (cell == neighbors.end())
            continue;
          for (Id other : cell->second)
            if (other != r.id && !spoke.contains(other) && available(residents[other]) &&
                near(r.id, position(other), 2.5f)) {
              auto bond = relationships.find(pairKey(r.id, other));
              if ((bond != relationships.end() && time - bond->second.lastTalk < 1800) ||
                  (bond == relationships.end() && relationships.size() >= 100000))
                continue;
              int topic = r.aggression > .7f && trust(r.id, other) < -.2f && random() < .1f ? 3 : 1;
              try {
                talk(r.id, other, topic);
                exchangeInformation(r.id, other);
                if (residents[other].hunger > .65f)
                  shareFood(r.id, other);
                if (r.hunger > .65f)
                  shareFood(other, r.id);
                spoke.insert(r.id);
                spoke.insert(other);
                done = true;
                break;
              } catch (const std::runtime_error &) {
              }
            }
        }
    }
}
void World::cheatResident(Id id, const std::string &stat, double value) {
  auto &r = residents.at(id);
  if (config.mode != Mode::Cheat || !r.alive || !r.present || !std::isfinite(value))
    throw std::runtime_error("Resident overrides require a living resident in Cheat mode");
  if (stat == "cash") {
    if (value < 0 || value > 1000000000 || std::floor(value) != value)
      throw std::runtime_error("Balance must be a whole number of cents from 0 to 1,000,000,000");
    Money difference = Money(value) - accounts[r.account].balance;
    if (difference > 0)
      pay(0, r.account, difference, "cheat-grant");
    else if (difference < 0)
      pay(r.account, 0, -difference, "cheat-withdrawal");
    return;
  }
  float *field = nullptr;
  double maximum = 1;
  if (stat == "health") {
    field = &r.health;
    maximum = 100;
  } else if (stat == "hunger") {
    field = &r.hunger;
    maximum = 1.4;
  } else if (stat == "thirst") {
    field = &r.thirst;
    maximum = 1.4;
  } else if (stat == "energy")
    field = &r.energy;
  else if (stat == "happiness")
    field = &r.happiness;
  else if (stat == "hygiene")
    field = &r.hygiene;
  else if (stat == "bladder")
    field = &r.bladder;
  else if (stat == "strength") {
    field = &r.strength;
    maximum = 100;
  } else if (stat == "aggression")
    field = &r.aggression;
  if (!field || value < 0 || value > maximum)
    throw std::runtime_error("Unknown statistic or value outside its displayed range");
  // Settle elapsed needs first; an override must not rewrite the previous hour.
  needs(id);
  if (!r.alive)
    throw std::runtime_error("The resident died before the override");
  *field = float(value);
  if (stat == "health" && value == 0)
    kill(id);
  log("cheat.changed", id);
}
} // namespace mega
