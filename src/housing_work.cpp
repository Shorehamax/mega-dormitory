#include "mega/world.hpp"
#include <algorithm>
namespace mega {
Vec3 World::homePosition(Id id) const {
  const auto &h = housing.at(id);
  return h.custom ? h.location : layout(h.floor).bed(h.floor, h.bed);
}
static HousingWork contract(World &w, Id actor, int floor) {
  auto &r = w.residents.at(actor);
  if (!r.alive || !r.present || floor < 0 || floor >= w.config.floors)
    throw std::runtime_error("Invalid housing construction site");
  Id workshop = w.nearestAt({0, floor * 3.f, 0}, FacilityKind::Workshop);
  if (workshop == none)
    throw std::runtime_error("A construction workshop is required");
  HousingWork p;
  p.id = Id(w.housingWorks.size());
  p.actor = actor;
  p.payer = actor == w.dormOwner ? w.dormAccount : r.account;
  p.workshop = workshop;
  p.contractor = w.facilities[workshop].owner;
  p.floor = floor;
  return p;
}
Id World::commissionBed(Id actor, int floor, Vec3 pos) {
  auto p = contract(*this, actor, floor);
  if (!floorPlans[floor].deck || structuralFloorLocked(floor))
    throw std::runtime_error("structure.error.floor");
  pos.y = floor * 3.f;
  if (!std::isfinite(pos.x) || !std::isfinite(pos.z))
    throw std::runtime_error("Invalid bed position");
  for (float x = -1.4f; x <= 1.4f; x += .35f)
    for (float z = -2; z <= 2; z += .4f)
      if (!clearPosition(pos + Vec3{x, 0, z}))
        throw std::runtime_error("A bed needs a clear plot and walking access");
  for (const auto &other : housingWorks)
    if (other.state != ProjectState::Completed && length(other.site - pos) < 4)
      throw std::runtime_error("Another construction contract occupies this plot");
  if (accounts[p.payer].balance < p.budget)
    throw std::runtime_error("Cannot fund bed construction");
  HousingUnit h;
  h.owner = p.payer;
  h.floor = floor;
  h.bed = 1000000 + Id(housing.size());
  h.custom = true;
  h.location = pos;
  h.active = false;
  h.price = housingPrice(floor);
  h.rent = h.price / 100;
  p.home = Id(housing.size());
  p.site = pos;
  p.escrow = account("Bed construction escrow");
  pay(p.payer, p.escrow, p.budget, "bed-construction");
  floorHomes[floor].push_back(p.home);
  housing.push_back(h);
  housingWorks.push_back(p);
  log("housing.construction", actor, p.id);
  return p.id;
}
Id World::proposeHomeRemoval(Id actor, Id home, Money compensation) {
  const auto &h = housing.at(home);
  auto p = contract(*this, actor, h.floor);
  if (!h.active || compensation < 0 || compensation > 1000000000 ||
      (h.owner != residents[actor].account && actor != dormOwner))
    throw std::runtime_error("Only the owner or dormitory manager may propose removal");
  for (auto &old : housingWorks)
    if (old.home == home && old.state != ProjectState::Completed)
      throw std::runtime_error("This bed already has a construction contract");
  p.kind = HousingWorkKind::RemoveBed;
  p.home = home;
  p.site = homePosition(home);
  p.budget = 80000;
  p.offer = compensation;
  p.ownerOffer = h.price;
  p.offeredOwner = h.owner;
  p.offeredTenant = h.tenant;
  p.remaining = p.total = 3600;
  p.supplied = true;
  p.state = ProjectState::AwaitingConsent;
  if (h.tenant != none &&
      (h.tenant == actor || (!residents[h.tenant].alive || !residents[h.tenant].present) ||
       (h.tenant != player && residents[h.tenant].automatic &&
        compensation >= h.rent * 7 + 10000 + Money(residents[h.tenant].aggression * 20000))))
    p.consented.insert(h.tenant);
  for (const auto &r : residents)
    if (r.account == h.owner && (r.id == actor || (r.id != player && r.automatic)))
      p.consented.insert(r.id);
  housingWorks.push_back(p);
  reviseHomeRemoval(actor, p.id, compensation, h.price);
  return p.id;
}
void World::consentHomeRemoval(Id resident, Id id) {
  auto &p = housingWorks.at(id);
  const auto &h = housing.at(p.home);
  auto &r = residents.at(resident);
  if (p.state != ProjectState::AwaitingConsent || !r.alive || !r.present ||
      (h.tenant != resident && h.owner != r.account))
    throw std::runtime_error("No outstanding relocation consent");
  p.consented.insert(resident);
  p.refused.erase(resident);
}
void World::fundHomeRemoval(Id actor, Id id, bool force) {
  auto &p = housingWorks.at(id);
  auto &h = housing.at(p.home);
  if (actor != p.actor || p.state != ProjectState::AwaitingConsent || !h.active ||
      !residents.at(actor).alive || !residents[actor].present)
    throw std::runtime_error("Invalid housing removal contract");
  if ((p.offeredOwner != none && (p.offeredOwner != h.owner || p.offeredTenant != h.tenant)) ||
      structuralFloorLocked(h.floor))
    throw std::runtime_error("structure.error.changed");
  for (const auto &r : residents)
    if (r.account == h.owner && r.alive && r.present && !p.consented.contains(r.id))
      throw std::runtime_error("Property owner has not accepted the buyout");
  Id tenant = h.tenant, replacement = p.replacement;
  if (replacement != none &&
      (replacement < 0 || std::size_t(replacement) >= housing.size() || replacement == p.home ||
       !housing[replacement].active || housing[replacement].tenant != none ||
       std::any_of(
           housingWorks.begin(), housingWorks.end(),
           [&](const HousingWork &q) {
             return q.home == replacement && q.state != ProjectState::Completed;
           })))
    throw std::runtime_error("structure.error.replacement");
  if (tenant != none && residents[tenant].alive && residents[tenant].present) {
    if (!p.consented.contains(tenant)) {
      if (!force || !near(actor, position(tenant), 2.5f) || !residents[tenant].surrendered)
        throw std::runtime_error(
            "Tenant has not accepted relocation; force requires physical confrontation");
    }
    float best = replacement == none ? 1e20f : -1.f;
    for (Id i = 0; i < Id(housing.size()); ++i)
      if (i != p.home && housing[i].active && housing[i].tenant == none &&
          std::none_of(housingWorks.begin(), housingWorks.end(), [&](const HousingWork &q) {
            return q.home == i && q.state != ProjectState::Completed;
          })) {
        float d = length(homePosition(i) - p.site);
        if (d < best) {
          best = d;
          replacement = i;
        }
      }
    if (replacement == none)
      throw std::runtime_error("Secure another vacant bed before removing an occupied home");
  }
  if (tenant != none && p.offer < h.arrears)
    throw std::runtime_error("structure.error.arrears");
  Money buyout = h.owner == p.payer ? 0 : (p.ownerOffer < 0 ? h.price : p.ownerOffer);
  Money total = p.budget + buyout + (tenant == none ? 0 : p.offer) +
                (replacement == none ? 0 : housing[replacement].rent);
  if (accounts[p.payer].balance < total)
    throw std::runtime_error("Insufficient compensation and construction budget");
  p.escrow = account("Housing removal escrow");
  pay(p.payer, p.escrow, total, "relocation-funding");
  pay(p.escrow, h.owner, buyout, "property-buyout");
  if (tenant != none) {
    Money debt = std::min(h.arrears, p.offer);
    pay(p.escrow, h.owner, debt, "relocation-rent-arrears");
    pay(p.escrow, residents[tenant].account, p.offer - debt, "tenant-relocation-compensation");
  }
  if (replacement != none) {
    auto &next = housing[replacement];
    pay(p.escrow, next.owner, next.rent, "replacement-bed-rent");
    next.tenant = tenant;
    next.paidUntil = time + 86400;
    next.arrears = 0;
    residentHomes[tenant] = replacement;
    residents[tenant].home = next.floor;
    // The new lease changes immediately; the person still has to walk there.
    try {
      travel(tenant, homePosition(replacement), TaskKind::Home, tenant);
    } catch (const std::runtime_error &) {
    }
  } else if (tenant != none)
    residentHomes[tenant] = none;
  if (force && !p.consented.contains(tenant)) {
    report(CrimeKind::Theft, actor, tenant, true);
    residents[tenant].happiness = std::max(0.f, residents[tenant].happiness - .3f);
  }
  h.owner = p.payer;
  h.tenant = none;
  h.arrears = 0;
  h.paidUntil = 0;
  h.listed = false;
  p.replacement = replacement;
  p.forced = force;
  p.state = ProjectState::Funded;
}

void World::replyHomeRemoval(Id resident, Id id, bool accept) {
  auto &p = housingWorks.at(id);
  const auto &h = housing.at(p.home);
  const auto &r = residents.at(resident);
  if (p.kind != HousingWorkKind::RemoveBed || p.state != ProjectState::AwaitingConsent ||
      !r.alive || !r.present || (h.tenant != resident && h.owner != r.account))
    throw std::runtime_error("structure.error.permission");
  if (accept) {
    p.consented.insert(resident);
    p.refused.erase(resident);
  } else {
    p.consented.erase(resident);
    p.refused.insert(resident);
  }
}
void World::counterHomeRemoval(Id resident, Id id, Money demand) {
  if (demand < 0 || demand > 1000000000)
    throw std::runtime_error("structure.error.offer");
  replyHomeRemoval(resident, id, false);
  housingWorks[id].demands[resident] = demand;
}
void World::reviseHomeRemoval(Id actor, Id id, Money tenantOffer, Money ownerOffer,
                              Id replacement) {
  auto &p = housingWorks.at(id);
  auto &h = housing.at(p.home);
  if (actor != p.actor || !residents.at(actor).alive || !residents[actor].present ||
      p.kind != HousingWorkKind::RemoveBed || p.state != ProjectState::AwaitingConsent ||
      tenantOffer < 0 || tenantOffer > 1000000000 || ownerOffer < 0 || ownerOffer > 1000000000 ||
      (replacement != none && (replacement < 0 || std::size_t(replacement) >= housing.size() ||
                               !housing[replacement].active ||
                               housing[replacement].tenant != none || replacement == p.home)))
    throw std::runtime_error("structure.error.offer");
  if (p.offeredOwner != h.owner || p.offeredTenant != h.tenant)
    p.demands.clear();
  p.offer = tenantOffer;
  p.ownerOffer = ownerOffer;
  p.offeredOwner = h.owner;
  p.offeredTenant = h.tenant;
  p.replacement = replacement;
  p.consented.clear();
  p.refused.clear();
  for (const auto &r : residents) {
    if (!r.alive || !r.present || (r.id != h.tenant && r.account != h.owner))
      continue;
    Money tenantDemand = h.rent * 7 + 10000 + Money(r.aggression * 20000);
    Money ownerDemand = h.price;
    if (p.demands.contains(r.id)) {
      if (r.account == h.owner)
        ownerDemand = std::max(ownerDemand, p.demands[r.id]);
      else
        tenantDemand = std::max(tenantDemand, p.demands[r.id]);
    }
    bool enough = (r.id != h.tenant || p.offer >= tenantDemand) &&
                  (r.account != h.owner || p.ownerOffer >= ownerDemand);
    if (r.id == actor || (r.automatic && r.id != player && enough))
      p.consented.insert(r.id);
    else if (r.id != player && r.automatic) {
      p.refused.insert(r.id);
      p.demands[r.id] = r.account == h.owner ? ownerDemand : tenantDemand;
    }
  }
}
void World::cancelHomeRemoval(Id actor, Id id) {
  auto &p = housingWorks.at(id);
  if (actor != p.actor || p.kind != HousingWorkKind::RemoveBed ||
      p.state != ProjectState::AwaitingConsent || !residents.at(actor).alive ||
      !residents[actor].present)
    throw std::runtime_error("structure.error.permission");
  p.cancelled = true;
  p.state = ProjectState::Completed;
}
Id World::commissionFloor(Id actor, int width, int depth) {
  if (actor != dormOwner && config.mode != Mode::Cheat)
    throw std::runtime_error("Only the dormitory owner can add floors");
  if (config.layoutVersion < 2 || config.floors >= 10000 || width < 96 || depth < 72 ||
      width > 2048 || depth > 2048)
    throw std::runtime_error("Invalid new floor dimensions");
  for (auto &p : housingWorks)
    if (p.kind == HousingWorkKind::AddFloor && p.state != ProjectState::Completed)
      throw std::runtime_error("Finish the existing structural extension first");
  if (!floorPlans.back().deck)
    throw std::runtime_error("structure.error.floor");
  auto p = contract(*this, actor, config.floors - 1);
  p.kind = HousingWorkKind::AddFloor;
  p.width = width;
  p.depth = depth;
  p.budget = Money(width) * depth * 3500;
  p.site = layout(p.floor).landing(0, p.floor * 3.f);
  p.remaining = p.total = std::max(7200.0, double(width) * depth * 1.8);
  if (accounts[p.payer].balance < p.budget)
    throw std::runtime_error("Cannot fund a new structural floor");
  p.escrow = account("New floor escrow");
  pay(p.payer, p.escrow, p.budget, "new-floor-construction");
  housingWorks.push_back(p);
  return p.id;
}
Vec3 World::housingWorkDestination(Id id) const {
  const auto &p = housingWorks.at(id);
  if (!p.supplied) {
    if (p.material == none)
      return facilities[p.workshop].pos + Vec3{0, 0, 1.6f};
    if (items[p.material].carrier != p.worker)
      return items[p.material].pos;
  }
  return accessible(p.site + Vec3{1.5f, 0, 0});
}
void World::assignHousingWork(Id worker, Id id) {
  auto &p = housingWorks.at(id);
  auto &r = residents.at(worker);
  if (!r.alive || !r.present || r.role != Role::Builder || builderCommitted(worker) ||
      r.carrying != none || p.worker != none || p.state != ProjectState::Funded)
    throw std::runtime_error("Builder cannot accept this housing contract");
  for (auto &other : housingWorks)
    if (other.worker == worker && other.state == ProjectState::Working)
      throw std::runtime_error("Builder already has a housing contract");
  clockOut(worker);
  p.worker = worker;
  p.state = ProjectState::Working;
  travel(worker, housingWorkDestination(id), TaskKind::None, none);
}
void World::housingWorkTick() {
  for (auto &p : housingWorks)
    if (p.state == ProjectState::Working && p.worker != none) {
      auto &r = residents[p.worker];
      if (!r.alive || !r.present || r.role != Role::Builder) {
        p.worker = none;
        p.state = ProjectState::Funded;
        p.workedAt = -1;
        continue;
      }
      if (!onShift(r.id)) {
        p.workedAt = -1;
        continue;
      }
      if (!p.supplied) {
        if (p.material == none) {
          auto &shop = facilities[p.workshop];
          if (!near(r.id, shop.pos) || !operational(shop.id) || shop.stock < 4)
            continue;
          Item item;
          item.id = Id(items.size());
          item.kind = ItemKind::Materials;
          item.owner = p.payer;
          item.carrier = r.id;
          item.pos = position(r.id);
          item.reservedAt = p.workshop;
          pay(p.escrow, shop.owner, 80000, "housing-building-materials");
          shop.stock -= 4;
          p.material = item.id;
          items.push_back(item);
          r.carrying = item.id;
          travel(r.id, housingWorkDestination(p.id), TaskKind::None, none);
          continue;
        }
        auto &item = items[p.material];
        if (item.carrier != r.id) {
          if (item.carrier != none || r.carrying != none || !near(r.id, item.pos))
            continue;
          item.carrier = r.id;
          r.carrying = item.id;
          travel(r.id, housingWorkDestination(p.id), TaskKind::None, none);
          continue;
        }
        if (!near(r.id, p.site, 2))
          continue;
        item.carrier = none;
        item.pos = p.site;
        r.carrying = none;
        p.supplied = true;
      }
      if (r.activity != Activity::Working || !near(r.id, p.site, 2)) {
        p.workedAt = -1;
        continue;
      }
      if (p.workedAt < 0) {
        p.workedAt = time;
        continue;
      }
      double dt = std::min(60.0, std::max(0.0, time - p.workedAt));
      p.workedAt = time;
      Money wage = Money(dt / 3600 * 1200);
      if (accounts[p.escrow].balance < wage)
        continue;
      pay(p.escrow, r.account, wage, "housing-construction-wage");
      p.remaining = std::max(0.0, p.remaining - dt);
      if (p.remaining == 0) {
        if (p.kind == HousingWorkKind::RemoveBed) {
          bool occupied = false;
          for (const auto &other : residents)
            if (other.id != r.id && other.alive && other.present && near(other.id, p.site, 1.3f)) {
              occupied = true;
              break;
            }
          if (occupied) {
            p.remaining = 60;
            continue;
          }
          housing[p.home].active = false;
        } else if (p.kind == HousingWorkKind::BuildBed)
          housing[p.home].active = true;
        else {
          floorPlans.push_back({p.width, p.depth});
          ++config.floors;
          invalidateGeometry(p.floor);
        }
        p.state = ProjectState::Completed;
        r.activity = Activity::Idle;
        if (p.material != none) {
          items[p.material].consumed = true;
          items[p.material].reservedAt = none;
        }
        pay(p.escrow, p.contractor, accounts[p.escrow].balance, "housing-contractor-profit");
        invalidateGeometry(p.floor);
        repriceHousing();
        log("housing.work_completed", r.id, p.id);
      }
    }
}
} // namespace mega
