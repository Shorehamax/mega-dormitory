#include "mega/world.hpp"
#include <algorithm>
#include <cmath>
namespace mega {
bool World::hasDevice(Id id) const {
  const auto &r = residents.at(id);
  if (!r.alive || !r.present)
    return false;
  for (Id i : r.items)
    if (i >= 0 && std::size_t(i) < items.size() && !items[i].consumed &&
        items[i].owner == r.account && items[i].carrier == id && items[i].durability > 0 &&
        (items[i].kind == ItemKind::Phone || items[i].kind == ItemKind::Laptop))
      return true;
  return r.terminal != none && r.terminalUntil > time && facilities.at(r.terminal).open &&
         near(id, facilities[r.terminal].pos);
}
void World::useTerminal(Id id, Id fid, double minutes) {
  auto &r = residents.at(id);
  auto &f = facilities.at(fid);
  if (!r.alive || !r.present || !operational(fid) || !accessAllowed(id, fid) || !near(id, f.pos) ||
      !(f.kind == FacilityKind::Terminal || f.kind == FacilityKind::Office) ||
      !std::isfinite(minutes) || minutes <= 0 || minutes > 120)
    throw std::runtime_error("Computer unavailable");
  if (f.kind == FacilityKind::Office && r.employer != operatingAccount(fid))
    throw std::runtime_error("This is an employee computer");
  int occupied = 0;
  for (const auto &other : residents)
    if (other.id != id && other.alive && other.present && other.terminal == fid &&
        other.terminalUntil > time && near(other.id, f.pos, 3))
      ++occupied;
  if (occupied >= f.stations)
    throw std::runtime_error("Computer unavailable");
  Money cost = f.kind == FacilityKind::Office ? 0 : Money(std::ceil(minutes)) * f.price;
  pay(r.account, operatingAccount(fid), cost, "computer-access");
  r.terminal = fid;
  r.terminalUntil = time + minutes * 60;
}
void World::changeClothes(Id id, Id garment) {
  auto &r = residents.at(id);
  auto &i = items.at(garment);
  if (!r.alive || !r.present || i.kind != ItemKind::Clothes || i.owner != r.account ||
      i.carrier != id || i.reservedAt != none || i.consumed || r.activity == Activity::Using)
    throw std::runtime_error("Clothing unavailable");
  r.outfit = garment;
}
void World::sendMessage(Id from, Id to, const std::string &text) {
  if (!hasDevice(from) || to < 0 || std::size_t(to) >= residents.size() || !residents[to].alive ||
      !residents[to].present || text.empty() || text.size() > 2000)
    throw std::runtime_error("Cannot send message");
  messages.push_back({from, to, time, text, false});
  log("message.sent", from, to);
}
Id World::order(Id buyer, Id shop, ItemKind product) {
  if (!hasDevice(buyer))
    throw std::runtime_error("A phone, laptop or paid computer is required");
  auto &f = facilities.at(shop);
  if (!f.open || f.kind != FacilityKind::Shop || f.stock <= 0)
    throw std::runtime_error("Shop out of stock");
  if (product != ItemKind::Food && product != ItemKind::Phone && product != ItemKind::Laptop &&
      product != ItemKind::Medicine && product != ItemKind::Tools && product != ItemKind::Handgun &&
      product != ItemKind::Ammunition && product != ItemKind::Extinguisher &&
      product != ItemKind::Fan && product != ItemKind::Tableware &&
      product != ItemKind::RiceCooker && product != ItemKind::Clothes)
    throw std::runtime_error("Product unavailable");
  Money price = productPrice(product);
  Money fee = deliveryPrice(buyer, shop);
  if (accounts[residents[buyer].account].balance < price + fee)
    throw std::runtime_error("Insufficient funds");
  Order o;
  o.id = static_cast<Id>(orders.size());
  o.buyer = buyer;
  o.shop = shop;
  o.price = price;
  o.deliveryFee = fee;
  o.product = product;
  o.placed = time;
  o.escrow = account("Order escrow " + std::to_string(o.id));
  pay(residents[buyer].account, o.escrow, price + fee, "order-escrow");
  --f.stock;
  Item item;
  item.id = static_cast<Id>(items.size());
  item.kind = product;
  item.quantity = product == ItemKind::Ammunition ? 12 : 1;
  item.owner = residents[buyer].account;
  item.pos = f.pos;
  item.order = o.id;
  o.item = item.id;
  items.push_back(item);
  orders.push_back(o);
  log("order.placed", buyer, o.id);
  dispatchOrder(o.id);
  return o.id;
}
void World::assignDelivery(Id oid, Id courier) {
  auto &o = orders.at(oid);
  auto &r = residents.at(courier);
  if (o.state != OrderState::Waiting || r.role != Role::Courier || !r.alive || !r.present ||
      r.carrying != none || r.task == TaskKind::DeliveryPickup ||
      r.activity == Activity::Fighting || r.activity == Activity::Jailed)
    throw std::runtime_error("Courier unavailable");
  if (std::any_of(orders.begin(), orders.end(), [&](const Order &other) {
        return other.id != oid && other.courier == courier &&
               (other.state == OrderState::ToPickup || other.state == OrderState::ToCustomer);
      }))
    throw std::runtime_error("Courier already has a parcel assignment");
  travel(courier, facilities[o.shop].pos + Vec3{1.6f, 0, 0}, TaskKind::DeliveryPickup, oid);
  o.courier = courier;
  o.state = OrderState::ToPickup;
}
void World::pickup(Id oid, Id courier) {
  auto &o = orders.at(oid);
  auto &r = residents.at(courier);
  if (o.courier != courier || o.state != OrderState::ToPickup || !r.alive || !r.present ||
      !near(courier, items[o.item].pos) || r.carrying != none)
    throw std::runtime_error("Courier must reach the parcel");
  if (!operational(o.shop)) {
    r.activity = Activity::Idle;
    log("facility.unstaffed", courier, o.shop);
    return;
  }
  r.carrying = o.item;
  items[o.item].carrier = courier;
  o.state = OrderState::ToCustomer;
  routeDelivery(oid, false);
  log("delivery.picked_up", courier, o.buyer);
}
void World::deliver(Id oid, Id courier) {
  auto &o = orders.at(oid);
  auto &r = residents.at(courier);
  auto &buyer = residents.at(o.buyer);
  if (o.state != OrderState::ToCustomer || o.courier != courier || r.carrying != o.item ||
      items[o.item].carrier != courier || !r.alive || !buyer.alive || !buyer.present ||
      !near(courier, position(o.buyer)))
    throw std::runtime_error("Physical handover to the recipient is required");
  pay(o.escrow, operatingAccount(o.shop), o.price, "product-sale");
  pay(o.escrow, r.account, o.deliveryFee, "delivery-pay");
  items[o.item].carrier = o.buyer;
  items[o.item].pos = position(o.buyer);
  buyer.items.push_back(o.item);
  r.carrying = none;
  o.state = OrderState::Delivered;
  o.delivered = time;
  if (auto plan = residentPlans.find(o.buyer);
      plan != residentPlans.end() && plan->second.order == o.id)
    plan->second.action = AutonomyAction::Received;
  r.activity = Activity::Idle;
  r.task = TaskKind::None;
  log("delivery.completed", courier, o.buyer);
}
void World::cancelOrder(Id oid, Id buyer) {
  auto &o = orders.at(oid);
  if (o.buyer != buyer || (o.state != OrderState::Waiting && o.state != OrderState::ToPickup))
    throw std::runtime_error("Order cannot be cancelled after pickup");
  pay(o.escrow, residents[buyer].present ? residents[buyer].account : 0, o.price + o.deliveryFee,
      "order-refund");
  ++facilities[o.shop].stock;
  items[o.item].consumed = true;
  o.state = OrderState::Cancelled;
  if (auto plan = residentPlans.find(o.buyer);
      plan != residentPlans.end() && plan->second.order == o.id)
    plan->second.action = AutonomyAction::Cancelled;
  if (o.courier != none) {
    auto &r = residents[o.courier];
    r.pos = position(r.id);
    r.route.clear();
    ++r.routeVersion;
    r.task = TaskKind::None;
    r.activity = Activity::Idle;
    r.carrying = none;
  }
}
void World::chooseJob(Id id, Role role, Id fid) {
  auto &r = residents.at(id);
  auto &f = facilities.at(fid);
  if (!r.alive || !r.present || !f.open || r.carrying != none || r.vehicle != none ||
      r.activity == Activity::Jailed || role == Role::Unemployed || activeFires.contains(fid))
    throw std::runtime_error("Cannot start this job");
  if (!f.hiring && r.jobSite != fid)
    throw std::runtime_error("This employer is not hiring");
  bool ok =
      (role == Role::Courier && f.kind == FacilityKind::Shop) ||
      (role == Role::Firefighter && f.kind == FacilityKind::FireStation) ||
      (role == Role::Warden && f.kind == FacilityKind::Jail) ||
      (role == Role::Cleaner &&
       (f.kind == FacilityKind::Garbage || f.kind == FacilityKind::Morgue ||
        f.kind == FacilityKind::Crematorium)) ||
      ((role == Role::Builder || role == Role::Repairer || role == Role::TowDriver) &&
       f.kind == FacilityKind::Workshop) ||
      ((role == Role::VideoEditor || role == Role::Programmer || role == Role::Administrator) &&
       f.kind == FacilityKind::Office) ||
      ((role == Role::Medic || role == Role::Pilot || role == Role::Warden) &&
       f.kind == FacilityKind::Infirmary) ||
      (f.staffNeeded && role == f.staffRole);
  if (!ok)
    throw std::runtime_error("Workplace does not offer this job");
  clockOut(id);
  if (r.jobSite != none)
    std::erase(facilities[r.jobSite].employees, id);
  r.role = role;
  r.jobSite = fid;
  if (std::find(f.employees.begin(), f.employees.end(), id) == f.employees.end())
    f.employees.push_back(id);
  r.routine.shiftStart = f.openHours == 24 ? 6 : f.opensAt;
  if (f.kind == FacilityKind::Office)
    r.routine.shiftStart = 9;
  r.routine.shiftHours = 8;
  r.routine.sleepStart = 22;
  r.routine.daysOff = f.kind == FacilityKind::Office ? 96 : 0;
  r.employer = operatingAccount(fid);
  r.hourlyWage = role == Role::Warden                                    ? 1400
                 : role == Role::Programmer || role == Role::Firefighter ? 1800
                                                                         : 900;
  log("job.accepted", id, fid);
}
void World::buyFacility(Id id, Id fid) {
  auto &r = residents.at(id);
  auto &f = facilities.at(fid);
  if (!r.alive || !r.present || f.askingPrice <= 0 || f.owner == r.account)
    throw std::runtime_error("Facility is not for sale");
  if (activeFires.contains(fid))
    throw std::runtime_error("Resolve the active fire before changing this property");
  if (leasedWorkspaces.contains(fid))
    throw std::runtime_error("Return the leased workspace before selling its property");
  for (const auto &p : projects)
    if (p.facility == fid && p.state != ProjectState::Completed)
      throw std::runtime_error(
          "Finish the existing construction contract before selling this facility");
  pay(r.account, f.owner, f.askingPrice, "facility-purchase");
  workspaceOffers.erase(fid);
  if (f.kind == FacilityKind::Office)
    changeOfficeEmployer(fid, r.account);
  for (Id employee : f.employees)
    clockOut(employee);
  f.owner = r.account;
  for (Id employee : f.employees)
    residents[employee].employer = f.owner;
  f.manager = id;
  f.askingPrice = 0;
  log("facility.purchased", id, fid);
}
void World::subscribeLift(Id id, Id fid) {
  auto &f = facilities.at(fid);
  auto &r = residents.at(id);
  if (!r.alive || !r.present ||
      (f.kind != FacilityKind::Elevator && f.kind != FacilityKind::Ramp &&
       f.kind != FacilityKind::FreightLift) ||
      !f.open)
    throw std::runtime_error("Lift unavailable");
  pay(r.account, f.owner, f.subscriptionPrice, "lift-subscription");
  f.subscribers[id] = std::max(time, f.subscribers[id]) + 30 * 86400;
}
void World::payLift(Id id, Id fid, int destination) {
  auto &f = facilities.at(fid);
  auto &r = residents.at(id);
  int current = int(std::round(position(id).y / 3));
  if (!r.alive || !r.present ||
      (f.kind != FacilityKind::Elevator && f.kind != FacilityKind::Ramp &&
       f.kind != FacilityKind::FreightLift) ||
      !f.open || std::find(f.stops.begin(), f.stops.end(), current) == f.stops.end() ||
      std::find(f.stops.begin(), f.stops.end(), destination) == f.stops.end())
    throw std::runtime_error("Lift does not serve these floors");
  Id payer = r.account;
  if ((f.kind == FacilityKind::Ramp || f.kind == FacilityKind::FreightLift) && r.vehicle != none &&
      vehicles[r.vehicle].towTruck && r.employer == vehicles[r.vehicle].owner)
    payer = r.employer;
  if (!f.whitelist.contains(id) && !(f.subscribers.contains(id) && f.subscribers.at(id) > time) &&
      payer != f.owner)
    pay(payer, f.owner, f.price, "lift-fare");
  log("lift.access_paid", id, fid);
}
Money World::housingPrice(int floor) const {
  if (floor < 0 || floor >= config.floors)
    throw std::runtime_error("Invalid floor");
  std::map<FacilityKind, int> closest;
  for (auto &f : facilities)
    if (f.open) {
      int d = std::abs(f.floor - floor);
      if (!closest.contains(f.kind) || d < closest[f.kind])
        closest[f.kind] = d;
    }
  double amenity = 0;
  for (auto kind :
       {FacilityKind::Food, FacilityKind::Water, FacilityKind::Toilet, FacilityKind::Shop,
        FacilityKind::Office, FacilityKind::Infirmary, FacilityKind::Cinema, FacilityKind::Pool})
    if (closest.contains(kind))
      amenity += std::exp(-closest[kind] / 2.5) * .25;
  return Money(std::llround(200000 * (1 + amenity)));
}
void World::repriceHousing() {
  std::map<int, Money> cache;
  for (auto &h : housing)
    if (h.active) {
      if (!cache.contains(h.floor))
        cache[h.floor] = housingPrice(h.floor);
      h.price = cache[h.floor]; /* Existing rents remain contracted; this is the
                                   next listing's price. */
      if (h.tenant == none)
        h.rent = h.price / 100;
    }
}
void World::rentHome(Id id, Id unit) {
  auto &r = residents.at(id);
  auto &h = housing.at(unit);
  if (!r.alive || !r.present || !h.active || (h.tenant != none && h.tenant != id))
    throw std::runtime_error("Home unavailable");
  for (const auto &p : housingWorks)
    if (p.home == unit && p.kind == HousingWorkKind::RemoveBed &&
        (p.state == ProjectState::Funded || p.state == ProjectState::Working))
      throw std::runtime_error("This bed is reserved for removal");
  if (h.tenant == id)
    return;
  pay(r.account, h.owner, h.rent, "housing-rent");
  Id old = residentHomes.at(id);
  if (old != none)
    housing[old].tenant = none;
  h.tenant = id;
  h.paidUntil = time + 86400;
  h.arrears = 0;
  residentHomes[id] = unit;
  r.home = h.floor;
  log("housing.rented", id, unit);
}
void World::buyHome(Id id, Id unit) {
  auto &r = residents.at(id);
  auto &h = housing.at(unit);
  if (!r.alive || !r.present || !h.active || !h.listed || h.owner == r.account)
    throw std::runtime_error("Home not for sale");
  pay(r.account, h.owner, h.price, "housing-purchase");
  h.owner = r.account;
  h.listed = false;
  log("housing.bought", id, unit);
}
Id World::commissionVideo(Id worker, Id client, Id employer, Money budget) {
  auto &r = residents.at(worker);
  if (r.role != Role::VideoEditor || r.employer != employer || !r.alive || !r.present ||
      budget < 1000 || client < 1 || std::size_t(client) >= accounts.size() ||
      accounts[client].balance < budget)
    throw std::runtime_error("Invalid work commission");
  VideoJob j;
  j.id = static_cast<Id>(videos.size());
  j.worker = worker;
  j.client = client;
  j.employer = employer;
  j.budget = budget;
  j.deadline = time + 8 * 3600;
  j.escrow = account("Video commission escrow");
  pay(client, j.escrow, budget, "commission-escrow");
  videos.push_back(j);
  return j.id;
}
float World::scoreVideo(const std::vector<int> &clips) const {
  if (clips.empty() || clips.size() > 6)
    return 0;
  std::set<int> seen;
  float score = 0;
  for (int c : clips) {
    if (c < 0 || c > 5 || !seen.insert(c).second)
      return 0;
    score += 8;
  }
  if (clips.front() == 0)
    score += 15;
  if (clips.back() == 5)
    score += 15;
  for (std::size_t i = 1; i < clips.size(); ++i)
    if (clips[i] > clips[i - 1])
      score += 4;
  if (seen.contains(1) && seen.contains(2) && seen.contains(3))
    score += 10;
  return std::min(100.f, score);
}
void World::submitVideo(Id id, const std::vector<int> &clips) {
  auto &v = videos.at(id);
  auto &r = residents.at(v.worker);
  if (v.submitted || !r.alive || !r.present || r.role != Role::VideoEditor ||
      r.employer != v.employer || !hasDevice(v.worker) ||
      !near(v.worker, facilities[r.jobSite].pos))
    throw std::runtime_error("Reach your workplace with computer access");
  v.score = scoreVideo(clips) * (time > v.deadline ? .65f : 1.f);
  Money revenue = Money(v.budget * v.score / 100), wage = revenue / 3;
  pay(v.escrow, v.employer, revenue - wage, "video-project-revenue");
  pay(v.escrow, r.account, wage, "video-project-pay");
  pay(v.escrow, v.client, v.budget - revenue, "quality-refund");
  v.timeline = clips;
  v.submitted = true;
  log("video.submitted", v.worker, id);
}
Id World::proposeProject(Id owner, Id fid, bool demolition, Money budget) {
  auto &r = residents.at(owner);
  auto &f = facilities.at(fid);
  if (f.kind == FacilityKind::Ramp || f.kind == FacilityKind::FreightLift)
    throw std::runtime_error("traffic.error.road_work");
  bool owns = f.owner == r.account || directsCompany(owner, companyForAccount(f.owner), true);
  if (!r.alive || !r.present || f.demolished || (!owns && owner != dormOwner) || budget < 10000)
    throw std::runtime_error("Permission and a real budget are required");
  if (activeFires.contains(fid))
    throw std::runtime_error("Resolve the active fire before changing this property");
  if (leasedWorkspaces.contains(fid))
    throw std::runtime_error("An active workspace lease must end before renovation or demolition");
  for (auto &existing : projects)
    if (existing.facility == fid && existing.state != ProjectState::Completed)
      throw std::runtime_error("A project is already active at this facility");
  Project p;
  p.id = static_cast<Id>(projects.size());
  p.owner = owner;
  p.facility = fid;
  p.budget = budget;
  p.demolition = demolition;
  p.escrow = account("Construction escrow");
  p.state = ProjectState::AwaitingConsent;
  for (auto &h : housing)
    if (h.active && h.floor == f.floor && h.tenant != none)
      p.affected.insert(h.tenant);
  if (!owns && f.owner != dormAccount) {
    for (auto &resident : residents)
      if (resident.account == f.owner)
        p.affected.insert(resident.id);
  }
  // A dorm manager cannot bypass a private business owner's consent.
  if (!owns && f.owner != dormAccount && f.manager != none)
    p.affected.insert(f.manager);
  p.compensation = Money(p.affected.size()) * 5000;
  projects.push_back(p);
  log("construction.proposed", owner, p.id);
  return p.id;
}
void World::consentProject(Id id, Id resident) {
  auto &p = projects.at(id);
  if (p.state != ProjectState::AwaitingConsent || !p.affected.contains(resident) ||
      !residents.at(resident).alive || !residents.at(resident).present)
    throw std::runtime_error("No outstanding consent for this resident");
  p.consented.insert(resident);
}
void World::fundProject(Id id, Id owner) {
  auto &p = projects.at(id);
  if (p.owner != owner || p.state != ProjectState::AwaitingConsent || p.consented != p.affected)
    throw std::runtime_error("All affected occupants must consent");
  if (activeFires.contains(p.facility))
    throw std::runtime_error("Construction must wait until the fire and smoke are cleared");
  if (leasedWorkspaces.contains(p.facility))
    throw std::runtime_error("An active workspace lease protects this property");
  Id company = companyForAccount(facilities[p.facility].owner);
  Id from = directsCompany(owner, company, true) ? companies[company].account
            : owner == dormOwner                 ? dormAccount
                                                 : residents[owner].account;
  pay(from, p.escrow, p.budget + p.compensation, "construction-budget");
  for (Id r : p.affected)
    pay(p.escrow, residents[r].account, p.offer, "renovation-compensation");
  p.state = ProjectState::Funded;
  if (p.demolition) {
    auto &f = facilities[p.facility];
    f.open = false;
    auto waiting = f.queue;
    for (auto &s : f.serving)
      waiting.push_back(s.resident);
    for (Id who : waiting)
      leaveQueue(who);
  }
  log("construction.funded", owner, id);
}
void World::assignBuilder(Id id, Id worker) {
  auto &p = projects.at(id);
  auto &r = residents.at(worker);
  if (p.state != ProjectState::Funded || p.worker != none || r.role != Role::Builder ||
      builderCommitted(worker) || !r.alive || !r.present)
    throw std::runtime_error("Builder unavailable");
  if (r.carrying != none)
    throw std::runtime_error("Put down the current parcel before accepting construction");
  p.worker = worker;
  p.state = ProjectState::Working;
  if (p.newConstruction && !p.materialsDelivered) {
    Vec3 pickup = p.materialItem == none ? facilities[p.source].pos + Vec3{0, 0, 1.6f}
                                         : items[p.materialItem].pos;
    travel(worker, pickup, TaskKind::ConstructionPickup, p.id);
  } else
    travel(worker, facilities[p.facility].pos, TaskKind::Work, p.facility);
}
void World::workProject(Id id, double seconds) {
  auto &p = projects.at(id);
  if (p.state != ProjectState::Working || p.worker == none || !std::isfinite(seconds) ||
      seconds <= 0 || seconds > 3600)
    return;
  auto &r = residents[p.worker];
  if (!r.alive || !r.present || r.activity != Activity::Working ||
      !near(r.id, facilities[p.facility].pos)) {
    if (p.newConstruction)
      p.workAt = -1;
    return;
  }
  if (p.newConstruction) {
    if (!p.materialsDelivered || !onShift(r.id)) {
      p.workAt = -1;
      return;
    }
    if (p.workAt < 0) {
      p.workAt = time;
      return;
    }
    seconds = std::min(seconds, std::max(0.0, time - p.workAt));
    p.workAt = time;
  }
  double worked = std::min(seconds, p.remaining);
  Money cost = Money(std::ceil(worked / 3600 * r.hourlyWage));
  if (accounts[p.escrow].balance < cost)
    return;
  pay(p.escrow, r.account, cost, "construction-wages");
  p.spent += cost;
  p.remaining -= worked;
  if (p.remaining <= 0) {
    auto &f = facilities[p.facility];
    if (p.demolition && (!f.queue.empty() || !f.serving.empty())) {
      p.remaining = 60;
      return;
    }
    f.open = !p.demolition;
    f.demolished = p.demolition;
    if (!p.demolition && !p.newConstruction) {
      ++f.stations;
      f.stock += 100;
    }
    p.state = ProjectState::Completed;
    if (p.newConstruction) {
      if (p.materialItem != none) {
        items[p.materialItem].consumed = true;
        items[p.materialItem].reservedAt = none;
      }
      pay(p.escrow, p.contractor, accounts[p.escrow].balance, "construction-company-profit");
    } else {
      Id company = companyForAccount(f.owner);
      Id refund = directsCompany(p.owner, company, true) ? companies[company].account
                  : p.owner == dormOwner                 ? dormAccount
                                                         : residents[p.owner].account;
      pay(p.escrow, refund, accounts[p.escrow].balance, "construction-unspent-budget");
    }
    if (f.open && f.kind == FacilityKind::Parking)
      generateParkingBays(f.id);
    if (f.open && f.kind == FacilityKind::Elevator)
      generateLifts();
    if (f.open && f.kind == FacilityKind::Infirmary)
      generateMedical();
    invalidateGeometry(f.floor);
    repriceHousing();
    log("construction.completed", r.id, id);
  }
}
} // namespace mega
