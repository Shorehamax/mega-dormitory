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
  FireStation
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
  Firefighter
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
  Extinguisher
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
  Evacuate
};
enum class CrimeKind { QueueCutting, Assault, ExcessiveForce, Murder, Theft };
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
  void validate() const;
};
struct FloorPlan {
  int width = 512, depth = 512;
};
struct Expansion {
  Id id = none, owner = none, contractor = none, worker = none, escrow = none;
  int floor = 0, width = 512, depth = 512;
  Money budget = 0, paid = 0;
  double remaining = 0, totalWork = 0, workAt = -1;
  bool complete = false;
  Vec3 site{};
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
struct Resident {
  Id id = none, account = none, home = 0, employer = none, jobSite = none;
  std::string name;
  int age = 25;
  Vec3 pos{};
  std::vector<Waypoint> route;
  std::uint64_t routeVersion = 0;
  Activity activity = Activity::Idle;
  Role role = Role::Unemployed;
  TaskKind task = TaskKind::None;
  Id target = none, queuedAt = none, carrying = none, opponent = none, escort = none;
  float hunger = .35f, thirst = .3f, energy = .8f, health = 100, happiness = .5f, strength = 40,
        aggression = .2f;
  double needsAt = 0, terminalUntil = 0, nextHit = 0, releaseAt = 0;
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
  bool cremated = false;
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
};
enum class VehicleKind { Bicycle, Scooter, CompactCar, DeliveryVan };
struct Vehicle {
  Id id = none, owner = none, dealer = none, driver = none, bay = none;
  VehicleKind kind = VehicleKind::Bicycle;
  Vec3 pos{};
  float yaw = 0, speed = 0, condition = 1;
  Money askingPrice = 0, originalPrice = 0;
  Appearance look;
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
  double lastTalk = -3600;
  int sharedInterest = 0;
};
struct Message {
  Id from = none, to = none;
  double time = 0;
  std::string text;
  bool read = false;
};
struct Incident {
  Id id = none, offender = none, victim = none, warden = none;
  CrimeKind kind = CrimeKind::Assault;
  Vec3 pos{};
  double time = 0;
  bool resolved = false;
  bool threat = false;
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
  bool supplied = false, forced = false;
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
enum class EventKind { Decide, Arrive, FinishService, Payroll, Release, FacilityTick };
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
  std::vector<Vehicle> vehicles;
  std::vector<Ambulance> ambulances;
  std::vector<Remains> remains;
  std::vector<ParkingBay> parking;
  std::vector<Order> orders;
  std::vector<Freight> freight;
  std::vector<LiftCar> lifts;
  std::vector<OfficeOrder> officeOrders;
  std::vector<Company> companies;
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
  std::vector<Project> projects;
  std::vector<VideoJob> videos;
  std::vector<News> news;
  std::priority_queue<Event, std::vector<Event>, EventLater> events;
  std::map<int, std::vector<Id>> floorFacilities;
  std::vector<FloorPlan> floorPlans;
  std::vector<Expansion> expansions;
  Layout layout(int floor = 1) const {
    if (floor >= 0 && std::size_t(floor) < floorPlans.size())
      return Layout{config.layoutVersion, float(floorPlans[floor].width),
                    float(floorPlans[floor].depth)};
    return Layout{config.layoutVersion, float(floor == 0 ? config.lobbyWidth : config.floorWidth),
                  float(floor == 0 ? config.lobbyDepth : config.floorDepth)};
  }
  Money expansionQuote(int floor, int width, int depth) const;
  Id commissionExpansion(Id actor, int floor, int width, int depth);
  void assignExpansion(Id worker, Id expansion);
  void beginExpansionWork(Id worker, Id expansion);
  void workExpansion(Id expansion, double seconds);
  struct CrowdBody {
    Vec3 pos{};
    std::uint64_t version = 0;
    std::size_t next = 1;
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
  std::vector<Vec3> floorPath(Vec3, Vec3, bool avoidHazards = true) const;
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
  void decide(Id);
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
  Id report(CrimeKind, Id, Id, bool threat = false);
  void attack(Id, Id, bool lethal = false);
  void combatHit(Id, Id, bool lethal, float reach);
  void shoot(Id attacker, Id victim);
  bool hasGun(Id resident) const;
  bool lineOfSight(Vec3 from, Vec3 to) const;
  void surrender(Id);
  void wardenRespond(Id);
  void apprehend(Id, Id);
  void kill(Id, Id = none, bool justified = false);
  void applyFall(Id, float, Id = none);
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
std::string vehicleKey(VehicleKind);
} // namespace mega
