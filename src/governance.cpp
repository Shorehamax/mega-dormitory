#include "mega/world.hpp"
#include <algorithm>
namespace mega {
Money World::dormValuation() const {
  Money value = std::max(Money(10000000), accounts[dormAccount].balance);
  for (const auto &h : housing)
    if (h.active && h.owner == dormAccount)
      value += h.price;
  for (const auto &f : facilities)
    if (f.open && f.owner == dormAccount)
      value += std::max(Money(1000000), f.askingPrice);
  return value;
}
void World::listDorm(Id owner, Money price) {
  auto &r = residents.at(owner);
  if (owner != dormOwner || !r.alive || !r.present || price < 0 || price > 100000000000000LL)
    throw std::runtime_error("Only the owner may list the dormitory at a valid price");
  governance.askingPrice = price;
  log("ownership.listed", owner);
}
void World::transferDorm(Id successor) {
  auto &r = residents.at(successor);
  if (!r.alive || !r.present)
    throw std::runtime_error("The successor must be a living resident");
  Id former = dormOwner;
  dormOwner = successor;
  for (auto &f : facilities)
    if (f.owner == dormAccount && f.manager == former)
      f.manager = successor;
  governance.askingPrice = 0;
  governance.successor = none;
  governance.retiring = false;
  log("ownership.transferred", successor, former);
}
void World::buyDorm(Id buyer) {
  auto &r = residents.at(buyer);
  if (!r.alive || !r.present || buyer == dormOwner || governance.askingPrice <= 0 ||
      !residents[dormOwner].alive || !residents[dormOwner].present)
    throw std::runtime_error("The dormitory is not offered for sale");
  pay(r.account, residents[dormOwner].account, governance.askingPrice,
      "dormitory-ownership-purchase");
  transferDorm(buyer);
}
void World::nominateSuccessor(Id owner, Id successor) {
  auto &r = residents.at(successor);
  if (owner != dormOwner || owner == successor || !residents[owner].alive ||
      !residents[owner].present || !r.alive || !r.present)
    throw std::runtime_error("Only the current owner may nominate another living resident");
  governance.successor = successor;
  governance.retiring = true;
  log("ownership.invitation", successor, owner);
}
void World::acceptSuccession(Id successor) {
  if (governance.successor != successor || !governance.retiring)
    throw std::runtime_error("No succession invitation for this resident");
  transferDorm(successor);
}
void World::seizeDorm(Id claimant) {
  auto &r = residents.at(claimant);
  auto &owner = residents[dormOwner];
  Id reception = nearest(claimant, FacilityKind::Reception);
  if (!r.alive || !r.present || claimant == dormOwner || r.surrendered ||
      r.activity == Activity::Jailed || r.activity == Activity::Escorted || reception == none ||
      !near(claimant, facilities[reception].pos, 3) ||
      (owner.alive && owner.present && !owner.surrendered))
    throw std::runtime_error(
        "Reach reception after the current owner has been defeated or has left");
  for (auto &guard : residents)
    if (guard.id != claimant && guard.role == Role::Warden && guard.alive && guard.present &&
        !guard.surrendered && guard.employer == dormAccount &&
        near(guard.id, facilities[reception].pos, 20))
      throw std::runtime_error("Security still controls reception");
  report(CrimeKind::Theft, claimant, dormOwner, true);
  transferDorm(claimant);
  for (auto &other : residents)
    if (other.alive && other.present)
      other.happiness = std::max(0.f, other.happiness - .12f);
  // Existing crimes, victims and warden responses survive a change of ownership.
}
void World::offerCompensation(Id owner, Id id, Money offer) {
  auto &p = projects.at(id);
  if (p.owner != owner || p.state != ProjectState::AwaitingConsent || offer < p.offer ||
      offer > 1000000000)
    throw std::runtime_error("Only the proposer may increase an outstanding compensation offer");
  p.offer = offer;
  p.compensation = offer * Money(p.affected.size());
  for (Id who : p.affected)
    if (who != player && residents[who].automatic && residents[who].alive &&
        residents[who].present) {
      Money required = 5000 + Money(residents[who].aggression * 15000);
      Id home = residentHomes[who];
      if (home != none)
        required += housing[home].rent * 3;
      const auto &f = facilities[p.facility];
      if (f.owner == residents[who].account || (f.owner != dormAccount && f.manager == who))
        required = std::max(required, std::max(Money(2000000), f.askingPrice));
      if (offer >= required)
        consentProject(id, who);
    }
  log("construction.offer", owner, id);
}
void World::governanceTick() {
  int day = int(time / 86400);
  if (day <= governance.reviewedDay)
    return;
  governance.reviewedDay = day;
  auto &owner = residents[dormOwner];
  auto eligible = [&](Id id) {
    const auto &r = residents[id];
    return id != dormOwner && r.alive && r.present && r.activity != Activity::Jailed &&
           std::none_of(incidents.begin(), incidents.end(),
                        [&](const Incident &i) { return i.offender == id && !i.resolved; });
  };
  if (governance.successor != none && !eligible(governance.successor)) {
    governance.successor = none;
    governance.retiring = false;
  }
  if ((!owner.alive || !owner.present) && governance.successor != none) {
    transferDorm(governance.successor);
    return;
  }
  bool vacant = !owner.alive || !owner.present;
  if ((vacant || (dormOwner != player && owner.automatic && day >= 30 && owner.age >= 60)) &&
      governance.successor == none) {
    Id best = none;
    Money score = -1;
    for (auto &r : residents)
      if (eligible(r.id)) {
        Money candidate = accounts[r.account].balance + Money(r.happiness * 1000000);
        if (candidate > score) {
          score = candidate;
          best = r.id;
        }
      }
    if (best != none) {
      if (vacant) {
        transferDorm(best);
        return;
      } else
        nominateSuccessor(dormOwner, best);
    }
  }
  if (governance.retiring && governance.successor != none && governance.successor != player &&
      residents[governance.successor].automatic) {
    acceptSuccession(governance.successor);
    return;
  }
  if (day > 1 && dormOwner != player && owner.alive && owner.present && owner.automatic &&
      accounts[dormAccount].balance > 2000000 &&
      std::count_if(projects.begin(), projects.end(), [](const Project &p) {
        return p.newConstruction && p.state != ProjectState::Completed;
      }) < 3) {
    Id crowded = none;
    std::size_t queue = 40;
    for (const auto &f : facilities)
      if (f.open && f.queue.size() > queue) {
        queue = f.queue.size();
        crowded = f.id;
      }
    if (crowded != none) {
      auto kind = facilities[crowded].kind;
      int floor = facilities[crowded].floor;
      for (int plot = 0; plot < 8; ++plot)
        try {
          commissionFacility(dormOwner, kind, floor,
                             {54.f + (plot % 4) * 12.f, floor * 3.f, (plot / 4) * 16.f}, 2);
          break;
        } catch (const std::runtime_error &) {
        }
    }
  }
  if (dormOwner != player && owner.automatic && !governance.retiring && day > 7 &&
      governance.askingPrice == 0 && random() < .025f)
    governance.askingPrice = dormValuation() * 2;
}
} // namespace mega
