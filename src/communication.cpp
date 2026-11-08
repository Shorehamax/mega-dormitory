#include "mega/world.hpp"
#include <algorithm>
#include <limits>

namespace mega {
namespace {
bool awake(const World &w, Id id) {
  return id >= 0 && std::size_t(id) < w.residents.size() && w.residents[id].alive &&
         w.residents[id].present && w.residents[id].activity != Activity::Sleeping;
}
bool faceContact(const World &w, Id a, Id b) {
  return a != b && awake(w, a) && awake(w, b) && w.residents[a].activity != Activity::Fighting &&
         w.residents[b].activity != Activity::Fighting && w.near(a, w.position(b), 2.5f) &&
         w.lineOfSight(w.position(a) + Vec3{0, 1.4f, 0}, w.position(b) + Vec3{0, 1.4f, 0});
}
bool newer(const Information &a, const Information &b) {
  return std::pair{a.observedAt, a.id} > std::pair{b.observedAt, b.id};
}
} // namespace
const Information &World::knownInformation(Id actor, std::uint64_t id) const {
  auto found = residentInformation.find(actor);
  if (found != residentInformation.end())
    for (const auto &info : found->second)
      if (info.id == id)
        return info;
  throw std::runtime_error("info.error.unknown");
}
bool World::retainInformation(Id actor, Information info) {
  auto &notes = residentInformation[actor];
  for (const auto &known : notes)
    if (known.id == info.id || (known.kind == info.kind && known.subject == info.subject &&
                                known.origin == info.origin && !newer(info, known)))
      return false;
  std::erase_if(notes, [&](const Information &known) {
    return known.kind == info.kind && known.subject == info.subject && known.origin == info.origin;
  });
  info.learnedAt = time;
  notes.push_back(info);
  if (notes.size() > 24)
    notes.erase(notes.begin());
  return true;
}
std::uint64_t World::observeInformation(Id actor, InformationKind kind, Id subject) {
  if (!awake(*this, actor) || int(kind) < 0 || int(kind) > int(InformationKind::Witness))
    throw std::runtime_error("info.error.observe");
  Information info;
  info.kind = kind;
  info.origin = info.via = actor;
  info.subject = subject;
  info.observedAt = info.learnedAt = time;
  if (kind == InformationKind::Service || kind == InformationKind::Work) {
    if (subject < 0 || std::size_t(subject) >= facilities.size())
      throw std::runtime_error("info.error.observe");
    const auto &f = facilities[subject];
    bool seen = near(actor, f.pos, 16) &&
                lineOfSight(position(actor) + Vec3{0, 1.4f, 0}, f.pos + Vec3{0, 1.4f, 2.6f});
    // Hiring notices are published on the existing jobs network; live service state is local.
    if ((!seen && !(kind == InformationKind::Work && hasDevice(actor))) ||
        (!f.publicAccess && !accessAllowed(actor, subject)))
      throw std::runtime_error("info.error.observe");
    info.where = f.pos;
    info.detail = int(f.kind);
    info.price = kind == InformationKind::Service ? f.price : f.wageOffer;
    info.stock = kind == InformationKind::Service ? f.stock : f.staffNeeded;
    info.waiting = kind == InformationKind::Service ? int(f.queue.size()) : int(f.employees.size());
    info.available = kind == InformationKind::Service ? operational(subject)
                                                      : f.open && !f.demolished && f.hiring;
  } else if (kind == InformationKind::Trade) {
    if (subject < 0 || std::size_t(subject) >= items.size())
      throw std::runtime_error("info.error.observe");
    const auto &i = items[subject];
    if (i.seller == none || i.askingPrice <= 0 || i.consumed ||
        (!hasDevice(actor) && i.seller != actor &&
         !(near(actor, position(i.seller), 2.5f) &&
           lineOfSight(position(actor) + Vec3{0, 1.4f, 0}, position(i.seller) + Vec3{0, 1.4f, 0}))))
      throw std::runtime_error("info.error.observe");
    info.locationKnown = i.seller == actor || (near(actor, position(i.seller), 2.5f) &&
                                               lineOfSight(position(actor) + Vec3{0, 1.4f, 0},
                                                           position(i.seller) + Vec3{0, 1.4f, 0}));
    info.where = info.locationKnown ? position(i.seller) : Vec3{};
    info.detail = int(i.kind);
    info.price = i.askingPrice;
    info.stock = i.quantity;
    info.available = residents[i.seller].alive && residents[i.seller].present &&
                     i.owner == residents[i.seller].account && i.carrier == i.seller &&
                     i.reservedAt == none && i.stolenFrom == none && !itemInUse(subject);
  } else if (kind == InformationKind::Help) {
    const auto &r = residents[actor];
    if (subject != actor || (r.hunger < .6f && r.thirst < .6f && r.health > 65))
      throw std::runtime_error("info.error.help");
    info.where = position(actor);
    info.stock = r.health <= 65 ? 2 : r.thirst >= .6f ? 1 : 0;
    info.detail = info.stock; // medicine / water / meal
    info.available = true;
  } else {
    if (subject < 0 || std::size_t(subject) >= incidents.size())
      throw std::runtime_error("info.error.observe");
    const auto &c = incidents[subject];
    auto witness = std::find_if(c.witnesses.begin(), c.witnesses.end(),
                                [&](const Testimony &s) { return s.witness == actor; });
    if (witness == c.witnesses.end() ||
        (kind == InformationKind::Threat &&
         (c.victim != actor ||
          (c.kind != CrimeKind::Assault && c.kind != CrimeKind::ExcessiveForce))))
      throw std::runtime_error("info.error.observe");
    info.observedAt = witness->seenAt;
    info.where = witness->identifies ? witness->offenderPosition : witness->seenScene;
    info.suspect = witness->identifies ? c.offender : none;
    info.detail = int(c.kind);
    info.available = true;
  }
  // Reopening an unchanged observation does not create a new fact every frame/click.
  auto known = residentInformation.find(actor);
  if (known != residentInformation.end())
    for (const auto &old : known->second)
      if (old.kind == kind && old.subject == subject && old.origin == actor &&
          old.channel == InformationChannel::Observation && old.price == info.price &&
          old.stock == info.stock && old.waiting == info.waiting &&
          old.available == info.available && old.suspect == info.suspect &&
          old.detail == info.detail && old.locationKnown == info.locationKnown &&
          length(old.where - info.where) < .01f &&
          ((kind == InformationKind::Witness || kind == InformationKind::Threat)
               ? old.observedAt == info.observedAt
               : time - old.observedAt < 300))
        return old.id;
  if (informationSequence == std::numeric_limits<std::uint64_t>::max())
    throw std::runtime_error("info.error.capacity");
  info.id = ++informationSequence;
  if (!retainInformation(actor, info))
    for (const auto &old : residentInformation.at(actor))
      if (old.kind == kind && old.subject == subject && old.origin == actor)
        return old.id;
  return info.id;
}
void World::collectInformation(Id actor) {
  if (!awake(*this, actor) || residents[actor].activity == Activity::Fighting)
    return;
  auto p = position(actor);
  int floor = int(std::round(p.y / 3));
  Id nearest = none;
  float distance = 16;
  auto row = floorFacilities.find(floor);
  if (row != floorFacilities.end())
    for (Id id : row->second) {
      const auto &f = facilities[id];
      float d = length(f.pos - p);
      if (!f.demolished && d < distance && (f.publicAccess || accessAllowed(actor, id)) &&
          lineOfSight(p + Vec3{0, 1.4f, 0}, f.pos + Vec3{0, 1.4f, 2.6f})) {
        nearest = id;
        distance = d;
      }
    }
  if (nearest != none) {
    observeInformation(actor, InformationKind::Service, nearest);
    if (facilities[nearest].hiring)
      observeInformation(actor, InformationKind::Work, nearest);
  }
  const auto &r = residents[actor];
  // A seller can describe their own published offer during a real conversation.
  for (Id id : r.items) {
    const auto &item = items[id];
    if (item.seller == actor && item.askingPrice > 0 && !item.consumed) {
      observeInformation(actor, InformationKind::Trade, id);
      break;
    }
  }
  if (r.hunger >= .6f || r.thirst >= .6f || r.health <= 65)
    observeInformation(actor, InformationKind::Help, actor);
}
bool World::shareInformation(Id from, Id to, std::uint64_t id, bool device) {
  if (from == to || !awake(*this, from) || to < 0 || std::size_t(to) >= residents.size() ||
      !residents[to].alive || !residents[to].present ||
      (device ? !hasDevice(from) : !faceContact(*this, from, to)))
    throw std::runtime_error(device ? "info.error.device" : "info.error.contact");
  auto info = knownInformation(from, id); // Copy before any receiving cache changes.
  info.via = from;
  info.channel = device ? InformationChannel::Device : InformationChannel::Conversation;
  if (!device)
    return retainInformation(to, info);
  auto key = std::tuple{from, to, id};
  if (informationMail.contains(key))
    return false;
  if (messages.size() >= 1000000)
    throw std::runtime_error("info.error.capacity");
  Message message;
  message.from = from;
  message.to = to;
  message.time = time;
  message.text = "info.attachment";
  message.hasInformation = true;
  message.information = info;
  messages.push_back(std::move(message));
  unreadInformation[to].push_back(messages.size() - 1);
  informationMail.insert(key);
  log("info.sent", from, to);
  return true;
}
bool World::shareLatestInformation(Id from, Id to, InformationKind kind, bool device) {
  // Validate the channel before collecting anything for a failed conversation.
  if (from == to || to < 0 || std::size_t(to) >= residents.size() || !residents[to].alive ||
      !residents[to].present ||
      (device ? !awake(*this, from) || !hasDevice(from) : !faceContact(*this, from, to)))
    throw std::runtime_error(device ? "info.error.device" : "info.error.contact");
  collectInformation(from);
  auto found = residentInformation.find(from);
  if (found != residentInformation.end()) {
    const Information *best = nullptr;
    for (const auto &info : found->second)
      if (info.kind == kind && (!best || newer(info, *best)))
        best = &info;
    if (best)
      return shareInformation(from, to, best->id, device);
  }
  throw std::runtime_error("info.error.unknown");
}
std::uint64_t World::requestTradeMeeting(Id buyer, Id item) {
  if (!awake(*this, buyer) || !hasDevice(buyer) || item < 0 || std::size_t(item) >= items.size())
    throw std::runtime_error("info.error.device");
  Id seller = items[item].seller;
  if (seller == buyer || !awake(*this, seller) || !hasDevice(seller) ||
      items[item].askingPrice <= 0 || items[item].owner != residents[seller].account ||
      items[item].stolenFrom != none || items[item].carrier != seller || items[item].consumed ||
      items[item].reservedAt != none || itemInUse(item))
    throw std::runtime_error("info.error.contact");
  // The seller replies through their own available device with a dated meeting location.
  auto id = observeInformation(seller, InformationKind::Trade, item);
  shareInformation(seller, buyer, id, true);
  readMessages(buyer);
  return id;
}
void World::readMessages(Id actor) {
  if (!awake(*this, actor) || !hasDevice(actor))
    throw std::runtime_error("info.error.device");
  auto pending = unreadInformation.find(actor);
  if (pending == unreadInformation.end())
    return;
  for (std::size_t index : pending->second) {
    auto &message = messages.at(index);
    if (!message.read) {
      if (message.hasInformation) {
        message.information.learnedAt = time;
        retainInformation(actor, message.information);
      }
      message.read = true;
    }
  }
  unreadInformation.erase(pending);
}
void World::exchangeInformation(Id a, Id b) {
  if (!faceContact(*this, a, b))
    return;
  collectInformation(a);
  collectInformation(b);
  // At most one new fact each way, retaining original time/source through relays.
  for (auto [from, to] : {std::pair{a, b}, std::pair{b, a}}) {
    auto found = residentInformation.find(from);
    if (found == residentInformation.end())
      continue;
    auto notes = found->second;
    for (auto it = notes.rbegin(); it != notes.rend(); ++it)
      if (shareInformation(from, to, it->id))
        break;
  }
}
void World::communicationTick() {
  std::set<Id> readers;
  for (const auto &[actor, pending] : unreadInformation)
    if (!pending.empty() && residents[actor].automatic && awake(*this, actor) && hasDevice(actor))
      readers.insert(actor);
  for (Id actor : readers)
    readMessages(actor);
  if (messages.size() >= 1000000)
    return; // Full mail history pauses automatic sending without stopping the world.
  for (auto &[key, bond] : relationships) {
    (void)key;
    if (messages.size() >= 1000000)
      break;
    if (time - bond.lastInformation < 3600)
      continue;
    for (auto [from, to] : {std::pair{bond.a, bond.b}, std::pair{bond.b, bond.a}}) {
      if (messages.size() >= 1000000 || !residents[from].automatic || !awake(*this, from) ||
          !hasDevice(from) || trust(from, to) < .35f || !residents[to].alive ||
          !residents[to].present)
        continue;
      auto found = residentInformation.find(from);
      if (found == residentInformation.end())
        continue;
      for (auto it = found->second.rbegin(); it != found->second.rend(); ++it)
        if (shareInformation(from, to, it->id, true)) {
          bond.lastInformation = time;
          break;
        }
    }
  }
}
bool World::informationStale(const Information &info) const {
  if (info.kind == InformationKind::Witness || info.kind == InformationKind::Threat)
    return false;
  return time - info.observedAt > (info.kind == InformationKind::Help ? 1800 : 3600);
}
void World::followInformation(Id actor, std::uint64_t id) {
  const auto info = knownInformation(actor, id);
  if (!awake(*this, actor) || !info.locationKnown)
    throw std::runtime_error("info.error.contact");
  // Visit the reported place; do not resolve a moving seller/facility to its hidden live position.
  travel(actor, accessible(info.where + Vec3{0, 0, 2.6f}), TaskKind::None, none);
}
void World::reportInformation(Id actor, std::uint64_t id) {
  const auto info = knownInformation(actor, id);
  if (info.kind != InformationKind::Witness && info.kind != InformationKind::Threat)
    throw std::runtime_error("info.error.report");
  Id source = info.origin;
  if (!awake(*this, actor) || !awake(*this, source) ||
      (source != actor && !faceContact(*this, actor, source) &&
       !(hasDevice(actor) && hasDevice(source))))
    throw std::runtime_error("info.error.report");
  // A callback reaches the original recorded witness. The relay never becomes an eyewitness.
  fileCrimeReport(source, info.subject);
}
void World::rebuildInformationMail() {
  informationMail.clear();
  unreadInformation.clear();
  for (std::size_t index = 0; index < messages.size(); ++index) {
    const auto &message = messages[index];
    if (message.hasInformation)
      informationMail.insert({message.from, message.to, message.information.id});
    if (!message.read)
      unreadInformation[message.to].push_back(index);
  }
}
void World::validateCommunication() const {
  auto person = [&](Id id) { return id >= 0 && std::size_t(id) < residents.size(); };
  auto noteValid = [&](const Information &info) {
    bool subject = false;
    if (info.kind == InformationKind::Service || info.kind == InformationKind::Work)
      subject = info.subject >= 0 && std::size_t(info.subject) < facilities.size();
    else if (info.kind == InformationKind::Trade)
      subject = info.subject >= 0 && std::size_t(info.subject) < items.size();
    else if (info.kind == InformationKind::Help)
      subject = person(info.subject) && info.subject == info.origin && info.stock <= 2;
    else if (info.kind == InformationKind::Witness || info.kind == InformationKind::Threat)
      subject = info.subject >= 0 && std::size_t(info.subject) < incidents.size() &&
                (info.suspect == none || info.suspect == incidents[info.subject].offender);
    if (subject &&
        (info.kind == InformationKind::Witness || info.kind == InformationKind::Threat)) {
      const auto &incident = incidents[info.subject];
      auto statement = std::find_if(incident.witnesses.begin(), incident.witnesses.end(),
                                    [&](const Testimony &s) { return s.witness == info.origin; });
      if (statement != incident.witnesses.end()) {
        const auto &s = *statement;
        subject = s.seenAt == info.observedAt &&
                  info.suspect == (s.identifies ? incident.offender : none) &&
                  length(info.where - (s.identifies ? s.offenderPosition : s.seenScene)) < .01f;
      } else {
        // Investigation slots replace old unfiled body-only statements at their cap.
        // Anonymous historical reports remain in residents' personal memories.
        subject = info.kind == InformationKind::Witness && info.suspect == none &&
                  incident.kind == CrimeKind::Murder && incident.witnesses.size() >= 24;
      }
      if (info.kind == InformationKind::Threat)
        subject =
            subject && incident.victim == info.origin &&
            (incident.kind == CrimeKind::Assault || incident.kind == CrimeKind::ExcessiveForce);
    }
    if (!info.id || info.id > informationSequence || !person(info.origin) || !person(info.via) ||
        (info.suspect != none &&
         (!person(info.suspect) ||
          (info.kind != InformationKind::Witness && info.kind != InformationKind::Threat))) ||
        (!info.locationKnown && (info.kind != InformationKind::Trade || length(info.where) > 0)) ||
        !subject || int(info.channel) < 0 || int(info.channel) > int(InformationChannel::Device) ||
        !std::isfinite(info.observedAt) || !std::isfinite(info.learnedAt) || info.observedAt < 0 ||
        info.learnedAt < info.observedAt || info.learnedAt > time || !std::isfinite(info.where.x) ||
        !std::isfinite(info.where.y) || !std::isfinite(info.where.z) || info.price < 0 ||
        info.stock < 0 || info.waiting < 0 || info.detail < 0 ||
        ((info.kind == InformationKind::Service || info.kind == InformationKind::Work) &&
         info.detail > int(FacilityKind::Locker)) ||
        (info.kind == InformationKind::Trade && info.detail > int(ItemKind::RiceCooker)) ||
        (info.kind == InformationKind::Help && info.detail > 2) ||
        ((info.kind == InformationKind::Witness || info.kind == InformationKind::Threat) &&
         info.detail > int(CrimeKind::Obstruction)))
      throw std::runtime_error("Invalid information record");
  };
  for (const auto &[actor, notes] : residentInformation) {
    if (!person(actor) || notes.size() > 24)
      throw std::runtime_error("Invalid information cache");
    std::set<std::uint64_t> ids;
    std::set<std::tuple<InformationKind, Id, Id>> topics;
    for (const auto &info : notes) {
      noteValid(info);
      if (!ids.insert(info.id).second ||
          !topics.insert({info.kind, info.subject, info.origin}).second ||
          (info.channel == InformationChannel::Observation &&
           (actor != info.origin || actor != info.via)))
        throw std::runtime_error("Invalid information ownership");
    }
  }
  std::set<std::tuple<Id, Id, std::uint64_t>> expected;
  std::map<Id, std::vector<std::size_t>> unread;
  for (std::size_t index = 0; index < messages.size(); ++index) {
    const auto &message = messages[index];
    if (!message.read)
      unread[message.to].push_back(index);
    if (!person(message.from) || !person(message.to) || !std::isfinite(message.time) ||
        message.time < 0 || message.time > time || message.text.empty() ||
        message.text.size() > 2000)
      throw std::runtime_error("Invalid message");
    if (message.hasInformation) {
      noteValid(message.information);
      if (message.information.channel != InformationChannel::Device ||
          message.information.via != message.from ||
          message.information.observedAt > message.time ||
          !expected.insert({message.from, message.to, message.information.id}).second)
        throw std::runtime_error("Invalid information message");
    }
  }
  if (unread != unreadInformation)
    throw std::runtime_error("Invalid unread information index");
  if (expected != informationMail)
    throw std::runtime_error("Invalid information mail index");
  for (const auto &[key, bond] : relationships) {
    (void)key;
    if (!std::isfinite(bond.lastInformation) || bond.lastInformation > time)
      throw std::runtime_error("Invalid information sharing clock");
  }
}
} // namespace mega
