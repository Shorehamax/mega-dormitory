#include "mega/world.hpp"
#include <algorithm>
#include <tuple>
namespace mega {
static std::uint64_t pairKey(Id a, Id b) {
  return (std::uint64_t(std::min(a, b)) << 32) | std::uint32_t(std::max(a, b));
}
float World::trust(Id a, Id b) const {
  auto it = relationships.find(pairKey(a, b));
  return it == relationships.end() ? 0 : it->second.trust;
}
std::string World::talk(Id speaker, Id listener, int topic) {
  auto &a = residents.at(speaker);
  auto &b = residents.at(listener);
  if (speaker == listener || !a.alive || !a.present || !b.alive || !b.present || topic < 0 ||
      topic > 4 || !near(speaker, position(listener), 2.5f) || b.activity == Activity::Sleeping ||
      b.activity == Activity::Fighting || a.activity == Activity::Fighting ||
      !lineOfSight(position(speaker) + Vec3{0, 1.4f, 0}, position(listener) + Vec3{0, 1.4f, 0}))
    throw std::runtime_error("Reach an awake resident who can hear you");
  auto key = pairKey(speaker, listener);
  auto it = relationships.find(key);
  if (it != relationships.end() && time - it->second.lastTalk < 300)
    throw std::runtime_error("Give this person time before talking again");
  if (relationships.size() >= 100000 && !relationships.contains(key)) {
    auto oldest = std::min_element(
        relationships.begin(), relationships.end(),
        [](const auto &a, const auto &b) { return a.second.lastTalk < b.second.lastTalk; });
    relationships.erase(oldest);
  }
  auto &bond = relationships[key];
  bond.a = std::min(speaker, listener);
  bond.b = std::max(speaker, listener);
  bond.lastTalk = time;
  int interest =
      int(std::max_element(b.interests.begin(), b.interests.end()) - b.interests.begin());
  bond.sharedInterest = interest;
  std::string reply;
  if (topic == 0) {
    bond.trust = std::min(1.f, bond.trust + .05f * (1 - b.aggression));
    reply = b.aggression > .6f ? "talk.guarded" : "talk.greet_reply";
  }
  if (topic == 1) {
    float affinity = 1 - std::abs(a.interests[interest] - b.interests[interest]);
    bond.trust = std::clamp(bond.trust + .1f * affinity - .02f, -1.f, 1.f);
    reply = "talk.interest" + std::to_string(interest);
    a.happiness = std::min(1.f, a.happiness + .015f * affinity);
    b.happiness = std::min(1.f, b.happiness + .015f * affinity);
  }
  if (topic == 2) {
    reply = b.role == Role::Unemployed ? "talk.job_seeking"
            : onShift(listener)        ? "talk.job_busy"
                                       : "talk.job_rest";
  }
  if (topic == 3) {
    bond.trust = std::max(-1.f, bond.trust - .3f);
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
    if (bond.trust < .1f)
      bond.trust = std::min(.1f, bond.trust + .12f);
    reply = "talk.apology_reply";
  }
  log(reply, listener, speaker);
  return reply;
}
void World::listItem(Id seller, Id id, Money price) {
  auto &r = residents.at(seller);
  auto &i = items.at(id);
  if (!r.alive || !r.present || i.owner != r.account || i.carrier != seller || i.consumed ||
      i.reservedAt != none || i.stolenFrom != none || r.outfit == id || price < 0 ||
      price > 100000000 || i.kind == ItemKind::Waste || i.kind == ItemKind::Materials)
    throw std::runtime_error("Only an owned, unreserved, unworn item may be offered for sale");
  i.askingPrice = price;
  i.seller = price ? seller : none;
  log("trade.listed", seller, id);
}
void World::buyItem(Id buyer, Id id) {
  auto &r = residents.at(buyer);
  auto &i = items.at(id);
  if (!r.alive || !r.present || i.askingPrice <= 0 || i.seller == none || i.seller == buyer ||
      i.consumed || i.reservedAt != none || i.stolenFrom != none)
    throw std::runtime_error("This item is not available for purchase");
  auto &seller = residents.at(i.seller);
  if (!seller.alive || !seller.present || i.owner != seller.account || i.carrier != seller.id ||
      seller.outfit == id || !near(buyer, position(seller.id), 2.5f))
    throw std::runtime_error("Meet the seller for a physical handover");
  pay(r.account, seller.account, i.askingPrice, "personal-item-resale");
  std::erase(seller.items, id);
  r.items.push_back(id);
  i.owner = r.account;
  i.carrier = buyer;
  i.pos = position(buyer);
  i.seller = none;
  i.askingPrice = 0;
  log("trade.completed", buyer, id);
}
void World::socialTick() {
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
