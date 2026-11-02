#include "mega/world.hpp"
#include <algorithm>

namespace mega {
namespace {
int solution(const Education &e, int question) {
  int k = e.seed + 1;
  if (e.subject == 0)
    return question == 0 ? 2 : question == 1 ? 4 * k + 2 : 2;
  if (e.subject == 1)
    return question == 0 ? 3 * k + 3 : question == 1 ? 16 - 2 * k : 2 * k + 1;
  return question == 0   ? std::max(0, 7 - 2 * k)
         : question == 1 ? 100 / (k + 3)
                         : std::max(0, (2 * k + 5) / 3 - k);
}
bool inClass(const World &w, Id who) {
  const auto &r = w.residents.at(who);
  if (!r.alive || !r.present || r.activity != Activity::Using || r.queuedAt == none ||
      !w.education.contains(who))
    return false;
  const auto &e = w.education.at(who);
  if (e.school != r.queuedAt || !w.operational(e.school))
    return false;
  const auto &f = w.facilities[e.school];
  for (const auto &s : f.serving)
    if (s.resident == who && s.until >= 0 && w.near(who, w.servicePosition(f.id, s.station), .8f))
      return true;
  return false;
}
} // namespace
void World::enrollLesson(Id actor, Id school, int subject) {
  const auto &r = residents.at(actor);
  const auto &f = facilities.at(school);
  if (!r.alive || !r.present || r.surrendered || r.age < 6 || subject < 0 || subject > 2 ||
      r.activity == Activity::Jailed || r.activity == Activity::Restrained ||
      r.activity == Activity::Escorted || r.activity == Activity::Fighting ||
      f.kind != FacilityKind::School || !accessAllowed(actor, school) || !operational(school) ||
      accounts[r.account].balance < f.price ||
      (education.contains(actor) && education.at(actor).lastDay == int(time / 86400)))
    throw std::runtime_error("education.error.class");
  // Plan before changing the subject or abandoning a queue. No tuition is
  // charged until the real attended service completes.
  travel(actor, serviceApproach(school), TaskKind::Service, school);
  education[actor].subject = subject;
}
void World::prepareLesson(Id actor, Id school) {
  auto &e = education[actor];
  if (residents.at(actor).age < 6 || e.lastDay == int(time / 86400))
    throw std::runtime_error("education.error.class");
  e.school = school;
  e.seed = (std::uint64_t(actor) * 17 + std::uint64_t(time / 86400) + school) % 5;
  e.answers = {-1, -1, -1};
}
std::array<int, 4> World::lessonChoices(Id actor, int question) const {
  if (question < 0 || question > 2)
    throw std::runtime_error("education.error.answer");
  const auto &e = education.at(actor);
  int value = solution(e, question);
  std::array<int, 4> choices{value - 3, value - 1, value + 2, value + 4};
  choices[(e.seed + question) % 4] = value;
  return choices;
}
void World::answerLesson(Id actor, int question, int choice) {
  if (question < 0 || question > 2 || choice < 0 || choice > 3 || !inClass(*this, actor))
    throw std::runtime_error("education.error.answer");
  education.at(actor).answers[question] = choice;
}
void World::completeLesson(Id actor, Id school) {
  auto found = education.find(actor);
  if (found == education.end() || found->second.school != school)
    return; // Historical imported services do not invent a course record.
  auto &e = found->second;
  if (e.lastDay == int(time / 86400))
    return;
  int correct = 0;
  for (int q = 0; q < 3; ++q) {
    if (residents[actor].automatic) {
      // An attended NPC exercise depends on prior knowledge and interest.
      float aptitude = .45f + .35f * e.skills[e.subject] +
                       .2f * residents[actor].interests[e.subject == 1 ? 3 : 6];
      e.answers[q] = random() < aptitude ? (e.seed + q) % 4 : (e.seed + q + 1) % 4;
    }
    if (e.answers[q] >= 0 && lessonChoices(actor, q)[e.answers[q]] == solution(e, q))
      ++correct;
  }
  e.skills[e.subject] = std::min(1.f, e.skills[e.subject] + .01f + .01f * correct);
  e.lessons[e.subject] = std::min(1000000, e.lessons[e.subject] + 1);
  e.lastDay = int(time / 86400);
  e.school = none;
  log("education.completed", actor, school);
}
float World::jobSkill(Id actor, Role role) const {
  auto found = education.find(actor);
  if (found == education.end())
    return 0;
  int subject = role == Role::Programmer      ? 0
                : role == Role::VideoEditor   ? 1
                : role == Role::Administrator ? 2
                                              : -1;
  return subject < 0 ? 0 : found->second.skills[subject];
}
bool World::educationDecision(Id actor) {
  const auto &r = residents.at(actor);
  int day = int(time / 86400);
  if (!r.automatic || !r.alive || !r.present || r.age < 6 || r.leaving ||
      r.activity != Activity::Idle || !r.route.empty() || r.queuedAt != none ||
      r.carrying != none || r.bodyCarrying != none || looseBodies.contains(actor) ||
      r.lift != none || r.vehicle != none || r.ambulance != none || r.carriedBy != none ||
      r.medicalDuty != none || r.surrendered || day % 7 >= 5 || std::fmod(time / 3600, 24) < 8 ||
      std::fmod(time / 3600, 24) >= 15 ||
      (education.contains(actor) && education.at(actor).lastDay == day))
    return false;
  // Adults train on one staggered weekday, after their own work/survival needs.
  if (r.age >= 18 && (day % 5 != actor % 5 || std::max(r.interests[6], r.interests[3]) < .65f))
    return false;
  Id school = serviceChoice(actor, FacilityKind::School);
  if (school == none || !operational(school) ||
      std::abs(position(actor).y - facilities[school].pos.y) > 36 ||
      accounts[r.account].balance <
          facilities[school].price + (r.age >= 18 ? householdReserve(actor) : 2000))
    return false;
  int subject = r.role == Role::Programmer        ? 0
                : r.role == Role::VideoEditor     ? 1
                : r.role == Role::Administrator   ? 2
                : r.age < 18                      ? day % 3
                : r.interests[3] > r.interests[6] ? 1
                                                  : 0;
  try {
    enrollLesson(actor, school, subject);
    return true;
  } catch (const std::runtime_error &) {
    return false;
  }
}
void World::validateEducation() const {
  for (const auto &[actor, e] : education) {
    if (actor < 0 || std::size_t(actor) >= residents.size() || e.subject < 0 || e.subject > 2 ||
        e.seed < 0 || e.seed > 4 || e.lastDay < -1 || e.lastDay > int(time / 86400) ||
        (e.school != none && (e.school < 0 || std::size_t(e.school) >= facilities.size() ||
                              facilities[e.school].kind != FacilityKind::School)))
      throw std::runtime_error("Invalid education record");
    for (int q = 0; q < 3; ++q)
      if (!std::isfinite(e.skills[q]) || e.skills[q] < 0 || e.skills[q] > 1 || e.lessons[q] < 0 ||
          e.lessons[q] > 1000000 || e.answers[q] < -1 || e.answers[q] > 3)
        throw std::runtime_error("Invalid learning progress");
  }
}
} // namespace mega
