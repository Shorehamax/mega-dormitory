#include "mega/world.hpp"
#include <algorithm>
namespace mega {
Id World::requestRepair(Id actor, Id fid) {
  auto &f = facilities.at(fid);
  const auto &r = residents.at(actor);
  if (!r.alive || !r.present || !f.open || f.condition >= .99f ||
      !(f.manager == actor || operatingAccount(fid) == r.account ||
        directsCompany(actor, companyForAccount(operatingAccount(fid))) ||
        (actor == dormOwner && f.owner == dormAccount)))
    throw std::runtime_error("No authorized repair is needed here");
  for (const auto &p : repairs)
    if (p.facility == fid && !p.complete)
      throw std::runtime_error("Repair already commissioned");
  Id workshop = nearestAt(f.pos, FacilityKind::Workshop);
  if (workshop == none)
    throw std::runtime_error("No repair workshop available");
  Repair p;
  p.id = Id(repairs.size());
  p.facility = fid;
  p.workshop = workshop;
  if (accounts[operatingAccount(fid)].balance < p.budget)
    throw std::runtime_error("Business cannot fund repair");
  p.escrow = account("Maintenance contract");
  pay(operatingAccount(fid), p.escrow, p.budget, "repair-escrow");
  repairs.push_back(p);
  return p.id;
}
void World::assignRepair(Id worker, Id id) {
  auto &r = residents.at(worker);
  auto &p = repairs.at(id);
  if (!r.alive || !r.present || r.role != Role::Repairer || !onShift(worker) || p.worker != none ||
      p.complete || r.carrying != none)
    throw std::runtime_error("Repair worker unavailable");
  for (auto &other : repairs)
    if (other.worker == worker && !other.complete)
      throw std::runtime_error("Finish the current repair contract first");
  clockOut(worker);
  p.worker = worker;
  p.workedAt = -1;
  travel(worker, p.parts ? items[p.partItem].pos : facilities[p.workshop].pos, TaskKind::None,
         none);
}
void World::repairTick() {
  for (auto &p : repairs)
    if (!p.complete && p.worker != none) {
      auto &r = residents[p.worker];
      if (!r.alive || !r.present || r.role != Role::Repairer) {
        if (p.partItem != none && items[p.partItem].carrier == r.id) {
          items[p.partItem].pos = position(r.id);
          items[p.partItem].carrier = none;
          r.carrying = none;
        }
        p.worker = none;
        p.workedAt = -1;
        continue;
      }
      if (!onShift(r.id)) {
        p.workedAt = -1;
        continue;
      }
      if (!p.parts) {
        auto &shop = facilities[p.workshop];
        if (!near(r.id, shop.pos, 3) || !operational(shop.id) || shop.stock < 1)
          continue;
        bool tools = false;
        for (Id item : r.items)
          if (items[item].kind == ItemKind::Tools && !items[item].consumed &&
              items[item].carrier == r.id && items[item].owner == r.account)
            tools = true;
        if (!tools) {
          Item tool;
          tool.id = Id(items.size());
          tool.kind = ItemKind::Tools;
          tool.owner = r.account;
          tool.carrier = r.id;
          items.push_back(tool);
          r.items.push_back(tool.id);
        }
        pay(p.escrow, shop.owner, 6000, "repair-parts-and-tools");
        --shop.stock;
        p.parts = true;
        Item part;
        part.id = Id(items.size());
        part.kind = ItemKind::Materials;
        part.owner = facilities[p.facility].owner;
        part.carrier = r.id;
        part.pos = position(r.id);
        part.reservedAt = p.facility;
        p.partItem = part.id;
        items.push_back(part);
        r.carrying = part.id;
        travel(r.id, facilities[p.facility].pos, TaskKind::None, none);
        continue;
      }
      auto &part = items[p.partItem];
      if (part.carrier != r.id) {
        if (part.carrier != none || r.carrying != none || !near(r.id, part.pos))
          continue;
        part.carrier = r.id;
        r.carrying = part.id;
        travel(r.id, facilities[p.facility].pos, TaskKind::None, none);
        continue;
      }
      if (!near(r.id, facilities[p.facility].pos, 3) || r.activity != Activity::Working) {
        p.workedAt = -1;
        continue;
      }
      if (p.workedAt < 0) {
        p.workedAt = time;
        continue;
      }
      p.remaining -= std::min(60.0, std::max(0.0, time - p.workedAt));
      p.workedAt = time;
      if (p.remaining <= 0) {
        p.remaining = 0;
        p.complete = true;
        part.consumed = true;
        part.carrier = none;
        part.reservedAt = none;
        r.carrying = none;
        facilities[p.facility].condition = 1;
        pay(p.escrow, r.account, 6000, "repair-completed-wage");
        pay(p.escrow, facilities[p.workshop].owner, accounts[p.escrow].balance,
            "repair-company-profit");
        r.activity = Activity::Idle;
        log("repair.completed", r.id, p.facility);
        serve(p.facility);
      }
    }
}
} // namespace mega
