#include "mega/world.hpp"
#include <algorithm>
namespace mega {
Money World::expansionQuote(int floor, int width, int depth) const {
  if (config.layoutVersion < 2 || floor < 0 || floor >= config.floors || width < 96 || depth < 72 ||
      width > 2048 || depth > 2048)
    throw std::runtime_error("Invalid floor expansion");
  auto current = layout(floor);
  int oldW = int(current.halfWidth() * 2), oldD = int(current.halfDepth() * 2);
  if (!floorPlans[floor].deck)
    throw std::runtime_error("structure.error.floor");
  if (width < oldW || depth < oldD || (width == oldW && depth == oldD))
    throw std::runtime_error("Expansion must increase the floor area");
  return (Money(width) * depth - Money(oldW) * oldD) * 3500;
}
Id World::commissionExpansion(Id actor, int floor, int width, int depth) {
  if (actor != dormOwner && config.mode != Mode::Cheat)
    throw std::runtime_error("Only the dormitory owner can expand its structure");
  if (!residents.at(actor).alive || !residents[actor].present)
    throw std::runtime_error("Resident cannot commission construction");
  for (auto &p : expansions)
    if (p.floor == floor && !p.complete)
      throw std::runtime_error("This floor already has an expansion contract");
  if (structuralFloorLocked(floor))
    throw std::runtime_error("structure.error.busy");
  Money quote = expansionQuote(floor, width, depth);
  Id workshop = nearestAt({0, floor * 3.f, 0}, FacilityKind::Workshop);
  if (workshop == none)
    throw std::runtime_error("No construction workshop is available");
  Id payer = actor == dormOwner ? dormAccount : residents[actor].account;
  if (accounts[payer].balance < quote)
    throw std::runtime_error("Insufficient funds for this expansion contract");
  Expansion p;
  p.id = Id(expansions.size());
  p.owner = payer;
  p.contractor = facilities[workshop].owner;
  p.floor = floor;
  p.width = width;
  p.depth = depth;
  p.budget = quote;
  p.escrow = account("Floor expansion escrow");
  p.totalWork = p.remaining = double(quote) / 3500 * 1.8;
  auto l = layout(floor);
  p.site = {l.halfWidth() - 4, floor * 3.f, l.halfDepth() - 8};
  pay(payer, p.escrow, quote, "expansion-funding");
  expansions.push_back(p);
  return p.id;
}
void World::assignExpansion(Id worker, Id id) {
  auto &r = residents.at(worker);
  auto &p = expansions.at(id);
  if (!r.alive || !r.present || r.role != Role::Builder || builderCommitted(worker) ||
      p.worker != none || p.complete)
    throw std::runtime_error("Construction worker unavailable");
  for (auto &other : expansions)
    if (other.worker == worker && !other.complete)
      throw std::runtime_error("Worker already has a construction contract");
  clockOut(worker);
  p.worker = worker;
  r.employer = p.contractor;
  travel(worker, p.site, TaskKind::None, none);
}
void World::beginExpansionWork(Id worker, Id id) {
  auto &p = expansions.at(id);
  auto &r = residents.at(worker);
  if (p.complete || p.worker != worker || !r.alive || !r.present || !near(worker, p.site, 2) ||
      !onShift(worker))
    throw std::runtime_error("Reach the construction site during your working shift");
  if (p.workAt < 0)
    p.workAt = time;
  r.activity = Activity::Working;
}
void World::workExpansion(Id id, double seconds) {
  auto &p = expansions.at(id);
  if (!std::isfinite(seconds) || seconds < 0 || seconds > 3600)
    throw std::runtime_error("Invalid construction interval");
  if (p.complete || p.worker == none)
    return;
  auto &r = residents[p.worker];
  if (!r.alive || !r.present || r.role != Role::Builder || !onShift(r.id) ||
      !near(r.id, p.site, 2) || r.activity != Activity::Working) {
    p.workAt = -1;
    return;
  }
  if (p.workAt < 0) {
    p.workAt = time;
    return;
  }
  seconds = std::min(seconds, std::max(0.0, time - p.workAt));
  p.workAt = time;
  p.remaining = std::max(0.0, p.remaining - seconds);
  Money earned = p.remaining == 0 ? p.budget : Money(p.budget * (1 - p.remaining / p.totalWork));
  Money payout = earned - p.paid;
  pay(p.escrow, p.contractor, payout, "construction-progress");
  p.paid = earned;
  Money wage = std::min(accounts[p.contractor].balance, Money(seconds / 3600 * r.hourlyWage));
  pay(p.contractor, r.account, wage, "construction-wage");
  if (p.remaining == 0) {
    floorPlans.at(p.floor).width = p.width;
    floorPlans.at(p.floor).depth = p.depth;
    p.complete = true;
    r.activity = Activity::Idle;
    invalidateGeometry(p.floor);
    log("construction.expanded", r.id, p.floor);
  }
}

void World::invalidateGeometry(int floor) {
  obstacles.erase(floor);
  navGrids.erase(floor);
  std::erase_if(obstacleCells, [&](const auto &cell) { return int(cell.first >> 26) == floor; });
}
Id World::commissionFacility(Id owner, FacilityKind kind, int floor, Vec3 pos, int stations,
                             Id company) {
  auto &r = residents.at(owner);
  if (!r.alive || !r.present || floor < 0 || floor >= config.floors || stations < 1 ||
      stations > 16 || kind < FacilityKind::Food || kind > FacilityKind::Washbasin)
    throw std::runtime_error("Invalid facility construction plan");
  if (!std::isfinite(pos.x) || !std::isfinite(pos.z))
    throw std::runtime_error("Invalid construction location");
  if (!floorPlans[floor].deck || structuralFloorLocked(floor))
    throw std::runtime_error("structure.error.floor");
  pos.y = floor * 3.f;
  // Occupied housing is never silently erased to make room for a new business.
  for (float x = -5; x <= 5; x += 1)
    for (float z = -2; z <= 11; z += 1)
      if (!clearPosition(pos + Vec3{x, 0, z}))
        throw std::runtime_error("Choose a clear 10 × 13 m plot with safe queue access");
  for (auto &f : facilities)
    if (f.floor == floor && std::abs(f.pos.x - pos.x) < 11 && std::abs(f.pos.z - pos.z) < 14)
      throw std::runtime_error("Another business or construction plot occupies this space");
  Id source = nearestAt(pos, FacilityKind::Workshop);
  if (source == none)
    throw std::runtime_error("A construction workshop is required");
  Money budget = 300000 + stations * 100000, permit = 40000;
  if (company != none && (!directsCompany(owner, company, true) || kind != FacilityKind::Office))
    throw std::runtime_error("Only the company owner may fund its office construction");
  Id payer = company != none      ? companies[company].account
             : owner == dormOwner ? dormAccount
                                  : r.account;
  if (accounts[payer].balance < budget + permit)
    throw std::runtime_error("Cannot fund the construction and plot permit");
  Facility f;
  f.id = Id(facilities.size());
  f.kind = kind;
  f.owner = payer;
  f.manager = owner;
  f.floor = floor;
  f.pos = pos;
  f.open = false;
  f.stations = stations;
  f.stock = 200;
  f.askingPrice = 0;
  f.price = (kind == FacilityKind::Food || kind == FacilityKind::Cafe) ? 450
            : kind == FacilityKind::Water                              ? 30
            : kind == FacilityKind::Toilet                             ? 15
            : kind == FacilityKind::Shop                               ? 800
                                                                       : 100;
  f.duration = kind == FacilityKind::Food     ? 210
               : kind == FacilityKind::Water  ? 40
               : kind == FacilityKind::Toilet ? 270
               : kind == FacilityKind::Shop   ? 90
                                              : 180;
  f.staffRole = kind == FacilityKind::FireStation                          ? Role::Firefighter
                : kind == FacilityKind::Food || kind == FacilityKind::Cafe ? Role::Chef
                : kind == FacilityKind::Morgue || kind == FacilityKind::Crematorium ? Role::Cleaner
                : kind == FacilityKind::Infirmary                                   ? Role::Medic
                : kind == FacilityKind::Shop || kind == FacilityKind::Dealership    ? Role::Vendor
                : kind == FacilityKind::Pool      ? Role::Lifeguard
                : kind == FacilityKind::Cinema    ? Role::Attendant
                : kind == FacilityKind::School    ? Role::Teacher
                : kind == FacilityKind::Reception ? Role::Administrator
                                                  : Role::Unemployed;
  f.staffNeeded = f.staffRole == Role::Unemployed || kind == FacilityKind::FireStation ? 0 : 1;
  if (kind == FacilityKind::Elevator)
    for (int n = floor; n < config.floors && n < floor + 100; n += 10)
      f.stops.push_back(n);
  Project p;
  p.id = Id(projects.size());
  p.owner = owner;
  p.facility = f.id;
  p.budget = budget;
  p.state = ProjectState::Funded;
  p.newConstruction = true;
  p.source = source;
  p.contractor = facilities[source].owner;
  p.materials = 3 + stations;
  p.remaining = 6 * 3600 + stations * 1800;
  p.escrow = account("New facility construction escrow");
  pay(payer, p.escrow, budget, "new-facility-construction");
  pay(payer, dormAccount, permit, "business-plot-permit");
  facilities.push_back(f);
  floorFacilities[floor].push_back(f.id);
  projects.push_back(p);
  invalidateGeometry(floor);
  log("build.facility_commissioned", owner, p.id);
  return p.id;
}
void World::collectMaterials(Id worker, Id id) {
  auto &p = projects.at(id);
  auto &r = residents.at(worker);
  if (!p.newConstruction || p.state != ProjectState::Working || p.worker != worker ||
      p.materialsDelivered || !r.alive || !r.present || r.carrying != none)
    throw std::runtime_error("Construction materials cannot be collected");
  if (p.materialItem == none) {
    auto &source = facilities.at(p.source);
    if (!operational(source.id) || !near(worker, source.pos) || source.stock < p.materials)
      throw std::runtime_error("Reach a stocked construction workshop");
    Money cost = Money(p.materials) * 20000;
    pay(p.escrow, source.owner, cost, "construction-materials");
    p.spent += cost;
    source.stock -= p.materials;
    Item item;
    item.id = Id(items.size());
    item.kind = ItemKind::Materials;
    item.owner = facilities[p.facility].owner;
    item.pos = position(worker);
    item.carrier = worker;
    item.reservedAt = p.facility;
    p.materialItem = item.id;
    items.push_back(item);
  } else {
    auto &item = items.at(p.materialItem);
    if (item.carrier != none || item.consumed || !near(worker, item.pos))
      throw std::runtime_error("Reach the dropped construction materials");
    item.carrier = worker;
  }
  r.carrying = p.materialItem;
  travel(worker, facilities[p.facility].pos + Vec3{0, 0, 1.6f}, TaskKind::ConstructionDrop, id);
}
void World::deliverMaterials(Id worker, Id id) {
  auto &p = projects.at(id);
  auto &r = residents.at(worker);
  if (!p.newConstruction || p.worker != worker || p.materialItem == none ||
      r.carrying != p.materialItem || items[p.materialItem].carrier != worker ||
      !near(worker, facilities[p.facility].pos))
    throw std::runtime_error("Carry the materials to the actual construction site");
  auto &item = items[p.materialItem];
  item.carrier = none;
  item.pos = facilities[p.facility].pos;
  r.carrying = none;
  p.materialsDelivered = true;
  beginProjectWork(worker, id);
}
void World::beginProjectWork(Id worker, Id id) {
  auto &p = projects.at(id);
  auto &r = residents.at(worker);
  if (p.worker != worker || p.state != ProjectState::Working || !r.alive || !r.present ||
      !near(worker, facilities[p.facility].pos) ||
      (p.newConstruction && (!p.materialsDelivered || !onShift(worker))))
    throw std::runtime_error("Reach the supplied construction site during your shift");
  if (p.workAt < 0)
    p.workAt = time;
  r.activity = Activity::Working;
}
} // namespace mega
