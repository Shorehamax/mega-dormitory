#include "mega/world.hpp"
#include <algorithm>

namespace mega {
namespace {
bool able(const World &w, Id who) {
  const auto &r = w.residents.at(who);
  return r.alive && r.present && !r.surrendered && !r.leaving && r.age >= 18 &&
         r.activity != Activity::Sleeping && r.activity != Activity::Fighting &&
         r.activity != Activity::Jailed && r.activity != Activity::Restrained &&
         r.activity != Activity::Escorted && r.carrying == none && r.bodyCarrying == none &&
         r.lift == none && r.ambulance == none && r.carriedBy == none && r.vehicle == none &&
         r.medicalDuty == none && !w.looseBodies.contains(who);
}
bool reach(const World &w, Id actor, Id prisoner) {
  return w.near(actor, w.position(prisoner), 2) &&
         w.lineOfSight(w.position(actor) + Vec3{0, 1.2f, 0},
                       w.position(prisoner) + Vec3{0, 1.2f, 0});
}
} // namespace
void World::startPrisonRescue(Id actor, Id prisoner) {
  const auto &r = residents.at(actor);
  const auto &target = residents.at(prisoner);
  auto found = custodyRecords.find(prisoner);
  if (actor == prisoner || !able(*this, actor) || !target.alive || !target.present ||
      !reach(*this, actor, prisoner) || r.energy < .25f || prisonRescues.contains(actor) ||
      found == custodyRecords.end() || !found->second.active || !found->second.cuffed ||
      (target.activity != Activity::Jailed && target.activity != Activity::Escorted &&
       target.activity != Activity::Restrained))
    throw std::runtime_error("security.error.rescue");
  for (const auto &[other, rescue] : prisonRescues)
    if (rescue.target == prisoner)
      throw std::runtime_error("security.error.rescue");
  Id tool = none;
  for (Id item : r.items) {
    const auto &i = items[item];
    if (i.kind == ItemKind::Tools && i.owner == r.account && i.carrier == actor && !i.consumed &&
        i.durability > .1f && i.reservedAt == none && !itemInUse(item)) {
      tool = item;
      break;
    }
  }
  if (tool == none)
    throw std::runtime_error("security.error.rescue_tools");
  Id victim = found->second.guard;
  if (victim == none || victim == actor)
    victim = prisoner;
  Id incident = recordCrime(CrimeKind::Obstruction, actor, victim);
  // The public case still needs an actual observation/report of this attempt.
  clockOut(actor);
  leaveQueue(actor);
  stopPersonalUse(actor);
  auto &person = residents[actor];
  person.pos = position(actor);
  person.route.clear();
  ++person.routeVersion;
  person.task = TaskKind::PrisonRescue;
  person.target = prisoner;
  person.activity = Activity::Working;
  prisonRescues[actor] = {prisoner, tool, incident, time, 0};
  log("security.rescue_started", actor, prisoner);
}
void World::stopPrisonRescue(Id actor) {
  prisonRescues.erase(actor);
  auto &r = residents.at(actor);
  if (r.task == TaskKind::PrisonRescue) {
    r.task = TaskKind::None;
    r.target = none;
    if (r.activity == Activity::Working)
      r.activity = Activity::Idle;
  }
}
void World::prisonRescueTick() {
  std::vector<Id> stop;
  for (auto &[actor, rescue] : prisonRescues) {
    auto &r = residents[actor];
    const auto &target = residents[rescue.target];
    auto &tool = items[rescue.tool];
    auto custody = custodyRecords.find(rescue.target);
    if (!able(*this, actor) || r.activity != Activity::Working ||
        r.task != TaskKind::PrisonRescue || r.target != rescue.target || !target.alive ||
        !target.present || !reach(*this, actor, rescue.target) || r.hunger > .85f ||
        r.thirst > .85f || r.energy < .2f || custody == custodyRecords.end() ||
        !custody->second.active || !custody->second.cuffed ||
        (target.activity != Activity::Jailed && target.activity != Activity::Escorted &&
         target.activity != Activity::Restrained) ||
        tool.carrier != actor || tool.owner != r.account || tool.consumed ||
        tool.durability <= .01f) {
      stop.push_back(actor);
      continue;
    }
    Id watching = none;
    for (const auto &g : residents)
      if (g.id != actor && g.role == Role::Warden && able(*this, g.id) && onShift(g.id) &&
          near(g.id, position(actor), 4) &&
          lineOfSight(position(g.id) + Vec3{0, 1.5f, 0}, position(actor) + Vec3{0, 1.2f, 0})) {
        watching = g.id;
        break;
      }
    if (watching != none) {
      // An officer arriving later observes the continuing act at this location.
      auto &c = incidents[rescue.incident];
      c.pos = position(actor);
      if (std::none_of(c.witnesses.begin(), c.witnesses.end(),
                       [&](const Testimony &s) { return s.witness == watching; })) {
        Testimony s;
        s.witness = watching;
        s.offenderPosition = position(actor);
        s.seenScene = position(actor);
        s.seenAt = time;
        s.confidence = .95f;
        s.identifies = true;
        if (c.witnesses.size() >= 24)
          c.witnesses.pop_back();
        c.witnesses.push_back(s);
      }
      try {
        fileCrimeReport(watching, c.id);
      } catch (const std::runtime_error &) {
      }
      stop.push_back(actor);
      log("security.rescue_stopped", watching, actor);
      schedule(time + 1, EventKind::Decide, watching);
      continue;
    }
    double dt = std::clamp(time - rescue.workedAt, 0., 60.);
    rescue.workedAt = time;
    rescue.progress += dt;
    tool.durability = std::max(0.f, tool.durability - float(dt / 6000));
    r.energy = std::max(0.f, r.energy - float(dt / 12000));
    if (rescue.progress >= 300) {
      escapeCustody(rescue.target);
      rememberEncounter(actor, rescue.target, EncounterKind::Rescue, .08f, .3f);
      log("security.rescue_completed", actor, rescue.target);
      stop.push_back(actor);
    }
  }
  for (Id actor : stop)
    stopPrisonRescue(actor);
}
void World::validatePrisonRescues() const {
  std::set<Id> targets;
  for (const auto &[actor, rescue] : prisonRescues) {
    auto person = [&](Id id) { return id >= 0 && std::size_t(id) < residents.size(); };
    if (!person(actor) || !person(rescue.target) || actor == rescue.target || rescue.tool < 0 ||
        std::size_t(rescue.tool) >= items.size() || items[rescue.tool].kind != ItemKind::Tools ||
        rescue.incident < 0 || std::size_t(rescue.incident) >= incidents.size() ||
        incidents[rescue.incident].kind != CrimeKind::Obstruction ||
        incidents[rescue.incident].offender != actor || !std::isfinite(rescue.workedAt) ||
        rescue.workedAt < 0 || rescue.workedAt > time || !std::isfinite(rescue.progress) ||
        rescue.progress < 0 || rescue.progress >= 300 || !targets.insert(rescue.target).second)
      throw std::runtime_error("Invalid prisoner intervention");
  }
}
} // namespace mega
