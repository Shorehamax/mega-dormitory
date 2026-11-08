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
int main() {
  auto dir = std::filesystem::temp_directory_path() /
             ("mega-information-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(dir);
  test("Visible facility gives a dated service snapshot, not live state", [] {
    auto w = small();
    Id f = facility(w, FacilityKind::Water);
    auto i = note(w, 0, InformationKind::Service, f);
    auto before = w.knownInformation(0, i);
    w.facilities[f].price += 50;
    w.facilities[f].stock = 0;
    require(before.price == 20 && w.knownInformation(0, i).price == 20,
            "Snapshot changed remotely");
    w.validate();
  });
  test("Walls prevent local observation", [] {
    auto w = small();
    Id f = facility(w, FacilityKind::Water);
    Id p = w.proposePartition(w.dormOwner, 0, {95, 0, 103}, {105, 0, 103}, false);
    w.interiorWalls[w.structuralWorks[p].wall].active = true;
    w.invalidateGeometry(0);
    rejects([&] { w.observeInformation(0, InformationKind::Service, f); });
  });
  test("Distant service status cannot be queried even with a phone", [] {
    auto w = small();
    Id f = facility(w, FacilityKind::Water, {150, 0, 150});
    add(w, 0, ItemKind::Phone);
    rejects([&] { w.observeInformation(0, InformationKind::Service, f); });
  });
  test("Published jobs can be read online but respect private access", [] {
    auto w = small();
    Id f = facility(w, FacilityKind::Water, {150, 0, 150});
    add(w, 0, ItemKind::Phone);
    w.facilities[f].hiring = true;
    auto i = note(w, 0, InformationKind::Work, f);
    require(w.knownInformation(0, i).available, "Hiring unavailable");
    w.facilities[f].publicAccess = false;
    w.facilities[f].owner = w.residents[3].account;
    w.facilities[f].whitelist.clear();
    rejects([&] { w.observeInformation(0, InformationKind::Work, f); });
  });
  test("Remote trade listing has no hidden seller location", [] {
    auto w = small();
    Id i = add(w, 3, ItemKind::Food);
    w.listItem(3, i, 100);
    place(w, 3, {180, 0, 180});
    add(w, 0, ItemKind::Phone);
    auto n = note(w, 0, InformationKind::Trade, i);
    require(!w.knownInformation(0, n).locationKnown && length(w.knownInformation(0, n).where) == 0,
            "Live seller location leaked");
    rejects([&] { w.followInformation(0, n); });
    w.validate();
  });
  test("Face-to-face offer preserves the place and price actually seen", [] {
    auto w = small();
    Id i = add(w, 1, ItemKind::Food);
    w.listItem(1, i, 100);
    offline(w, 0);
    auto n = note(w, 0, InformationKind::Trade, i);
    place(w, 1, {180, 0, 180});
    w.items[i].askingPrice = 200;
    require(w.knownInformation(0, n).price == 100 && w.knownInformation(0, n).where.z == 101,
            "Offer followed hidden seller");
    w.followInformation(0, n);
    require(w.residents[0].route.back().p.z < 110, "Follow used live location");
  });
  test("Help must originate in the resident's own real need", [] {
    auto w = small();
    rejects([&] { w.observeInformation(0, InformationKind::Help, 0); });
    w.residents[1].hunger = .9f;
    rejects([&] { w.observeInformation(0, InformationKind::Help, 1); });
    auto n = help(w);
    require(w.knownInformation(0, n).detail == 0, "Wrong help kind");
  });
  test("Nearby conversation relays original source, time and place", [] {
    auto w = small();
    auto n = help(w);
    auto original = w.knownInformation(0, n);
    w.time += 400;
    require(w.shareInformation(0, 1, n), "No first relay");
    place(w, 2, {100, 0, 102});
    w.time += 400;
    require(w.shareInformation(1, 2, n), "No second relay");
    const auto &x = w.knownInformation(2, n);
    require(x.origin == 0 && x.via == 1 && x.observedAt == original.observedAt &&
                x.learnedAt == w.time && x.where.z == 100 &&
                x.channel == InformationChannel::Conversation,
            "Provenance was refreshed or lost");
    w.validate();
  });
  test("Contact requires proximity, line of sight and awake peaceful actors", [] {
    auto w = small();
    auto n = help(w);
    place(w, 1, {100, 0, 104});
    rejects([&] { w.shareInformation(0, 1, n); });
    place(w, 1, {100, 0, 102});
    Id p = w.proposePartition(w.dormOwner, 0, {98, 0, 101}, {102, 0, 101}, false);
    w.interiorWalls[w.structuralWorks[p].wall].active = true;
    w.invalidateGeometry(0);
    rejects([&] { w.shareInformation(0, 1, n); });
    w.interiorWalls[w.structuralWorks[p].wall].active = false;
    w.invalidateGeometry(0);
    w.residents[1].activity = Activity::Sleeping;
    rejects([&] { w.shareInformation(0, 1, n); });
    w.residents[1].activity = Activity::Fighting;
    rejects([&] { w.shareInformation(0, 1, n); });
  });
  test("Invalid requested conversations have no collection side effect", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    w.residents[0].hunger = .9f;
    rejects([&] { w.shareLatestInformation(0, none, InformationKind::Help, true); });
    rejects([&] { w.shareLatestInformation(0, 0, InformationKind::Help, true); });
    require(w.residentInformation.empty() && w.messages.empty(), "Failed action changed knowledge");
  });
  test("Unknown and other people's private notes cannot be sent", [] {
    auto w = small();
    auto n = help(w);
    rejects([&] { w.shareInformation(1, 0, n); });
    rejects([&] { w.shareInformation(0, 1, n + 100); });
    require(w.messages.empty(), "Invalid send queued mail");
  });
  test("Device delivery waits for access and reads once", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    offline(w, 1);
    auto n = help(w);
    require(w.shareInformation(0, 1, n, true), "Not queued");
    rejects([&] { w.knownInformation(1, n); });
    rejects([&] { w.readMessages(1); });
    require(!w.messages.back().read, "Offline mail read");
    add(w, 1, ItemKind::Phone);
    w.time += 60;
    w.readMessages(1);
    require(w.knownInformation(1, n).learnedAt == w.time && w.messages.back().read,
            "Delivery failed");
    w.readMessages(1);
    require(w.residentInformation[1].size() == 1, "Duplicate reception");
    require(!w.shareInformation(0, 1, n, true), "Duplicate queued");
    w.validate();
  });
  test("Sleeping device owner receives only after waking", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    add(w, 1, ItemKind::Phone);
    auto n = help(w);
    w.shareInformation(0, 1, n, true);
    w.residents[1].automatic = true;
    w.residents[1].activity = Activity::Sleeping;
    w.communicationTick();
    require(!w.messages.back().read, "Sleeping resident read");
    w.residents[1].activity = Activity::Idle;
    w.communicationTick();
    require(w.messages.back().read && w.knownInformation(1, n).origin == 0,
            "Awake reception failed");
  });
  test("Public computer charges the actual owner and access expires", [] {
    auto w = small();
    Id f = facility(w, FacilityKind::Terminal, {110, 0, 110});
    offline(w, 1);
    place(w, 1, {110, 0, 111});
    add(w, 0, ItemKind::Phone);
    auto n = help(w);
    w.shareInformation(0, 1, n, true);
    auto balance = w.accounts[w.residents[1].account].balance;
    auto owner = w.accounts[w.operatingAccount(f)].balance;
    w.useTerminal(1, f, 1);
    w.readMessages(1);
    require(w.accounts[w.residents[1].account].balance == balance - 20 &&
                w.accounts[w.operatingAccount(f)].balance == owner + 20,
            "Computer payment missing");
    w.time += 61;
    rejects([&] { w.readMessages(1); });
    require(w.knownInformation(1, n).origin == 0, "Knowledge lost with session");
    w.validate();
  });
  test("Leaving a paid terminal stops further reception", [] {
    auto w = small();
    Id f = facility(w, FacilityKind::Terminal, {110, 0, 110});
    offline(w, 1);
    place(w, 1, {110, 0, 111});
    w.useTerminal(1, f, 5);
    place(w, 1, {120, 0, 120});
    rejects([&] { w.readMessages(1); });
  });
  test("Stale information remains dated when relayed", [] {
    auto w = small();
    auto n = help(w);
    w.time += 4000;
    w.shareInformation(0, 1, n);
    require(w.informationStale(w.knownInformation(1, n)) &&
                w.knownInformation(1, n).observedAt < w.time - 3900,
            "Relay refreshed stale fact");
  });
  test("Unchanged observation reuses its identity; changed observation replaces it", [] {
    auto w = small();
    auto n = help(w);
    require(help(w) == n, "Unchanged duplicated");
    w.time += 1;
    w.residents[0].thirst = .9f;
    auto next = help(w);
    require(next != n && w.residentInformation[0].size() == 1 &&
                w.knownInformation(0, next).detail == 1,
            "Changed need not replaced");
  });
  test("Actor cache retains at most 24 independent recent reports", [] {
    auto w = small();
    for (Id i = 1; i <= 30; ++i) {
      place(w, i, {100, 0, 101});
      auto n = help(w, i);
      w.shareInformation(i, 0, n);
    }
    require(w.residentInformation[0].size() == 24 && w.residentInformation[0].front().origin == 7,
            "Cache bound not enforced");
    w.validate();
  });
  test("Uninvolved residents cannot fabricate threats or eyewitness notes", [] {
    auto w = small();
    auto c = crime(w);
    rejects([&] { w.observeInformation(10, InformationKind::Witness, c); });
    rejects([&] { w.observeInformation(2, InformationKind::Threat, c); });
    require(w.residentInformation[10].empty(), "Fabricated knowledge");
  });
  test("Witness and victim observations retain actual identity", [] {
    auto w = small();
    auto c = crime(w);
    auto n = note(w, 2, InformationKind::Witness, c);
    auto t = note(w, 1, InformationKind::Threat, c);
    require(w.knownInformation(2, n).suspect == 0 && w.knownInformation(1, t).suspect == 0,
            "Witness identity absent");
    w.validate();
  });
  test("A relayed report never creates eyewitness testimony", [] {
    auto w = small();
    auto c = crime(w);
    auto n = note(w, 2, InformationKind::Witness, c);
    place(w, 3, w.position(2) + Vec3{0, 0, 1});
    w.shareInformation(2, 3, n);
    add(w, 3, ItemKind::Phone);
    auto count = w.incidents[c].witnesses.size();
    rejects([&] { w.fileCrimeReport(3, c); });
    offline(w, 2);
    place(w, 2, {180, 0, 180});
    rejects([&] { w.reportInformation(3, n); });
    add(w, 2, ItemKind::Phone);
    w.reportInformation(3, n);
    require(w.incidents[c].reporter == 2 && w.incidents[c].suspect == 0 &&
                w.incidents[c].witnesses.size() == count,
            "Relay fabricated testimony");
    w.validate();
  });
  test("Independent sources retain identifying and anonymous reports together", [] {
    auto w = small();
    auto c = crime(w);
    auto n = note(w, 2, InformationKind::Witness, c);
    place(w, 3, w.position(2) + Vec3{0, 0, 1});
    w.shareInformation(2, 3, n);
    Testimony t;
    t.witness = 4;
    t.seenAt = w.time + 1;
    t.seenScene = w.incidents[c].pos;
    t.identifies = false;
    w.time += 1;
    w.incidents[c].witnesses.push_back(t);
    auto anonymous = note(w, 4, InformationKind::Witness, c);
    place(w, 4, w.position(3) + Vec3{1, 0, 0});
    w.shareInformation(4, 3, anonymous);
    require(w.knownInformation(3, n).suspect == 0 &&
                w.knownInformation(3, anonymous).suspect == none,
            "Anonymous report erased identity or leaked suspect");
    w.validateCommunication();
  });
  test("Death preserves received history but prevents a fake callback", [] {
    auto w = small();
    auto c = crime(w);
    auto n = note(w, 2, InformationKind::Witness, c);
    place(w, 3, w.position(2) + Vec3{0, 0, 1});
    w.shareInformation(2, 3, n);
    w.kill(2, none);
    require(w.knownInformation(3, n).origin == 2, "Dead source forgotten");
    rejects([&] { w.reportInformation(3, n); });
    w.validate();
  });
  test("NPC face-to-face exchange uses actual contact", [] {
    auto w = small();
    auto n = help(w);
    place(w, 1, {180, 0, 180});
    w.exchangeInformation(0, 1);
    rejects([&] { w.knownInformation(1, n); });
    place(w, 1, {100, 0, 101});
    w.exchangeInformation(0, 1);
    require(w.knownInformation(1, n).origin == 0, "Local exchange absent");
  });
  test("Trusted NPCs send device reports with an hourly cooldown", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    add(w, 1, ItemKind::Phone);
    w.relationships[1] = {0, 1, .8f, w.time - 3600, 0};
    w.residents[0].automatic = w.residents[1].automatic = true;
    auto n = help(w);
    w.communicationTick();
    require(w.messages.size() == 1, "NPC did not share");
    w.communicationTick();
    require(w.messages.size() == 1 && w.knownInformation(1, n).origin == 0,
            "NPC duplicate or no reception");
    w.time += 1;
    w.residents[0].thirst = .9f;
    help(w);
    w.communicationTick();
    require(w.messages.size() == 1, "Cooldown ignored");
    w.validate();
  });
  test("Round-trip restores pending delivery and duplicate indices", [&] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    offline(w, 1);
    auto n = help(w);
    w.shareInformation(0, 1, n, true);
    w.save(dir / "pending.mdsave");
    auto loaded = World::load(dir / "pending.mdsave");
    require(!loaded.shareInformation(0, 1, n, true), "Save lost send dedup");
    add(loaded, 1, ItemKind::Phone);
    loaded.readMessages(1);
    require(loaded.knownInformation(1, n).origin == 0, "Save lost pending payload");
    loaded.validate();
  });
  test("Old text-only saves gain no invented knowledge", [&] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    w.sendMessage(0, 1, "hello");
    w.save(dir / "legacy.mdsave");
    oldFormat(dir / "legacy.mdsave");
    auto loaded = World::load(dir / "legacy.mdsave");
    require(loaded.informationSequence == 0 && loaded.residentInformation.empty() &&
                !loaded.messages[0].hasInformation && loaded.messages[0].text == "hello",
            "Legacy text mutated");
    loaded.validate();
  });
  test("Malformed records and derived mail indices are rejected", [] {
    auto w = small();
    auto n = help(w);
    w.informationSequence = 0;
    rejects([&] { w.validateCommunication(); });
    w.informationSequence = n;
    w.residentInformation[0][0].origin = 999;
    rejects([&] { w.validateCommunication(); });
    w.residentInformation[0][0].origin = 0;
    w.residentInformation[0][0].learnedAt = w.time + 1;
    rejects([&] { w.validateCommunication(); });
    w.residentInformation[0][0].learnedAt = w.time;
    add(w, 0, ItemKind::Phone);
    w.shareInformation(0, 1, n, true);
    w.unreadInformation.clear();
    rejects([&] { w.validateCommunication(); });
    w.rebuildInformationMail();
    w.validateCommunication();
  });
  test("A remote meeting needs an awake connected seller and records their reply", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    offline(w, 1);
    Id item = add(w, 1, ItemKind::Food);
    w.listItem(1, item, 100);
    place(w, 1, {180, 0, 180});
    rejects([&] { w.requestTradeMeeting(0, item); });
    require(w.messages.empty(), "Offline seller fabricated reply");
    add(w, 1, ItemKind::Phone);
    w.residents[1].activity = Activity::Sleeping;
    rejects([&] { w.requestTradeMeeting(0, item); });
    w.residents[1].activity = Activity::Idle;
    auto n = w.requestTradeMeeting(0, item);
    const auto &report = w.knownInformation(0, n);
    require(report.origin == 1 && report.via == 1 && report.locationKnown &&
                report.where.x == 180 && report.channel == InformationChannel::Device &&
                w.messages.back().read,
            "Meeting lacked seller's real dated reply");
    place(w, 1, {200, 0, 200});
    w.followInformation(0, n);
    require(w.residents[0].route.back().p.x < 190, "Meeting tracked seller after reply");
    w.validate();
  });
  test("Local sellers can explain their own real offers", [] {
    auto w = small();
    Id item = add(w, 1, ItemKind::Food);
    w.listItem(1, item, 100);
    w.shareLatestInformation(1, 0, InformationKind::Trade);
    const auto &notes = w.residentInformation.at(0);
    require(std::any_of(notes.begin(), notes.end(),
                        [&](const Information &i) {
                          return i.kind == InformationKind::Trade && i.subject == item &&
                                 i.origin == 1;
                        }),
            "Seller could not tell buyer about offer");
    w.validate();
  });
  test("Receipt records delivery time in both inbox and resident knowledge", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    add(w, 1, ItemKind::Phone);
    auto n = help(w);
    w.shareInformation(0, 1, n, true);
    w.time += 90;
    w.readMessages(1);
    require(w.messages.back().information.learnedAt == w.time &&
                w.knownInformation(1, n).learnedAt == w.time,
            "Inbox showed sender's learning time");
    w.validate();
  });
  test("Broken, closed, demolished or access-revoked terminals stop delivery", [] {
    auto w = small();
    Id f = facility(w, FacilityKind::Terminal, {110, 0, 110});
    offline(w, 1);
    place(w, 1, {110, 0, 111});
    w.useTerminal(1, f, 5);
    w.facilities[f].condition = 0;
    require(!w.hasDevice(1), "Broken computer accepted");
    w.facilities[f].condition = 1;
    w.facilities[f].open = false;
    require(!w.hasDevice(1), "Closed computer accepted");
    w.facilities[f].open = true;
    w.facilities[f].demolished = true;
    require(!w.hasDevice(1), "Demolished computer accepted");
    w.facilities[f].demolished = false;
    w.facilities[f].publicAccess = false;
    require(!w.hasDevice(1), "Revoked access accepted");
  });
  test("Forged eyewitness origin and suspect cannot be loaded as real reports", [] {
    auto w = small();
    auto c = crime(w);
    auto n = note(w, 2, InformationKind::Witness, c);
    auto &notes = w.residentInformation[2];
    auto found =
        std::find_if(notes.begin(), notes.end(), [&](const Information &i) { return i.id == n; });
    found->origin = 10;
    found->channel = InformationChannel::Device;
    rejects([&] { w.validateCommunication(); });
    found->origin = 2;
    found->suspect = none;
    rejects([&] { w.validateCommunication(); });
  });
  std::filesystem::remove_all(dir);
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
