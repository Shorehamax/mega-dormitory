#include "mega/world.hpp"
#include <algorithm>
#include <set>
namespace mega {
std::vector<std::size_t> World::messageHistory(Id actor, Id contact) const {
  if (actor < 0 || std::size_t(actor) >= residents.size() ||
      (contact != none && (contact < 0 || std::size_t(contact) >= residents.size())))
    throw std::runtime_error("history.error.contact");
  std::vector<std::size_t> result;
  for (std::size_t i = messages.size(); i > 0; --i) {
    const auto &message = messages[i - 1];
    // A pending outbound message is known to its sender, not yet to its recipient.
    if ((message.from == actor || (message.to == actor && message.read)) &&
        (contact == none || (message.from == actor ? message.to : message.from) == contact))
      result.push_back(i - 1);
  }
  return result;
}
std::vector<Id> World::knownContacts(Id actor) const {
  if (actor < 0 || std::size_t(actor) >= residents.size())
    throw std::runtime_error("history.error.contact");
  std::set<Id> known;
  auto add = [&](Id id) {
    if (id >= 0 && std::size_t(id) < residents.size() && id != actor)
      known.insert(id);
  };
  for (const auto &[key, bond] : relationships) {
    (void)key;
    if (bond.a != actor && bond.b != actor)
      continue;
    Id other = bond.a == actor ? bond.b : bond.a;
    if (bond.conversations > 0 || trust(actor, other) != 0 ||
        std::any_of(bond.memories.begin(), bond.memories.end(), [&](const EncounterMemory &m) {
          return bond.a == actor ? m.knownToA : m.knownToB;
        }))
      add(other);
  }
  for (const auto &message : messages) {
    if (message.from == actor || (message.to == actor && message.read)) {
      add(message.from == actor ? message.to : message.from);
      if (message.hasInformation) {
        add(message.information.origin);
        add(message.information.via);
        add(message.information.suspect);
      }
    }
  }
  auto reports = residentInformation.find(actor);
  if (reports != residentInformation.end())
    for (const auto &report : reports->second) {
      add(report.origin);
      add(report.via);
      add(report.suspect); // Only identities actually contained in received observations.
    }
  auto decisions = socialChoices.find(actor);
  if (decisions != socialChoices.end())
    for (const auto &decision : decisions->second)
      if (decision.reason != "choice.unavailable")
        add(decision.other);
  Id group = householdOf(actor);
  if (group != none) {
    for (Id member : households.at(group).members)
      add(member);
    for (const auto &[invitee, expires] : households.at(group).invitations)
      if (expires >= time)
        add(invitee);
  }
  for (const auto &household : households)
    if (household.invitations.contains(actor))
      add(household.founder);
  return {known.begin(), known.end()};
}
} // namespace mega
