#include "json.hpp"
#include "mega/world.hpp"
#include <fstream>
#include <iterator>
#include <zstd.h>
namespace mega {
using nlohmann::json;
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec3, x, y, z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Config, floors, population, seed, mode, dayMinutes,
                                                scarcity, startingCash, aggression, bedsPerFloor,
                                                supplies, wardensPerThousand, layoutVersion,
                                                floorWidth, floorDepth, lobbyWidth, lobbyDepth,
                                                accidentRate)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FloorPlan, width, depth)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Expansion, id, owner, contractor, worker, escrow, floor, width,
                                   depth, budget, paid, remaining, totalWork, complete, site,
                                   workAt)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Account, balance, name)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Transfer, time, from, to, amount, reason)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Waypoint, p, time)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Routine, shiftStart, shiftHours, sleepStart,
                                                sleepHours, daysOff, workSince, unpaidWages,
                                                wageClaims)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Appearance, primary, secondary, style, accessory)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HomeStyle, frame, linen, blanket, closet, curtain,
                                                closetOpen)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Resident, id, account, home, employer, jobSite, name, age, pos, route, routeVersion, activity,
    role, task, target, queuedAt, carrying, opponent, escort, hunger, thirst, energy, health,
    happiness, strength, aggression, needsAt, terminalUntil, nextHit, releaseAt, terminal, alive,
    present, surrendered, items, hourlyWage, automatic, interests, hygiene, bladder, outfit,
    vehicle, routine, ladder, ladderExit, ladderWait, ladderTarget, ladderDirection, ambulance,
    medicalDuty, carriedBy, bodyCarrying, medicalProvider, medicalTier, medicalUntil, infectedAt,
    immuneUntil)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Ambulance, id, provider, patient, medic, pilot,
                                                guard, state, tier, pos, landing, route, readyAt)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Remains, resident, carrier, storage, worker,
                                                destination, ambulance, lift, pos, diedAt,
                                                burnUntil, cremated, burnRemaining, burnWorkedAt)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LocalName, en, fr, zh)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HousingUnit, owner, tenant, floor, bed, rent, price,
                                                listed, active, decor, paidUntil, arrears, custom,
                                                location)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Service, resident, until, remaining, price, station)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Facility, id, owner, manager, kind, floor, pos,
                                                stations, stock, price, duration, open, queue,
                                                serving, stops, whitelist, subscriptionPrice,
                                                subscribers, askingPrice, staffRole, staffNeeded,
                                                opensAt, openHours, publicAccess, employees,
                                                autoRestock, hiring, wageOffer, condition, waste,
                                                demolished)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Item, id, owner, carrier, kind, pos, consumed,
                                                order, cleanliness, durability, reservedAt, look,
                                                stolenFrom, quantity, askingPrice, seller)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Order, id, buyer, shop, courier, item, escrow, product, price,
                                   deliveryFee, state, placed, delivered)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Freight, id, facility, worker, item, escrow,
                                                quantity, cost, fee, state, placed, waste, source,
                                                readyAt, origin, landing)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LiftCall, resident, from, to, called)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LiftPassenger, resident, destination, from, seat, start, until,
                                   exiting)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LiftCar, id, facility, y, startY, targetY, departAt, arriveAt,
                                   doorsUntil, moving, calls, passengers)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LiftJourney, lift, destination, goal, task, target)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Repair, id, facility, worker, escrow, workshop,
                                                partItem, budget, remaining, workedAt, parts,
                                                complete)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Fire, id, facility, worker, station, payer, escrow,
                                                heat, smoke, fuel, started, updated, budget, paid,
                                                contained, out)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FireSpray, fire, item, workedAt)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Company, id, owner, account, leader, name,
                                                assignments, completed, revenue)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WorkspaceLease, id, company, facility, landlord,
                                                manager, escrow, dailyRent, deposit, arrears,
                                                nextDue, missedDays, active)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OfficeOrder, id, facility, worker, escrow, role,
                                                brief, budget, remaining, total, workAt, due,
                                                submitted, quality, submission, client, employer,
                                                liveRequests, tickets, dispatched, outcomes)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Relationship, a, b, trust, lastTalk, sharedInterest)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Message, from, to, time, text, read)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Vehicle, id, owner, dealer, driver, bay, kind, pos,
                                                yaw, speed, condition, askingPrice, originalPrice,
                                                look)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParkingBay, id, facility, vehicle, pos, width, dailyPrice,
                                   paidUntil)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Incident, id, offender, victim, warden, kind, pos, time,
                                   resolved, threat)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Project, id, owner, facility, worker, escrow, state,
                                                budget, spent, compensation, remaining, affected,
                                                consented, demolition, offer, newConstruction,
                                                materialsDelivered, materialItem, source,
                                                contractor, materials, workAt)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HousingWork, id, actor, payer, contractor, workshop,
                                                worker, home, escrow, material, replacement, kind,
                                                state, budget, offer, floor, width, depth, site,
                                                remaining, total, workedAt, supplied, forced,
                                                consented)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Governance, askingPrice, successor, retiring,
                                                reviewedDay)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(VideoJob, id, worker, client, employer, escrow, timeline, score,
                                   submitted, budget, deadline)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(News, time, key, subject, other)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Event, time, seq, kind, id, other, version)
static std::uint32_t frameTag(const char *p) {
  return std::uint32_t(static_cast<unsigned char>(p[0])) |
         (std::uint32_t(static_cast<unsigned char>(p[1])) << 8) |
         (std::uint32_t(static_cast<unsigned char>(p[2])) << 16) |
         (std::uint32_t(static_cast<unsigned char>(p[3])) << 24);
}
void World::save(const std::filesystem::path &path) const {
  validate();
  json j = {{"schema", 1},
            {"config", config},
            {"floorPlans", floorPlans},
            {"expansions", expansions},
            {"time", time},
            {"rng", rng},
            {"sequence", sequence},
            {"player", player},
            {"dormOwner", dormOwner},
            {"governance", governance},
            {"dormAccount", dormAccount},
            {"gameOver", gameOver},
            {"endReason", endReason},
            {"saveName", saveName},
            {"accounts", accounts},
            {"ledger", ledger},
            {"residents", residents},
            {"names", names},
            {"housing", housing},
            {"housingWorks", housingWorks},
            {"residentHomes", residentHomes},
            {"facilities", facilities},
            {"items", items},
            {"vehicles", vehicles},
            {"ambulances", ambulances},
            {"remains", remains},
            {"parking", parking},
            {"orders", orders},
            {"freight", freight},
            {"lifts", lifts},
            {"officeOrders", officeOrders},
            {"companies", companies},
            {"workspaceLeases", workspaceLeases},
            {"workspaceOffers", workspaceOffers},
            {"repairs", repairs},
            {"fires", fires},
            {"fireSprays", fireSprays},
            {"nextAccidentAt", nextAccidentAt},
            {"liftJourneys", liftJourneys},
            {"lastEconomyDay", lastEconomyDay},
            {"messages", messages},
            {"relationships", relationships},
            {"incidents", incidents},
            {"projects", projects},
            {"videos", videos},
            {"news", news}};
  // Save the actual nearby crowd positions and their remaining path, not a
  // stale interpolation deadline that would teleport them after loading.
  for (const auto &[id, b] : crowd) {
    auto r = residents[id];
    r.pos = b.pos;
    if (!r.route.empty()) {
      std::vector<Waypoint> remaining{{b.pos, time}};
      double at = time;
      Vec3 p = b.pos;
      for (std::size_t n = b.next; n < r.route.size(); ++n) {
        double speed = std::abs(r.route[n].p.y - p.y) > .1f ? 3.0 : walkingSpeed(id);
        at += length(r.route[n].p - p) / speed * (1440 / config.dayMinutes);
        p = r.route[n].p;
        remaining.push_back({p, at});
      }
      r.route = std::move(remaining);
    }
    j["residents"][id] = r;
  }
  for (auto &r : residents) {
    j["residents"][r.id]["leaving"] = r.leaving;
    j["residents"][r.id]["waitSince"] = r.waitSince;
    j["residents"][r.id]["lift"] = r.lift;
    j["residents"][r.id]["lethalThreat"] = r.lethalThreat;
    j["residents"][r.id]["personalWaste"] = r.personalWaste;
    j["residents"][r.id]["fitness"] = r.fitness;
  }
  auto copy = events;
  std::vector<Event> pending;
  while (!copy.empty()) {
    pending.push_back(copy.top());
    copy.pop();
  }
  j["events"] = pending;
  auto raw = json::to_cbor(j);
  if (raw.size() > 512 * 1024 * 1024ULL)
    throw std::runtime_error("Save exceeds supported size");
  std::vector<char> compressed(ZSTD_compressBound(raw.size()));
  auto ctx = ZSTD_createCCtx();
  if (!ctx)
    throw std::runtime_error("Cannot allocate save compressor");
  ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, 3);
  ZSTD_CCtx_setParameter(ctx, ZSTD_c_checksumFlag, 1);
  auto n = ZSTD_compress2(ctx, compressed.data(), compressed.size(), raw.data(), raw.size());
  ZSTD_freeCCtx(ctx);
  if (ZSTD_isError(n))
    throw std::runtime_error("Save compression failed");
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path());
  auto tmp = path;
  tmp += ".tmp";
  auto backup = path;
  backup += ".bak";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f.write("MDORM001", 8);
    f.write(compressed.data(), static_cast<std::streamsize>(n));
    f.flush();
    if (!f)
      throw std::runtime_error("Cannot write save");
  }
  auto meta = path;
  meta += ".meta.json";
  auto backupMeta = backup;
  backupMeta += ".meta.json";
  bool repairing = !recoveredSavePath.empty() &&
                   recoveredSavePath == std::filesystem::absolute(path).lexically_normal();
  if (std::filesystem::exists(path) && !repairing) {
    std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing);
    std::error_code ignored;
    std::filesystem::copy_file(meta, backupMeta, std::filesystem::copy_options::overwrite_existing,
                               ignored);
  }
  std::filesystem::rename(tmp, path);
  if (repairing)
    recoveredSavePath.clear();
  // The world file remains authoritative if advisory metadata cannot be written.
  try {
    json info = {{"version", 1},           {"name", saveName},
                 {"time", time},           {"floors", config.floors},
                 {"population", living()}, {"mode", int(config.mode)},
                 {"bytes", n + 8},         {"frameTag", frameTag(compressed.data() + n - 4)}};
    auto temporary = meta;
    temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << info.dump();
    out.flush();
    if (!out)
      throw std::runtime_error("Cannot write slot metadata");
    out.close();
    std::filesystem::rename(temporary, meta);
  } catch (const std::exception &) {
  }
}
SaveInfo World::inspectSave(const std::filesystem::path &path) {
  SaveInfo info;
  std::error_code error;
  auto actual = path;
  if (!std::filesystem::is_regular_file(actual, error)) {
    actual += ".bak";
    info.backup = true;
  }
  if (!std::filesystem::is_regular_file(actual, error))
    return info;
  info.available = true;
  auto meta = actual;
  meta += ".meta.json";
  try {
    if (std::filesystem::file_size(meta) > 8192)
      return info;
    std::ifstream input(meta, std::ios::binary);
    auto data = json::parse(input);
    if (data.at("version") != 1 ||
        data.at("bytes").get<std::uint64_t>() != std::filesystem::file_size(actual))
      return info;
    std::ifstream world(actual, std::ios::binary);
    char header[8], tag[4];
    world.read(header, 8);
    world.seekg(-4, std::ios::end);
    world.read(tag, 4);
    if (!world || std::string(header, 8) != "MDORM001" ||
        data.at("frameTag").get<std::uint32_t>() != frameTag(tag))
      return info;
    auto name = data.at("name").get<std::string>();
    auto at = data.at("time").get<double>();
    int floors = data.at("floors").get<int>(), population = data.at("population").get<int>(),
        mode = data.at("mode").get<int>();
    if (name.size() > 160 || name.find_first_of("\r\n\t") != std::string::npos ||
        name.find('\0') != std::string::npos || !std::isfinite(at) || at < 0 || floors < 1 ||
        floors > 10000 || population < 0 || population > 500000 || mode < 0 || mode > 2)
      return info;
    info.name = std::move(name);
    info.time = at;
    info.floors = floors;
    info.population = population;
    info.mode = Mode(mode);
    info.metadata = true;
  } catch (const std::exception &) {
  }
  return info;
}
static World readSave(const std::filesystem::path &path) {
  auto actual = path;
  auto size = std::filesystem::file_size(actual);
  if (size < 8 || size > 256 * 1024 * 1024ULL)
    throw std::runtime_error("Invalid save size");
  std::ifstream f(actual, std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(f)), {});
  if (std::string(bytes.data(), 8) != "MDORM001")
    throw std::runtime_error("Unsupported save header");
  auto rawSize = ZSTD_getFrameContentSize(bytes.data() + 8, bytes.size() - 8);
  if (rawSize == ZSTD_CONTENTSIZE_ERROR || rawSize == ZSTD_CONTENTSIZE_UNKNOWN ||
      rawSize > 512 * 1024 * 1024ULL)
    throw std::runtime_error("Invalid save payload size");
  std::vector<std::uint8_t> raw(rawSize);
  auto n = ZSTD_decompress(raw.data(), raw.size(), bytes.data() + 8, bytes.size() - 8);
  if (ZSTD_isError(n) || n != raw.size())
    throw std::runtime_error("Save checksum or decompression failed");
  auto j = json::from_cbor(raw);
  if (j.at("schema") != 1)
    throw std::runtime_error("Unsupported save schema");
  World w;
#define READ(field) j.at(#field).get_to(w.field)
  READ(config);
  if (!j.at("config").contains("accidentRate"))
    w.config.accidentRate = 0;
  if (!j.at("config").contains("layoutVersion"))
    w.config.layoutVersion = 1;
  if (j.contains("floorPlans"))
    j.at("floorPlans").get_to(w.floorPlans);
  else
    for (int f = 0; f < w.config.floors; ++f)
      w.floorPlans.push_back({f == 0 ? w.config.lobbyWidth : w.config.floorWidth,
                              f == 0 ? w.config.lobbyDepth : w.config.floorDepth});
  if (j.contains("expansions"))
    j.at("expansions").get_to(w.expansions);
  READ(time);
  READ(rng);
  READ(sequence);
  READ(player);
  READ(dormOwner);
  if (j.contains("governance"))
    j.at("governance").get_to(w.governance);
  READ(dormAccount);
  READ(gameOver);
  READ(endReason);
  w.saveName = j.value("saveName", std::string{});
  READ(accounts);
  READ(ledger);
  READ(residents);
  for (auto &r : w.residents) {
    r.leaving = j["residents"][r.id].value("leaving", false);
    r.waitSince = j["residents"][r.id].value("waitSince", w.time);
    r.lift = j["residents"][r.id].value("lift", none);
    r.lethalThreat = j["residents"][r.id].value("lethalThreat", false);
    r.personalWaste = j["residents"][r.id].value("personalWaste", 0);
    r.fitness = std::clamp(j["residents"][r.id].value("fitness", 0.f), 0.f, 100.f);
  }
  READ(names);
  READ(housing);
  if (j.contains("housingWorks"))
    j.at("housingWorks").get_to(w.housingWorks);
  for (Id i = 0; i < Id(w.housing.size()); ++i)
    w.floorHomes[w.housing[i].floor].push_back(i);
  READ(residentHomes);
  READ(facilities);
  if (j.contains("companies"))
    j.at("companies").get_to(w.companies);
  if (j.contains("workspaceLeases"))
    j.at("workspaceLeases").get_to(w.workspaceLeases);
  if (j.contains("workspaceOffers"))
    j.at("workspaceOffers").get_to(w.workspaceOffers);
  if (!j.contains("companies"))
    w.setupCompanies();
  for (const auto &l : w.workspaceLeases)
    if (l.active && !w.leasedWorkspaces.emplace(l.facility, l.id).second)
      throw std::runtime_error("Duplicate workspace lease");
  if (j.contains("fires"))
    j.at("fires").get_to(w.fires);
  if (j.contains("fireSprays"))
    j.at("fireSprays").get_to(w.fireSprays);
  w.nextAccidentAt = j.value("nextAccidentAt", w.time + 3600);
  for (const auto &f : w.fires)
    if (!f.out && !w.activeFires.emplace(f.facility, f.id).second)
      throw std::runtime_error("Duplicate active fire");
  READ(items);
  READ(vehicles);
  if (j.contains("ambulances"))
    j.at("ambulances").get_to(w.ambulances);
  if (j.contains("remains"))
    j.at("remains").get_to(w.remains);
  READ(parking);
  READ(orders);
  if (j.contains("freight"))
    j.at("freight").get_to(w.freight);
  if (j.contains("repairs"))
    j.at("repairs").get_to(w.repairs);
  if (j.contains("officeOrders")) {
    j.at("officeOrders").get_to(w.officeOrders);
    for (auto &o : w.officeOrders) {
      if (o.employer == none)
        o.employer = w.operatingAccount(o.facility);
      if (!o.submitted)
        w.activeOfficeOrders[o.worker] = o.id;
    }
  }
  if (j.contains("lifts"))
    j.at("lifts").get_to(w.lifts);
  if (j.contains("liftJourneys"))
    j.at("liftJourneys").get_to(w.liftJourneys);
  w.lastEconomyDay = j.value("lastEconomyDay", int(w.time / 86400));
  READ(messages);
  if (j.contains("relationships"))
    j.at("relationships").get_to(w.relationships);
  READ(incidents);
  READ(projects);
  READ(videos);
  READ(news);
#undef READ
  for (auto &e : j.at("events").get<std::vector<Event>>()) {
    if (!std::isfinite(e.time) || e.time < w.time || e.seq >= w.sequence || int(e.kind) < 0 ||
        int(e.kind) > int(EventKind::FacilityTick))
      throw std::runtime_error("Invalid saved event");
    bool service = e.kind == EventKind::FinishService;
    if (e.id < 0 || std::size_t(e.id) >= (service ? w.facilities.size() : w.residents.size()) ||
        (service && (e.other < 0 || std::size_t(e.other) >= w.residents.size())))
      throw std::runtime_error("Invalid event target");
    w.events.push(e);
  }
  for (auto &facility : w.facilities) {
    w.floorFacilities[facility.floor].push_back(facility.id);
    std::set<int> used;
    for (auto &service : facility.serving) {
      if (service.station < 0) {
        int slot = 0;
        while (used.contains(slot))
          ++slot;
        service.station = slot;
      }
      used.insert(service.station);
    }
  }
  w.validate();
  return w;
}
World World::loadBackup(const std::filesystem::path &path) {
  auto backup = path;
  backup += ".bak";
  auto world = readSave(backup);
  world.recoveredSavePath = std::filesystem::absolute(path).lexically_normal();
  world.log("save.recovered");
  return world;
}
World World::load(const std::filesystem::path &path) {
  try {
    return readSave(path);
  } catch (const std::exception &) {
    auto backup = path;
    backup += ".bak";
    if (!std::filesystem::exists(backup))
      throw;
    return loadBackup(path);
  }
}
} // namespace mega
