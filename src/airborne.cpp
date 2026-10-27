#include "mega/world.hpp"
#include <algorithm>
namespace mega {
namespace {
bool attached(const Resident &r) {
  return r.ladder >= 0 || r.vehicle != none || r.lift != none || r.ambulance != none ||
         r.carriedBy != none || r.medicalDuty != none || r.activity == Activity::Jailed ||
         r.activity == Activity::Restrained || r.activity == Activity::Escorted;
}
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
} // namespace
void World::ensurePhysicsTick() {
  if ((!looseBodies.empty() || !airborneRemains.empty()) && nextPhysicsAt < 0) {
    nextPhysicsAt = time + .02 * (1440 / config.dayMinutes);
    schedule(nextPhysicsAt, EventKind::PhysicsTick, 0);
  }
}
void World::clearPhysics(Id id) {
  looseBodies.erase(id);
  if (looseBodies.empty() && airborneRemains.empty())
    nextPhysicsAt = -1;
}
Body World::stepBody(Id id, Vec3 desired, float seconds, bool jump) {
  auto &r = residents.at(id);
  if (!r.alive || !r.present || attached(r) || !r.route.empty() || r.queuedAt != none ||
      (r.activity != Activity::Idle && r.activity != Activity::Fighting &&
       r.activity != Activity::Surrendered) ||
      !finite(desired) || !std::isfinite(seconds) || seconds < 0 || seconds > .1f ||
      length(desired) > 100)
    throw std::runtime_error("physics.error.body");
  Body body;
  body.position = position(id);
  auto prior = looseBodies.find(id);
  LooseBody state;
  if (prior != looseBodies.end()) {
    state = prior->second;
    body.velocity = state.velocity;
  } else {
    int f = int(std::round(body.position.y / 3));
    body.grounded = f >= 0 && f < config.floors && std::abs(body.position.y - f * 3.f) < .025f &&
                    layout(f).supported(body.position);
  }
  if (jump && body.grounded)
    body.velocity.y = 4.5f;
  float impact = moveBody(id, body, desired, seconds);
  r.pos = body.position;
  if (auto near = crowd.find(id); near != crowd.end())
    near->second.pos = body.position;
  if (!body.grounded) {
    state.velocity = body.velocity;
    state.updated = time;
    looseBodies[id] = state;
    ensurePhysicsTick();
  } else {
    clearPhysics(id);
  }
  if (impact > 7)
    applyFall(id, impact, state.pusher, state.justified);
  if (r.alive && r.pos.y < -100) {
    Id carried = r.bodyCarrying;
    if (carried != none)
      loseRemains(carried);
    applyFall(id, 60, state.pusher, state.justified);
    for (Id b = 0; b < Id(remains.size()); ++b)
      if (remains[b].resident == id && !remains[b].cremated)
        loseRemains(b);
  }
  return body;
}
void World::physicsTick() {
  nextPhysicsAt = -1;
  std::vector<Id> ids;
  for (const auto &[id, state] : looseBodies) {
    (void)state;
    ids.push_back(id);
  }
  for (Id id : ids) {
    auto found = looseBodies.find(id);
    if (found == looseBodies.end())
      continue;
    auto state = found->second;
    auto &r = residents[id];
    if (!r.alive || !r.present || attached(r)) {
      clearPhysics(id);
      continue;
    }
    if (!r.route.empty() || r.queuedAt != none ||
        (r.activity != Activity::Idle && r.activity != Activity::Fighting &&
         r.activity != Activity::Surrendered)) {
      // A deferred job/service callback cannot replace a physical fall with a static task.
      stopPersonalUse(id);
      clockOut(id);
      leaveQueue(id);
      r.route.clear();
      ++r.routeVersion;
      r.task = TaskKind::None;
      r.target = none;
      r.activity = r.surrendered ? Activity::Surrendered : Activity::Idle;
      if (auto near = crowd.find(id); near != crowd.end())
        near->second.pos = r.pos;
    }
    if (id == physicsController) {
      found->second.updated = time;
      continue;
    }
    float elapsed =
        std::clamp(float((time - state.updated) / (1440 / config.dayMinutes)), 0.f, .1f);
    stepBody(id, {state.velocity.x, 0, state.velocity.z}, elapsed);
  }
  std::vector<Id> bodies;
  for (const auto &[id, state] : airborneRemains) {
    (void)state;
    bodies.push_back(id);
  }
  for (Id id : bodies) {
    auto found = airborneRemains.find(id);
    if (found == airborneRemains.end())
      continue;
    auto &body = remains[id];
    if (body.lost || body.cremated || body.carrier != none || body.ambulance != none ||
        body.lift != none || body.storage != none) {
      airborneRemains.erase(id);
      continue;
    }
    auto &state = found->second;
    Body physical;
    physical.position = body.pos;
    physical.velocity = state.velocity;
    float dt = std::clamp(float((time - state.updated) / (1440 / config.dayMinutes)), 0.f, .1f);
    moveBody(body.resident, physical, {state.velocity.x, 0, state.velocity.z}, dt);
    body.pos = physical.position;
    state.velocity = physical.velocity;
    state.updated = time;
    if (body.pos.y < -100)
      loseRemains(id);
    else if (physical.grounded)
      airborneRemains.erase(id);
  }
  // stepBody may already schedule the next event; one marker suppresses stale ticks.
  if (looseBodies.empty() && airborneRemains.empty())
    nextPhysicsAt = -1;
  ensurePhysicsTick();
}
void World::loseRemains(Id id) {
  auto &body = remains.at(id);
  if (body.lost || body.cremated)
    return;
  for (Id actor : {body.worker, body.carrier})
    if (actor != none) {
      auto &r = residents.at(actor);
      if (r.bodyCarrying == id)
        r.bodyCarrying = none;
      if ((r.task == TaskKind::BodyPickup && r.target == id) ||
          (r.task == TaskKind::BodyDrop && actor == body.carrier)) {
        r.pos = position(actor);
        r.route.clear();
        ++r.routeVersion;
        r.task = TaskKind::None;
        r.target = none;
        if (r.alive)
          r.activity = Activity::Idle;
      }
    }
  airborneRemains.erase(id);
  if (airborneRemains.empty() && looseBodies.empty())
    nextPhysicsAt = -1;
  body.lost = true;
  body.carrier = body.worker = body.storage = body.destination = body.ambulance = body.lift = none;
  body.burnUntil = body.burnWorkedAt = -1;
  log("physics.body_lost", body.resident);
}
void World::shove(Id actor, Id victim) {
  auto &a = residents.at(actor);
  auto &v = residents.at(victim);
  if (actor == victim || !a.alive || !a.present || !v.alive || !v.present || attached(a) ||
      attached(v) || a.surrendered || v.activity == Activity::Sleeping || time < a.nextHit ||
      a.energy < .08f || !near(actor, position(victim), 1.8f) || looseBodies.contains(actor))
    throw std::runtime_error("physics.error.shove");
  Vec3 direction = position(victim) - position(actor);
  direction.y = 0;
  if (length(direction) < .05f)
    throw std::runtime_error("physics.error.shove");
  bool helpless = v.surrendered;
  bool defense = !helpless && v.opponent == actor && v.activity == Activity::Fighting;
  bool enforcement =
      a.role == Role::Warden &&
      std::any_of(incidents.begin(), incidents.end(), [&](const Incident &c) {
        return !c.resolved && c.offender == victim && c.kind != CrimeKind::QueueCutting;
      });
  bool justified =
      !helpless && (defense || enforcement) && v.lethalThreat && v.activity == Activity::Fighting;
  if (helpless)
    report(CrimeKind::ExcessiveForce, actor, victim);
  else if (!defense && !enforcement)
    report(CrimeKind::Assault, actor, victim);
  stopPersonalUse(actor);
  stopPersonalUse(victim);
  clockOut(actor);
  clockOut(victim);
  a.pos = position(actor);
  v.pos = position(victim);
  leaveQueue(actor);
  leaveQueue(victim);
  a.route.clear();
  v.route.clear();
  ++a.routeVersion;
  ++v.routeVersion;
  a.task = v.task = TaskKind::None;
  a.target = v.target = none;
  a.activity = Activity::Fighting;
  a.opponent = victim;
  a.nextHit = time + 20;
  a.lethalThreat = false;
  if (!helpless) {
    v.activity = v.aggression > .55f ? Activity::Fighting : Activity::Idle;
    v.opponent = v.aggression > .55f ? actor : none;
  }
  a.energy = std::max(0.f, a.energy - .06f);
  v.energy = std::max(0.f, v.energy - .03f);
  v.happiness = std::max(0.f, v.happiness - .08f);
  float power = std::clamp(2.5f + (a.strength - v.strength) * .035f, 1.2f, 5.5f);
  auto velocity = normalized(direction) * power;
  velocity.y = 1.7f;
  looseBodies[victim] = {velocity, actor, justified, time};
  ensurePhysicsTick();
  log("physics.shoved", actor, victim);
}
void World::validatePhysics() const {
  if (!std::isfinite(nextPhysicsAt) || nextPhysicsAt < -1 ||
      (nextPhysicsAt >= 0 && nextPhysicsAt < time) ||
      (looseBodies.empty() && airborneRemains.empty() && nextPhysicsAt != -1) ||
      ((!looseBodies.empty() || !airborneRemains.empty()) && nextPhysicsAt < 0))
    throw std::runtime_error("Invalid physics scheduler");
  for (const auto &[id, state] : looseBodies) {
    if (id < 0 || std::size_t(id) >= residents.size() || !residents[id].alive ||
        !residents[id].present || !finite(state.velocity) || length(state.velocity) > 2000 ||
        !std::isfinite(state.updated) || state.updated < 0 || state.updated > time ||
        (state.pusher != none &&
         (state.pusher < 0 || std::size_t(state.pusher) >= residents.size())) ||
        !finite(residents[id].pos) || std::abs(residents[id].pos.x) > 1e6f ||
        std::abs(residents[id].pos.z) > 1e6f || residents[id].pos.y < -101 ||
        residents[id].pos.y > config.floors * 3.f + 5000)
      throw std::runtime_error("Invalid airborne body");
  }
  for (const auto &[id, state] : airborneRemains) {
    if (id < 0 || std::size_t(id) >= remains.size() || remains[id].lost || remains[id].cremated ||
        remains[id].carrier != none || remains[id].worker != none || remains[id].storage != none ||
        remains[id].destination != none || remains[id].ambulance != none ||
        remains[id].lift != none || !finite(state.velocity) || length(state.velocity) > 2000 ||
        !std::isfinite(state.updated) || state.updated < 0 || state.updated > time ||
        !finite(remains[id].pos) || remains[id].pos.y < -101 ||
        remains[id].pos.y > config.floors * 3.f + 5000 ||
        (state.pusher != none &&
         (state.pusher < 0 || std::size_t(state.pusher) >= residents.size())))
      throw std::runtime_error("Invalid airborne remains");
  }
  if (!looseBodies.empty() || !airborneRemains.empty()) {
    auto copy = events;
    bool found = false;
    while (!copy.empty()) {
      auto event = copy.top();
      copy.pop();
      if (event.kind == EventKind::PhysicsTick && event.time == nextPhysicsAt) {
        found = true;
        break;
      }
    }
    if (!found)
      throw std::runtime_error("Missing physics event");
  }
}
} // namespace mega
