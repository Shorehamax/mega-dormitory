#include "mega/world.hpp"
#include <algorithm>
namespace mega {
namespace {
bool reachableBody(const World &w, Id finder, const Remains &body, Vec3 &at) {
  const auto &r = w.residents.at(finder);
  if (!r.alive || !r.present || r.activity == Activity::Sleeping || r.surrendered ||
      r.activity == Activity::Jailed || r.activity == Activity::Restrained ||
      r.activity == Activity::Escorted || r.ambulance != none || r.carriedBy != none ||
      body.cremated || body.lost || body.ambulance != none || body.lift != none)
    return false;
  if (body.storage != none && !w.accessAllowed(finder, body.storage))
    return false;
  at = body.carrier == none ? body.pos : w.position(body.carrier) + Vec3{0, .5f, .7f};
  return w.near(finder, at, 4) && std::abs(w.position(finder).y - at.y) < 2 &&
         w.lineOfSight(w.position(finder) + Vec3{0, 1.5f, 0}, at + Vec3{0, .3f, 0});
}
} // namespace
bool World::bodyVisible(Id finder, Id id) const {
  Vec3 at;
  return reachableBody(*this, finder, remains.at(id), at);
}
Id World::discoverBody(Id finder, Id id) {
  const auto &body = remains.at(id);
  Vec3 at;
  if (!reachableBody(*this, finder, body, at))
    throw std::runtime_error("security.error.body");
  for (auto it = incidents.rbegin(); it != incidents.rend(); ++it) {
    auto &c = *it;
    if (c.kind != CrimeKind::Murder || c.victim != body.resident || c.resolved)
      continue;
    auto statement = std::find_if(c.witnesses.begin(), c.witnesses.end(),
                                  [&](const Testimony &s) { return s.witness == finder; });
    if (statement == c.witnesses.end()) {
      // Direct observations keep their original twelve slots; discovery can add
      // a bounded set of body-only observations without deleting eyewitnesses.
      Testimony s;
      s.witness = finder;
      s.seenAt = time;
      s.confidence = .2f;
      s.bodyOnly = true;
      s.seenScene = at;
      if (c.witnesses.size() >= 24) {
        auto old = std::find_if(c.witnesses.begin(), c.witnesses.end(),
                                [](const Testimony &t) { return t.bodyOnly && !t.filed; });
        if (old == c.witnesses.end())
          return c.id;
        *old = s;
      } else
        c.witnesses.push_back(s);
      log("security.body_found", finder, c.victim);
    }
    c.discovered = true;
    observeInformation(finder, InformationKind::Witness, c.id);
    try {
      if (residents[finder].automatic &&
          !chooseSocial(finder, none, ChoiceKind::Report, c.id).willing)
        return c.id;
      fileCrimeReport(finder, c.id);
    } catch (const std::runtime_error &e) {
      if (std::string(e.what()) != "security.error.connection")
        throw;
    }
    return c.id;
  }
  // No violent-death fact exists for this body; do not manufacture an offender.
  return none;
}
void World::discoverDeaths() {
  std::map<Id, Id> unexplained;
  for (const auto &c : incidents)
    if (c.kind == CrimeKind::Murder && !c.reported && !c.resolved)
      unexplained[c.victim] = c.id;
  if (unexplained.empty())
    return;
  std::vector<Id> observers;
  for (const auto &r : residents)
    if (r.automatic && r.alive && r.present &&
        ((r.role == Role::Warden && onShift(r.id)) || r.role == Role::Cleaner ||
         r.role == Role::Medic))
      observers.push_back(r.id);
  for (const auto &body : remains) {
    if (!unexplained.contains(body.resident))
      continue;
    for (Id observer : observers) {
      Vec3 at;
      if (reachableBody(*this, observer, body, at))
        discoverBody(observer, Id(&body - remains.data()));
      if (incidents[unexplained.at(body.resident)].reported)
        break;
    }
  }
}
} // namespace mega
