#include "json.hpp"
#include "mega/world.hpp"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <zstd.h>
using namespace mega;
namespace {
int passed = 0, failed = 0;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
  bool rejected = false;
  try {
    action();
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "Invalid action/state was accepted");
}
void test(const char *name, const std::function<void()> &action) {
  try {
    action();
    ++passed;
    std::cout << "PASS " << name << '\n';
  } catch (const std::exception &e) {
    ++failed;
    std::cerr << "FAIL " << name << ": " << e.what() << '\n';
  }
}
World small() {
  Config config;
  config.population = 40;
  config.floors = 20;
  config.mode = Mode::Cheat;
  config.accidentRate = 0;
  auto w = World::generate(config);
  while (!w.events.empty())
    w.events.pop();
  for (auto &r : w.residents) {
    w.clockOut(r.id);
    w.leaveQueue(r.id);
    r.route.clear();
    r.task = TaskKind::None;
    r.automatic = false;
    r.activity = Activity::Idle;
    r.role = Role::Unemployed;
    r.jobSite = r.employer = none;
    r.pos = {100 + r.id * 3.f, 0, 100};
    r.aggression = 0;
    r.personality = {};
    r.strength = 40;
    r.hunger = r.thirst = .1f;
    r.hygiene = r.energy = 1;
    r.health = 100;
    r.interests.fill(.5f);
  }
  w.residents[1].pos = {100, 0, 101};
  w.crowd.clear();
  return w;
}
std::uint64_t pair(Id a, Id b) {
  return (std::uint64_t(std::min(a, b)) << 32) | std::uint32_t(std::max(a, b));
}
Id add(World &w, Id actor, ItemKind kind) {
  Item item;
  item.id = Id(w.items.size());
  item.owner = w.residents[actor].account;
  item.kind = kind;
  item.carrier = actor;
  item.pos = w.position(actor);
  w.items.push_back(item);
  w.residents[actor].items.push_back(item.id);
  return item.id;
}
void oldFieldsOnly(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
  auto size = ZSTD_getFrameContentSize(bytes.data() + 8, bytes.size() - 8);
  require(size != ZSTD_CONTENTSIZE_ERROR && size != ZSTD_CONTENTSIZE_UNKNOWN, "Invalid test save");
  std::vector<std::uint8_t> raw(size);
  require(
      !ZSTD_isError(ZSTD_decompress(raw.data(), raw.size(), bytes.data() + 8, bytes.size() - 8)),
      "Cannot decode test save");
  auto json = nlohmann::json::from_cbor(raw);
  json.erase("socialChoices");
  for (auto &resident : json["residents"])
    resident.erase("personality");
  for (auto &entry : json["relationships"]) {
    auto &bond = entry[1];
    bond.erase("regardA");
    bond.erase("regardB");
    bond.erase("memories");
  }
  raw = nlohmann::json::to_cbor(json);
  std::vector<char> compressed(ZSTD_compressBound(raw.size()));
  auto n = ZSTD_compress(compressed.data(), compressed.size(), raw.data(), raw.size(), 3);
  require(!ZSTD_isError(n), "Cannot encode old test save");
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write("MDORM001", 8);
  out.write(compressed.data(), n);
}
} // namespace
int main() {
  auto directory = std::filesystem::temp_directory_path() /
                   ("mega-choices-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  test("Traits are stable per seed/identity and do not consume world randomness", [] {
    auto w = small();
    auto rng = w.rng;
    auto a = Personality::generated(w.config.seed, 0), b = Personality::generated(w.config.seed, 0),
         c = Personality::generated(w.config.seed, 1);
    require(a.courage == b.courage && a.generosity == b.generosity &&
                a.conscience != c.conscience && w.rng == rng,
            "Traits changed RNG or identity");
  });
  test("Generous and selfish strangers make different real meal choices", [] {
    auto w = small();
    w.residents[0].personality.generosity = .95f;
    w.residents[1].hunger = .8f;
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    auto cash = w.internalMoney();
    require(w.shareFood(0, 1), "Generous stranger refused");
    require(w.items[w.residents[1].items.back()].owner == w.residents[1].account &&
                w.internalMoney() == cash,
            "No real conserved transfer");
    auto other = small();
    other.residents[0].personality.generosity = 0;
    other.residents[1].hunger = .8f;
    add(other, 0, ItemKind::Food);
    add(other, 0, ItemKind::Food);
    auto count = other.residents[0].items.size();
    require(!other.shareFood(0, 1) && other.residents[0].items.size() == count &&
                other.socialChoices[0].back().reason == "choice.selfish",
            "Selfish refusal had no effect");
    w.validate();
    other.validate();
  });
  test("Friendship built by actual conversations changes a later aid refusal", [] {
    auto w = small();
    w.residents[0].personality.generosity = 0;
    w.residents[1].hunger = .8f;
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    require(!w.shareFood(0, 1), "Initial stranger should refuse");
    for (int i = 0; i < 8; ++i) {
      w.talk(0, 1, 1);
      w.time += 301;
    }
    require(w.shareFood(0, 1) && w.socialChoices[0].back().reason == "choice.friend",
            "Real friendship did not affect aid");
    w.validate();
  });
  test("Recipient remembers assault and refuses an otherwise generous donor", [] {
    auto w = small();
    w.relationships[pair(0, 1)] = {0, 1, .8f, -3600, 0};
    w.rememberEncounter(0, 1, EncounterKind::Assault, 0, -.85f);
    w.residents[0].personality.generosity = 1;
    w.residents[1].hunger = .8f;
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    require(!w.shareFood(0, 1) && w.socialChoices[1].back().reason == "choice.grievance",
            "Recipient consent ignored known harm");
    w.validate();
  });
  test("An unrecognized theft cannot make its victim refuse a named stranger", [] {
    auto w = small();
    w.residents[1].activity = Activity::Sleeping;
    auto item = add(w, 1, ItemKind::Phone);
    w.stealItem(0, 1, item);
    w.residents[1].activity = Activity::Idle;
    require(w.considerChoice(1, 0, ChoiceKind::Cooperate).willing,
            "Hidden theft created named grievance");
  });
  test("A hungry donor and a donor with one meal preserve their own reserve", [] {
    auto w = small();
    w.residents[0].personality.generosity = 1;
    w.residents[1].hunger = .9f;
    add(w, 0, ItemKind::Food);
    require(!w.shareFood(0, 1), "Last meal gifted");
    add(w, 0, ItemKind::Food);
    w.residents[0].hunger = .8f;
    require(!w.shareFood(0, 1) && w.socialChoices[0].back().reason == "choice.reserve",
            "Starving donor gave reserve");
  });
  test("Sleeping, distant or wall-separated requests cannot transfer food", [] {
    auto w = small();
    w.residents[0].personality.generosity = 1;
    w.residents[1].hunger = .8f;
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    w.residents[1].activity = Activity::Sleeping;
    require(!w.shareFood(0, 1), "Asleep handover");
    w.residents[1].activity = Activity::Idle;
    w.residents[1].pos = {120, 0, 120};
    require(!w.shareFood(0, 1) && w.socialChoices.empty(), "Remote choice acted");
  });
  test("Low sociability and hostility can prevent information cooperation", [] {
    auto w = small();
    w.residents[0].hunger = .8f;
    w.residents[0].personality.sociability = 0;
    rejects([&] { w.shareLatestInformation(0, 1, InformationKind::Help); });
    require(w.residentInformation.empty() && w.socialChoices[0].back().reason == "choice.reserved",
            "Refusal still collected/shared");
    w.residents[0].personality.sociability = .5f;
    w.rememberEncounter(1, 0, EncounterKind::Assault, 0, -.85f);
    rejects([&] { w.shareLatestInformation(0, 1, InformationKind::Help); });
    require(w.socialChoices[0].back().reason == "choice.grievance",
            "Assault did not affect cooperation");
  });
  test("Trust earned later can overcome reserved cooperation", [] {
    auto w = small();
    w.residents[0].personality.sociability = 0;
    require(!w.considerChoice(0, 1, ChoiceKind::Cooperate).willing, "Reserved stranger cooperated");
    for (int i = 0; i < 8; ++i) {
      w.talk(0, 1, 1);
      w.time += 301;
    }
    require(w.considerChoice(0, 1, ChoiceKind::Cooperate).willing,
            "Reserved friend never cooperated");
  });
  test("A rejected seller meeting gives no address or reply", [] {
    auto w = small();
    add(w, 0, ItemKind::Phone);
    add(w, 1, ItemKind::Phone);
    auto item = add(w, 1, ItemKind::Food);
    w.listItem(1, item, 100);
    w.residents[1].personality.sociability = 0;
    rejects([&] { w.requestTradeMeeting(0, item); });
    require(w.messages.empty() && !w.residentInformation.contains(0),
            "Refused seller leaked location");
  });
  test("Explicitly controlled sharing remains a voluntary player action", [] {
    auto w = small();
    w.residents[0].hunger = .8f;
    auto n = w.observeInformation(0, InformationKind::Help, 0);
    w.residents[0].personality.sociability = 0;
    require(w.shareInformation(0, 1, n), "Personality overrode direct player send");
  });
  test("Conscience changes theft choice in the same reachable opportunity", [] {
    auto w = small();
    auto i = add(w, 1, ItemKind::Phone);
    w.residents[0].aggression = .8f;
    w.residents[0].personality.conscience = 1;
    require(!w.considerChoice(0, 1, ChoiceKind::Theft, i).willing,
            "Conscientious actor wanted theft");
    w.residents[0].personality.conscience = 0;
    require(w.considerChoice(0, 1, ChoiceKind::Theft, i).willing,
            "Amoral actor rejected tempting theft");
  });
  test("Trust prevents exploiting a friend even when amoral", [] {
    auto w = small();
    auto i = add(w, 1, ItemKind::Phone);
    w.residents[0].aggression = 1;
    w.residents[0].personality.conscience = 0;
    w.relationships[pair(0, 1)] = {0, 1, .8f, -3600, 0};
    auto c = w.considerChoice(0, 1, ChoiceKind::Theft, i);
    require(!c.willing && c.reason == "choice.loyalty", "Thief betrayed trusted friend by default");
  });
  test("Desperation can change a marginal theft choice", [] {
    auto w = small();
    auto i = add(w, 1, ItemKind::Food);
    w.residents[0].aggression = .2f;
    w.residents[0].personality.conscience = .4f;
    require(!w.considerChoice(0, 1, ChoiceKind::Theft, i).willing, "Calm well-fed actor stole");
    w.residents[0].hunger = 1.4f;
    w.residents[0].thirst = 1.2f;
    require(w.considerChoice(0, 1, ChoiceKind::Theft, i).willing,
            "Desperation did not change choice");
  });
  test("Only a visible nearby warden deters opportunistic theft", [] {
    auto w = small();
    auto i = add(w, 1, ItemKind::Phone);
    w.residents[0].aggression = 1;
    w.residents[0].personality.conscience = 0;
    w.residents[2].role = Role::Warden;
    w.residents[2].pos = {100, 0, 105};
    require(w.considerChoice(0, 1, ChoiceKind::Theft, i).reason == "choice.guard",
            "Visible guard not considered");
    w.residents[2].pos = {100, 3, 105};
    require(w.considerChoice(0, 1, ChoiceKind::Theft, i).willing,
            "Hidden other-floor guard deterred actor");
  });
  test("Distant, reserved or worn items are not theft opportunities", [] {
    auto w = small();
    auto i = add(w, 1, ItemKind::Phone);
    w.residents[0].aggression = 1;
    w.residents[0].personality.conscience = 0;
    w.residents[1].pos = {200, 0, 200};
    require(!w.considerChoice(0, 1, ChoiceKind::Theft, i).willing, "Remote theft considered");
    w.residents[1].pos = {100, 0, 101};
    w.items[i].reservedAt = 0;
    require(!w.considerChoice(0, 1, ChoiceKind::Theft, i).willing, "Reserved item considered");
  });
  test("A real queued NPC attempts reachable theft and creates actual evidence", [] {
    auto w = small();
    auto f = w.nearestAt({0, 0, 0}, FacilityKind::Water);
    w.facilities[f].staffNeeded = 0;
    w.facilities[f].stations = 1;
    w.facilities[f].duration = 86400;
    w.facilities[f].opensAt = 0;
    w.facilities[f].openHours = 24;
    for (Id id : {2, 1, 0}) {
      w.residents[id].pos = w.facilities[f].pos + Vec3{0, 0, 1.6f};
      w.joinQueue(id, f);
    }
    auto slots = w.queueSlots(f, w.facilities[f].queue.size());
    for (std::size_t n = 0; n < slots.size(); ++n) {
      Id id = w.facilities[f].queue[n];
      w.residents[id].pos = slots[n];
      w.residents[id].route.clear();
      w.residents[id].activity = Activity::Queuing;
      w.crowd.erase(id);
    }
    add(w, 1, ItemKind::Phone);
    w.residents[0].automatic = true;
    w.residents[0].aggression = 1;
    w.residents[0].personality.conscience = 0;
    auto before = w.incidents.size();
    require(w.opportunisticTheft(0) && w.incidents.size() == before + 1 &&
                w.incidents.back().kind == CrimeKind::Theft,
            "NPC did not make real theft attempt");
    w.validate();
  });
  test("A manual resident never makes an automatic theft attempt", [] {
    auto w = small();
    require(!w.opportunisticTheft(0) && w.incidents.empty(), "Manual player robbed autonomously");
  });
  test("Reporting uses conscience, courage and a visible identified threat", [] {
    auto w = small();
    w.residents[0].pos = {150, 0, 150};
    w.residents[1].pos = {151, 0, 150};
    w.residents[2].pos = {150, 0, 154};
    w.residents[0].strength = 100;
    w.residents[2].strength = 25;
    auto c = w.recordCrime(CrimeKind::Assault, 0, 1);
    w.residents[2].personality = {.5f, .1f, .05f, .5f};
    auto timid = w.considerChoice(2, none, ChoiceKind::Report, c);
    require(!timid.willing && timid.reason == "choice.fear", "Timid witness ignored threat");
    w.residents[2].personality.conscience = 1;
    w.residents[2].personality.courage = 1;
    require(w.considerChoice(2, none, ChoiceKind::Report, c).willing, "Brave witness refused");
  });
  test("Unknown offender identity does not leak into reporting choices", [] {
    auto w = small();
    w.residents[0].pos = {150, 0, 150};
    w.residents[1].pos = {151, 0, 150};
    w.residents[2].pos = {150, 0, 154};
    auto c = w.recordCrime(CrimeKind::Assault, 0, 1);
    for (auto &s : w.incidents[c].witnesses)
      if (s.witness == 2)
        s.identifies = false;
    auto a = w.considerChoice(2, none, ChoiceKind::Report, c);
    w.relationships[pair(0, 2)] = {0, 2, 1, -3600, 0};
    w.residents[0].strength = 1000;
    auto b = w.considerChoice(2, none, ChoiceKind::Report, c);
    require(a.other == none && b.other == none && a.willing == b.willing && a.reason == b.reason,
            "Hidden identity affected choice");
  });
  test("A trusted offender can receive a witness's loyalty without changing evidence", [] {
    auto w = small();
    w.residents[0].pos = {150, 0, 150};
    w.residents[1].pos = {151, 0, 150};
    w.residents[2].pos = {150, 0, 154};
    auto c = w.recordCrime(CrimeKind::Theft, 0, 1);
    w.residents[2].personality.conscience = .3f;
    w.relationships[pair(0, 2)] = {0, 2, .9f, -3600, 0};
    auto x = w.considerChoice(2, none, ChoiceKind::Report, c);
    require(!x.willing && x.reason == "choice.loyalty" && !w.incidents[c].reported,
            "Loyalty changed legal evidence");
  });
  test("An actual warden witness reports as a professional duty", [] {
    auto w = small();
    w.residents[0].pos = {150, 0, 150};
    w.residents[1].pos = {151, 0, 150};
    w.residents[2].pos = {150, 0, 154};
    auto c = w.recordCrime(CrimeKind::Assault, 0, 1);
    w.residents[2].role = Role::Warden;
    w.residents[2].personality.conscience = w.residents[2].personality.courage = 0;
    auto x = w.considerChoice(2, none, ChoiceKind::Report, c);
    require(x.willing && x.reason == "choice.duty", "Duty did not matter");
  });
  test("Uninvolved residents cannot manufacture a reporting decision", [] {
    auto w = small();
    w.residents[0].pos = {150, 0, 150};
    w.residents[1].pos = {151, 0, 150};
    auto c = w.recordCrime(CrimeKind::Assault, 0, 1);
    require(!w.considerChoice(10, none, ChoiceKind::Report, c).willing,
            "Unobserved event became known");
  });
  test("NPC reporting can refuse now and report after real danger leaves", [] {
    auto w = small();
    w.residents[0].pos = {150, 0, 150};
    w.residents[1].pos = {151, 0, 150};
    w.residents[2].pos = {150, 0, 154};
    w.residents[0].strength = 100;
    auto c = w.recordCrime(CrimeKind::Assault, 0, 1);
    add(w, 2, ItemKind::Phone);
    w.residents[2].automatic = true;
    w.residents[2].personality = {.5f, .65f, .05f, .5f};
    w.residents[2].strength = 20;
    w.securityTick();
    require(!w.incidents[c].reported, "Fearful witness reported immediately");
    w.residents[0].pos = {200, 0, 200};
    w.securityTick();
    require(w.incidents[c].reported && w.incidents[c].reporter == 2,
            "Witness never reported after danger left");
    w.validate();
  });
  test("An NPC refusal does not prevent another witness reporting", [] {
    auto w = small();
    w.residents[0].pos = {150, 0, 150};
    w.residents[1].pos = {151, 0, 150};
    w.residents[2].pos = {150, 0, 154};
    auto c = w.recordCrime(CrimeKind::Theft, 0, 1);
    for (Id id : {1, 2}) {
      w.residents[id].automatic = true;
      add(w, id, ItemKind::Phone);
    }
    w.residents[1].personality.conscience = 0;
    w.residents[1].personality.courage = 0;
    w.residents[0].strength = 100;
    w.residents[1].strength = 20;
    w.residents[2].personality.conscience = w.residents[2].personality.courage = 1;
    w.securityTick();
    require(w.incidents[c].reported && w.incidents[c].reporter == 2,
            "Victim refusal suppressed independent witness");
  });
  test("Timid and brave residents react differently to the same real attack", [] {
    auto a = small(), b = small();
    for (auto *w : {&a, &b}) {
      w->residents[0].strength = 50;
      w->residents[1].strength = 40;
      w->residents[1].energy = 1;
    }
    a.residents[1].personality.courage = 0;
    b.residents[1].personality.courage = 1;
    a.attack(0, 1);
    b.attack(0, 1);
    require(a.residents[1].surrendered && !b.residents[1].surrendered,
            "Courage did not change physical surrender");
    a.validate();
    b.validate();
  });
  test("Critical injury and overwhelming strength make even brave residents yield", [] {
    auto w = small();
    w.residents[1].personality.courage = 1;
    w.residents[1].health = 25;
    require(w.considerChoice(1, 0, ChoiceKind::Surrender).willing, "Critical injury ignored");
    w.residents[1].health = 100;
    w.residents[0].strength = 200;
    require(w.considerChoice(1, 0, ChoiceKind::Surrender).willing, "Overwhelming strength ignored");
  });
  test("A hidden firearm does not cause remote surrender", [] {
    auto w = small();
    w.residents[0].pos = {200, 0, 200};
    add(w, 0, ItemKind::Handgun);
    add(w, 0, ItemKind::Ammunition);
    require(!w.considerChoice(1, 0, ChoiceKind::Surrender).willing, "Remote threat perceived");
  });
  test("Decision history deduplicates polling and stays bounded", [] {
    auto w = small();
    for (int i = 0; i < 10; ++i)
      w.chooseSocial(0, 1, ChoiceKind::Cooperate);
    require(w.socialChoices[0].size() == 1, "Polling spammed history");
    for (int i = 0; i < 12; ++i) {
      w.time += 301;
      w.chooseSocial(0, 1, ChoiceKind::Cooperate);
    }
    require(w.socialChoices[0].size() == 8, "History unbounded");
    w.validate();
  });
  test("Traits, reasons and private targets survive save and reload", [&] {
    auto w = small();
    w.residents[0].personality = {.2f, .3f, .4f, .1f};
    w.chooseSocial(0, 1, ChoiceKind::Cooperate);
    w.save(directory / "current.mdsave");
    auto loaded = World::load(directory / "current.mdsave");
    require(loaded.residents[0].personality.conscience == .3f &&
                loaded.socialChoices[0].size() == 1 &&
                loaded.socialChoices[0].back().reason == "choice.reserved",
            "Saved decisions changed");
    loaded.validate();
  });
  test("Older saves get neutral traits and no invented decision history", [&] {
    auto w = small();
    w.save(directory / "old.mdsave");
    oldFieldsOnly(directory / "old.mdsave");
    auto loaded = World::load(directory / "old.mdsave");
    require(loaded.socialChoices.empty() && loaded.residents[0].personality.generosity == .5f &&
                loaded.residents[0].personality.conscience == .65f,
            "Legacy migration invented traits/history");
    loaded.validate();
  });
  test("Historical choices retain identities after death and departure", [] {
    auto w = small();
    w.chooseSocial(0, 1, ChoiceKind::Cooperate);
    w.kill(1, none);
    require(w.socialChoices[0].back().other == 1 &&
                !w.considerChoice(0, 1, ChoiceKind::Help).willing,
            "Dead target forgot or could act");
    w.validate();
  });
  test("Malformed traits, future decisions and invalid subjects are rejected", [] {
    auto w = small();
    w.residents[0].personality.courage = 2;
    rejects([&] { w.validateSocialChoices(); });
    w.residents[0].personality.courage = .5f;
    w.chooseSocial(0, 1, ChoiceKind::Cooperate);
    w.socialChoices[0].back().time = w.time + 1;
    rejects([&] { w.validateSocialChoices(); });
    w.socialChoices[0].back().time = w.time;
    w.socialChoices[0].back().reason = "made.up";
    rejects([&] { w.validateSocialChoices(); });
  });
  test("Invalid decision requests do not alter history or knowledge", [] {
    auto w = small();
    rejects([&] { w.chooseSocial(0, 0, ChoiceKind::Cooperate); });
    rejects([&] { w.chooseSocial(0, 1, ChoiceKind::Theft, none); });
    rejects([&] { w.chooseSocial(0, none, ChoiceKind::Report, 999); });
    require(w.socialChoices.empty() && w.residentInformation.empty(),
            "Invalid request mutated state");
  });
  test("Witness interviews respect an NPC refusal without manufacturing evidence", [] {
    auto w = small();
    w.residents[0].pos = {150, 0, 150};
    w.residents[1].pos = {151, 0, 150};
    w.residents[2].pos = {150, 0, 154};
    w.residents[3].pos = {150, 0, 155};
    w.residents[0].strength = 100;
    auto c = w.recordCrime(CrimeKind::Assault, 0, 1);
    w.residents[3].role = Role::Warden;
    w.residents[2].automatic = true;
    w.residents[2].personality = {.5f, 0, 0, .5f};
    w.residents[2].strength = 20;
    w.interviewWitness(3, c, 2);
    require(!w.incidents[c].reported, "Interview forced unwilling testimony");
    w.residents[2].personality.conscience = w.residents[2].personality.courage = 1;
    w.interviewWitness(3, c, 2);
    require(w.incidents[c].reported && w.incidents[c].suspect == 0,
            "Willing witness could not cooperate");
    w.validate();
  });
  test("A callback cannot force an unwilling NPC's eyewitness complaint", [] {
    auto w = small();
    w.residents[0].pos = {150, 0, 150};
    w.residents[1].pos = {151, 0, 150};
    w.residents[2].pos = {150, 0, 154};
    w.residents[3].pos = {150, 0, 155};
    w.residents[0].strength = 100;
    auto c = w.recordCrime(CrimeKind::Assault, 0, 1);
    auto n = w.observeInformation(2, InformationKind::Witness, c);
    w.shareInformation(2, 3, n);
    add(w, 2, ItemKind::Phone);
    add(w, 3, ItemKind::Phone);
    w.residents[2].automatic = true;
    w.residents[2].personality = {.5f, 0, 0, .5f};
    rejects([&] { w.reportInformation(3, n); });
    require(!w.incidents[c].reported, "Callback forced testimony");
    w.validate();
  });
  test("An automatic fighter reassesses surrender before retaliating", [] {
    auto w = small();
    w.residents[0].strength = 50;
    w.residents[1].strength = 40;
    w.residents[1].personality.courage = 1;
    w.attack(0, 1);
    require(!w.residents[1].surrendered, "Fixture surrendered early");
    w.residents[1].personality.courage = 0;
    w.residents[1].automatic = true;
    auto hp = w.residents[0].health;
    w.decide(1);
    require(w.residents[1].surrendered && w.residents[0].health == hp,
            "NPC retaliated despite deciding to yield");
    w.validate();
  });
  test("Recognized harm cancels a household aid plan before travel", [] {
    auto w = small();
    w.residents[0].automatic = true;
    w.residents[0].personality.generosity = 1;
    w.households.push_back({0, 0, {0, 1}, {}});
    w.rebuildHouseholds();
    w.relationships[1] = {0, 1, .6f, -3600, 0};
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    w.residents[1].hunger = .8f;
    w.residents[1].pos = {100, 0, 115};
    w.rememberEncounter(1, 0, EncounterKind::Assault, 0, -.85f);
    require(!w.householdDecision(0) && w.residents[0].route.empty(),
            "Household ignored donor's grievance");
    w.validate();
  });
  test("New arrivals receive stable traits without rewriting established identities", [] {
    auto w = small();
    Id vacant = none;
    for (Id id = 0; std::size_t(id) < w.housing.size(); ++id)
      if (w.housing[id].active && w.housing[id].tenant == none) {
        vacant = id;
        break;
      }
    require(vacant != none, "Missing vacant fixture home");
    auto name = w.residents[0].name;
    Id id = w.immigrate(vacant, 100000);
    auto expected = Personality::generated(w.config.seed, id);
    require(w.residents[id].personality.courage == expected.courage && w.residents[0].name == name,
            "Arrival traits changed identity");
    w.validate();
  });
  test("A carried firearm affects fear only when the visible opponent draws it in combat", [] {
    auto w = small();
    add(w, 0, ItemKind::Handgun);
    add(w, 0, ItemKind::Ammunition);
    require(!w.considerChoice(1, 0, ChoiceKind::Surrender).willing,
            "Concealed inventory caused fear");
    w.attack(0, 1);
    require(w.residents[1].surrendered, "Visible drawn firearm had no effect");
    w.validate();
  });
  test("Theft withdraws a listed item while preserving stolen ownership and resources", [] {
    auto w = small();
    auto item = add(w, 1, ItemKind::Phone);
    w.listItem(1, item, 500);
    w.residents[0].strength = 200;
    auto money = w.internalMoney();
    require(w.stealItem(0, 1, item, true), "Overwhelming robbery fixture failed");
    require(w.items[item].seller == none && w.items[item].askingPrice == 0 &&
                !w.itemOffers.contains(item) &&
                w.items[item].stolenFrom == w.residents[1].account && w.internalMoney() == money,
            "Stolen listing or money duplicated");
    w.validate();
  });
  std::filesystem::remove_all(directory);
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
