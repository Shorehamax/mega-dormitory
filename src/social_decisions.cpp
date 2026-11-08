#include "mega/world.hpp"
#include <algorithm>
#include <array>
#include <set>
namespace mega {
namespace {
constexpr std::array<const char *, 17> reasons{
    "choice.unavailable", "choice.reserve",   "choice.grievance", "choice.generous",
    "choice.friend",      "choice.selfish",   "choice.cooperate", "choice.reserved",
    "choice.conscience",  "choice.desperate", "choice.tempted",   "choice.guard",
    "choice.duty",        "choice.loyalty",   "choice.fear",      "choice.overwhelmed",
    "choice.resist"};
bool awake(const Resident &r) {
  return r.alive && r.present && !r.leaving && r.activity != Activity::Sleeping &&
         r.ambulance == none && r.carriedBy == none;
}
float power(const Resident &r) {
  return std::max(1.f, r.strength * (.35f + .65f * std::clamp(r.health / 100, 0.f, 1.f)) *
                           (.6f + .4f * std::clamp(r.energy, 0.f, 1.f)));
}
} // namespace
Personality Personality::generated(std::uint64_t seed, Id actor) {
  // Independent hashes preserve the world's existing RNG stream, names, wealth and jobs.
  auto value = [&](std::uint64_t salt) {
    std::uint64_t x = seed ^ (std::uint64_t(actor) * 0x9e3779b97f4a7c15ULL) ^ salt;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return float((x ^ (x >> 31)) >> 40) / float(0xffffff);
  };
  return {.05f + .9f * value(17), .25f + .7f * value(31), .05f + .9f * value(53),
          .15f + .8f * value(79)};
}
SocialChoice World::considerChoice(Id actor, Id other, ChoiceKind kind, Id subject) const {
  const auto &r = residents.at(actor);
  SocialChoice choice{kind, other, subject, time, false, "choice.unavailable"};
  if (int(kind) < 0 || int(kind) > int(ChoiceKind::Surrender))
    throw std::runtime_error("Invalid social choice kind");
  auto decide = [&](bool willing, const char *reason) {
    choice.willing = willing;
    choice.reason = reason;
    return choice;
  };
  if (other == actor || (other != none && (other < 0 || std::size_t(other) >= residents.size())))
    throw std::runtime_error("Invalid social choice contact");
  if (!awake(r))
    return choice;
  const auto &p = r.personality;
  float opinion = other == none ? 0 : trust(actor, other);
  bool harmed = false;
  if (other != none)
    for (const auto &memory : relationshipHistory(actor, other))
      if (memory.actor == other &&
          (memory.kind == EncounterKind::Assault || memory.kind == EncounterKind::Theft) &&
          time - memory.time < 86400)
        harmed = true;
  if (kind == ChoiceKind::Report) {
    if (subject < 0 || std::size_t(subject) >= incidents.size())
      return choice;
    const auto &c = incidents[subject];
    auto seen = std::find_if(c.witnesses.begin(), c.witnesses.end(),
                             [&](const Testimony &s) { return s.witness == actor; });
    bool victim = c.victim == actor && (!c.remoteProperty || near(actor, c.pos, 4));
    if (c.resolved || (seen == c.witnesses.end() && !victim))
      return choice;
    // Never consult a hidden offender's personality, relationship or current position.
    Id known = seen != c.witnesses.end() && seen->identifies ? c.offender : none;
    choice.other = known;
    if (r.role == Role::Warden)
      return decide(true, "choice.duty");
    float loyalty = known == none ? 0 : std::max(0.f, trust(actor, known));
    float grievance = known == none ? 0 : std::max(0.f, -trust(actor, known));
    float threat = 0;
    if (known != none && near(actor, position(known), 12) &&
        lineOfSight(position(actor) + Vec3{0, 1.4f, 0}, position(known) + Vec3{0, 1.4f, 0}))
      threat = std::clamp(power(residents[known]) / power(r) - .7f, 0.f, 1.f) * (1 - p.courage);
    float severity = c.kind == CrimeKind::Murder         ? .25f
                     : c.kind == CrimeKind::QueueCutting ? 0
                                                         : .1f;
    float score = p.conscience * .85f + p.courage * .25f + severity + (victim ? .25f : 0) +
                  grievance * .25f - threat * .6f - loyalty * .65f - .45f;
    return decide(score >= 0, score >= 0      ? "choice.conscience"
                              : threat > .2f  ? "choice.fear"
                              : loyalty > .4f ? "choice.loyalty"
                                              : "choice.reserved");
  }
  if (other == none || other == actor || !residents[other].alive || !residents[other].present)
    return choice;
  const auto &target = residents[other];
  if (kind == ChoiceKind::Surrender) {
    bool visible =
        near(actor, position(other), 35) &&
        lineOfSight(position(actor) + Vec3{0, 1.4f, 0}, position(other) + Vec3{0, 1.4f, 0});
    if (!visible)
      return choice;
    float ratio = power(target) / power(r);
    if (target.activity == Activity::Fighting && hasGun(other))
      ratio *= 1.7f;
    float authority = target.role == Role::Warden && hasWarrant(actor) ? .4f : 0;
    float threshold = 1.2f + p.courage * .6f + r.aggression * .2f - authority;
    // Recognized prior violence changes risk, not legal responsibility or remote knowledge.
    if (harmed)
      threshold -= .15f * (1 - p.courage);
    bool yield = r.health < 30 || r.energy < .1f || ratio > 2.5f || ratio > threshold;
    return decide(yield, yield ? "choice.overwhelmed" : "choice.resist");
  }
  if (r.surrendered || inCustody(actor) || r.activity == Activity::Fighting ||
      (kind != ChoiceKind::Theft && !awake(target)))
    return choice;
  if (kind == ChoiceKind::Cooperate) {
    if (harmed || opinion < -.35f)
      return decide(false, "choice.grievance");
    if (p.sociability < .18f && opinion < .1f)
      return decide(false, "choice.reserved");
    return decide(true, opinion >= .5f ? "choice.friend" : "choice.cooperate");
  }
  if (kind == ChoiceKind::Help) {
    int meals = 0;
    for (Id id : r.items) {
      const auto &i = items[id];
      meals += i.kind == ItemKind::Food && i.owner == r.account && i.carrier == actor &&
               !i.consumed && i.durability > .2f && i.reservedAt == none && i.stolenFrom == none &&
               !itemInUse(id);
    }
    if (r.hunger > .65f || meals < 2 || target.hunger < .55f)
      return decide(false, "choice.reserve");
    if (harmed || opinion < -.35f)
      return decide(false, "choice.grievance");
    float score = p.generosity * .65f + opinion * .7f + std::min(1.f, target.hunger) * .15f -
                  r.aggression * .3f - .5f;
    return decide(score >= 0, score < 0        ? "choice.selfish"
                              : opinion >= .5f ? "choice.friend"
                                               : "choice.generous");
  }
  if (subject < 0 || std::size_t(subject) >= items.size())
    return choice;
  const auto &i = items[subject];
  if (!near(actor, position(other), 1.5f) ||
      !lineOfSight(position(actor) + Vec3{0, 1.3f, 0}, position(other) + Vec3{0, 1.3f, 0}) ||
      i.owner != target.account || i.carrier != other || i.consumed || i.reservedAt != none ||
      i.order != none || i.stolenFrom != none || target.outfit == subject || itemInUse(subject))
    return choice;
  // A prospective thief can only judge people actually visible nearby.
  for (const auto &g : residents)
    if (g.id != actor && g.role == Role::Warden && awake(g) && near(actor, position(g.id), 12) &&
        lineOfSight(position(actor) + Vec3{0, 1.4f, 0}, position(g.id) + Vec3{0, 1.4f, 0}))
      return decide(false, "choice.guard");
  if (opinion >= .5f)
    return decide(false, "choice.loyalty");
  float desperation = std::clamp((r.hunger - .6f) + (r.thirst - .8f), 0.f, 1.f);
  float poverty = accounts[r.account].balance < 2000 ? .3f : 0;
  float score = .25f + r.aggression * .4f + desperation * .4f + poverty +
                std::max(0.f, -opinion) * .15f - p.conscience * .9f - .35f;
  return decide(score > 0, score <= 0          ? "choice.conscience"
                           : desperation > .2f ? "choice.desperate"
                                               : "choice.tempted");
}
SocialChoice World::chooseSocial(Id actor, Id other, ChoiceKind kind, Id subject) {
  if ((kind == ChoiceKind::Theft && (subject < 0 || std::size_t(subject) >= items.size())) ||
      (kind == ChoiceKind::Report && (subject < 0 || std::size_t(subject) >= incidents.size())) ||
      (kind == ChoiceKind::Surrender && subject != none &&
       (subject < 0 || std::size_t(subject) >= incidents.size())) ||
      ((kind == ChoiceKind::Help || kind == ChoiceKind::Cooperate) && subject != none))
    throw std::runtime_error("Invalid social choice subject");
  auto choice = considerChoice(actor, other, kind, subject);
  auto &history = socialChoices[actor];
  if (!history.empty()) {
    const auto &last = history.back();
    if (last.kind == choice.kind && last.other == choice.other && last.subject == choice.subject &&
        last.willing == choice.willing && last.reason == choice.reason && time - last.time < 300)
      return choice;
  }
  history.push_back(choice);
  if (history.size() > 8)
    history.erase(history.begin());
  return choice;
}
bool World::opportunisticTheft(Id actor) {
  const auto &r = residents.at(actor);
  if (!r.automatic || r.activity != Activity::Queuing || r.queuedAt == none || r.surrendered ||
      inCustody(actor) || !awake(r))
    return false;
  for (Id victim : facilities[r.queuedAt].queue) {
    if (victim == actor || !near(actor, position(victim), 1.3f) ||
        !lineOfSight(position(actor) + Vec3{0, 1.3f, 0}, position(victim) + Vec3{0, 1.3f, 0}))
      continue;
    auto carried = residents[victim].items;
    for (Id item : carried)
      if (items[item].kind == ItemKind::Phone || items[item].kind == ItemKind::Food) {
        if (!chooseSocial(actor, victim, ChoiceKind::Theft, item).willing)
          continue;
        try {
          stealItem(actor, victim, item);
          return true;
        } catch (const std::runtime_error &) {
        }
      }
    break;
  }
  return false;
}
void World::validateSocialChoices() const {
  auto person = [&](Id id) { return id >= 0 && std::size_t(id) < residents.size(); };
  for (const auto &r : residents) {
    const auto &p = r.personality;
    for (float f : {p.generosity, p.conscience, p.courage, p.sociability})
      if (!std::isfinite(f) || f < 0 || f > 1)
        throw std::runtime_error("Invalid personality");
  }
  for (const auto &[actor, notes] : socialChoices) {
    if (!person(actor) || notes.size() > 8)
      throw std::runtime_error("Invalid social decision history");
    double prior = -1;
    for (const auto &c : notes) {
      bool subject = c.subject == none;
      if (c.kind == ChoiceKind::Theft)
        subject = c.subject >= 0 && std::size_t(c.subject) < items.size();
      if (c.kind == ChoiceKind::Report || (c.kind == ChoiceKind::Surrender && c.subject != none))
        subject = c.subject >= 0 && std::size_t(c.subject) < incidents.size();
      if (int(c.kind) < 0 || int(c.kind) > int(ChoiceKind::Surrender) || !subject ||
          (c.other != none && (!person(c.other) || c.other == actor)) || !std::isfinite(c.time) ||
          c.time < 0 || c.time < prior || c.time > time ||
          std::find(reasons.begin(), reasons.end(), c.reason) == reasons.end())
        throw std::runtime_error("Invalid social decision");
      prior = c.time;
    }
  }
}
} // namespace mega
