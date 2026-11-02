#pragma once
#include "layout.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
namespace mega {
using Id = std::int32_t;
using Money = std::int64_t;
constexpr Id none = -1;
enum class Mode { Survival, Manager, Cheat };
enum class Activity {
  Idle,
  Walking,
  Queuing,
  Using,
  Sleeping,
  Working,
  Fighting,
  Surrendered,
  Restrained,
  Escorted,
  Jailed,
  Dead,
  Departed
};
enum class FacilityKind {
  Food,
  Water,
  Toilet,
  Shop,
  Office,
  Infirmary,
  Jail,
  Terminal,
  Workshop,
  Garbage,
  Elevator,
  Crematorium,
  School,
  Laundry,
  Shower,
  Leisure,
  Pool,
  Arcade,
  Playground,
  Cinema,
  Garden,
  Gym,
  Library,
  Cafe,
  Dealership,
  Parking,
  Reception,
  Morgue,
  FireStation,
  Washbasin,
  Ramp,
  FreightLift,
  Locker
};
enum class Role {
  Unemployed,
  Courier,
  Warden,
  Builder,
  VideoEditor,
  Programmer,
  Medic,
  Pilot,
  Administrator,
  Repairer,
  Cleaner,
  Vendor,
  Teacher,
  Chef,
  Lifeguard,
  Attendant,
  Firefighter,
  TowDriver
};
enum class ItemKind {
  Phone,
  Laptop,
  Food,
  Medicine,
  Tools,
  Parcel,
  Clothes,
  Materials,
  Handgun,
  Ammunition,
  Waste,
  Extinguisher,
  Fan,
  Tableware,
  RiceCooker
};
enum class OrderState { Waiting, ToPickup, ToCustomer, Delivered, Cancelled, Lost };
enum class TaskKind {
  None,
  Service,
  DeliveryPickup,
  DeliveryDrop,
  Patrol,
  Investigate,
  Escort,
  Work,
  Home,
  ConstructionPickup,
  ConstructionDrop,
  MedicalReach,
  MedicalBoard,
  MedicalClinic,
  BodyPickup,
  BodyDrop,
  FreightPickup,
  FreightDrop,
  Emigrate,
  LiftWait,
  FireResponse,
  Evacuate,
  TradeMeet,
  ShoppingTerminal,
  LeaveService,
  HouseholdMeet,
  PrisonRescue,
  StorageRetrieve,
  StorageDeposit,
  StorageRent,
  CrewWork,
  BelongingCollect
};
enum class CrimeKind { QueueCutting, Assault, ExcessiveForce, Murder, Theft, Escape, Obstruction };
enum class ProjectState { Proposed, AwaitingConsent, Funded, Working, Completed, Disputed };
struct Config {
  int floors = 2000;
  int population = 100000;
  std::uint64_t seed = 20261018;
  Mode mode = Mode::Survival;
  double dayMinutes = 30;
  float scarcity = .45f;
  Money startingCash = 80000;
  float aggression = .22f;
  int bedsPerFloor = 256;
  int layoutVersion = 2;
  int floorWidth = 512, floorDepth = 512, lobbyWidth = 640, lobbyDepth = 640;
  float supplies = 1, accidentRate = 1;
  int wardensPerThousand = 4;
  void validate(bool generation = true) const;
};
struct FloorPlan {
  int width = 512, depth = 512;
  bool deck = true;
};
struct InteriorWall {
  Id id = none, owner = none;
  int floor = 0;
  Vec3 a{}, b{};
  bool door = true, active = false;
};
enum class StructuralKind {
  Partition,
  RemovePartition,
  RemoveFloor,
  RestoreFloor,
  Transport,
  RemoveTransport
};
enum class StructuralState { Proposed, Funded, Working, Completed, Cancelled };
struct StructuralWork {
  Id id = none, actor = none, payer = none, workshop = none, escrow = none, team = none,
     wall = none, material = none, hauler = none, transport = none;
  int floor = 0;
  StructuralKind kind = StructuralKind::Partition;
  StructuralState state = StructuralState::Proposed;
  Vec3 site{};
  Money budget = 0, spent = 0;
  double remaining = 0, total = 0, workedAt = -1;
  bool supplied = false;
  std::map<Id, double> attendance;
  std::string blocked;
};
struct ConstructionTeam {
  Id id = none, workshop = none, foreman = none, work = none, contract = none;
  std::set<Id> members;
};
enum class CrewContractKind { Facility, Housing, Expansion, Repair };
struct CrewContract {
  Id id = none, team = none, work = none, workshop = none;
  CrewContractKind kind = CrewContractKind::Facility;
  bool active = true;
  std::map<Id, double> attendance;
};
struct Expansion {
  Id id = none, owner = none, contractor = none, worker = none, escrow = none;
  int floor = 0, width = 512, depth = 512;
  Money budget = 0, paid = 0;
  double remaining = 0, totalWork = 0, workAt = -1;
  bool complete = false;
  Vec3 site{};
  Id workshop = none, material = none;
  int materials = 0;
  bool supplied = true;
};
struct Account {
  Money balance = 0;
  std::string name;
};
struct Transfer {
  double time = 0;
  Id from = 0, to = 0;
  Money amount = 0;
  std::string reason;
};
struct Waypoint {
  Vec3 p;
  double time = 0;
};
struct Routine {
  double shiftStart = 9, shiftHours = 8, sleepStart = 23, sleepHours = 8;
  int daysOff = 0;
  double workSince = -1, unpaidWages = 0;
  std::map<Id, double> wageClaims;
};
struct Appearance {
  int primary = 0, secondary = 1, style = 0, accessory = 0;
};
struct HomeStyle {
  int frame = 0, linen = 1, blanket = 2, closet = 0;
  bool curtain = false, closetOpen = false;
};
enum class AutonomyAction {
  Saving,
  Essentials,
  Ordered,
  WaitingDelivery,
  NeedConnection,
  Trading,
  JobSeeking,
  JobAccepted,
  Invested,
  NoSupply,
  SharedMeal,
  TradeCancelled,
  Received,
  Cancelled,
  ParcelLost,
  HouseholdErrand,
  Founded
};
struct ResidentPlan {
  double nextShopping = 0, nextCareer = 0, nextBusiness = 0, nextListing = 0;
  double tradeDeadline = 0, lastCareer = -86400;
  Money reserve = 0, spentToday = 0, tradePrice = 0;
  int day = -1, tradeRetries = 0;
  Id order = none, tradeItem = none, seller = none, lastBusiness = none;
  ItemKind wanted = ItemKind::Food;
  AutonomyAction action = AutonomyAction::Saving;
  double nextHousehold = 0, helpDeadline = 0;
  Id helpTarget = none;
  int helpRetries = 0;
  Id storageItem = none, storageGroup = none;
  double storageDeadline = 0, nextStorage = 0, nextEstate = 0;
};
struct Resident {
  Id id = none, account = none, home = 0, employer = none, jobSite = none;
  std::string name;
  int age = 25, sex = -1;
  Vec3 pos{};
  std::vector<Waypoint> route;
  std::uint64_t routeVersion = 0;
  Activity activity = Activity::Idle;
  Role role = Role::Unemployed;
  TaskKind task = TaskKind::None;
  Id target = none, queuedAt = none, carrying = none, opponent = none, escort = none;
  float hunger = .35f, thirst = .3f, energy = .8f, health = 100, happiness = .5f, strength = 40,
        aggression = .2f;
  double needsAt = 0, terminalUntil = 0, nextHit = 0, releaseAt = 0, surrenderUntil = 0;
  Id terminal = none;
  bool alive = true, present = true, surrendered = false;
  std::vector<Id> items;
  Money hourlyWage = 900;
  bool automatic = true, leaving = false;
  double waitSince = 0;
  Id lift = none;
  bool lethalThreat = false;
  int personalWaste = 0;
  std::array<float, 8> interests{.5f, .5f, .5f, .5f, .5f, .5f, .5f, .5f};
  float hygiene = .8f, bladder = .2f, fitness = 0;
  Id outfit = none;
  Id vehicle = none;
  Id ambulance = none, medicalDuty = none, carriedBy = none, bodyCarrying = none;
  Id medicalProvider = none;
  int medicalTier = -1;
  double medicalUntil = 0, infectedAt = -1, immuneUntil = 0;
  Routine routine;
  int ladder = -1;
  bool ladderExit = false, ladderWait = false;
  int ladderTarget = -1, ladderDirection = 0;
};
struct Service {
  Id resident = none;
  double until = 0, remaining = 0;
  Money price = 0;
  int station = -1;
};
enum class RescueState {
  Idle,
  Dispatch,
  FlyingOut,
  ReachingPatient,
  CarryingPatient,
  FlyingHome,
  ReachingClinic,
  Recalling,
  Returning
};
struct Ambulance {
  Id id = none, provider = none, patient = none, medic = none, pilot = none, guard = none;
  RescueState state = RescueState::Idle;
  int tier = 0;
  Vec3 pos{}, landing{};
  std::vector<Waypoint> route;
  double readyAt = 0;
};
struct Remains {
  Id resident = none, carrier = none, storage = none, worker = none, destination = none,
     ambulance = none, lift = none;
  Vec3 pos{};
  double diedAt = 0, burnUntil = -1, burnRemaining = 1800, burnWorkedAt = -1;
  bool cremated = false, lost = false;
};
struct LocalName {
  std::string en, fr, zh;
};
struct HousingUnit {
  Id owner = none, tenant = none;
  int floor = 0, bed = 0;
  Money rent = 0, price = 0;
  bool listed = true, active = true;
  HomeStyle decor;
  double paidUntil = 0;
  Money arrears = 0;
  bool custom = false;
  Vec3 location{};
};
struct Facility {
  Id id = none, owner = none, manager = none;
  FacilityKind kind = FacilityKind::Food;
  int floor = 0;
  Vec3 pos{};
  int stations = 1, stock = 100;
  Money price = 100;
  double duration = 180;
  bool open = true;
  std::vector<Id> queue;
  std::vector<Service> serving;
  std::vector<int> stops;
  std::set<Id> whitelist;
  Money subscriptionPrice = 5000;
  std::map<Id, double> subscribers;
  Money askingPrice = 0;
  Role staffRole = Role::Unemployed;
  int staffNeeded = 0;
  double opensAt = 0, openHours = 24;
  bool publicAccess = true;
  std::vector<Id> employees;
  bool autoRestock = true, hiring = true, demolished = false;
  Money wageOffer = 1200;
  float condition = 1;
  int waste = 0;
  float fuelStock = 0;
  Money fuelPrice = 160;
};
struct Item {
  Id id = none, owner = none, carrier = none;
  ItemKind kind = ItemKind::Phone;
  Vec3 pos{};
  bool consumed = false;
  Id order = none;
  float cleanliness = 1, durability = 1;
  Id reservedAt = none;
  Appearance look;
  Id stolenFrom = none;
  int quantity = 1;
  Money askingPrice = 0;
  Id seller = none;
  Id storedAt = none, sharedWith = none;
  int storageSlot = -1;
};
enum class VehicleKind { Bicycle, Scooter, CompactCar, DeliveryVan };
struct Vehicle {
  Id id = none, owner = none, dealer = none, driver = none, bay = none;
  VehicleKind kind = VehicleKind::Bicycle;
  Vec3 pos{};
  float yaw = 0, speed = 0, condition = 1;
  float fuel = 0, verticalSpeed = 0, pitch = 0;
  bool airborne = false, towTruck = false;
  Id freightLift = none, road = none, towing = none, towedBy = none;
  Vec3 garage{};
  double updatedAt = 0, nextImpact = 0;
  Money askingPrice = 0, originalPrice = 0;
  Appearance look;
};
enum class RoadKind { Ramp, FreightLift };
struct VehicleLiftCall {
  Id vehicle = none, driver = none;
  int from = 0, to = 0;
  double called = 0;
};
struct RoadLink {
  Id id = none, facility = none;
  RoadKind kind = RoadKind::Ramp;
  int from = 0, to = 1;
  Vec3 pos{};
  float width = 4.2f, length = 24, loadLimit = 12000;
  bool active = false, moving = false;
  float height = 0, startHeight = 0, targetHeight = 0;
  double departAt = 0, arriveAt = 0, doorsUntil = 0;
  Id occupant = none;
  std::vector<VehicleLiftCall> calls;
};
struct DrivePoint {
  Vec3 pos{};
  Id link = none;
  int destination = -1;
};
struct VehicleJourney {
  Id vehicle = none, driver = none;
  std::vector<DrivePoint> path;
  std::size_t next = 0;
  Vec3 goal{};
  double blockedAt = -1;
  bool autoExit = true, reversing = false;
  Id parkingDestination = none;
  Vec3 backupOrigin{};
  double recoveryUntil = -1;
  int recoveries = 0;
};
enum class VehicleJobState { Waiting, ToVehicle, Working, Returning, Complete, Cancelled };
struct VehicleJob {
  Id id = none, customer = none, vehicle = none, workshop = none, worker = none, truck = none,
     escrow = none;
  bool tow = false, parts = false, retrofit = false;
  Id payer = none;
  VehicleJobState state = VehicleJobState::Waiting;
  Money budget = 0;
  double remaining = 1800, workedAt = -1;
  float startingCondition = 1;
};
struct ParkingBay {
  Id id = none, facility = none, vehicle = none;
  Vec3 pos{};
  float width = 2.4f;
  Money dailyPrice = 1800;
  double paidUntil = 0;
};
struct Order {
  Id id = none, buyer = none, shop = none, courier = none, item = none, escrow = none;
  ItemKind product = ItemKind::Food;
  Money price = 0, deliveryFee = 0;
  OrderState state = OrderState::Waiting;
  double placed = 0, delivered = 0;
};
enum class FreightState { Waiting, Collecting, Carrying, Delivered };
struct Freight {
  Id id = none, facility = none, worker = none, item = none, escrow = none;
  int quantity = 0;
  Money cost = 0, fee = 0;
  FreightState state = FreightState::Waiting;
  double placed = 0;
  bool waste = false;
  Id source = none;
  double readyAt = 0;
  Vec3 origin{}, landing{};
};
struct LiftCall {
  Id resident = none;
  int from = 0, to = 0;
  double called = 0;
};
struct LiftPassenger {
  Id resident = none;
  int destination = 0;
  Vec3 from{}, seat{};
  double start = 0, until = 0;
  bool exiting = false;
};
struct LiftCar {
  Id id = none, facility = none;
  float y = 0, startY = 0, targetY = 0;
  double departAt = 0, arriveAt = 0, doorsUntil = 0;
  bool moving = false;
  std::vector<LiftCall> calls;
  std::vector<LiftPassenger> passengers;
};
struct LiftJourney {
  Id lift = none;
  int destination = 0;
  Vec3 goal{};
  TaskKind task = TaskKind::None;
  Id target = none;
};
struct Repair {
  Id id = none, facility = none, worker = none, escrow = none, workshop = none, partItem = none;
  Money budget = 24000;
  double remaining = 3600, workedAt = -1;
  bool parts = false, complete = false;
};
struct Fire {
  Id id = none, facility = none, worker = none, station = none, payer = none, escrow = none;
  float heat = .35f, smoke = .2f, fuel = 1;
  double started = 0, updated = 0;
  Money budget = 18000, paid = 0;
  bool contained = false, out = false;
  std::map<Id, double> spreadExposure;
};
struct FireSpray {
  Id fire = none, item = none;
  double workedAt = 0;
};
struct Company {
  Id id = none, owner = none, account = none, leader = none;
  std::string name;
  std::map<Id, int> assignments;
  std::map<Id, int> completed;
  Money revenue = 0;
  std::map<Id, int> shares; // Account IDs; exactly 10,000 units in a company.
  Id parent = none;
  bool autoExpand = false;
  double nextExpansionAt = 0;
};
struct ShareOffer {
  Id id = none, company = none, seller = none; // seller is an account, not a resident ID
  int units = 0;
  Money unitPrice = 0;
  bool capital = false, active = true;
};
enum class AcquisitionState { Pending, Complete, Cancelled };
struct Acquisition {
  Id id = none, buyer = none, target = none, proposer = none, escrow = none;
  Money price = 0;
  double expires = 0;
  AcquisitionState state = AcquisitionState::Pending;
  std::map<Id, bool> ballots; // Account votes, weighted by current shares.
};
struct WorkspaceLease {
  Id id = none, company = none, facility = none, landlord = none, manager = none, escrow = none;
  Money dailyRent = 0, deposit = 0, arrears = 0;
  double nextDue = 0;
  int missedDays = 0;
  bool active = true;
};
struct OfficeOrder {
  Id id = none, facility = none, worker = none, escrow = none;
  Role role = Role::Programmer;
  int brief = 0;
  Money budget = 24000;
  double remaining = 5400, total = 5400, workAt = -1, due = 0;
  bool submitted = false;
  float quality = 0;
  std::vector<int> submission;
  Id client = 0, employer = none;
  bool liveRequests = false;
  std::array<Id, 4> tickets{none, none, none, none}, dispatched{none, none, none, none};
  std::array<int, 4>
      outcomes{}; // 0 pending, 1 dispatched, 2 already handled, 3 blocked, 4 misrouted
};
struct Shot {
  Id shooter = none, victim = none;
  Vec3 from{}, to{};
  double time = 0;
};
struct Relationship {
  Id a = none, b = none;
  float trust = 0;
  double lastTalk = -3600, lastAid = -86400;
  int sharedInterest = 0;
  int conversations = 0;
};
struct Education {
  std::array<float, 3> skills{};
  std::array<int, 3> lessons{};
  std::array<int, 3> answers{-1, -1, -1};
  int subject = 0, seed = 0, lastDay = -1;
  Id school = none;
};
struct Household {
  Id id = none, founder = none;
  std::set<Id> members;
  std::map<Id, double> invitations;
};
struct StorageLease {
  Id id = none, facility = none, holder = none, custodian = none, household = none;
  int slot = 0;
  Money dailyRent = 500, arrears = 0;
  double paidUntil = 0;
  bool active = true, released = false, open = false;
  std::vector<Id> items;
};
struct Message {
  Id from = none, to = none;
  double time = 0;
  std::string text;
  bool read = false;
};
struct Testimony {
  Id witness = none;
  Vec3 offenderPosition{};
  double seenAt = 0;
  float confidence = 0;
  bool identifies = false, filed = false;
  bool bodyOnly = false;
  Vec3 seenScene{};
};
struct EvidenceReceipt {
  Id id = none, item = none, incident = none, owner = none, carrier = none, locker = none;
  bool claimed = false;
};
struct CustodyRecord {
  Id resident = none, guard = none, jail = none;
  double bookedAt = 0, sentenceUntil = 0, escapeAt = -1, escapeProgress = 0, careAt = 0;
  bool active = true, escaped = false, cuffed = true;
  std::vector<Id> charges;
};
struct PrisonRescue {
  Id target = none, tool = none, incident = none;
  double workedAt = 0, progress = 0;
};
struct Incident {
  Id id = none, offender = none, victim = none, warden = none;
  CrimeKind kind = CrimeKind::Assault;
  Vec3 pos{};
  double time = 0;
  bool resolved = false;
  bool threat = false;
  bool reported = true, inspected = false, discovered = true;
  Id suspect = none, item = none, reporter = none;
  float confidence = 1;
  Vec3 lastKnown{};
  double seenAt = -1, reportedAt = -1, searchAfter = 0;
  std::vector<Testimony> witnesses;
  Vec3 scene{};
  bool sceneLocated = false;
  bool remoteProperty = false;
};
struct Project {
  Id id = none, owner = none, facility = none, worker = none, escrow = none;
  ProjectState state = ProjectState::Proposed;
  Money budget = 0, spent = 0, compensation = 0, offer = 5000;
  double remaining = 3600;
  std::set<Id> affected, consented;
  bool demolition = false;
  bool newConstruction = false, materialsDelivered = false;
  Id materialItem = none, source = none, contractor = none;
  int materials = 0;
  double workAt = -1;
};
enum class HousingWorkKind { BuildBed, RemoveBed, AddFloor };
struct HousingWork {
  Id id = none, actor = none, payer = none, contractor = none, workshop = none, worker = none,
     home = none, escrow = none, material = none, replacement = none;
  HousingWorkKind kind = HousingWorkKind::BuildBed;
  ProjectState state = ProjectState::Funded;
  Money budget = 180000, offer = 0;
  int floor = 0, width = 512, depth = 512;
  Vec3 site{};
  double remaining = 7200, total = 7200, workedAt = -1;
  bool supplied = false, forced = false, cancelled = false;
  Money ownerOffer = -1;
  Id offeredOwner = none, offeredTenant = none;
  std::map<Id, Money> demands;
  std::set<Id> refused;
  std::set<Id> consented;
};
struct Governance {
  Money askingPrice = 0;
  Id successor = none;
  bool retiring = false;
  int reviewedDay = -1;
};
struct VideoJob {
  Id id = none, worker = none, client = none, employer = none, escrow = none;
  std::vector<int> timeline;
  float score = 0;
  bool submitted = false;
  Money budget = 24000;
  double deadline = 0;
};
struct News {
  double time = 0;
  std::string key;
  Id subject = none, other = none;
};
enum class EventKind {
  Decide,
  Arrive,
  FinishService,
  Payroll,
  Release,
  FacilityTick,
  PhysicsTick,
  VehicleTick
};
struct Event {
  double time = 0;
  std::uint64_t seq = 0;
  EventKind kind = EventKind::Decide;
  Id id = none, other = none;
  std::uint64_t version = 0;
};
struct EventLater {
  bool operator()(const Event &a, const Event &b) const {
    return a.time == b.time ? a.seq > b.seq : a.time > b.time;
  }
};
struct LooseBody {
  Vec3 velocity{};
  Id pusher = none;
  bool justified = false;
  double updated = 0;
};
struct EstateDebt {
  Id creditor = none;
  Money remaining = 0;
};
struct Estate {
  Id id = none, deceased = none, beneficiary = none;
  double openedAt = 0, reviewAt = 0, acceptedAt = -1;
  bool accepted = false;
  Money distributed = 0, inheritedDebt = 0;
  std::set<Id> candidates, declined, belongings;
  std::vector<EstateDebt> debts;
};
struct PersonalUse {
  Id item = none, ingredient = none;
  double remaining = 0, workedAt = 0;
  Vec3 origin{};
};
struct SaveInfo {
  std::string name;
  double time = 0;
  int floors = 0, population = 0;
  Mode mode = Mode::Survival;
  bool available = false, backup = false, metadata = false;
};
class World {
public:
  Config config;
  double time = 6 * 3600;
  std::uint64_t rng = 1, sequence = 0;
  Id player = 0, dormOwner = 1, dormAccount = 1;
  Governance governance;
  bool gameOver = false;
  std::string endReason, saveName;
  // Historical browser-only records are retained for audit/future migration, not executed.
  std::string browserArchive;
  std::filesystem::path browserSourcePath;
  std::map<std::string, std::int64_t> importReport;
  static World importBrowser(const std::filesystem::path &, Mode mode = Mode::Cheat);
  static SaveInfo inspectSave(const std::filesystem::path &);
  static World loadBackup(const std::filesystem::path &);
  mutable std::filesystem::path recoveredSavePath;
  std::vector<Account> accounts;
  std::vector<Transfer> ledger;
  std::vector<Resident> residents;
  std::vector<Facility> facilities;
  std::vector<LocalName> names;
  std::vector<HousingUnit> housing;
  std::vector<HousingWork> housingWorks;
  std::map<int, std::vector<Id>> floorHomes;
  std::vector<Id> residentHomes;
  std::vector<Item> items;
  std::map<Id, PersonalUse> personalUses;
  bool itemInUse(Id item) const;
  void usePersonalItem(Id resident, Id item);
  void stopPersonalUse(Id resident);
  void personalUseTick();
  void validatePersonalUses() const;
  std::vector<Id> washableItems(Id resident) const;
  void seekWashbasin(Id resident);

private:
  void reserveWashing(Id resident, Id facility);
  void finishWashing(Id resident, Id facility);

public:
  std::vector<Vehicle> vehicles;
  std::vector<RoadLink> roads;
  std::map<Id, VehicleJourney> vehicleJourneys;
  std::vector<VehicleJob> vehicleJobs;
  double nextVehicleAt = -1;
  Id vehicleController = none;
  std::vector<Ambulance> ambulances;
  std::vector<Remains> remains;
  std::map<Id, LooseBody> looseBodies, airborneRemains;
  double nextPhysicsAt = -1;
  Id physicsController = none; // transient: the active first-person input controller
  Body stepBody(Id resident, Vec3 desired, float seconds, bool jump = false);
  void ensurePhysicsTick();
  void physicsTick();
  void clearPhysics(Id resident);
  void shove(Id actor, Id victim);
  void loseRemains(Id body);
  void validatePhysics() const;
  std::vector<ParkingBay> parking;
  std::vector<Order> orders;
  std::map<Id, ResidentPlan> residentPlans;
  std::map<Id, Education> education;
  void enrollLesson(Id actor, Id school, int subject);
  void prepareLesson(Id actor, Id school);
  std::array<int, 4> lessonChoices(Id actor, int question) const;
  void answerLesson(Id actor, int question, int choice);
  void completeLesson(Id actor, Id school);
  bool educationDecision(Id actor);
  float jobSkill(Id actor, Role role) const;
  void validateEducation() const;
  std::vector<StorageLease> storageLeases;
  std::map<Id, std::set<Id>> storageByAccount, storageByHousehold, storageByItemOwner;
  void rebuildStorage();
  Vec3 storagePosition(Id lease) const;
  Vec3 storedItemPosition(Id lease, int slot) const;
  bool storagePermission(Id actor, Id lease) const;
  bool storageItemAllowed(Id actor, Id item) const;
  bool storageFacilityBusy(Id facility) const;
  Id rentStorage(Id actor, Id facility, Id household = none);
  void setStorageOpen(Id actor, Id lease, bool open);
  void depositStorage(Id actor, Id lease, Id item, bool share = false);
  void withdrawStorage(Id actor, Id item, bool steal = false);
  void payStorageArrears(Id actor, Id lease);
  void releaseStorage(Id actor, Id lease);
  void settleStorageLease(Id lease);
  void storageTick();
  void validateStorage() const;
  int storedPossessions(Id actor, ItemKind kind) const;
  int sharedStoredMeals(Id group, Id actor = none) const;
  bool storageDecision(Id actor, bool urgent = false);
  bool storageDepartureDecision(Id actor);
  void finishStorageVisit(Id actor, TaskKind task, Id target);
  std::vector<Household> households;
  std::map<Id, Id> householdMembership; // reconstructed from groups, never grants property rights
  Id householdOf(Id actor) const;
  Id createHousehold(Id actor);
  void inviteHousehold(Id actor, Id other);
  void acceptHousehold(Id actor, Id group);
  void leaveHousehold(Id actor);
  void rebuildHouseholds();
  void householdTick();
  void householdSocial(Id a, Id b);
  int householdMealDeficit(Id actor) const;
  bool householdDecision(Id actor);
  void finishHouseholdVisit(Id actor);
  void validateHouseholds() const;
  std::set<Id> itemOffers; // live resale advertisements; rebuilt from saved items
  void rebuildItemOffers();
  static Money productPrice(ItemKind);
  Money deliveryPrice(Id buyer, Id shop) const;
  Money householdReserve(Id resident) const;
  bool autonomyDecision(Id resident);
  void finishAutonomyTrade(Id resident);
  void householdShopping(Id resident);
  void careerDecision(Id resident);
  void businessDecision(Id resident);
  void orderTick();
  void dispatchOrder(Id order);
  bool routeDelivery(Id order, bool handover = true);
  bool shareFood(Id donor, Id recipient);
  void validateAutonomy() const;
  std::vector<Freight> freight;
  std::vector<LiftCar> lifts;
  std::vector<OfficeOrder> officeOrders;
  std::vector<Company> companies;
  mutable std::unordered_map<Id, Id> equityPeople;
  mutable std::size_t equityPopulation = 0;
  std::vector<ShareOffer> shareOffers;
  std::vector<Acquisition> acquisitions;
  std::map<Id, Id> personalHeirs;
  std::vector<Estate> estates;
  void nominatePersonalHeir(Id actor, Id heir);
  void openEstate(Id deceased);
  void acceptEstate(Id actor, Id estate);
  void declineEstate(Id actor, Id estate);
  Money estateLiabilities(Id estate) const;
  Money estateValuation(Id estate) const;
  void estateTick();
  void validateEstates() const;
  void collectBelonging(Id actor, Id item);
  bool estateDecision(Id actor);
  std::map<Id, Id> equityHeirs; // Resident nominations, affecting company equity only.
  Id companyController(Id company) const;
  void refreshCompanyControl(Id company);
  bool controlsAccount(Id actor, Id account) const;
  int heldShares(Id actor, Id company) const;
  int reservedShares(Id account, Id company) const;
  Money companyValue(Id company) const;
  Id listShares(Id actor, Id company, int units, Money unitPrice, bool capital = false);
  void cancelShareOffer(Id actor, Id offer);
  void buyShares(Id buyer, Id offer, int units);
  void distributeEquity(Id source, Id company, Money amount, const std::string &reason);
  Id proposeAcquisition(Id actor, Id buyer, Id target, Money price);
  void voteAcquisition(Id actor, Id acquisition, bool approve);
  void completeAcquisition(Id acquisition);
  void cancelAcquisition(Id actor, Id acquisition);
  void nominateEquityHeir(Id actor, Id heir);
  void equityTick();
  void validateEquity() const;
  Id expandCompany(Id actor, Id company, bool unoccupiedOnly = false);
  void setCompanyExpansion(Id actor, Id company, bool enabled);
  void expansionTick(Id company);
  void seedCapitalMarkets();
  std::vector<WorkspaceLease> workspaceLeases;
  std::map<Id, Id> leasedWorkspaces;
  std::map<Id, Money> workspaceOffers;
  Id operatingAccount(Id facility) const;
  Id companyForAccount(Id account) const;
  bool directsCompany(Id actor, Id company, bool ownerOnly = false) const;
  void setupCompanies();
  Id foundCompany(Id actor, const std::string &name, Money capital);
  void investCompany(Id actor, Id company, Money amount);
  Money companyReserve(Id company) const;
  void companyDividend(Id actor, Id company, Money amount);
  void acquireWorkspace(Id actor, Id company, Id facility);
  void offerWorkspace(Id actor, Id facility, Money dailyRent);
  Id leaseWorkspace(Id actor, Id company, Id facility);
  void endWorkspaceLease(Id actor, Id lease);
  void settleWorkspaceLease(Id lease);
  void closeWorkspaceLease(Id lease);
  void changeOfficeEmployer(Id facility, Id account, bool dismiss = false);
  void appointTeamLeader(Id actor, Id company, Id worker);
  void assignCompanyWork(Id actor, Id company, Id worker, Role role, int brief);
  void dismissEmployee(Id actor, Id facility, Id worker);
  void companyTick();
  void validateCompanies() const;
  std::vector<Repair> repairs;
  std::vector<Fire> fires;
  std::map<Id, Id> activeFires;
  std::map<Id, FireSpray> fireSprays;
  double nextAccidentAt = 0;
  Id ignite(Id facility, float intensity = .35f);
  void triggerFire(Id actor, Id facility);
  void spreadFireTick(const std::map<Id, double> &elapsed);
  float dangerAt(Vec3 position) const;
  void requestFireResponse(Id actor, Id fire);
  Id issueExtinguisher(Id actor, Id station);
  Id extinguisher(Id actor) const;
  Vec3 fireApproach(Id fire) const;
  void assignFirefighter(Id actor, Id fire);
  void beginExtinguish(Id actor, Id fire);
  void stopExtinguish(Id actor);
  void extinguishTick();
  bool evacuate(Id actor);
  void emergencyTick();
  void validateEmergencies() const;
  std::vector<Shot> shots;
  std::map<Id, Id> activeOfficeOrders;
  std::map<Id, double> officeRetryAt;
  std::map<Id, LiftJourney> liftJourneys;
  int lastEconomyDay = 0;
  std::vector<Message> messages;
  std::map<std::uint64_t, Relationship> relationships;
  std::vector<Incident> incidents;
  std::vector<EvidenceReceipt> evidenceReceipts;
  std::map<Id, CustodyRecord> custodyRecords;
  Id recordCrime(CrimeKind kind, Id offender, Id victim, bool threat = false, Id item = none);
  void observeCrime(Id incident);
  void fileCrimeReport(Id reporter, Id incident);
  void inspectCrimeScene(Id warden, Id incident);
  void interviewWitness(Id warden, Id incident, Id witness);
  bool canEnforce(const Incident &incident) const;
  bool hasWarrant(Id resident) const;
  Vec3 investigationDestination(Id warden, Id incident);
  void investigateArrival(Id warden, Id incident);
  void recoverEvidence(Id warden, Id incident, Id item);
  void depositEvidence(Id warden, Id jail);
  void claimEvidence(Id owner, Id receipt);
  bool evidenceAuthority(Id actor, Id receipt) const;
  void securityTick();
  Id discoverBody(Id observer, Id remains);
  bool bodyVisible(Id observer, Id remains) const;
  void discoverDeaths();
  void beginCustody(Id guard, Id suspect, Id jail);
  std::map<Id, PrisonRescue> prisonRescues;
  void startPrisonRescue(Id actor, Id prisoner);
  void stopPrisonRescue(Id actor);
  void prisonRescueTick();
  void validatePrisonRescues() const;
  void startEscape(Id resident);
  void stopEscape(Id resident);
  void escapeCustody(Id resident);
  double sentenceHours(Id suspect) const;
  void validateSecurity() const;
  std::vector<Project> projects;
  std::vector<VideoJob> videos;
  std::vector<News> news;
  std::priority_queue<Event, std::vector<Event>, EventLater> events;
  std::map<int, std::vector<Id>> floorFacilities;
  std::vector<FloorPlan> floorPlans;
  std::vector<Expansion> expansions;
  std::vector<InteriorWall> interiorWalls;
  std::vector<StructuralWork> structuralWorks;
  std::vector<ConstructionTeam> constructionTeams;
  Id proposePartition(Id actor, int floor, Vec3 a, Vec3 b, bool door);
  Id proposePartitionRemoval(Id actor, Id wall);
  Id proposeFloorChange(Id actor, int floor, bool restore);
  void fundStructuralWork(Id actor, Id work);
  void planFloorClearance(Id actor, Id work);
  void collectStructuralSalvage(Id worker, Id work);
  void returnStructuralSalvage(Id worker, Id work);
  void cancelStructuralWork(Id actor, Id work);
  Id createConstructionTeam(Id actor, Id workshop, Id foreman);
  void recruitConstructionWorker(Id actor, Id team, Id worker);
  void removeConstructionWorker(Id actor, Id team, Id worker);
  void appointForeman(Id actor, Id team, Id worker);
  void assignStructuralTeam(Id actor, Id work, Id team);
  bool structuralDecision(Id worker);
  void structuralTick();
  void beginStructuralWork(Id worker, Id work);
  Vec3 structuralDestination(Id worker, Id work) const;
  std::vector<Obstacle> wallSegments(const InteriorWall &) const;
  std::vector<CrewContract> crewContracts;
  Id assignCrewContract(Id actor, Id team, CrewContractKind kind, Id work);
  void cancelCrewContract(Id actor, Id contract);
  Id crewContractFor(Id worker) const;
  Vec3 crewContractDestination(Id worker, Id contract) const;
  void beginCrewContractWork(Id worker, Id contract);
  bool crewContractDecision(Id worker);
  void crewContractTick();
  void validateCrewContracts() const;
  bool builderCommitted(Id worker) const;
  void validateStructures() const;
  void checkPartition(Id wall);
  bool floorCleared(int floor, Id except = none) const;
  bool structuralFloorLocked(int floor) const;
  void reviseHomeRemoval(Id actor, Id work, Money tenantOffer, Money ownerOffer,
                         Id replacement = none);
  void replyHomeRemoval(Id resident, Id work, bool accept);
  void counterHomeRemoval(Id resident, Id work, Money demand);
  void cancelHomeRemoval(Id actor, Id work);
  Layout layout(int floor = 1) const;
  Id proposeRoad(Id actor, RoadKind kind, int from, int to, Vec3 pos, float length = 24);
  Id proposeRoadRemoval(Id actor, Id road);
  void activateRoad(Id road, bool active);
  void checkRoadSite(Id road) const;
  void generateRoads();
  float freightHeight(Id road) const;
  void requestVehicleLift(Id driver, Id road, int destination);
  void freightVehicleTick();
  float supportHeight(Vec3 p, float ceiling) const;
  float vehicleWidth(Id vehicle) const;
  float vehicleLength(Id vehicle) const;
  float vehicleMass(Id vehicle) const;
  bool vehicleContact(Id vehicle, Vec3 pos, float yaw, Id other) const;
  bool vehicleFits(Id vehicle, Vec3 pos, float yaw, bool people = true, Id ignore = none) const;
  std::vector<DrivePoint> planVehicleRoute(Id driver, Id vehicle, Vec3 goal) const;
  void driveTo(Id driver, Vec3 destination, bool autoExit = true);
  void cancelDrive(Id driver);
  void vehicleTick();
  void ensureVehicleTick();
  void vehicleServicesTick();
  bool vehicleWorkDecision(Id worker);
  bool chooseVehicleTrip(Id resident);
  void stockFuel(Id actor, Id workshop, float litres);
  void refuelVehicle(Id actor, Id vehicle, Id workshop, float litres);
  Id requestVehicleJob(Id customer, Id vehicle, Id workshop, bool tow);
  void cancelVehicleJob(Id customer, Id job);
  void assignVehicleJob(Id worker, Id job);
  Id convertServiceTruck(Id actor, Id vehicle, Id workshop);
  void validateVehicles() const;
  Money expansionQuote(int floor, int width, int depth) const;
  Id commissionExpansion(Id actor, int floor, int width, int depth);
  void assignExpansion(Id worker, Id expansion);
  Vec3 expansionDestination(Id expansion) const;
  void beginExpansionWork(Id worker, Id expansion);
  void workExpansion(Id expansion, double seconds);
  struct CrowdBody {
    Vec3 pos{};
    std::uint64_t version = 0;
    std::size_t next = 1;
    Vec3 detour{};
    float detourTime = 0;
    double serviceCheckAt = 0;
  };
  std::map<Id, CrowdBody> crowd;
  mutable std::map<int, std::vector<Obstacle>> obstacles;
  mutable std::map<int, std::vector<unsigned char>> navGrids;
  mutable std::unordered_map<std::uint64_t, std::vector<Obstacle>> obstacleCells;
  std::map<std::tuple<int, int, int>, std::vector<Id>> crowdGrid;
  int focusFloor = -100;
  std::vector<Id> nearbyResidents;
  Vec3 coarsePosition(Id) const;
  const std::vector<Obstacle> &floorObstacles(int) const;
  bool clearPosition(Vec3, float radius = .29f) const;
  Vec3 accessible(Vec3) const;
  std::vector<Vec3> floorPath(Vec3, Vec3, bool avoidHazards = true, Id vehicle = none) const;
  void routeTo(Id, Vec3);
  float walkingSpeed(Id, bool running = false) const;
  void exercise(Id, double seconds, float intensity);
  void focus(int floor);
  void moveCrowd(double gameSeconds);
  Vec3 moveCapsule(Id, Vec3 from, Vec3 delta, bool people = true) const;
  float moveBody(Id, Body &, Vec3 desired, float dt) const;
  int nearbyLadder(Id) const;
  void grabLadder(Id);
  void releaseLadder(Id);
  void climb(Id, float direction, float seconds);
  bool climbing(Id) const;
  static World generate(const Config &);
  float random();
  Id account(std::string name, Money opening = 0);
  void pay(Id from, Id to, Money amount, const std::string &reason);
  void schedule(double at, EventKind kind, Id id, Id other = none, std::uint64_t version = 0);
  void advance(double seconds, std::size_t eventBudget = 500000);
  void dispatch(const Event &);
  void decide(Id, bool scheduleNext = true);
  static bool inHours(double time, double start, double length);
  bool onShift(Id) const;
  bool sleepTime(Id) const;
  void setupRoutines();
  void clockIn(Id);
  void clockOut(Id);
  int staffOnDuty(Id) const;
  bool accessAllowed(Id resident, Id facility) const;
  bool operational(Id facility) const;
  std::string availability(Id facility) const;
  void setFacilityHours(Id actor, Id facility, double start, double hours, bool publicAccess);
  void refreshServices();
  void customizeHome(Id actor, Id unit, const HomeStyle &);
  void customizeClothes(Id actor, Id item, const Appearance &);
  void customizeVehicle(Id actor, Id vehicle, const Appearance &);
  bool stealItem(Id thief, Id victim, Id item, bool robbery = false);
  void needs(Id);
  Vec3 position(Id id) const;
  void travel(Id, Vec3, TaskKind, Id target);
  void arrive(Id);
  bool near(Id, Vec3, float distance = 1.8f) const;
  Id nearest(Id, FacilityKind) const;
  Id nearestAt(Vec3 position, FacilityKind) const;
  void joinQueue(Id, Id);
  void leaveQueue(Id);
  void serve(Id);
  void cutQueue(Id, Id, std::size_t place = 0);
  bool hasDevice(Id) const;
  void useTerminal(Id, Id, double minutes);
  void sendMessage(Id, Id, const std::string &);
  std::string talk(Id speaker, Id listener, int topic);
  void socialTick();
  void cheatResident(Id resident, const std::string &stat, double value);
  float trust(Id, Id) const;
  void listItem(Id seller, Id item, Money price);
  void buyItem(Id buyer, Id item);
  void changeClothes(Id, Id);
  Vec3 servicePosition(Id facility, int station) const;
  void layoutQueue(Id);
  std::vector<Vec3> queueSlots(Id facility, std::size_t count) const;
  Vec3 serviceApproach(Id facility) const;
  bool atServiceEntrance(Id resident, Id facility) const;
  Id legacyServiceDeparture(Id resident) const;
  void clearServiceArea(Id resident, Id facility);
  void resumeAfterService(Id resident, Id facility);
  Id order(Id, Id, ItemKind);
  void assignDelivery(Id, Id);
  void pickup(Id, Id);
  void deliver(Id, Id);
  void cancelOrder(Id, Id);
  void chooseJob(Id, Role, Id);
  void buyFacility(Id, Id);
  void subscribeLift(Id, Id);
  void payLift(Id, Id, int destination);
  Money housingPrice(int floor) const;
  void repriceHousing();
  void rentHome(Id, Id);
  void buyHome(Id, Id);
  std::string displayName(Id, const std::string &) const;
  Id commissionVideo(Id, Id, Id, Money);
  float scoreVideo(const std::vector<int> &) const;
  void submitVideo(Id, const std::vector<int> &);
  Id commissionFacility(Id owner, FacilityKind kind, int floor, Vec3 pos, int stations,
                        Id company = none);
  void collectMaterials(Id worker, Id project);
  void deliverMaterials(Id worker, Id project);
  void beginProjectWork(Id worker, Id project);
  void invalidateGeometry(int floor);
  Id proposeProject(Id, Id, bool, Money);
  void consentProject(Id, Id);
  void offerCompensation(Id owner, Id project, Money perResident);
  Money dormValuation() const;
  void listDorm(Id owner, Money price);
  void buyDorm(Id buyer);
  void nominateSuccessor(Id owner, Id successor);
  void acceptSuccession(Id successor);
  void seizeDorm(Id claimant);
  void governanceTick();
  void transferDorm(Id nextOwner);
  void fundProject(Id, Id);
  void assignBuilder(Id, Id);
  void workProject(Id, double);
  Id report(CrimeKind, Id, Id, bool threat = false, bool published = true);
  void attack(Id, Id, bool lethal = false);
  void combatHit(Id, Id, bool lethal, float reach);
  void shoot(Id attacker, Id victim);
  bool hasGun(Id resident) const;
  bool lineOfSight(Vec3 from, Vec3 to) const;
  void surrender(Id);
  void wardenRespond(Id);
  void apprehend(Id, Id);
  void kill(Id, Id = none, bool justified = false);
  void applyFall(Id, float, Id = none, bool justified = false);
  void depart(Id);
  void setupOffices();
  void prepareAdministration(OfficeOrder &order);
  void dispatchAdministration(OfficeOrder &order, const std::vector<int> &answer);
  Id officeOrder(Id worker);
  void officeTick();
  std::vector<int> runProgram(const std::vector<int> &input, const std::vector<int> &ops) const;
  float scoreOffice(Id order, const std::vector<int> &answer) const;
  void submitOffice(Id order, const std::vector<int> &answer);
  void generateLifts();
  Vec3 liftDoor(int floor) const;
  float liftHeight(Id lift) const;
  Vec3 liftPassengerPosition(Id resident) const;
  bool planLift(Id resident, Vec3 destination, TaskKind task, Id target);
  void requestLift(Id resident, Id lift, int destination);
  void goByLift(Id resident, Id lift, int destination);
  void cancelLift(Id resident);
  void liftTick();
  Id requestRepair(Id actor, Id facility);
  void assignRepair(Id worker, Id repair);
  void repairTick();
  void economyTick();
  void refreshManagement();
  void setBusiness(Id actor, Id facility, Money price, Money wage, bool hiring, bool restock);
  void hireEmployee(Id actor, Id facility, Id worker, Role role, double shift);
  Id requestStock(Id actor, Id facility, int quantity);
  Id requestWaste(Id actor, Id facility);
  void assignFreight(Id worker, Id freight);
  Vec3 freightPosition(Id freight) const;
  void freightTick();
  void collectFreight(Id worker, Id freight);
  void deliverFreight(Id worker, Id freight);
  Id immigrate(Id home, Money capital);
  void listHome(Id actor, Id home, Money price);
  Vec3 homePosition(Id home) const;
  Id commissionBed(Id actor, int floor, Vec3 pos);
  Id proposeHomeRemoval(Id actor, Id home, Money compensation);
  void consentHomeRemoval(Id resident, Id contract);
  void fundHomeRemoval(Id actor, Id contract, bool force = false);
  Id commissionFloor(Id actor, int width, int depth);
  void assignHousingWork(Id worker, Id contract);
  Vec3 housingWorkDestination(Id contract) const;
  void housingWorkTick();
  void settleRent(Id resident);
  Id serviceChoice(Id resident, FacilityKind kind) const;
  void generateMedical();
  void subscribeMedical(Id, Id provider, int tier);
  void callAmbulance(Id);
  Vec3 ambulancePosition(Id) const;
  Vec3 medicalLanding(Id provider) const;
  void medicalTick();
  void medicalReached(Id medic, Id ambulance);
  void medicalBoard(Id medic, Id ambulance);
  void medicalClinic(Id medic, Id ambulance);
  void transportBody(Id worker, Id body);
  void pickupBody(Id worker, Id body);
  void storeBody(Id worker, Id body);
  void healthTick();
  void consumeItem(Id, Id item);
  void sleepAtHome(Id);
  void generateVehicles();
  void generateParkingBays(Id facility);
  void buyVehicle(Id resident, Id vehicle);
  void sellVehicle(Id resident, Id vehicle, Id dealer);
  void enterVehicle(Id resident, Id vehicle);
  void exitVehicle(Id resident);
  void driveVehicle(Id resident, float throttle, float steering, float seconds);
  void reserveParking(Id resident, Id vehicle, Id bay);
  void parkVehicle(Id resident, Id vehicle, Id bay);
  void releaseParking(Id resident, Id vehicle);
  float happiness() const;
  std::size_t living() const;
  Money internalMoney() const;
  void validate() const;
  void log(std::string, Id = none, Id = none);
  void save(const std::filesystem::path &) const;
  static World load(const std::filesystem::path &);
};
std::string roleKey(Role);
std::string activityKey(Activity);
std::string facilityKey(FacilityKind);
std::string autonomyKey(AutonomyAction);
std::string vehicleKey(VehicleKind);
} // namespace mega
