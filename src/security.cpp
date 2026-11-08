#include "mega/world.hpp"
#include <algorithm>
#include <cmath>
namespace mega {
namespace {
bool available(const Resident &r) {
  return r.alive && r.present && r.activity != Activity::Jailed &&
         r.activity != Activity::Restrained && r.ambulance == none && r.carriedBy == none;
}
bool guardReady(const World &w, Id id) {
  const auto &r = w.residents.at(id);
  return available(r) && r.role == Role::Warden && !r.surrendered;
}
bool sees(const World &w, Id observer, Vec3 p, float range) {
  auto at = w.position(observer);
  return std::abs(at.y - p.y) < 2.5f && length(at - p) <= range &&
         w.lineOfSight(at + Vec3{0, 1.5f, 0}, p + Vec3{0, 1.2f, 0});
}
} // namespace
bool World::inCustody(Id id) const {
  if (id < 0 || std::size_t(id) >= residents.size())
    return false;
  const auto &r = residents[id];
  auto record = custodyRecords.find(id);
  return r.activity == Activity::Jailed || r.activity == Activity::Restrained ||
         (r.activity == Activity::Escorted && r.task != TaskKind::Escort) ||
         (record != custodyRecords.end() && record->second.active);
}
Id World::recordCrime(CrimeKind kind, Id offender, Id victim, bool threat, Id item) {
  Id id = report(kind, offender, victim, threat, false);
  incidents[id].item = item;
  observeCrime(id);
  return id;
}
void World::observeCrime(Id id) {
  auto &c = incidents.at(id);
  std::vector<std::pair<float, Id>> nearby;
  for (const auto &r : residents)
    if (r.id != c.offender && r.alive && r.present && r.activity != Activity::Sleeping &&
        r.ambulance == none && r.carriedBy == none && r.lift == none &&
        sees(*this, r.id, c.pos, 24))
      nearby.emplace_back(length(position(r.id) - c.pos), r.id);
  std::stable_sort(nearby.begin(), nearby.end(), [&](const auto &a, const auto &b) {
    auto priority = [&](Id person) {
      return residents[person].role == Role::Warden ? 0 : person == c.victim ? 1 : 2;
    };
    return std::pair{priority(a.second), a} < std::pair{priority(b.second), b};
  });
  for (auto [distance, id] : nearby) {
    if (c.witnesses.size() >= 12)
      break;
    bool knows = sees(*this, id, position(c.offender), 24);
    c.witnesses.push_back({id, position(c.offender), time,
                           id == c.victim ? .95f
                           : distance < 8 ? .85f
                                          : .65f,
                           knows, false});
    c.witnesses.back().seenScene = c.pos;
    c.discovered = true;
    observeInformation(id, InformationKind::Witness, c.id);
    if (id == c.victim && (c.kind == CrimeKind::Assault || c.kind == CrimeKind::ExcessiveForce))
      observeInformation(id, InformationKind::Threat, c.id);
  }
  if (c.kind == CrimeKind::Murder) {
    for (auto it = incidents.rbegin(); it != incidents.rend(); ++it)
      if (it->id != c.id && it->offender == c.offender && it->victim == c.victim &&
          it->kind == CrimeKind::Assault && time - it->time < 7200) {
        if (it->reported && it->suspect != none) {
          c.reported = true;
          c.suspect = it->suspect;
          c.confidence = it->confidence;
          c.reporter = it->reporter;
          c.reportedAt = time;
          c.lastKnown = it->lastKnown;
          c.seenAt = it->seenAt;
          c.scene = it->sceneLocated ? it->scene : it->pos;
          c.sceneLocated = true;
        }
        break;
      }
  }
  for (const auto &s : c.witnesses)
    if (residents[s.witness].role == Role::Warden && guardReady(*this, s.witness))
      fileCrimeReport(s.witness, c.id);
}
void World::fileCrimeReport(Id reporter, Id id) {
  auto &c = incidents.at(id);
  auto &r = residents.at(reporter);
  if (!r.alive || !r.present || r.activity == Activity::Sleeping || c.resolved)
    throw std::runtime_error("security.error.report");
  auto statement = std::find_if(c.witnesses.begin(), c.witnesses.end(),
                                [&](const Testimony &s) { return s.witness == reporter; });
  bool victim = reporter == c.victim && r.activity != Activity::Sleeping &&
                (!c.remoteProperty || near(reporter, c.pos, 4));
  bool inspector =
      r.role == Role::Warden && c.inspected && near(reporter, c.sceneLocated ? c.scene : c.pos, 4);
  if (statement == c.witnesses.end() && !victim && !inspector)
    throw std::runtime_error("security.error.report");
  bool contact = hasDevice(reporter) || inspector;
  for (const auto &g : residents)
    if (g.role == Role::Warden && guardReady(*this, g.id) && near(reporter, position(g.id), 4) &&
        sees(*this, g.id, position(reporter), 4))
      contact = true;
  for (const auto &f : facilities)
    if (f.kind == FacilityKind::Jail && operational(f.id) && near(reporter, f.pos, 3))
      contact = true;
  if (!contact)
    throw std::runtime_error("security.error.connection");
  c.reported = c.discovered = true;
  c.reporter = reporter;
  if (c.reportedAt < 0)
    c.reportedAt = time;
  if (statement != c.witnesses.end()) {
    statement->filed = true;
    if (!c.sceneLocated || !statement->bodyOnly) {
      c.scene = statement->seenScene;
      c.sceneLocated = true;
    }
    if (!statement->identifies)
      c.confidence = std::max(c.confidence, .2f);
    if (statement->identifies) {
      c.suspect = c.offender;
      c.confidence = std::max(c.confidence, statement->confidence);
      c.lastKnown = statement->offenderPosition;
      c.seenAt = statement->seenAt;
    }
  } else {
    c.confidence = std::max(c.confidence, .2f);
    if (!c.sceneLocated) {
      c.scene = c.pos;
      c.sceneLocated = true;
    }
  }
  log("security.reported", reporter, c.suspect);
}
bool World::canEnforce(const Incident &c) const {
  return c.reported && !c.resolved && c.suspect != none && c.confidence >= .6f;
}
bool World::hasWarrant(Id person) const {
  for (const auto &c : incidents)
    if (canEnforce(c) && c.suspect == person && c.kind != CrimeKind::QueueCutting)
      return true;
  return false;
}
void World::inspectCrimeScene(Id warden, Id id) {
  auto &c = incidents.at(id);
  Vec3 scene = c.sceneLocated ? c.scene : c.pos;
  if (!guardReady(*this, warden) || !near(warden, scene, 4) || !sees(*this, warden, scene, 4))
    throw std::runtime_error("security.error.scene");
  if (c.inspected)
    return;
  c.inspected = c.discovered = true;
  c.scene = scene;
  c.sceneLocated = true;
  if (!c.reported) {
    c.reported = true;
    c.reporter = warden;
    c.reportedAt = time;
  }
  c.confidence = std::min(1.f, c.confidence + .15f);
  if (c.suspect == none)
    c.confidence = std::min(.45f, c.confidence);
  log("security.scene_checked", warden, id);
}
void World::interviewWitness(Id warden, Id id, Id witness) {
  auto &c = incidents.at(id);
  if (!guardReady(*this, warden) || !residents.at(witness).alive || !residents[witness].present ||
      !near(warden, position(witness), 2.5f) || !sees(*this, warden, position(witness), 2.5f))
    throw std::runtime_error("security.error.witness");
  auto s = std::find_if(c.witnesses.begin(), c.witnesses.end(),
                        [&](const Testimony &t) { return t.witness == witness; });
  if (s == c.witnesses.end())
    throw std::runtime_error("security.error.witness");
  if (residents[witness].automatic &&
      !chooseSocial(witness, none, ChoiceKind::Report, id).willing) {
    log("choice.refused", witness, warden);
    return;
  }
  s->filed = true;
  if (!c.sceneLocated || !s->bodyOnly) {
    c.scene = s->seenScene;
    c.sceneLocated = true;
  }
  c.reported = true;
  c.reporter = warden;
  if (c.reportedAt < 0)
    c.reportedAt = time;
  if (s->identifies) {
    c.suspect = c.offender;
    c.confidence = std::max(c.confidence, s->confidence);
    c.lastKnown = s->offenderPosition;
    c.seenAt = s->seenAt;
  }
  log("security.testimony", witness, id);
}
Vec3 World::investigationDestination(Id warden, Id id) {
  auto &c = incidents.at(id);
  if (c.suspect != none && residents[c.suspect].alive && residents[c.suspect].present &&
      sees(*this, warden, position(c.suspect), 35)) {
    c.lastKnown = position(c.suspect);
    c.seenAt = time;
  }
  Vec3 goal =
      c.suspect == none || !canEnforce(c) ? (c.sceneLocated ? c.scene : c.pos) : c.lastKnown;
  goal.y = std::round(goal.y / 3) * 3;
  return accessible(goal);
}
void World::investigateArrival(Id warden, Id id) {
  auto &c = incidents.at(id);
  auto &r = residents.at(warden);
  if (!guardReady(*this, warden) || c.resolved)
    return;
  if (near(warden, c.sceneLocated ? c.scene : c.pos, 4) && !c.inspected)
    inspectCrimeScene(warden, id);
  for (const auto &s : c.witnesses)
    if (!s.filed && residents[s.witness].alive && near(warden, position(s.witness), 2.5f)) {
      interviewWitness(warden, id, s.witness);
      if (s.filed)
        break;
    }
  if (canEnforce(c) && c.suspect != none && sees(*this, warden, position(c.suspect), 35)) {
    auto &target = residents[c.suspect];
    c.lastKnown = position(c.suspect);
    c.seenAt = time;
    if (near(warden, c.lastKnown, 2.5f)) {
      bool yields =
          target.surrendered || chooseSocial(c.suspect, warden, ChoiceKind::Surrender, id).willing;
      if (yields && availableJail(warden) == none) {
        c.warden = none;
        c.searchAfter = time + 300;
        log("security.capacity_wait", warden, id);
        return;
      }
      if (target.surrendered)
        apprehend(warden, c.suspect);
      else if (yields) {
        surrender(c.suspect);
        apprehend(warden, c.suspect);
      } else
        attack(warden, c.suspect, false);
    } else
      travel(warden, c.lastKnown, TaskKind::Investigate, id);
  } else {
    r.task = TaskKind::None;
    r.activity = Activity::Idle;
    c.warden = none;
    c.searchAfter = time + 900;
    log("security.search_lost", warden, id);
  }
}
void World::recoverEvidence(Id warden, Id id, Id item) {
  auto &c = incidents.at(id);
  auto &i = items.at(item);
  auto &g = residents.at(warden);
  if (!guardReady(*this, warden) || g.carrying != none || i.consumed || i.reservedAt != none ||
      itemInUse(item))
    throw std::runtime_error("security.error.property");
  bool stolen = i.stolenFrom != none, contraband = i.kind == ItemKind::Handgun ||
                                                   i.kind == ItemKind::Ammunition ||
                                                   i.kind == ItemKind::Tools;
  Id holder = i.carrier;
  Vec3 p = holder == none ? i.pos : position(holder);
  if (!near(warden, p, 2.5f) || (!stolen && !contraband))
    throw std::runtime_error("security.error.property");
  // A case number is not a blanket power to confiscate unrelated property.
  bool booked =
      holder != none && custodyRecords.contains(holder) && custodyRecords.at(holder).active &&
      std::find(custodyRecords.at(holder).charges.begin(), custodyRecords.at(holder).charges.end(),
                id) != custodyRecords.at(holder).charges.end();
  if (!c.reported || (stolen ? c.item != item || i.stolenFrom != residents[c.victim].account
                             : c.suspect != holder || (!canEnforce(c) && !booked)))
    throw std::runtime_error("security.error.property");
  if (holder != none && holder != warden && residents[holder].alive && residents[holder].present &&
      !residents[holder].surrendered && residents[holder].activity != Activity::Jailed)
    throw std::runtime_error("security.error.property");
  if (holder != none && holder != warden && !hasWarrant(holder) && residents[holder].alive &&
      residents[holder].activity != Activity::Jailed)
    throw std::runtime_error("security.error.property");
  Id existing = none;
  for (const auto &e : evidenceReceipts)
    if (e.item == item && !e.claimed) {
      if (e.carrier != none || e.locker != none)
        throw std::runtime_error("security.error.property");
      existing = e.id;
    }
  Id locker = (g.jobSite != none && facilities[g.jobSite].kind == FacilityKind::Jail)
                  ? g.jobSite
                  : nearest(warden, FacilityKind::Jail);
  if (locker == none)
    throw std::runtime_error("security.error.locker");
  if (!lineOfSight(position(warden) + Vec3{0, 1.4f, 0}, p + Vec3{0, 1.2f, 0}))
    throw std::runtime_error("security.error.property");
  if (i.storedAt != none) {
    auto &lease = storageLeases.at(i.storedAt);
    if (!lease.open)
      throw std::runtime_error("storage.error.access");
    storageByItemOwner[i.owner].erase(item);
    std::erase(lease.items, item);
    i.storedAt = i.sharedWith = none;
    i.storageSlot = -1;
  }
  if (holder != none)
    std::erase(residents[holder].items, item);
  EvidenceReceipt e;
  e.id = existing == none ? Id(evidenceReceipts.size()) : existing;
  e.item = item;
  e.incident = id;
  e.owner = stolen ? i.stolenFrom : i.owner;
  e.carrier = warden;
  i.carrier = warden;
  i.pos = position(warden);
  i.owner = e.owner;
  i.askingPrice = 0;
  i.seller = none;
  i.reservedAt = locker;
  g.items.push_back(item);
  if (existing == none)
    evidenceReceipts.push_back(e);
  else
    evidenceReceipts[existing] = e;
  log("security.seized", warden, item);
}
void World::depositEvidence(Id warden, Id jail) {
  const auto &f = facilities.at(jail);
  if (!guardReady(*this, warden) || f.kind != FacilityKind::Jail || !near(warden, f.pos, 4))
    throw std::runtime_error("security.error.locker");
  for (auto &e : evidenceReceipts)
    if (!e.claimed && e.carrier == warden) {
      auto &i = items[e.item];
      i.carrier = none;
      i.pos = f.pos;
      i.reservedAt = jail;
      std::erase(residents[warden].items, i.id);
      e.carrier = none;
      e.locker = jail;
      log("security.deposited", warden, i.id);
    }
}
bool World::evidenceAuthority(Id actor, Id receipt) const {
  const auto &r = residents.at(actor);
  Id owner = evidenceReceipts.at(receipt).owner;
  if (owner == r.account || (owner == dormAccount && actor == dormOwner))
    return true;
  Id company = companyForAccount(owner);
  if (company != none && directsCompany(actor, company))
    return true;
  for (const auto &f : facilities)
    if (operatingAccount(f.id) == owner && f.manager == actor) {
      // A shop manager does not become custodian of its owner's private wallet
      // or personal belongings when that wallet also funds the shop.
      for (const auto &person : residents)
        if (person.account == owner)
          return false;
      return true;
    }
  return false;
}
void World::claimEvidence(Id owner, Id receipt) {
  auto &e = evidenceReceipts.at(receipt);
  auto &r = residents.at(owner);
  auto &i = items[e.item];
  if (!available(r) || e.claimed || !evidenceAuthority(owner, receipt) || e.locker == none ||
      !near(owner, facilities[e.locker].pos, 3) || i.consumed || i.carrier != none)
    throw std::runtime_error("security.error.claim");
  if (i.stolenFrom == none && hasWarrant(owner))
    throw std::runtime_error("security.error.claim");
  i.owner = e.owner;
  i.stolenFrom = none;
  i.carrier = owner;
  i.reservedAt = none;
  i.pos = position(owner);
  if (std::find(r.items.begin(), r.items.end(), i.id) == r.items.end())
    r.items.push_back(i.id);
  e.claimed = true;
  log("security.claimed", owner, i.id);
}
double World::sentenceHours(Id suspect) const {
  double hours = 8;
  for (const auto &c : incidents)
    if (canEnforce(c) && c.suspect == suspect)
      hours = std::max(hours, c.kind == CrimeKind::Murder           ? 168.
                              : c.kind == CrimeKind::ExcessiveForce ? 24.
                              : c.kind == CrimeKind::Theft          ? 16.
                              : c.kind == CrimeKind::Escape         ? 24.
                              : c.kind == CrimeKind::Obstruction    ? 16.
                                                                    : 8.);
  return hours;
}
void World::beginCustody(Id guard, Id suspect, Id jail) {
  CustodyRecord record;
  record.resident = suspect;
  record.guard = guard;
  record.jail = jail;
  record.bookedAt = time;
  record.careAt = time;
  record.sentenceUntil = time + sentenceHours(suspect) * 3600;
  if (custodyRecords.contains(suspect))
    record.sentenceUntil = std::max(record.sentenceUntil, custodyRecords[suspect].sentenceUntil);
  for (const auto &c : incidents)
    if (canEnforce(c) && c.suspect == suspect)
      record.charges.push_back(c.id);
  // Seizure takes place only after the guard physically reaches the surrendered person.
  auto possessions = residents[suspect].items;
  for (Id item : possessions) {
    auto &i = items[item];
    if (i.carrier == suspect && !i.consumed &&
        (i.stolenFrom != none || i.kind == ItemKind::Handgun || i.kind == ItemKind::Ammunition ||
         i.kind == ItemKind::Tools)) {
      Id charge = none;
      for (Id candidate : record.charges)
        if (i.stolenFrom == none || incidents[candidate].item == item) {
          charge = candidate;
          break;
        }
      if (charge != none)
        try {
          recoverEvidence(guard, charge, item);
        } catch (const std::runtime_error &) {
        }
    }
  }
  custodyRecords[suspect] = record;
}
void World::startEscape(Id id) {
  auto &r = residents.at(id);
  if (!r.alive || !r.present ||
      (r.activity != Activity::Jailed && r.activity != Activity::Escorted) || r.energy < .15f)
    throw std::runtime_error("security.error.escape");
  if (!custodyRecords.contains(id)) {
    if (r.target < 0 || std::size_t(r.target) >= facilities.size() ||
        facilities[r.target].kind != FacilityKind::Jail)
      throw std::runtime_error("security.error.escape");
    CustodyRecord c;
    c.resident = id;
    c.jail = r.target;
    c.guard = r.escort;
    c.bookedAt = time;
    c.careAt = time;
    c.sentenceUntil = r.releaseAt;
    custodyRecords[id] = c;
  }
  auto &c = custodyRecords[id];
  if (c.escapeAt >= 0)
    throw std::runtime_error("security.error.escape");
  c.escapeAt = time;
  c.escapeProgress = 0;
  log("security.escape_started", id);
}
void World::stopEscape(Id id) {
  auto it = custodyRecords.find(id);
  if (it != custodyRecords.end()) {
    it->second.escapeAt = -1;
    it->second.escapeProgress = 0;
  }
}
void World::escapeCustody(Id id) {
  auto &r = residents.at(id);
  auto &c = custodyRecords.at(id);
  r.pos = position(id);
  r.route.clear();
  ++r.routeVersion;
  Id guard = c.guard;
  if (guard != none && residents[guard].escort == id) {
    auto &g = residents[guard];
    g.pos = position(guard);
    g.escort = none;
    g.route.clear();
    ++g.routeVersion;
    g.task = TaskKind::None;
    if (g.alive)
      g.activity = Activity::Idle;
  }
  r.escort = none;
  r.surrendered = false;
  r.activity = Activity::Idle;
  r.task = TaskKind::None;
  r.releaseAt = 0;
  c.active = false;
  c.escaped = true;
  c.cuffed = false;
  c.escapeAt = -1;
  Id victim = guard != none
                  ? guard
                  : (facilities[c.jail].manager != none ? facilities[c.jail].manager : id);
  if (victim == id) {
    for (const auto &person : residents)
      if (person.id != id) {
        victim = person.id;
        break;
      }
  }
  Id crime = report(CrimeKind::Escape, id, victim, false, true);
  incidents[crime].pos = incidents[crime].lastKnown = r.pos;
  incidents[crime].scene = r.pos;
  log("security.escaped", id, c.jail);
}
void World::securityTick() {
  discoverDeaths();
  prisonRescueTick();
  for (auto &r : residents)
    if (r.alive && r.present && r.surrendered && r.activity == Activity::Surrendered &&
        r.surrenderUntil <= time &&
        (!custodyRecords.contains(r.id) || !custodyRecords.at(r.id).active)) {
      r.surrendered = false;
      r.activity = Activity::Idle;
      log("security.recovered", r.id);
    }
  for (auto &e : evidenceReceipts)
    if (!e.claimed && e.carrier != none &&
        (!residents[e.carrier].alive || !residents[e.carrier].present)) {
      auto &i = items[e.item];
      std::erase(residents[e.carrier].items, i.id);
      i.carrier = none;
      i.pos = position(e.carrier);
      e.carrier = none;
      e.locker = none;
      i.reservedAt = none;
    }
  for (auto &c : incidents) {
    if (c.warden != none &&
        (!available(residents[c.warden]) || residents[c.warden].role != Role::Warden))
      c.warden = none;
    if (c.resolved)
      continue;
    if (c.kind == CrimeKind::Theft && !c.reported && residents[c.victim].alive &&
        residents[c.victim].present && residents[c.victim].activity != Activity::Sleeping &&
        residents[c.victim].automatic && (!c.remoteProperty || near(c.victim, c.pos, 4))) {
      try {
        if (chooseSocial(c.victim, none, ChoiceKind::Report, c.id).willing)
          fileCrimeReport(c.victim, c.id);
      } catch (const std::runtime_error &) {
      }
    }
    for (auto &s : c.witnesses)
      if (!s.filed && available(residents[s.witness]) && residents[s.witness].automatic &&
          residents[s.witness].activity != Activity::Fighting) {
        try {
          if (!chooseSocial(s.witness, none, ChoiceKind::Report, c.id).willing)
            continue;
          fileCrimeReport(s.witness, c.id);
        } catch (const std::runtime_error &) {
        }
      }
  }
  for (auto &[id, c] : custodyRecords) {
    auto &r = residents[id];
    if (!r.alive || !r.present) {
      c.active = false;
      c.escapeAt = -1;
      continue;
    }
    // Repair a booked legacy inmate whose ordinary-service interaction replaced the
    // activity label. releaseAt is set at real intake, not while an escort is pending.
    if (c.active && r.activity != Activity::Jailed && r.releaseAt > time && r.target == c.jail &&
        near(id, facilities[c.jail].pos, 4)) {
      Vec3 actual = position(id);
      stopPersonalUse(id);
      clockOut(id);
      leaveQueue(id);
      r.route.clear();
      ++r.routeVersion;
      r.pos = actual;
      if (auto body = crowd.find(id); body != crowd.end())
        body->second.pos = actual;
      r.activity = Activity::Jailed;
      r.task = TaskKind::None;
      r.escort = none;
    }
    if (c.active && r.activity == Activity::Jailed) {
      r.ladder = -1;
      r.ladderExit = r.ladderWait = false;
      r.ladderTarget = -1;
      r.ladderDirection = 0;
    }
    // Custody consumes the jail's delivered supplies, including for penniless prisoners.
    // The operator pays freight normally; sentence length never creates free rations.
    if (c.active && r.activity == Activity::Jailed && time >= c.careAt + 3600) {
      c.careAt = time;
      needs(id);
      if (!r.alive) {
        c.active = false;
        continue;
      }
      auto &jail = facilities[c.jail];
      if (!jail.demolished && jail.open && jail.stock > 0 &&
          (r.hunger > .4f || r.thirst > .4f || r.hygiene < .6f)) {
        --jail.stock;
        ++jail.waste;
        r.hunger = std::max(0.f, r.hunger - .3f);
        r.thirst = std::max(0.f, r.thirst - .5f);
        r.hygiene = std::min(1.f, r.hygiene + .25f);
        r.bladder = 0;
      }
      if (sleepTime(id) && c.escapeAt < 0)
        r.energy = std::min(1.f, r.energy + .22f);
      if (r.automatic && r.alive && r.energy > .3f && c.escapeAt < 0 && r.aggression > .6f &&
          random() < .04f)
        startEscape(id);
    }
    if (c.active && r.activity != Activity::Jailed &&
        (c.guard == none || !guardReady(*this, c.guard) || residents[c.guard].escort != id ||
         residents[c.guard].task != TaskKind::Escort)) {
      r.pos = position(id);
      stopPersonalUse(id);
      clockOut(id);
      leaveQueue(id);
      r.route.clear();
      ++r.routeVersion;
      r.escort = none;
      r.surrendered = false;
      r.activity = Activity::Idle;
      r.task = TaskKind::None;
      c.active = false;
      c.escapeAt = -1;
      if (c.guard != none && residents[c.guard].escort == id)
        residents[c.guard].escort = none;
      for (Id charge : c.charges) {
        incidents[charge].resolved = false;
        incidents[charge].warden = none;
      }
      log("security.escort_interrupted", id);
      continue;
    }
    if (!c.active || c.escapeAt < 0)
      continue;
    if (r.activity != Activity::Jailed && r.activity != Activity::Escorted) {
      c.escapeAt = -1;
      continue;
    }
    Id watching = none;
    for (const auto &g : residents)
      if (g.id != id && g.role == Role::Warden && guardReady(*this, g.id) &&
          g.activity != Activity::Fighting && g.activity != Activity::Sleeping && onShift(g.id) &&
          near(g.id, position(id), 4) && sees(*this, g.id, position(id), 4)) {
        watching = g.id;
        break;
      }
    if (watching != none &&
        (r.activity == Activity::Jailed || r.strength < residents[watching].strength * 1.6f)) {
      c.escapeAt = -1;
      c.escapeProgress = 0;
      c.sentenceUntil += 3600;
      if (r.activity == Activity::Jailed) {
        r.releaseAt = c.sentenceUntil;
        schedule(r.releaseAt, EventKind::Release, id);
      }
      log("security.escape_stopped", watching, id);
      continue;
    }
    double elapsed = std::clamp(time - c.escapeAt, 0., 60.);
    c.escapeAt = time;
    c.escapeProgress += elapsed;
    r.energy = std::max(0.f, r.energy - float(elapsed / 3600) * .15f);
    double required =
        r.activity == Activity::Escorted ? 180 : std::max(600., 3600 - r.strength * 25.);
    if (c.escapeProgress >= required)
      escapeCustody(id);
  }
}
void World::validateSecurity() const {
  validatePrisonRescues();
  auto resident = [&](Id id) { return id >= 0 && std::size_t(id) < residents.size(); };
  auto facility = [&](Id id) { return id >= 0 && std::size_t(id) < facilities.size(); };
  for (std::size_t n = 0; n < incidents.size(); ++n) {
    const auto &c = incidents[n];
    if (c.id != Id(n) || int(c.kind) < 0 || int(c.kind) > int(CrimeKind::Obstruction) ||
        !resident(c.offender) || !resident(c.victim) ||
        (c.suspect != none && !resident(c.suspect)) ||
        (c.reporter != none && !resident(c.reporter)) ||
        (c.item != none && (c.item < 0 || std::size_t(c.item) >= items.size())) ||
        !std::isfinite(c.confidence) || c.confidence < 0 || c.confidence > 1 ||
        !std::isfinite(c.time) || !std::isfinite(c.reportedAt) || !std::isfinite(c.seenAt) ||
        !std::isfinite(c.searchAfter) || !std::isfinite(c.pos.x) || !std::isfinite(c.pos.y) ||
        !std::isfinite(c.pos.z) || !std::isfinite(c.lastKnown.x) || !std::isfinite(c.lastKnown.y) ||
        !std::isfinite(c.lastKnown.z) || c.witnesses.size() > 24 || !std::isfinite(c.scene.x) ||
        !std::isfinite(c.scene.y) || !std::isfinite(c.scene.z))
      throw std::runtime_error("Invalid investigation");
    std::set<Id> seen;
    for (const auto &s : c.witnesses)
      if (!resident(s.witness) || !seen.insert(s.witness).second || !std::isfinite(s.seenAt) ||
          !std::isfinite(s.confidence) || s.confidence < 0 || s.confidence > 1 ||
          !std::isfinite(s.offenderPosition.x) || !std::isfinite(s.offenderPosition.y) ||
          !std::isfinite(s.offenderPosition.z) || !std::isfinite(s.seenScene.x) ||
          !std::isfinite(s.seenScene.y) || !std::isfinite(s.seenScene.z) ||
          (s.bodyOnly && s.identifies))
        throw std::runtime_error("Invalid testimony");
  }
  for (std::size_t n = 0; n < evidenceReceipts.size(); ++n) {
    const auto &e = evidenceReceipts[n];
    if (e.id != Id(n) || e.item < 0 || std::size_t(e.item) >= items.size() || e.incident < 0 ||
        std::size_t(e.incident) >= incidents.size() || e.owner <= 0 ||
        std::size_t(e.owner) >= accounts.size() || (e.carrier != none && !resident(e.carrier)) ||
        (e.locker != none &&
         (!facility(e.locker) || facilities[e.locker].kind != FacilityKind::Jail)) ||
        (e.carrier != none && e.locker != none))
      throw std::runtime_error("Invalid evidence receipt");
  }
  for (auto [id, c] : custodyRecords) {
    if (!resident(id) || c.resident != id || (c.guard != none && !resident(c.guard)) ||
        !facility(c.jail) || facilities[c.jail].kind != FacilityKind::Jail ||
        !std::isfinite(c.bookedAt) || !std::isfinite(c.sentenceUntil) ||
        !std::isfinite(c.escapeAt) || !std::isfinite(c.escapeProgress) ||
        !std::isfinite(c.careAt) || c.escapeProgress < 0)
      throw std::runtime_error("Invalid custody record");
    for (Id charge : c.charges)
      if (charge < 0 || std::size_t(charge) >= incidents.size())
        throw std::runtime_error("Invalid custody charge");
  }
}
} // namespace mega
