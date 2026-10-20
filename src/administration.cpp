#include "mega/world.hpp"
#include <algorithm>
namespace mega {
void World::prepareAdministration(OfficeOrder &order) {
  order.liveRequests = true;
  order.client = dormAccount;
  order.budget = 0;
  std::set<std::pair<int, Id>> booked;
  for (const auto &other : officeOrders)
    if (other.liveRequests)
      for (int slot = 0; slot < 4; ++slot)
        if (other.tickets[slot] != none &&
            (!other.submitted || (other.outcomes[slot] != 4 && time < other.due)))
          booked.emplace((slot + other.brief) % 4, other.tickets[slot]);
  const auto &office = facilities.at(order.facility);
  std::array<Id, 4> candidates{none, none, none, none};
  std::array<float, 4> distances{1e30f, 1e30f, 1e30f, 1e30f};
  auto consider = [&](int type, Id id, Vec3 p) {
    float distance = length(p - office.pos);
    if (std::abs(p.y - office.pos.y) <= 60 && distance < distances[type] &&
        !booked.contains({type, id})) {
      candidates[type] = id;
      distances[type] = distance;
    }
  };
  std::set<Id> supplied, repaired, medical;
  for (const auto &shipment : freight)
    if (!shipment.waste && shipment.state != FreightState::Delivered)
      supplied.insert(shipment.facility);
  for (const auto &repair : repairs)
    if (!repair.complete)
      repaired.insert(repair.facility);
  for (const auto &rescue : ambulances)
    if (rescue.state != RescueState::Idle)
      medical.insert(rescue.patient);
  for (auto floor = floorFacilities.lower_bound(office.floor - 20);
       floor != floorFacilities.end() && floor->first <= office.floor + 20; ++floor)
    for (Id id : floor->second) {
      const auto &f = facilities[id];
      if (!f.open || f.demolished || f.manager == none || !residents[f.manager].alive)
        continue;
      if (f.autoRestock && f.stock < 100 && !supplied.contains(id))
        consider(0, id, f.pos);
      if (f.condition < .5f && !repaired.contains(id))
        consider(3, id, f.pos);
    }
  for (const auto &incident : incidents)
    if (!incident.resolved && incident.warden == none && incident.kind != CrimeKind::QueueCutting &&
        residents[incident.offender].alive && residents[incident.offender].present)
      consider(1, incident.id, position(incident.offender));
  for (const auto &resident : residents)
    if (resident.alive && resident.present && resident.health < 50 &&
        resident.medicalUntil > time && resident.medicalProvider != none &&
        resident.ambulance == none && resident.carriedBy == none &&
        !medical.contains(resident.id) && hasDevice(resident.id))
      consider(2, resident.id, position(resident.id));
  for (int slot = 0; slot < 4; ++slot) {
    order.tickets[slot] = candidates[(slot + order.brief) % 4];
    if (order.tickets[slot] != none)
      order.budget += 6000;
  }
  if (!order.budget)
    throw std::runtime_error("There are no unassigned local administration requests");
  if (accounts[order.client].balance < order.budget)
    throw std::runtime_error("The dormitory cannot fund an administration commission");
}
void World::dispatchAdministration(OfficeOrder &order, const std::vector<int> &answer) {
  for (int slot = 0; slot < 4; ++slot) {
    Id target = order.tickets[slot];
    if (target == none)
      continue;
    int type = (slot + order.brief) % 4;
    if (answer.size() != 4 || answer[slot] != type) {
      order.outcomes[slot] = 4;
      continue;
    }
    try {
      if (type == 0) {
        const auto &f = facilities.at(target);
        bool supplied = f.stock >= 100;
        for (const auto &s : freight)
          if (!s.waste && s.facility == target && s.state != FreightState::Delivered)
            supplied = true;
        if (supplied) {
          order.outcomes[slot] = 2;
          continue;
        }
        if (!f.autoRestock)
          throw std::runtime_error("Owner withdrew supply authorization");
        order.dispatched[slot] = requestStock(f.manager, target, 500);
      } else if (type == 3) {
        const auto &f = facilities.at(target);
        bool repaired = f.condition >= .5f;
        for (const auto &repair : repairs)
          if (repair.facility == target && !repair.complete)
            repaired = true;
        if (repaired) {
          order.outcomes[slot] = 2;
          continue;
        }
        order.dispatched[slot] = requestRepair(f.manager, target);
      } else if (type == 2) {
        const auto &patient = residents.at(target);
        bool handled = !patient.alive || !patient.present || patient.health >= 50;
        for (const auto &rescue : ambulances)
          if (rescue.patient == target && rescue.state != RescueState::Idle)
            handled = true;
        if (handled) {
          order.outcomes[slot] = 2;
          continue;
        }
        callAmbulance(target);
        for (const auto &rescue : ambulances)
          if (rescue.patient == target && rescue.state != RescueState::Idle)
            order.dispatched[slot] = rescue.id;
      } else {
        auto &incident = incidents.at(target);
        if (incident.resolved || incident.warden != none || !residents[incident.offender].alive ||
            !residents[incident.offender].present) {
          order.outcomes[slot] = 2;
          continue;
        }
        Id guard = none;
        float best = 1e30f;
        Vec3 location = position(incident.offender);
        for (const auto &r : residents) {
          if (!r.alive || !r.present || r.role != Role::Warden || !r.automatic || !onShift(r.id) ||
              r.medicalDuty != none || r.ambulance != none || r.opponent != none ||
              (r.activity != Activity::Idle && r.task != TaskKind::Patrol) ||
              std::abs(position(r.id).y - location.y) > 60)
            continue;
          bool assigned = false;
          for (const auto &c : incidents)
            if (!c.resolved && c.warden == r.id)
              assigned = true;
          float distance = length(position(r.id) - location);
          if (!assigned && distance < best) {
            guard = r.id;
            best = distance;
          }
        }
        if (guard == none)
          throw std::runtime_error("No unassigned on-duty warden is available");
        travel(guard, location, TaskKind::Investigate, target);
        incident.warden = guard;
        order.dispatched[slot] = guard;
      }
      order.outcomes[slot] = 1;
    } catch (const std::runtime_error &) {
      // A correct route cannot create money, staff, stock or consent. Record the blockage
      // so the player can see that the service still needs resources.
      order.outcomes[slot] = 3;
    }
  }
}
} // namespace mega
