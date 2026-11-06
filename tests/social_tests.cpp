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
                   ("mega-social-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  test("Unknown people have no invented contact history", [] {
    auto w = small();
    require(w.relationshipStatus(0, 1) == RelationshipStatus::Stranger &&
                w.relationshipHistory(0, 1).empty(),
            "Stranger has a relationship");
    w.validate();
  });
  test("Repeated real conversation creates friendship and chronological memories", [] {
    auto w = small();
    for (int i = 0; i < 8; ++i) {
      w.talk(0, 1, 1);
      w.time += 301;
    }
    require(w.relationshipStatus(0, 1) == RelationshipStatus::Friend &&
                w.relationshipStatus(1, 0) == RelationshipStatus::Friend,
            "Shared interests did not build friendship");
    auto history = w.relationshipHistory(1, 0);
    require(history.size() == 8 && history.back().kind == EncounterKind::Interests &&
                history.back().actor == 0 && history.front().time < history.back().time,
            "Encounter chronology missing");
    w.validate();
  });
  test("Insults create asymmetric grievances and rivalry", [] {
    auto w = small();
    w.talk(0, 1, 3);
    w.time += 301;
    w.talk(0, 1, 3);
    require(w.trust(1, 0) < w.trust(0, 1) &&
                w.relationshipStatus(1, 0) == RelationshipStatus::Rival,
            "Victim and offender have identical regard");
    w.validate();
  });
  test("Assault changes the victim's regard without informing strangers", [] {
    auto w = small();
    float before = w.trust(1, 0);
    w.attack(0, 1);
    require(w.trust(1, 0) < before - .5f && w.trust(1, 0) < w.trust(0, 1),
            "Assault has no personal grievance");
    require(w.relationshipHistory(1, 0).back().kind == EncounterKind::Assault &&
                w.relationshipHistory(2, 0).empty(),
            "Attack history fabricated a witness relationship");
    w.validate();
  });
  test("Unnoticed sleeping-victim theft does not reveal the thief", [] {
    auto w = small();
    Id item = add(w, 1, ItemKind::Phone);
    w.residents[1].activity = Activity::Sleeping;
    w.stealItem(0, 1, item);
    require(w.relationshipHistory(0, 1).size() == 1 && w.relationshipHistory(1, 0).empty() &&
                w.trust(1, 0) == 0 && w.relationshipStatus(1, 0) == RelationshipStatus::Stranger,
            "Sleeping victim learned an unknown thief");
    w.validate();
  });
  test("Observed attempted theft creates a grievance even when no item changes hands", [] {
    auto w = small();
    Id item = add(w, 1, ItemKind::Phone);
    w.stealItem(0, 1, item);
    require(w.trust(1, 0) < -.5f && w.relationshipHistory(1, 0).back().kind == EncounterKind::Theft,
            "Observed attempt has no consequence");
    w.validate();
  });
  test("A robbery wakes the victim and preserves its known encounter", [] {
    auto w = small();
    Id item = add(w, 1, ItemKind::Phone);
    w.residents[1].activity = Activity::Sleeping;
    w.residents[0].strength = 100;
    require(w.stealItem(0, 1, item, true), "Strong robber fixture failed");
    require(w.relationshipHistory(1, 0).size() == 1 && w.trust(1, 0) < -.5f,
            "Robbery knowledge was lost");
    w.validate();
  });
  test("Invalid conversations and distant attacks create no memories", [] {
    auto w = small();
    w.residents[1].pos = {120, 0, 120};
    rejects([&] { w.talk(0, 1, 0); });
    rejects([&] { w.attack(0, 1); });
    require(w.relationships.empty(), "Failed action created an encounter");
    w.residents[1].pos = {100, 0, 101};
    w.residents[0].activity = Activity::Sleeping;
    rejects([&] { w.talk(0, 1, 1); });
    require(w.relationships.empty(), "Sleeping actor conversed");
  });
  test("Only successful paid local trades create trust and one memory", [] {
    auto w = small();
    Id item = add(w, 1, ItemKind::Phone);
    w.listItem(1, item, 100);
    Money money = w.internalMoney();
    w.residents[1].pos = {120, 0, 120};
    rejects([&] { w.buyItem(0, item); });
    require(w.relationshipHistory(0, 1).empty(), "Remote refused trade created trust");
    w.residents[1].pos = {100, 0, 101};
    w.buyItem(0, item);
    rejects([&] { w.buyItem(0, item); });
    require(w.relationshipHistory(0, 1).size() == 1 && w.trust(0, 1) > 0 &&
                w.items[item].owner == w.residents[0].account && w.internalMoney() == money,
            "Trade memory or actual settlement duplicated");
    w.validate();
  });
  test("Real meal aid has asymmetric gratitude and cannot farm the cooldown", [] {
    auto w = small();
    w.relationships[pair(0, 1)] = {0, 1, .6f, -3600, 0};
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    w.residents[1].hunger = .8f;
    require(w.shareFood(0, 1), "Trusted meal aid failed");
    require(w.trust(1, 0) > w.trust(0, 1) &&
                w.relationshipHistory(1, 0).back().kind == EncounterKind::Meal,
            "Meal gratitude missing");
    require(!w.shareFood(0, 1) && w.relationshipHistory(0, 1).size() == 1,
            "Failed repeated aid created another reward");
    w.validate();
  });
  test("A harmed resident refuses aid despite the donor's old shared trust", [] {
    auto w = small();
    w.relationships[pair(0, 1)] = {0, 1, .8f, -3600, 0};
    w.rememberEncounter(0, 1, EncounterKind::Assault, 0, -.85f);
    add(w, 0, ItemKind::Food);
    add(w, 0, ItemKind::Food);
    w.residents[1].hunger = .8f;
    require(!w.shareFood(0, 1), "Donor's opinion overrode recipient's refusal");
    w.validate();
  });
  test("Trimming old memories never forgives accumulated grievances", [] {
    auto w = small();
    for (int i = 0; i < 4; ++i) {
      w.talk(0, 1, 3);
      w.time += 301;
    }
    float grievance = w.trust(1, 0);
    for (int i = 0; i < 20; ++i) {
      w.talk(0, 1, 2);
      w.time += 301;
    }
    auto history = w.relationshipHistory(1, 0);
    require(history.size() == 16 && history.front().kind == EncounterKind::Work &&
                w.trust(1, 0) == grievance,
            "History retention erased social consequences");
    w.validate();
  });
  test("Apologies repair slowly without resetting an attack", [] {
    auto w = small();
    w.attack(0, 1);
    for (Id id : {0, 1}) {
      auto &r = w.residents[id];
      r.activity = Activity::Idle;
      r.opponent = none;
      r.surrendered = false;
    }
    w.time += 301;
    float before = w.trust(1, 0);
    w.talk(0, 1, 4);
    require(w.trust(1, 0) > before && w.relationshipStatus(1, 0) == RelationshipStatus::Rival,
            "One apology erased violence");
    w.validate();
  });
  test("Directional and private memory round-trips once", [&] {
    auto w = small();
    Id item = add(w, 1, ItemKind::Phone);
    w.residents[1].activity = Activity::Sleeping;
    w.stealItem(0, 1, item);
    auto file = directory / "private.mdsave";
    w.save(file);
    auto restored = World::load(file);
    require(restored.relationshipHistory(0, 1).size() == 1 &&
                restored.relationshipHistory(1, 0).empty() &&
                restored.trust(0, 1) == w.trust(0, 1) && restored.trust(1, 0) == 0,
            "Saved private memory leaked or duplicated");
    restored.validate();
  });
  test("Older shared-trust saves load without inventing directional events", [&] {
    auto w = small();
    w.relationships[pair(0, 1)] = {0, 1, .7f, w.time, 0};
    auto file = directory / "old-fields.mdsave";
    w.save(file);
    oldFieldsOnly(file);
    auto restored = World::load(file);
    require(restored.trust(0, 1) == .7f && restored.trust(1, 0) == .7f &&
                restored.relationshipHistory(0, 1).empty(),
            "Legacy trust was lost or fabricated");
    restored.validate();
  });
  test("Death and departure retain identities and actual encounter history", [&] {
    auto w = small();
    w.talk(0, 1, 1);
    w.kill(1);
    auto file = directory / "death.mdsave";
    w.save(file);
    auto restored = World::load(file);
    require(!restored.residents[1].alive && restored.relationshipHistory(0, 1).size() == 1,
            "Death erased encounter history");
    auto departed = small();
    departed.talk(0, 1, 1);
    departed.residents[1].present = false;
    require(departed.relationshipHistory(0, 1).size() == 1, "Departure erased encounter history");
  });
  test("Malformed memories, hidden effects and future events are rejected", [] {
    auto w = small();
    w.talk(0, 1, 1);
    auto &memory = w.relationships.at(pair(0, 1)).memories.back();
    auto valid = memory;
    memory.kind = EncounterKind(500);
    rejects([&] { w.validate(); });
    memory = valid;
    memory.actor = 5;
    rejects([&] { w.validate(); });
    memory = valid;
    memory.time = w.time + 1;
    rejects([&] { w.validate(); });
    memory = valid;
    memory.knownToB = false;
    memory.changeB = .1f;
    rejects([&] { w.validate(); });
    memory = valid;
    w.relationships.at(pair(0, 1)).regardA = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { w.validate(); });
  });
  test("Directional trust governs fallback heirs rather than stale imported trust", [] {
    auto w = small();
    w.relationships[pair(0, 1)] = {0, 1, .8f, -3600, 0};
    w.rememberEncounter(1, 0, EncounterKind::Theft, 0, -.65f);
    w.kill(0);
    require(!w.estates.back().candidates.contains(1),
            "A known thief inherited via stale shared trust");
    w.validate();
  });
  std::filesystem::remove_all(directory);
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
