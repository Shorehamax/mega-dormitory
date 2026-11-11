#include "json.hpp"
#include "mega/world.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <zstd.h>
using namespace mega;
namespace {
int passed = 0, failed = 0;
void require(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "Invalid action accepted");
}
void test(const char *n, std::function<void()> f) {
  try {
    f();
    ++passed;
    std::cout << "PASS " << n << '\n';
  } catch (const std::exception &e) {
    ++failed;
    std::cout << "FAIL " << n << ": " << e.what() << '\n';
  }
}
void place(World &w, Id id, Vec3 p) {
  w.residents[id].pos = p;
  w.residents[id].route.clear();
  ++w.residents[id].routeVersion;
  w.crowd.erase(id);
}
World small() {
  Config c;
  c.population = 40;
  c.floors = 20;
  c.mode = Mode::Cheat;
  c.accidentRate = 0;
  auto w = World::generate(c);
  while (!w.events.empty())
    w.events.pop();
  for (auto &r : w.residents) {
    w.clockOut(r.id);
    w.leaveQueue(r.id);
    r.automatic = false;
    r.personality = {};
    r.aggression = 0;
    r.interests.fill(.5f);
    r.route.clear();
    r.task = TaskKind::None;
    r.activity = Activity::Idle;
    r.role = Role::Unemployed;
    r.jobSite = r.employer = none;
    r.pos = {100 + r.id * 3.f, 0, 100};
    r.hunger = r.thirst = .1f;
    r.health = 100;
    r.energy = r.hygiene = 1;
  }
  place(w, 1, {100, 0, 101});
  w.crowd.clear();
  return w;
}
Id add(World &w, Id actor, ItemKind kind) {
  Item i;
  i.id = Id(w.items.size());
  i.kind = kind;
  i.owner = w.residents[actor].account;
  i.carrier = actor;
  i.pos = w.position(actor);
  w.items.push_back(i);
  w.residents[actor].items.push_back(i.id);
  return i.id;
}
void offline(World &w, Id actor) {
  auto &r = w.residents[actor];
  for (Id i : r.items)
    if (w.items[i].kind == ItemKind::Phone || w.items[i].kind == ItemKind::Laptop) {
      w.items[i].carrier = none;
      w.items[i].pos = r.pos;
    }
  std::erase_if(r.items, [&](Id i) { return w.items[i].carrier != actor; });
  r.terminal = none;
  r.terminalUntil = 0;
}
Id facility(World &w, FacilityKind kind, Vec3 pos = {100, 0, 106}) {
  for (auto &f : w.facilities)
    if (f.floor == 0 && f.kind == kind) {
      f.pos = pos;
      f.publicAccess = true;
      f.open = true;
      f.opensAt = 0;
      f.openHours = 24;
      f.staffNeeded = 0;
      f.price = 20;
      w.invalidateGeometry(0);
      return f.id;
    }
  throw std::runtime_error("Missing fixture facility");
}
std::uint64_t help(World &w, Id actor = 0) {
  w.residents[actor].hunger = .8f;
  return w.observeInformation(actor, InformationKind::Help, actor);
}
Id crime(World &w) {
  place(w, 0, {150, 0, 150});
  place(w, 1, {151, 0, 150});
  place(w, 2, {150, 0, 154});
  return w.recordCrime(CrimeKind::Assault, 0, 1);
}
std::uint64_t note(World &w, Id actor, InformationKind kind, Id subject) {
  auto i = w.observeInformation(actor, kind, subject);
  require(w.knownInformation(actor, i).origin == actor, "Missing observation");
  return i;
}
void oldFormat(const std::filesystem::path &p) {
  std::ifstream in(p, std::ios::binary);
  std::vector<char> b((std::istreambuf_iterator<char>(in)), {});
  auto size = ZSTD_getFrameContentSize(b.data() + 8, b.size() - 8);
  std::vector<std::uint8_t> raw(size);
  require(!ZSTD_isError(ZSTD_decompress(raw.data(), raw.size(), b.data() + 8, b.size() - 8)),
          "decode");
  auto j = nlohmann::json::from_cbor(raw);
  j.erase("informationSequence");
  j.erase("residentInformation");
  for (auto &m : j["messages"]) {
    m.erase("hasInformation");
    m.erase("information");
  }
  for (auto &e : j["relationships"])
    e[1].erase("lastInformation");
  raw = nlohmann::json::to_cbor(j);
  std::vector<char> z(ZSTD_compressBound(raw.size()));
  auto n = ZSTD_compress(z.data(), z.size(), raw.data(), raw.size(), 3);
  require(!ZSTD_isError(n), "encode");
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write("MDORM001", 8);
  out.write(z.data(), n);
}
} // namespace
bool knows(const World &w, Id actor, Id other) {
  auto contacts = w.knownContacts(actor);
  return std::find(contacts.begin(), contacts.end(), other) != contacts.end();
}
int main() {
  auto dir = std::filesystem::temp_directory_path() /
             ("mega-history-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(dir);
  test("Unknown world residents do not appear in a fresh contact book", [] {
    auto w = small();
    require(w.knownContacts(0).empty() && w.messageHistory(0).empty(),
            "World population leaked into contacts");
  });
  test("Real conversation makes a known contact and full private history", [] {
    auto w = small();
    for (int i = 0; i < 20; ++i) {
      w.talk(0, 1, 1);
      w.time += 301;
    }
    require(knows(w, 0, 1) && knows(w, 1, 0) && w.relationshipHistory(0, 1).size() == 16 &&
                w.relationshipStatus(0, 1) == RelationshipStatus::Friend,
            "Conversation/history incomplete");
    w.validate();
  });
  test("The thief's private memory does not appear in a sleeping victim's contacts", [] {
    auto w = small();
    for (Id id = 2; std::size_t(id) < w.residents.size(); ++id)
      place(w, id, {180, 57, 180});
    Id i = add(w, 1, ItemKind::Phone);
    w.residents[1].activity = Activity::Sleeping;
    w.stealItem(0, 1, i);
    require(knows(w, 0, 1) && !knows(w, 1, 0) && w.relationshipHistory(1, 0).empty() &&
                !knows(w, 2, 0),
            "Unobserved thief was named to victim/stranger");
    w.validate();
  });
  test("Hidden crime, remote location and secret inventory do not alter another contact book", [] {
    auto w = small();
    auto before = w.knownContacts(3);
    place(w, 0, {150, 0, 150});
    place(w, 1, {151, 0, 150});
    place(w, 3, {180, 57, 180});
    w.recordCrime(CrimeKind::Assault, 0, 1);
    add(w, 0, ItemKind::Handgun);
    place(w, 0, {210, 0, 210});
    require(w.knownContacts(3) == before && w.messageHistory(3).empty(),
            "Hidden event changed private book");
  });
  test("Legacy opinions retain contacts without inventing encounters", [] {
    auto w = small();
    w.relationships[1] = {0, 1, .6f, -3600, 0};
    require(knows(w, 0, 1) && w.relationshipHistory(0, 1).empty(),
            "Legacy opinion lost or fake encounter invented");
  });
  test("Directional acquaintances and rivals do not expose the counterpart's private opinion", [] {
    auto w = small();
    w.talk(0, 1, 3);
    w.time += 301;
    w.talk(0, 1, 3);
    require(w.relationshipStatus(1, 0) == RelationshipStatus::Rival &&
                w.relationshipStatus(0, 1) != RelationshipStatus::Rival && knows(w, 1, 0),
            "Opinions became symmetric");
    auto own = w.relationshipHistory(0, 1);
    require(own.size() == 2 && own.back().changeA != own.back().changeB,
            "Directional changes lost");
  });
  test("Pending inbound text is absent until actual device delivery", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    offline(w, 1);
    w.sendMessage(0, 1, "hello");
    require(knows(w, 0, 1) && !knows(w, 1, 0) && w.messageHistory(1).empty() &&
                w.messageHistory(0).size() == 1,
            "Offline inbox metadata leaked");
    add(w, 1, ItemKind::Phone);
    w.readMessages(1);
    require(knows(w, 1, 0) && w.messageHistory(1).size() == 1, "Delivered sender missing");
    w.validate();
  });
  test("An undelivered forwarded report does not reveal its origin or suspect", [] {
    auto w = small();
    auto c = crime(w);
    auto n = note(w, 2, InformationKind::Witness, c);
    place(w, 3, w.position(2) + Vec3{0, 0, 1});
    w.shareInformation(2, 3, n);
    add(w, 3, ItemKind::Phone);
    offline(w, 4);
    w.shareInformation(3, 4, n, true);
    require(!knows(w, 4, 2) && !knows(w, 4, 0) && !knows(w, 4, 3),
            "Pending report exposed metadata");
    add(w, 4, ItemKind::Phone);
    w.readMessages(4);
    require(knows(w, 4, 2) && knows(w, 4, 3) && knows(w, 4, 0), "Delivered provenance missing");
    w.validate();
  });
  test("Anonymous testimony never turns the hidden offender into a contact", [] {
    auto w = small();
    auto c = crime(w);
    for (auto &s : w.incidents[c].witnesses)
      if (s.witness == 2)
        s.identifies = false;
    w.residentInformation.erase(2);
    auto n = w.observeInformation(2, InformationKind::Witness, c);
    place(w, 3, w.position(2) + Vec3{0, 0, 1});
    w.shareInformation(2, 3, n);
    require(knows(w, 3, 2) && !knows(w, 3, 0) && w.knownInformation(3, n).suspect == none,
            "Anonymous relay named culprit");
  });
  test("Delivered archive provenance remains known after the active report cache evicts it", [] {
    auto w = small();
    auto n = help(w, 0);
    place(w, 1, {100, 0, 101});
    w.shareInformation(0, 1, n);
    add(w, 1, ItemKind::Phone);
    add(w, 2, ItemKind::Phone);
    w.shareInformation(1, 2, n, true);
    w.readMessages(2);
    for (Id id = 3; id < 30; ++id) {
      place(w, id, w.position(2) + Vec3{0, 0, 1});
      auto next = help(w, id);
      w.shareInformation(id, 2, next);
    }
    rejects([&] { w.knownInformation(2, n); });
    require(knows(w, 2, 0) && knows(w, 2, 1), "Archived provenance was forgotten by browser");
    w.validate();
  });
  test("More than eight messages remain complete, newest-first and actor-specific", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    add(w, 1, ItemKind::Phone);
    for (int i = 0; i < 25; ++i) {
      w.sendMessage(0, 1, "entry " + std::to_string(i));
      w.time += 1;
    }
    w.sendMessage(2, 0, "unrelated");
    w.readMessages(1);
    auto rows = w.messageHistory(1);
    require(rows.size() == 25 && rows.front() == 24 && rows.back() == 0,
            "History cutoff or wrong order");
    std::set<std::size_t> ids(rows.begin(), rows.end());
    require(ids.size() == 25 && w.messageHistory(3).empty(), "Duplicate or another inbox exposed");
    w.validate();
  });
  test("Conversation filters isolate each actual correspondent", [] {
    auto w = small();
    for (Id id : {0, 1, 2})
      add(w, id, ItemKind::Phone);
    w.sendMessage(0, 1, "one");
    w.sendMessage(2, 1, "two");
    w.sendMessage(1, 0, "reply");
    w.readMessages(1);
    auto rows = w.messageHistory(1, 0);
    require(rows.size() == 2 && rows[0] == 2 && rows[1] == 0 &&
                w.messageHistory(1, 2) == std::vector<std::size_t>{1},
            "Conversation filter mixed contacts");
  });
  test("Self-addressed notes do not create a self contact or duplicate history", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    w.sendMessage(0, 0, "note");
    w.readMessages(0);
    require(w.messageHistory(0).size() == 1 && !knows(w, 0, 0), "Self-note duplicated");
    w.validate();
  });
  test("Household membership is a known contact without fabricated encounter details", [] {
    auto w = small();
    w.households.push_back({0, 0, {0, 1}, {}});
    w.rebuildHouseholds();
    require(knows(w, 0, 1) && w.relationshipHistory(0, 1).empty(),
            "Household not known or invented history");
  });
  test("Reopening histories is a read-only operation", [] {
    auto w = small();
    w.talk(0, 1, 1);
    auto rng = w.rng;
    auto seq = w.sequence;
    auto money = w.internalMoney();
    auto conversations = w.relationships[1].conversations;
    for (int i = 0; i < 10; ++i) {
      w.knownContacts(0);
      w.messageHistory(0);
      w.relationshipHistory(0, 1);
    }
    require(w.rng == rng && w.sequence == seq && w.internalMoney() == money &&
                w.relationships[1].conversations == conversations,
            "Browsing changed simulation");
    w.validate();
  });
  test("Invalid contact/history requests are rejected without mutation", [] {
    auto w = small();
    rejects([&] { w.knownContacts(none); });
    rejects([&] { w.messageHistory(0, 999); });
    rejects([&] { w.messageHistory(999); });
    require(w.relationships.empty() && w.residentInformation.empty(),
            "Invalid query changed state");
  });
  test("Names switch language while all history/contact identities remain stable", [] {
    auto w = small();
    w.talk(0, 1, 1);
    auto before = w.knownContacts(0);
    auto en = w.displayName(1, "en"), fr = w.displayName(1, "fr"), zh = w.displayName(1, "zh");
    require(!en.empty() && !fr.empty() && !zh.empty() && en != zh && w.knownContacts(0) == before &&
                w.relationshipHistory(0, 1).back().actor == 0,
            "Language changed identities");
  });
  test("Delivered report recall retains the original observation and receipt dates", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    add(w, 1, ItemKind::Phone);
    auto n = help(w);
    double observed = w.time;
    w.shareInformation(0, 1, n, true);
    w.time += 60;
    w.readMessages(1);
    double learned = w.time;
    w.residentInformation.erase(1);
    w.time += 4000;
    require(w.recallMessage(1, 0), "Recall failed");
    const auto &i = w.knownInformation(1, n);
    require(i.origin == 0 && i.via == 0 && i.observedAt == observed && i.learnedAt == learned &&
                w.informationStale(i) && !w.recallMessage(1, 0),
            "Recall fabricated fresh observation or receipt");
    w.validate();
  });
  test("Recalling an older report cannot overwrite a newer observation from that source", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    add(w, 1, ItemKind::Phone);
    auto old = help(w);
    w.shareInformation(0, 1, old, true);
    w.readMessages(1);
    w.time += 1;
    w.residents[0].thirst = .9f;
    auto next = w.observeInformation(0, InformationKind::Help, 0);
    w.shareInformation(0, 1, next, true);
    w.readMessages(1);
    require(!w.recallMessage(1, 0) && w.knownInformation(1, next).detail == 1,
            "Old report replaced current source");
    w.validate();
  });
  test("Free text, another inbox and pending copies cannot be recalled as trusted reports", [] {
    auto w = small();
    for (Id id : {0, 1, 2})
      add(w, id, ItemKind::Phone);
    w.sendMessage(0, 1, "witness #0 did it");
    w.readMessages(1);
    rejects([&] { w.recallMessage(1, 0); });
    auto n = help(w);
    w.shareInformation(0, 1, n, true);
    rejects([&] { w.recallMessage(1, 1); });
    w.readMessages(1);
    rejects([&] { w.recallMessage(2, 1); });
    rejects([&] { w.recallMessage(0, 1); });
    rejects([&] { w.recallMessage(1, 999); });
    require(!w.residentInformation.contains(2), "Recall leaked or promoted free text");
  });
  test("Report recall requires awake actual device access", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    add(w, 1, ItemKind::Phone);
    auto n = help(w);
    w.shareInformation(0, 1, n, true);
    w.readMessages(1);
    w.residentInformation.erase(1);
    w.residents[1].activity = Activity::Sleeping;
    rejects([&] { w.recallMessage(1, 0); });
    w.residents[1].activity = Activity::Idle;
    offline(w, 1);
    rejects([&] { w.recallMessage(1, 0); });
    require(!w.residentInformation.contains(1), "Offline/asleep recall succeeded");
  });
  test("Death and departure retain historical contacts without publishing current locations", [] {
    auto w = small();
    w.talk(0, 1, 1);
    place(w, 2, {100, 0, 102});
    w.talk(0, 2, 1);
    auto before = w.knownContacts(0);
    w.kill(1, none);
    place(w, 2, w.layout(0).entrance());
    w.depart(2);
    require(w.knownContacts(0) == before && w.relationshipHistory(0, 1).size() == 1,
            "History identities lost after death/departure");
    w.validate();
  });
  test("Full histories, archives and recalled facts round-trip without replaying actions", [&] {
    auto w = small();
    for (Id id : {0, 1})
      add(w, id, ItemKind::Phone);
    for (int i = 0; i < 18; ++i) {
      w.talk(0, 1, 1);
      w.time += 301;
      w.sendMessage(0, 1, "saved " + std::to_string(i));
    }
    auto n = help(w);
    w.shareInformation(0, 1, n, true);
    w.readMessages(1);
    w.residentInformation.erase(1);
    auto contacts = w.knownContacts(1);
    auto rows = w.messageHistory(1);
    auto money = w.internalMoney();
    w.save(dir / "full.mdsave");
    auto loaded = World::load(dir / "full.mdsave");
    require(loaded.knownContacts(1) == contacts && loaded.messageHistory(1) == rows &&
                loaded.relationshipHistory(1, 0).size() == 16 && loaded.internalMoney() == money &&
                loaded.recallMessage(1, rows.front()) && loaded.knownInformation(1, n).origin == 0,
            "Reload lost or replayed history");
    loaded.validate();
  });
  test("Old text-only saves retain delivered history without inventing reports", [&] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    add(w, 1, ItemKind::Phone);
    w.sendMessage(0, 1, "old note");
    w.readMessages(1);
    w.save(dir / "legacy.mdsave");
    oldFormat(dir / "legacy.mdsave");
    auto loaded = World::load(dir / "legacy.mdsave");
    require(loaded.messageHistory(1).size() == 1 && knows(loaded, 1, 0) &&
                loaded.residentInformation.empty(),
            "Legacy history changed");
    loaded.validate();
  });
  test("Acceptance: real repeated friendship changes a later physical decision", [] {
    auto w = small();
    w.residents[0].personality.generosity = 0;
    w.residents[1].hunger = .8f;
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    require(!w.shareFood(0, 1), "Stranger should refuse");
    for (int i = 0; i < 8; ++i) {
      w.talk(0, 1, 1);
      w.time += 301;
    }
    require(w.shareFood(0, 1) &&
                w.items[w.residents[1].items.back()].owner == w.residents[1].account &&
                w.socialChoices[0].back().reason == "choice.friend",
            "Friendship did not change actual choice");
    w.validate();
  });
  test("Acceptance: equal conflict and different personalities produce explainable conserved "
       "outcomes",
       [] {
         auto generous = small(), selfish = small();
         for (auto *w : {&generous, &selfish}) {
           w->residents[1].hunger = .8f;
           add(*w, 0, ItemKind::Food);
           add(*w, 0, ItemKind::Food);
         }
         generous.residents[0].personality.generosity = 1;
         selfish.residents[0].personality.generosity = 0;
         auto cash = generous.internalMoney();
         require(generous.shareFood(0, 1) && !selfish.shareFood(0, 1) &&
                     generous.socialChoices[0].back().willing &&
                     !selfish.socialChoices[0].back().willing && generous.internalMoney() == cash &&
                     selfish.internalMoney() == cash,
                 "Conflict outcomes not distinct or conserved");
         generous.validate();
         selfish.validate();
       });
  test("Acceptance: interrupted delivery, source death and reload preserve player/NPC identities",
       [&] {
         auto w = small();
         add(w, 0, ItemKind::Phone);
         offline(w, 1);
         w.talk(0, 1, 1);
         auto n = help(w);
         w.shareInformation(0, 1, n, true);
         double observed = w.time;
         require(w.messageHistory(1).empty(), "Offline recipient learned message");
         auto playerName = w.displayName(0, "en"), npcName = w.displayName(1, "zh");
         w.save(dir / "interrupted.mdsave");
         auto resumed = World::load(dir / "interrupted.mdsave");
         add(resumed, 1, ItemKind::Phone);
         resumed.time += 60;
         resumed.readMessages(1);
         double received = resumed.time;
         resumed.kill(0, none);
         resumed.residentInformation.erase(1);
         resumed.save(dir / "deceased.mdsave");
         auto loaded = World::load(dir / "deceased.mdsave");
         require(!loaded.residents[0].alive && loaded.displayName(0, "en") == playerName &&
                     loaded.displayName(1, "zh") == npcName &&
                     loaded.relationshipHistory(1, 0).size() == 1 && knows(loaded, 1, 0) &&
                     loaded.recallMessage(1, 0),
                 "Interrupted/dead history lost identity");
         const auto &fact = loaded.knownInformation(1, n);
         require(fact.origin == 0 && fact.observedAt == observed && fact.learnedAt == received,
                 "Death/reload rewrote knowledge source or date");
         loaded.validate();
       });
  test("Unavailable contacts and sleeping senders fail locally without adding archive records", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    w.kill(1, none);
    std::string error;
    try {
      w.sendMessage(0, 1, "hello");
    } catch (const std::exception &e) {
      error = e.what();
    }
    require(error == "history.error.delivery" && w.messages.empty(),
            "Delivery failure not localized or archived");
    w.residents[0].activity = Activity::Sleeping;
    rejects([&] { w.sendMessage(0, 2, "hello"); });
    require(w.messages.empty() && w.messageHistory(0).empty(), "Sleeping sender transmitted text");
  });
  std::filesystem::remove_all(dir);
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
