#include "mega/assets.hpp"
#include "mega/camera.hpp"
#include "mega/renderer.hpp"
#include "mega/ui.hpp"
#include "mega/world.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
namespace mega {
static std::string number(std::int64_t n) {
  std::string s = std::to_string(n);
  for (int i = int(s.size()) - 3; i > 0; i -= 3)
    s.insert(i, ",");
  return s;
}
static std::string money(Money n) { return "¥" + number(n / 100); }
static std::string timeLabel(double t) {
  auto seconds = std::int64_t(std::clamp(t, 0., 1e15));
  long long d = seconds / 86400 + 1;
  int h = int((seconds / 3600) % 24), m = int((seconds / 60) % 60);
  char s[48];
  std::snprintf(s, sizeof(s), "%03lld  /  %02d:%02d", d, h, m);
  return s;
}
static Color palette(int i) {
  static const unsigned colors[] = {0x607a87, 0xb5b4a5, 0xbd965c, 0x985e54,
                                    0x708b77, 0x80748c, 0xd9d1b9, 0x303d49};
  return hex(colors[std::clamp(i, 0, 7)]);
}
static const char *itemLabel(ItemKind kind) {
  switch (kind) {
  case ItemKind::Phone:
    return "item.phone";
  case ItemKind::Laptop:
    return "item.laptop";
  case ItemKind::Food:
    return "item.food";
  case ItemKind::Medicine:
    return "item.medicine";
  case ItemKind::Tools:
    return "item.tools";
  case ItemKind::Clothes:
    return "trade.clothes";
  case ItemKind::Handgun:
    return "item.handgun";
  case ItemKind::Ammunition:
    return "item.ammunition";
  case ItemKind::Extinguisher:
    return "item.extinguisher";
  case ItemKind::Fan:
    return "item.fan";
  case ItemKind::Tableware:
    return "item.tableware";
  case ItemKind::RiceCooker:
    return "item.ricecooker";
  default:
    return "trade.item";
  }
}
static std::string hoursLabel(double start, double duration) {
  char b[48];
  int a = int(start * 60) % 1440, z = int((start + duration) * 60) % 1440;
  std::snprintf(b, sizeof(b), "%02d:%02d–%02d:%02d", a / 60, a % 60, z / 60, z % 60);
  return b;
}
class Application {
public:
  bool drawerOpen = false, menuKeyHeld = false, escapeHeld = false, viewKeyHeld = false;
  float drawerSlide = 0;
  int visibleFloors = 96;
  bool gpuSkinning = true;
  std::vector<SkinRequest> frameSkins;
  double sceneBuildSeconds = 0;
  int instanceFloor = -1, instanceRange = -1;
  bool instanceView = false, cutaway = true, instanceCutaway = true;
  double nextInstances = 0;
  bool interactHeld = false, helpHeld = false, helpOpen = false, punchHeld = false,
       fireHeld = false, surrenderHeld = false, shoveHeld = false;
  std::vector<Vertex> sceneGeometry;
  float observerHeight = 0;
  Vec3 cameraFocus{}, firstPersonOffset{0, 1.6f, 0};
  Id cameraResident = none;
  std::string floorWidth = "512", floorDepth = "512", lobbyWidth = "640", lobbyDepth = "640";
  float networkScroll = 0, helpScroll = 0;
  int customCategory = 0, buildKind = 1, cheatStat = 0;
  bool householdOpen = false;
  std::string cheatValue = "100";
  bool savesOpen = false, savingSlots = false, savesWasPaused = false, savesEscapeHeld = false;
  int overwriteSlot = 0;
  double nextSlotScan = 0;
  std::array<SaveInfo, 8> saveCatalogue{};
  std::string saveDraft;
  bool importOpen = false, importSummary = false, importWasPaused = false;
  Mode importMode = Mode::Cheat;
  std::string browserPath;
  std::future<World> importing;
  bool emergencyScreen = false;
  bool cheatOpen = false, companyScreen = false, companyConstruction = false;
  Id selectedCompany = none;
  int companyPage = 0;
  double nextCompanyStats = 0;
  Money reservedCompanyCash = 0;
  std::string companyName;
  Id lethalConfirm = none;
  std::string buildX = "54", buildZ = "0", buildStations = "1";
  bool structuralOpen = false, wallAlongX = true, wallDoor = true;
  std::string wallLength = "8", relocationCounter = "50000";
  std::string roadEndFloor = "2", roadLength = "24", driveX = "80", driveZ = "110",
              driveFloor = "2";
  Id selectedTeam = none;
  std::map<Id, std::array<std::string, 3>> relocationInputs;
  std::map<Id, float> closetAnimation, characterHeading, walkBlend, liftDoorAnimation;
  struct PoseTransition {
    CharacterPose previous = CharacterPose::Idle, current = CharacterPose::Idle;
    float blend = 1;
  };
  std::map<Id, PoseTransition> characterPoses;
  float sceneDelta = .016f;
  GLFWwindow *window;
  Locale locale;
  Font font;
  UI ui;
  AssetLibrary assets;
  Vec3 cameraEye{};
  std::unique_ptr<Renderer> renderer;
  std::unique_ptr<World> world;
  std::future<World> generation;
  Config config;
  bool menu = true, paused = false, phone = false, firstPerson = false, mouseLocked = false,
       previousMouse = false, exitConfirm = false;
  int tab = 0, networkTab = 0, currentFloor = 0, slot = 1;
  Id selected = 0, video = none, customVehicle = none, office = none;
  std::vector<int> program;
  std::vector<int> assignments{-1, -1, -1, -1};
  float speed = 1, orbit = .65f, distance = 44, pitch = .25f, playerYaw = 0, playerPitch = 0;
  Body body;
  double oldMx = 0, oldMy = 0, observerMouseX = 0, observerMouseY = 0;
  bool observerDragging = false;
  float scrollOffset = 0;
  std::string toast, floors = "2000", population = "100000", seed = "20261018", capacity = "0.45",
                     day = "30", wealth = "80000", recipient = "2", message, inspectId = "1";
  double toastUntil = 0;
  std::vector<int> clips{4, 2, 0, 5, 1, 3};
  std::filesystem::path saveRoot = std::filesystem::path(MEGA_PROJECT) / "userdata";
  float cachedHappiness = .5f;
  std::size_t cachedLiving = 0, cachedQueued = 0;
  double nextStats = 0;
  Application(GLFWwindow *w)
      : window(w), locale(std::filesystem::path(MEGA_ASSETS) / "locales"),
        font(std::filesystem::path(MEGA_ASSETS) / "fonts/NotoSansCJK-Regular.ttc",
             locale.charset()),
        ui(font) {
    assets.loadDormitory(std::filesystem::path(MEGA_ASSETS) / "models");
    renderer = std::make_unique<Renderer>(w, MEGA_SHADERS, font.atlas, font.atlasSize);
    font.dirty = false;
    renderer->uploadMaterials(assets.textures, AssetLibrary::atlasSize);
    for (const char *name :
         {"resident_Male", "resident_Female", "resident_Male_lod", "resident_Female_lod"})
      renderer->addSkinMesh(name, assets.skinGeometry(name));
    for (auto &pixels : assets.textures) {
      pixels.clear();
      pixels.shrink_to_fit();
    }
    glfwSetWindowUserPointer(w, this);
    glfwSetCharCallback(w, [](GLFWwindow *w, unsigned cp) {
      static_cast<Application *>(glfwGetWindowUserPointer(w))->ui.charEvent(cp);
    });
    glfwSetKeyCallback(w, [](GLFWwindow *w, int key, int, int action, int) {
      if (action == GLFW_PRESS || action == GLFW_REPEAT)
        static_cast<Application *>(glfwGetWindowUserPointer(w))->ui.keyEvent(key);
    });
    glfwSetScrollCallback(w, [](GLFWwindow *w, double, double y) {
      static_cast<Application *>(glfwGetWindowUserPointer(w))->ui.scroll += y;
    });
  }
  std::string t(const std::string &key) { return locale(key); }
  void notify(const std::string &s) {
    toast = s;
    toastUntil = glfwGetTime() + 6;
  }
  template <class F> void act(F fn) {
    try {
      fn();
    } catch (const std::exception &e) {
      notify(t(e.what()));
    }
  }
  std::filesystem::path savePath(int which = 0) {
    return saveRoot / ("slot-" + std::to_string(which > 0 ? which : slot) + ".mdsave");
  }
  void refreshSaveCatalogue(bool force = false) {
    if (!force && glfwGetTime() < nextSlotScan)
      return;
    for (int i = 0; i < 8; ++i)
      saveCatalogue[i] = World::inspectSave(savePath(i + 1));
    nextSlotScan = glfwGetTime() + 2;
  }
  void openSaves(bool writing) {
    savingSlots = writing && world && !menu;
    savesWasPaused = paused;
    paused = true;
    savesOpen = true;
    overwriteSlot = 0;
    saveDraft = world ? world->saveName : std::string{};
    mouseLocked = false;
    phone = false;
    helpOpen = false;
    ui.blur();
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    refreshSaveCatalogue(true);
  }
  void closeSaves() {
    savesOpen = false;
    paused = savesWasPaused;
    overwriteSlot = 0;
    ui.blur();
  }
  void saveBrowser() {
    refreshSaveCatalogue();
    float width = std::min(800.f, float(ui.width) - 48),
          height = std::min(740.f, float(ui.height) - 48);
    float x = (ui.width - width) / 2, y = (ui.height - height) / 2;
    ui.enabled = true;
    ui.fill({0, 0, float(ui.width), float(ui.height)}, hex(0x080d14, .86f));
    ui.fill({x, y, width, height}, hex(0x171f28), 12);
    ui.text(x + 24, y + 22, t(savingSlots ? "save.browser_save" : "save.browser_load"), 25,
            hex(0xe6eadf));
    if (ui.button("closeSaveBrowser", {x + width - 56, y + 16, 32, 32}, "×")) {
      closeSaves();
      return;
    }
    if (savingSlots) {
      ui.text(x + 24, y + 70, t("save.name"), 14, hex(0xb1c2bd));
      ui.input("worldSaveName", {x + 144, y + 58, width - 168, 34}, saveDraft);
    } else {
      ui.wrap(x + 24, y + 66, width - 48, t("save.browser_help"), 13, hex(0x9db0b4));
    }
    float rowHeight = std::min(54.f, (height - 235) / 8);
    for (int i = 0; i < 8; ++i) {
      const auto &info = saveCatalogue[i];
      float ry = y + 110 + i * rowHeight;
      Rect row{x + 22, ry, width - 44, rowHeight - 5};
      if (ui.button("savedSlot" + std::to_string(i), row, "", false, slot == i + 1)) {
        slot = i + 1;
        overwriteSlot = 0;
        ui.blur();
      }
      ui.text(row.x + 12, ry + 10, std::to_string(i + 1), 17, hex(0xa5cbb9));
      auto name = !info.available     ? t("save.empty")
                  : info.name.empty() ? t("save.untitled")
                                      : info.name;
      Rect previous = ui.clipRect();
      ui.clip({row.x + 48, ry + 2, row.w - 62, row.h - 3});
      ui.text(row.x + 50, ry + 5, name, 14, hex(info.available ? 0xdde5df : 0x8b9a9e));
      std::string detail;
      if (info.metadata) {
        detail = t(info.mode == Mode::Survival  ? "mode.survival"
                   : info.mode == Mode::Manager ? "mode.manager"
                                                : "mode.cheat") +
                 " · " + timeLabel(info.time) + " · " + t("hud.population") + " " +
                 number(info.population);
      } else if (info.available)
        detail = t("save.legacy_info");
      if (info.backup && info.available)
        detail = t("save.backup") + " · " + detail;
      ui.text(row.x + 50, ry + 25, detail, 11, hex(0x93aaa3));
      ui.clip(previous);
    }
    float foot = y + height - 112;
    if (savingSlots) {
      auto &info = saveCatalogue[slot - 1];
      bool confirm = info.available && overwriteSlot != slot;
      ui.wrap(x + 24, foot, width - 48,
              t(info.available ? "save.overwrite_help" : "save.name_help"), 13, hex(0xc5b999));
      if (ui.button("writeSelectedSlot", {x + 24, y + height - 58, width - 48, 36},
                    t(confirm ? "save.confirm_overwrite" : "nav.save"), true)) {
        if (confirm) {
          overwriteSlot = slot;
          return;
        }
        act([&] {
          auto previous = world->saveName;
          try {
            world->saveName = saveDraft;
            world->save(savePath());
          } catch (...) {
            world->saveName = std::move(previous);
            throw;
          }
          refreshSaveCatalogue(true);
          closeSaves();
          notify(t("save.saved"));
        });
      }
    } else {
      if (ui.button("openBrowserImporter", {x + 24, y + height - 162, width - 48, 32},
                    t("import.title"))) {
        openImporter();
        return;
      }
      const auto &info = saveCatalogue[slot - 1];
      auto backup = savePath();
      backup += ".bak";
      std::error_code error;
      bool hasBackup = std::filesystem::is_regular_file(backup, error);
      ui.wrap(x + 24, foot, width - 48, t("save.backup_help"), 13, hex(0x9fb1b1));
      ui.enabled = info.available;
      if (ui.button("readSelectedSlot", {x + 24, y + height - 58, (width - 56) / 2, 36},
                    t("nav.load"), true)) {
        act([&] {
          auto loaded = World::load(savePath());
          savesOpen = false;
          setWorld(std::move(loaded));
        });
        return;
      }
      ui.enabled = hasBackup;
      if (ui.button("readSelectedBackup",
                    {x + 32 + (width - 56) / 2, y + height - 58, (width - 56) / 2, 36},
                    t("save.load_backup"))) {
        act([&] {
          auto loaded = World::loadBackup(savePath());
          savesOpen = false;
          setWorld(std::move(loaded));
        });
        return;
      }
      ui.enabled = true;
    }
  }
  void openImporter() {
    if (savesOpen)
      closeSaves();
    importWasPaused = paused;
    paused = true;
    importOpen = true;
    importSummary = false;
    helpOpen = phone = false;
    mouseLocked = false;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    ui.blur();
  }
  void closeImporter() {
    if (importing.valid())
      return;
    importOpen = false;
    paused = importSummary ? true : importWasPaused;
    ui.blur();
  }
  void importPanel() {
    const bool busy = importing.valid();
    float width = std::min(900.f, float(ui.width) - 48),
          height = std::min(780.f, float(ui.height) - 48);
    float x = (ui.width - width) / 2, y = (ui.height - height) / 2;
    ui.enabled = true;
    ui.clip({0, 0, float(ui.width), float(ui.height)});
    ui.fill({0, 0, float(ui.width), float(ui.height)}, hex(0x080d14, .9f));
    ui.fill({x, y, width, height}, hex(0x171f28), 12);
    ui.text(x + 24, y + 24, t(importSummary ? "import.summary" : "import.title"), 25,
            hex(0xe6eadf));
    ui.enabled = !busy;
    if (ui.button("closeImporter", {x + width - 56, y + 16, 32, 32}, "×")) {
      closeImporter();
      return;
    }
    if (importSummary) {
      float at = ui.wrap(x + 24, y + 80, width - 48, t("import.ready"), 15, hex(0xa5cbb9)) + 20;
      for (auto key : {"residents", "homes", "items", "services", "restarted", "funds",
                       "outsideFunds", "infrastructure"}) {
        ui.text(x + 28, at, t(std::string("import.") + key), 14, hex(0x9fb5b0));
        auto value = world->importReport.at(key);
        std::string text = std::string(key) == "funds" || std::string(key) == "outsideFunds"
                               ? money(value)
                               : number(value);
        ui.text(x + width - 245, at, text, 16, hex(0xe2e8df));
        at += 32;
      }
      at = ui.wrap(x + 24, at + 14, width - 48, t("import.changed"), 14, hex(0xc7ba9e)) + 18;
      ui.wrap(x + 24, at, width - 48, t("import.archived"), 14, hex(0x99acae));
      if (ui.button("saveImportedWorld", {x + 24, y + height - 104, width - 48, 36},
                    t("save.browser_save"), true)) {
        importOpen = false;
        openSaves(true);
        return;
      }
      if (ui.button("continueImportedWorld", {x + 24, y + height - 58, width - 48, 34},
                    t("action.play"))) {
        importOpen = false;
        paused = false;
      }
      return;
    }
    float at = ui.wrap(x + 24, y + 80, width - 48, t("import.desc"), 15, hex(0xa5b8b7)) + 26;
    ui.text(x + 24, at, t("import.path"), 14, hex(0xb2c6bb));
    at += 25;
    ui.input("browserExportPath", {x + 24, at, width - 48, 40}, browserPath);
    at += 62;
    for (int i = 0; i < 3; ++i) {
      auto mode = Mode(i);
      float card = (width - 64) / 3;
      if (ui.button("importMode" + std::to_string(i), {x + 24 + i * (card + 8), at, card, 38},
                    t(i == 0   ? "mode.survival"
                      : i == 1 ? "mode.manager"
                               : "mode.cheat"),
                    false, importMode == mode))
        importMode = mode;
    }
    at = ui.wrap(x + 24, at + 64, width - 48, t("import.warning"), 15, hex(0xd1b689)) + 24;
    at = ui.wrap(x + 24, at, width - 48, t("import.changed"), 14, hex(0xa5b7b3)) + 20;
    ui.wrap(x + 24, at, width - 48, t("import.archived"), 14, hex(0x99acae));
    ui.enabled = !busy && !browserPath.empty();
    if (ui.button("convertBrowserExport", {x + 24, y + height - 62, width - 48, 38},
                  t(busy ? "import.busy" : "import.convert"), true)) {
      importing = std::async(std::launch::async, [path = browserPath, mode = importMode] {
        return World::importBrowser(std::filesystem::path(path), mode);
      });
      ui.blur();
    }
    ui.enabled = true;
    if (importing.valid() &&
        importing.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
      act([&] {
        auto imported = importing.get();
        setWorld(std::move(imported));
        importOpen = importSummary = true;
        helpOpen = false;
        paused = true;
      });
  }
  void setWorld(World &&w) {
    world = std::make_unique<World>(std::move(w));
    selected = world->player;
    savesOpen = importOpen = importSummary = false;
    emergencyScreen = companyScreen = companyConstruction = cheatOpen = false;
    selectedCompany = video = office = customVehicle = lethalConfirm = none;
    companyPage = 0;
    nextCompanyStats = 0;
    scrollOffset = networkScroll = helpScroll = 0;
    tab = networkTab = 0;
    program.clear();
    assignments.assign(4, -1);
    characterHeading.clear();
    walkBlend.clear();
    characterPoses.clear();
    closetAnimation.clear();
    liftDoorAnimation.clear();
    mouseLocked = false;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    currentFloor = int(world->position(selected).y / 3);
    observerHeight = 0;
    body.position = world->position(selected);
    helpOpen = true;
    cameraFocus = {0, 0, world->config.layoutVersion == 1 ? 0 : world->layout(0).halfDepth() - 50};
    menu = false;
    drawerOpen = false;
    drawerSlide = 0;
    phone = false;
    firstPerson = false;
    paused = true;
    if (world->config.mode == Mode::Survival)
      setFirstPerson(true);
    nextStats = 0;
    nextInstances = 0;
    instanceFloor = -1;
    cutaway = world->config.mode != Mode::Manager;
    ui.blur();
  }
  void header() {
    float width = float(ui.width);
    ui.fill({0, 0, width, 70}, hex(0x111d22, .98f));
    ui.fill({24, 20, 29, 30}, hex(0xa5cbb9), 4);
    for (int x = 0; x < 3; ++x)
      for (int y = 0; y < 3; ++y)
        ui.fill({30.f + x * 6, 26.f + y * 6, 3, 3}, hex(0x18262b));
    ui.text(68, 17, t("brand"), 22, hex(0xf0ece3));
    ui.text(69, 43, t("subtitle"), 12, hex(0x82999c));
    float x = width - 204;
    for (auto lang : {"en", "fr", "zh"}) {
      if (ui.button(std::string("lang.") + lang, {x, 22, 54, 28},
                    std::string(lang) == "zh"   ? "中文"
                    : std::string(lang) == "fr" ? "FR"
                                                : "EN",
                    false, locale.language == lang))
        locale.language = lang;
      x += 60;
    }
    ui.line(0, 69, width, 69, hex(0x34464b));
  }
  void newWorld() {
    float w = float(ui.width), h = float(ui.height);
    ui.fill({0, 70, w, h - 70}, hex(0x101c22, .94f));
    float left = std::max(32.f, (w - 1200) / 2), right = left + 530;
    ui.text(left, 130, t("new.world"), 12, hex(0xa5cbb9));
    ui.wrap(left, 170, 470, t("new.title"), 42, hex(0xf0ece3));
    ui.wrap(left, 300, 430, t("new.intro"), 19, hex(0x98abae));
    if (ui.button("newAccidents", {left, 370, 350, 34},
                  t("fire.accidents") + " · " + std::to_string(int(config.accidentRate)) + "×"))
      config.accidentRate = config.accidentRate == 0 ? 1 : config.accidentRate == 1 ? 3 : 0;
    for (int i = 0; i < 3; ++i) {
      std::string key = i == 0 ? "survival" : i == 1 ? "manager" : "cheat";
      float y = 440 + i * 72;
      if (ui.button("mode" + key, {left, y, 440, 60},
                    t("mode." + key) + "  ·  " + t("mode." + key + ".desc"), false,
                    int(config.mode) == i))
        config.mode = Mode(i);
    }
    ui.fill({right, 110, w - right - 40, h - 160}, hex(0x18282e), 12);
    float x = right + 28, y = 145, column = (w - right - 96) / 2;
    auto field = [&](const char *key, std::string &value, int col) {
      float fx = x + col * (column + 20);
      ui.text(fx, y, t(key), 14, hex(0x9eb0b1));
      ui.input(key, {fx, y + 23, column, 40}, value, true);
    };
    field("new.floors", floors, 0);
    field("new.population", population, 1);
    y += 82;
    field("new.seed", seed, 0);
    field("new.day", day, 1);
    y += 82;
    field("new.scarcity", capacity, 0);
    field("new.wealth", wealth, 1);
    y += 82;
    field("new.floor_width", floorWidth, 0);
    field("new.floor_depth", floorDepth, 1);
    y += 82;
    field("new.lobby_width", lobbyWidth, 0);
    field("new.lobby_depth", lobbyDepth, 1);
    y += 84;
    ui.text(x, y, t("new.harsh"), 13, hex(0xa5cbb9));
    ui.wrap(x, y + 30, w - right - 94, t("new.capacity"), 16, hex(0x98abae));
    bool busy = generation.valid();
    if (ui.button("create", {x, h - 156, w - right - 96, 48},
                  t(busy ? "new.generating" : "new.begin"), true) &&
        !busy)
      act([&] {
        config.floorWidth = std::stoi(floorWidth);
        config.floorDepth = std::stoi(floorDepth);
        config.lobbyWidth = std::stoi(lobbyWidth);
        config.lobbyDepth = std::stoi(lobbyDepth);
        config.floors = std::stoi(floors);
        config.population = std::stoi(population);
        config.seed = std::stoull(seed);
        config.dayMinutes = std::stod(day);
        config.scarcity = std::stof(capacity);
        config.startingCash = std::stoll(wealth);
        config.validate();
        generation =
            std::async(std::launch::async, [cfg = config] { return World::generate(cfg); });
      });
    bool enabled = ui.enabled;
    ui.enabled = enabled && !busy;
    if (ui.button("menuLoad", {x, h - 98, w - right - 96, 34}, t("new.load")))
      openSaves(false);
    ui.enabled = enabled;
    ui.text(left, h - 50, t("status.early"), 11, hex(0x738b91));
    if (generation.valid() &&
        generation.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
      act([&] { setWorld(generation.get()); });
  }
  void update(float dt) {
    if (!world || menu || world->gameOver)
      return;
    auto &w = *world;
    bool menuKey = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
    bool escapeKey = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (!ui.typing() && ((menuKey && !menuKeyHeld) || (escapeKey && !escapeHeld))) {
      if (escapeKey && phone)
        phone = false;
      else if (escapeKey && helpOpen)
        helpOpen = false;
      else
        drawerOpen = !drawerOpen;
      mouseLocked = false;
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    menuKeyHeld = menuKey;
    escapeHeld = escapeKey;
    bool helpKey = glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS;
    if (helpKey && !helpHeld && !ui.typing()) {
      helpOpen = !helpOpen;
      if (helpOpen)
        phone = false;
      mouseLocked = false;
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    helpHeld = helpKey;
    bool viewKey = glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS;
    if (viewKey && !viewKeyHeld && !ui.typing() && !phone && !helpOpen && !drawerOpen)
      setFirstPerson(!firstPerson);
    viewKeyHeld = viewKey;
    // The simulation owns momentum. Only an actively driven body skips event integration.
    const auto &controlled = w.residents[selected];
    bool movingInput = glfwGetKey(window, GLFW_KEY_W) || glfwGetKey(window, GLFW_KEY_A) ||
                       glfwGetKey(window, GLFW_KEY_S) || glfwGetKey(window, GLFW_KEY_D);
    auto airborne = w.looseBodies.find(selected);
    bool knocked = airborne != w.looseBodies.end() && airborne->second.pusher != none;
    bool canControl = selected == w.player || w.config.mode == Mode::Cheat;
    w.vehicleController = firstPerson && canControl && !phone && !helpOpen && !drawerOpen &&
                                  !paused && controlled.vehicle != none &&
                                  (movingInput || !w.vehicleJourneys.contains(controlled.vehicle))
                              ? controlled.vehicle
                              : none;
    w.physicsController =
        firstPerson && canControl && !phone && !helpOpen && !drawerOpen && !ui.typing() &&
                !controlled.surrendered && !knocked &&
                (movingInput || (controlled.activity == Activity::Idle && controlled.route.empty()))
            ? selected
            : none;
    w.focus(firstPerson ? int(std::max(0.f, w.position(selected).y) / 3) : currentFloor);
    if (!paused && !w.gameOver && !helpOpen) {
      double elapsed = std::min(dt, .1f) * (1440 / w.config.dayMinutes) * speed;
      w.moveCrowd(elapsed);
      w.advance(elapsed, 6000);
    }
    if (glfwGetTime() > nextStats) {
      cachedHappiness = w.happiness();
      cachedLiving = w.living();
      cachedQueued = 0;
      for (auto &f : w.facilities)
        cachedQueued += f.queue.size();
      nextStats = glfwGetTime() + 1;
    }
    bool interact = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    if (firstPerson && canControl && !phone && !ui.typing() && !helpOpen && !drawerOpen &&
        w.residents[selected].alive && !w.gameOver) {
      double mx, my;
      glfwGetCursorPos(window, &mx, &my);
      if (mouseLocked) {
        playerYaw += float(mx - oldMx) * .0025f;
        playerPitch = std::clamp(playerPitch - float(my - oldMy) * .0025f, -1.35f, 1.35f);
      }
      oldMx = mx;
      oldMy = my;
      auto &r = w.residents[selected];
      bool punch = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS,
           fire = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS,
           yield = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS,
           push = glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS;
      if (!paused && mouseLocked && r.lift == none && r.ambulance == none && r.carriedBy == none &&
          r.medicalDuty == none) {
        if (yield && !surrenderHeld)
          act([&] { w.surrender(selected); });
        if ((push && !shoveHeld) || (punch && !punchHeld) ||
            (fire && !fireHeld && w.hasGun(selected))) {
          float reach = push ? 1.8f : punch ? 2.5f : 35.f;
          Id target = none;
          float best = reach;
          Vec3 forward{std::sin(playerYaw) * std::cos(playerPitch), std::sin(playerPitch),
                       -std::cos(playerYaw) * std::cos(playerPitch)};
          Vec3 origin = w.position(selected) + Vec3{0, 1.35f, 0};
          for (auto &other : w.residents)
            if (other.id != selected && other.alive && other.present) {
              Vec3 delta = w.position(other.id) + Vec3{0, 1.25f, 0} - origin;
              float d = length(delta);
              if (d < best && dot(normalized(delta), forward) > ((push || punch) ? .6f : .994f) &&
                  w.lineOfSight(origin, origin + delta)) {
                target = other.id;
                best = d;
              }
            }
          if (target != none)
            act([&] {
              if (push)
                w.shove(selected, target);
              else if (punch)
                w.attack(selected, target, false);
              else
                w.shoot(selected, target);
            });
        }
      }
      shoveHeld = push;
      punchHeld = punch;
      fireHeld = fire;
      surrenderHeld = yield;
      if (interact && !interactHeld && r.lift == none && r.ambulance == none &&
          r.carriedBy == none && r.medicalDuty == none)
        act([&] {
          if (r.ladder >= 0) {
            w.releaseLadder(selected);
            return;
          }
          if (w.nearbyLadder(selected) >= 0) {
            w.grabLadder(selected);
            return;
          }
          if (r.vehicle != none) {
            w.exitVehicle(selected);
            return;
          }
          for (const auto &vehicle : w.vehicles)
            if (vehicle.owner == r.account && w.near(selected, vehicle.pos, 2.5f)) {
              w.enterVehicle(selected, vehicle.id);
              return;
            }
          for (auto &lift : w.lifts)
            if (w.near(selected, w.liftDoor(currentFloor), 3) &&
                std::find(w.facilities[lift.facility].stops.begin(),
                          w.facilities[lift.facility].stops.end(),
                          currentFloor) != w.facilities[lift.facility].stops.end()) {
              tab = 1;
              drawerOpen = true;
              scrollOffset = 0;
              mouseLocked = false;
              glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
              return;
            }
          Id best = none;
          float d = 3;
          for (const auto &f : w.facilities) {
            float candidate = length(facilityApproach(f) - w.position(selected));
            if (f.open && candidate < d) {
              d = candidate;
              best = f.id;
            }
          }
          if (best == none)
            return;
          auto &f = w.facilities[best];
          if (f.kind == FacilityKind::Reception) {
            if (!w.operational(f.id))
              throw std::runtime_error("The reception desk is unstaffed");
            helpOpen = true;
            mouseLocked = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
          } else if (f.kind == FacilityKind::Elevator) {
            tab = 1;
            drawerOpen = true;
            mouseLocked = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
          } else if (f.kind == FacilityKind::Dealership || f.kind == FacilityKind::Parking ||
                     f.kind == FacilityKind::Ramp || f.kind == FacilityKind::FreightLift) {
            networkTab = 4;
            networkScroll = 0;
            phone = true;
            mouseLocked = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
          } else if (f.kind == FacilityKind::Terminal || f.kind == FacilityKind::Office) {
            w.useTerminal(selected, best, 30);
            openNetwork();
          } else
            w.joinQueue(selected, best);
        });
      if (!paused && r.ladder >= 0) {
        float direction = float(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) -
                          float(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
        w.climb(selected, direction, dt);
        body.position = r.pos;
        body.velocity = {};
        body.grounded = false;
      } else if (!paused && r.vehicle != none) {
        float throttle = float(glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) -
                         float(glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
        float steer = float(glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) -
                      float(glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
        if (throttle != 0 || steer != 0 || !w.vehicleJourneys.contains(r.vehicle))
          act([&] { w.driveVehicle(selected, throttle, steer, std::min(dt, .05f)); });
        playerYaw = w.vehicles[r.vehicle].yaw;
      } else if (!paused && r.activity != Activity::Jailed && r.activity != Activity::Escorted &&
                 r.activity != Activity::Restrained && !r.surrendered && r.lift == none &&
                 r.ambulance == none && r.carriedBy == none && r.medicalDuty == none &&
                 !(w.looseBodies.contains(selected) && w.looseBodies.at(selected).pusher != none)) {
        Vec3 front{std::sin(playerYaw), 0, -std::cos(playerYaw)},
            right{std::cos(playerYaw), 0, std::sin(playerYaw)}, move{};
        if (glfwGetKey(window, GLFW_KEY_W))
          move = move + front;
        if (glfwGetKey(window, GLFW_KEY_S))
          move = move - front;
        if (glfwGetKey(window, GLFW_KEY_D))
          move = move + right;
        if (glfwGetKey(window, GLFW_KEY_A))
          move = move - right;
        if (length(move) > .01f) {
          if (r.activity == Activity::Sleeping || w.personalUses.contains(selected)) {
            w.stopPersonalUse(selected);
            r.pos = w.accessible(w.position(selected));
            w.crowd.erase(selected);
          }
          body.position = w.position(selected);
          w.clockOut(selected);
          w.leaveQueue(selected);
          r.route.clear();
          ++r.routeVersion;
          r.task = TaskKind::None;
          r.activity = Activity::Idle;
        }
        if (r.route.empty() && r.activity == Activity::Idle) {
          Vec3 before = w.position(selected);
          bool running = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
          body = w.stepBody(selected, normalized(move) * w.walkingSpeed(selected, running),
                            std::min(dt, .1f), glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
          if (length(body.position - before) > .01f && body.grounded)
            w.exercise(selected, dt * (1440 / w.config.dayMinutes), running ? 1.f : .2f);
        }
      }
      currentFloor =
          std::clamp(int(std::max(0.f, w.position(selected).y) / 3), 0, w.config.floors - 1);
    }
    interactHeld = interact;
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && mouseLocked) {
      mouseLocked = false;
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
  }
  void stats() {
    auto &w = *world;
    float width = float(ui.width);
    ui.fill({0, 70, width, 88}, hex(0x132127, .97f));
    std::array<std::string, 5> labels = {t("status.day"), t("status.population"),
                                         t("status.happiness"), t("status.waiting"),
                                         t("status.cash")};
    std::array<std::string, 5> values = {
        timeLabel(w.time), number(cachedLiving), std::to_string(int(cachedHappiness * 100)) + "%",
        number(cachedQueued), money(w.accounts[w.residents[w.player].account].balance)};
    for (int i = 0; i < 5; ++i) {
      float x = 26 + i * (width - 52) / 5;
      ui.text(x, 86, labels[i], 11, hex(0x8ca2a5));
      ui.text(x, 112, values[i], 25, hex(i == 4 ? 0xa5cbb9 : 0xe0e6e2));
      if (i)
        ui.line(x - 16, 88, x - 16, 141, hex(0x2a3e44));
    }
    ui.line(0, 157, width, 157, hex(0x34464b));
  }
  void meter(float x, float y, float width, const std::string &label, float value, Color c) {
    value = std::clamp(value, 0.f, 1.f);
    ui.text(x, y, label, 13, hex(0xa6b6b7));
    ui.text(x + width - 36, y, std::to_string(int(value * 100)), 13, hex(0xd8e3df));
    ui.fill({x, y + 25, width, 4}, hex(0x2b3d42), 2);
    ui.fill({x, y + 25, width * value, 4}, c, 2);
  }
  float emergencyPanel(float x, float y, float width) {
    auto &w = *world;
    auto &r = w.residents[w.player];
    ui.text(x, y, t("fire.title"), 23, hex(0xf0d8b0));
    y += 37;
    if (ui.button("fireBack", {x, y, width, 31}, t("fire.back"))) {
      emergencyScreen = false;
      scrollOffset = 0;
    }
    y += 42;
    y = ui.wrap(x, y, width, t("fire.description"), 13, hex(0xb4bdc6)) + 14;
    Id tool = w.extinguisher(w.player);
    if (tool != none) {
      ui.text(x, y,
              t("item.extinguisher") + " · " + std::to_string(int(w.items[tool].durability * 100)) +
                  "%",
              16, hex(0xa5cbb9));
      y += 30;
    }
    if (w.dangerAt(w.position(w.player)) > .01f) {
      if (ui.button("evacuateNow", {x, y, width, 35}, t("fire.evacuate"), true))
        act([&] {
          if (!w.evacuate(w.player))
            throw std::runtime_error(t("fire.no_escape"));
          paused = false;
        });
      y += 45;
    }
    if (w.fireSprays.contains(w.player)) {
      if (ui.button("stopSpray", {x, y, width, 32}, t("fire.stop")))
        w.stopExtinguish(w.player);
      y += 42;
    }
    Id station = w.nearest(w.player, FacilityKind::FireStation);
    if (station != none) {
      const auto &f = w.facilities[station];
      y = ui.wrap(x, y, width,
                  t("facility.fire_station") + " · " + t("status.floor") + " " +
                      std::to_string(f.floor + 1) + " · " + t("business.stock") + " " +
                      std::to_string(f.stock),
                  15, hex(0xc2d4cf)) +
          12;
      if (ui.button("fireStationWalk", {x, y, width, 31}, t("fire.to_station")))
        act([&] {
          w.travel(w.player, f.pos + Vec3{0, 0, -1.6f}, TaskKind::None, none);
          paused = false;
        });
      y += 40;
      if (r.role != Role::Firefighter) {
        if (ui.button("fireJob", {x, y, width, 31}, t("fire.join")))
          act([&] { w.chooseJob(w.player, Role::Firefighter, station); });
        y += 40;
      }
      if (w.near(w.player, f.pos, 3) && tool == none) {
        if (ui.button("fireEquipment", {x, y, width, 32}, t("fire.equipment")))
          act([&] { w.issueExtinguisher(w.player, station); });
        y += 43;
      }
    } else {
      y = ui.wrap(x, y, width, t("fire.no_station"), 13, hex(0xd4aa8b)) + 16;
    }
    for (const auto &[fid, id] : w.activeFires) {
      auto &fire = w.fires[id];
      auto &f = w.facilities[fid];
      ui.line(x, y, x + width, y, hex(0x4a4140));
      y += 18;
      y = ui.wrap(x, y, width,
                  t(facilityKey(f.kind)) + " · " + t("status.floor") + " " +
                      std::to_string(f.floor + 1),
                  17, hex(0xf0bf91)) +
          9;
      y = ui.wrap(x, y, width,
                  t(fire.contained ? "fire.contained" : "fire.burning") + " · " + t("fire.heat") +
                      " " + std::to_string(int(fire.heat * 100)) + "% · " + t("fire.smoke") + " " +
                      std::to_string(int(fire.smoke * 100)) + "%",
                  13, hex(0xcbbcaf)) +
          12;
      y = ui.wrap(x, y, width,
                  fire.worker == none
                      ? t("fire.no_crew")
                      : t("fire.crew") + " " + w.displayName(fire.worker, locale.language),
                  14, hex(0xa6bfb3)) +
          12;
      if (fire.escrow == none && !fire.contained) {
        if (ui.button("fundFire" + std::to_string(id), {x, y, width, 32},
                      t("fire.fund") + " · " + money(fire.budget)))
          act([&] {
            w.requestFireResponse(w.player, id);
            paused = false;
          });
        y += 41;
      }
      if (fire.worker == none && fire.escrow != none && r.role == Role::Firefighter &&
          !fire.contained) {
        if (ui.button("takeFire" + std::to_string(id), {x, y, width, 32}, t("fire.accept")))
          act([&] {
            w.assignFirefighter(w.player, id);
            paused = false;
          });
        y += 41;
      }
      if (!fire.contained) {
        if (ui.button("goFire" + std::to_string(id), {x, y, width, 32},
                      t(tool == none ? "fire.to_safe_edge" : "fire.to_scene")))
          act([&] {
            Vec3 goal = tool == none ? w.accessible(f.pos + Vec3{0, 0, -24}) : w.fireApproach(id);
            w.travel(w.player, goal, tool == none ? TaskKind::None : TaskKind::FireResponse,
                     tool == none ? none : id);
            paused = false;
          });
        y += 41;
        if (tool != none && w.near(w.player, f.pos, 7)) {
          if (ui.button("sprayFire" + std::to_string(id), {x, y, width, 32}, t("fire.spray"), true))
            act([&] {
              w.beginExtinguish(w.player, id);
              paused = false;
            });
          y += 41;
        }
      }
      y += 10;
    }
    if (w.activeFires.empty()) {
      y = ui.wrap(x, y, width, t("fire.clear"), 15, hex(0x9fc6af)) + 18;
    }
    if (w.config.mode == Mode::Cheat) {
      if (ui.button("accidentRate", {x, y, width, 32},
                    t("fire.accidents") + " · " + std::to_string(int(w.config.accidentRate)) + "×"))
        w.config.accidentRate = w.config.accidentRate == 0 ? 1 : w.config.accidentRate == 1 ? 3 : 0;
      y += 42;
      for (const auto &f : w.facilities)
        if (f.floor == currentFloor && f.open && !f.demolished && !w.activeFires.contains(f.id) &&
            f.kind != FacilityKind::FireStation) {
          if (ui.button("igniteTest", {x, y, width, 32},
                        t("fire.test") + " · " + t(facilityKey(f.kind))))
            act([&] { w.triggerFire(w.player, f.id); });
          y += 42;
          break;
        }
    }
    return y;
  }
  float lifePanel(float x, float y, float width) {
    if (emergencyScreen)
      return emergencyPanel(x, y, width);
    if (ui.button("openEmergencies", {x, y, width, 33}, t("fire.title"))) {
      emergencyScreen = true;
      scrollOffset = 0;
    }
    y += 45;
    auto &w = *world;
    auto &r = w.residents[selected];
    ui.text(x, y, w.displayName(selected, locale.language), 26, hex(0xe6e8df));
    ui.text(x, y + 39, "#" + std::to_string(selected + 1) + " · " + t(roleKey(r.role)), 13,
            hex(0xa5cbb9));
    ui.text(x, y + 65,
            t(w.looseBodies.contains(r.id) ? "physics.falling" : activityKey(r.activity)), 15,
            hex(0x99adaf));
    ui.text(x, y + 88,
            t("routine.sleep") + " " + hoursLabel(r.routine.sleepStart, r.routine.sleepHours), 12,
            hex(0x9cafa9));
    if (r.jobSite != none)
      ui.text(x, y + 110,
              t("routine.shift") + " " + hoursLabel(r.routine.shiftStart, r.routine.shiftHours), 12,
              hex(0xa5cbb9));
    y += 145;
    ui.text(x, y, t("status.cash") + "  " + money(w.accounts[r.account].balance), 17,
            hex(0xa5cbb9));
    y += 34;
    meter(x, y, width, t("life.health"), r.health / 100, hex(0x8eb9a3));
    meter(x, y + 54, width, t("life.hunger"), r.hunger, hex(0xdbab72));
    meter(x, y + 108, width, t("life.thirst"), r.thirst, hex(0x79abb9));
    meter(x, y + 162, width, t("life.energy"), r.energy, hex(0xa8b37c));
    meter(x, y + 216, width, t("life.hygiene"), r.hygiene, hex(0x79abb9));
    y += 270;
    ui.text(x, y,
            t("hud.fitness") + " " + std::to_string(int(r.fitness)) + " · " + t("hud.strength") +
                " " + std::to_string(int(r.strength)),
            14, hex(0xb4cbbf));
    y += 32;
    if (ui.button("camera", {x, y, width, 38},
                  t(firstPerson ? "action.observe" : "action.possess")))
      setFirstPerson(!firstPerson);
    y += 48;
    if (ui.button("openNet", {x, y, width, 38}, t("nav.network"), true))
      openNetwork();
    y += 48;
    if (ui.button("styleOpen", {x, y, width, 36}, t("custom.title"))) {
      phone = true;
      networkTab = 5;
      networkScroll = 0;
    }
    y += 44;
    if (ui.button("sleepHome", {x, y, width, 36}, t("life.sleep")))
      act([&] {
        w.sleepAtHome(selected);
        paused = false;
      });
    y += 44;
    if (ui.button("medicalOpen", {x, y, width, 36}, t("network.medical"))) {
      openNetwork();
      networkTab = 6;
      networkScroll = 0;
    }
    y += 44;
    if (ui.button("householdPlan", {x, y, width, 36}, t("autonomy.title")))
      householdOpen = !householdOpen;
    y += 44;
    if (householdOpen) {
      Money reserve = w.householdReserve(selected);
      y = ui.wrap(x, y, width, t("autonomy.reserve") + " " + money(reserve), 14, hex(0xa5cbb9)) +
          10;
      y = ui.wrap(x, y, width,
                  t("autonomy.spendable") + " " +
                      money(std::max<Money>(0, w.accounts[r.account].balance - reserve)),
                  14, hex(0xa5bfc4)) +
          10;
      auto plan = w.residentPlans.find(selected);
      if (plan != w.residentPlans.end()) {
        y = ui.wrap(x, y, width, t(autonomyKey(plan->second.action)), 13, hex(0xc1cbb7)) + 12;
        y = ui.wrap(
                x, y, width,
                t("autonomy.spent") + " " +
                    money(plan->second.day == int(w.time / 86400) ? plan->second.spentToday : 0),
                12, hex(0x8da5aa)) +
            12;
        if (plan->second.order != none) {
          const auto &order = w.orders[plan->second.order];
          y = ui.wrap(x, y, width,
                      t(itemLabel(order.product)) + " · " +
                          t(order.state == OrderState::Delivered   ? "order.delivered"
                            : order.state == OrderState::Cancelled ? "autonomy.cancelled"
                            : order.state == OrderState::Lost      ? "autonomy.parcel_lost"
                                                                   : "autonomy.waiting"),
                      13, hex(0xb9c6c4)) +
              12;
        }
      }
      if (selected == w.player || w.config.mode == Mode::Cheat) {
        if (ui.button("householdAutomatic", {x, y, width, 34},
                      t(r.automatic ? "autonomy.disable" : "autonomy.enable")))
          act([&] {
            r.automatic = !r.automatic;
            if (r.automatic)
              w.schedule(w.time + 1, EventKind::Decide, selected);
          });
        y += 42;
      }
      y = ui.wrap(x, y, width, t("autonomy.explain"), 12, hex(0x8da5aa)) + 20;
    }
    for (Id item : r.items)
      if (!w.items[item].consumed && w.items[item].carrier == selected &&
          (w.items[item].kind == ItemKind::Food || w.items[item].kind == ItemKind::Medicine)) {
        if (ui.button("consume" + std::to_string(item), {x, y, width, 34},
                      t(w.items[item].kind == ItemKind::Food ? "life.eat" : "life.medicine")))
          act([&] { w.consumeItem(selected, item); });
        y += 42;
        break;
      }
    if (r.queuedAt != none && w.facilities[r.queuedAt].kind == FacilityKind::Washbasin) {
      const auto &basin = w.facilities[r.queuedAt];
      auto service = std::find_if(basin.serving.begin(), basin.serving.end(),
                                  [&](const Service &s) { return s.resident == selected; });
      std::string state;
      if (service != basin.serving.end())
        state =
            service->until < 0
                ? t("wash.paused")
                : t("wash.remaining") + " " +
                      std::to_string(int(std::ceil(std::max(0., service->until - w.time) / 60))) +
                      " " + t("personal.minutes");
      else {
        auto place = std::find(basin.queue.begin(), basin.queue.end(), selected);
        state = t("wash.waiting") + " " + std::to_string(place - basin.queue.begin() + 1);
      }
      y = ui.wrap(x, y, width, state, 14, hex(0xa5cbb9)) + 16;
      if (ui.button("cancelWashing", {x, y, width, 34}, t("wash.cancel")))
        act([&] { w.leaveQueue(selected); });
      y += 44;
      y = ui.wrap(x, y, width, t("wash.info"), 12, hex(0x8da5aa)) + 20;
    }
    if (w.personalUses.contains(selected)) {
      const auto &use = w.personalUses.at(selected);
      ui.text(x, y,
              t(itemLabel(w.items[use.item].kind)) + " · " +
                  std::to_string(int(std::ceil(use.remaining / 60))) + " " + t("personal.minutes"),
              14, hex(0xa5cbb9));
      y += 30;
      if (ui.button("stopPersonalUse", {x, y, width, 34}, t("personal.stop")))
        w.stopPersonalUse(selected);
      y += 44;
    } else {
      for (Id item : r.items) {
        const auto &object = w.items[item];
        if (!object.consumed && object.carrier == selected &&
            (object.kind == ItemKind::Fan || object.kind == ItemKind::Tableware ||
             object.kind == ItemKind::RiceCooker)) {
          if (ui.button("personalItem" + std::to_string(item), {x, y, width, 34},
                        t("personal.use") + " · " + t(itemLabel(object.kind))))
            act([&] {
              w.usePersonalItem(selected, item);
              paused = false;
            });
          y += 42;
        }
      }
    }
    auto dirtyTools = w.washableItems(selected);
    if (!dirtyTools.empty()) {
      ui.text(x, y, t("wash.tools") + " " + std::to_string(dirtyTools.size()), 13, hex(0xa5bfc4));
      y += 26;
      for (Id item : dirtyTools) {
        y = ui.wrap(x, y, width,
                    t(itemLabel(w.items[item].kind)) + " · " + t("wash.cleanliness") + " " +
                        std::to_string(int(w.items[item].cleanliness * 100)) + "%",
                    12, hex(w.items[item].cleanliness < .25f ? 0xd6a66d : 0xa5bfc4)) +
            8;
      }
      if (ui.button("seekWashbasin", {x, y, width, 36}, t("wash.find")))
        act([&] {
          w.seekWashbasin(selected);
          paused = false;
        });
      y += 44;
      y = ui.wrap(x, y, width, t("wash.info"), 12, hex(0x8da5aa)) + 20;
    }
    Id nearby = none;
    for (const auto &other : w.residents)
      if (other.id != selected && other.alive && other.present &&
          w.near(selected, w.position(other.id), 1.5f)) {
        nearby = other.id;
        break;
      }
    if (nearby != none) {
      ui.text(x, y, w.displayName(nearby, locale.language), 14, hex(0xb2c1bd));
      y += 25;
      ui.text(x, y, t("talk.trust") + " " + std::to_string(int(w.trust(selected, nearby) * 100)),
              13, hex(0xa5bcae));
      y += 27;
      for (int topic = 0; topic < 6; ++topic) {
        if (ui.button("talk" + std::to_string(topic), {x, y, width, 30},
                      t("talk.topic" + std::to_string(topic))))
          act([&] { notify(t(w.talk(selected, nearby, topic))); });
        y += 38;
      }
      if (ui.button("punchNearby", {x, y, width / 2 - 5, 34}, t("combat.attack")))
        act([&] {
          w.attack(selected, nearby, false);
          paused = false;
        });
      if (ui.button("yieldNearby", {x + width / 2 + 5, y, width / 2 - 5, 34}, t("combat.yield")))
        act([&] { w.surrender(selected); });
      y += 42;
      if (ui.button("shoveNearby", {x, y, width, 32}, t("action.shove")))
        act([&] {
          w.shove(selected, nearby);
          paused = false;
        });
      y += 40;
      if (w.hasGun(selected)) {
        if (ui.button("shootNearby", {x, y, width, 32}, t("combat.shoot")))
          act([&] {
            w.shoot(selected, nearby);
            paused = false;
          });
        y += 40;
      }
      if (ui.button("lethalNearby", {x, y, width, 32},
                    t(lethalConfirm == nearby ? "combat.confirm_lethal" : "combat.lethal"))) {
        if (lethalConfirm == nearby)
          act([&] {
            w.attack(selected, nearby, true);
            lethalConfirm = none;
            paused = false;
          });
        else
          lethalConfirm = nearby;
      }
      y += 40;
      if (ui.button("stealNearby", {x, y, width, 32}, t("crime.attempt")))
        act([&] {
          Id item = none;
          for (Id candidate : w.residents[nearby].items)
            if (candidate != w.residents[nearby].outfit && w.items[candidate].carrier == nearby &&
                w.items[candidate].order == none) {
              item = candidate;
              break;
            }
          if (item == none)
            throw std::runtime_error("No loose item is within reach");
          w.stealItem(selected, nearby, item);
        });
      y += 42;
    }
    if (r.queuedAt != none && r.activity == Activity::Queuing) {
      if (ui.button("cut", {x, y, width, 36}, t("action.cut")))
        act([&] { w.cutQueue(selected, r.queuedAt); });
      y += 44;
    }
    if (ui.button("cancel", {x, y, width, 34}, t("action.cancel")))
      act([&] {
        if (r.lift != none || r.medicalDuty != none || r.ambulance != none || r.carriedBy != none ||
            r.activity == Activity::Jailed || r.activity == Activity::Escorted || r.surrendered)
          throw std::runtime_error("Cannot cancel custody or surrender");
        w.cancelLift(selected);
        w.clockOut(selected);
        w.leaveQueue(selected);
        r.pos = r.activity == Activity::Sleeping ? w.accessible(w.position(selected))
                                                 : w.position(selected);
        w.crowd.erase(selected);
        r.route.clear();
        ++r.routeVersion;
        r.activity = Activity::Idle;
        r.task = TaskKind::None;
      });
    y += 44;
    if (w.config.mode == Mode::Cheat) {
      if (ui.button("cash", {x, y, width, 34}, t("action.cheat_cash")))
        act([&] { w.pay(0, r.account, 1000000, "cheat-grant"); });
      y += 44;
      if (ui.button("cheatControls", {x, y, width, 34}, t("cheat.controls")))
        cheatOpen = !cheatOpen;
      y += 44;
      if (cheatOpen) {
        static const std::array<const char *, 10> stats = {
            "health",  "hunger",  "thirst",   "energy",     "happiness",
            "hygiene", "bladder", "strength", "aggression", "cash"};
        if (ui.button("cheatPrevious", {x, y, 32, 34}, "‹"))
          cheatStat = (cheatStat + 9) % 10;
        ui.text(x + 40, y + 8, t(std::string("cheat.") + stats[cheatStat]), 13, hex(0xa5cbb9));
        if (ui.button("cheatNext", {x + width - 32, y, 32, 34}, "›"))
          cheatStat = (cheatStat + 1) % 10;
        y += 42;
        ui.input("cheatValue", {x, y, width - 84, 36}, cheatValue, true);
        if (ui.button("cheatApply", {x + width - 78, y, 78, 36}, t("cheat.apply")))
          act([&] { w.cheatResident(selected, stats[cheatStat], std::stod(cheatValue)); });
        y += 44;
        if (ui.button("cheatAuto", {x, y, width, 34},
                      t(r.automatic ? "cheat.manual" : "cheat.autopilot")))
          r.automatic = !r.automatic;
        y += 44;
      }
      ui.input("inspectid", {x, y, width - 92, 36}, inspectId, true);
      if (ui.button("inspect", {x + width - 84, y, 84, 36}, "→"))
        act([&] {
          Id id = std::stoi(inspectId) - 1;
          (void)w.residents.at(id);
          bool possessed = firstPerson;
          if (possessed)
            setFirstPerson(false);
          selected = id;
          currentFloor = int(w.position(id).y / 3);
          cameraFocus = w.position(id);
          observerHeight = 0;
          body.position = w.position(id);
          if (possessed)
            setFirstPerson(true);
        });
      y += 44;
    }
    if (ui.button("exit", {x, y, width, 34},
                  t(exitConfirm ? "action.confirm_exit" : "action.exit"))) {
      if (exitConfirm)
        act([&] { w.depart(w.player); });
      else {
        exitConfirm = true;
        act([&] { w.travel(w.player, w.layout(0).entrance(), TaskKind::None, none); });
      }
    }
    return y;
  }
  void setFirstPerson(bool value) {
    if (firstPerson && !value && world) {
      cameraFocus = world->position(selected);
      currentFloor = std::clamp(int(cameraFocus.y / 3), 0, world->config.floors - 1);
      observerHeight = 0;
      distance = std::clamp(distance, 8.f, 80.f);
    }
    firstPerson = value;
    if (value)
      cameraResident = none;
    if (world && selected != world->player && world->config.mode == Mode::Cheat)
      world->residents[selected].automatic = !value;
    if (!value) {
      mouseLocked = false;
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else if (world) {
      speed = 1;
      body.position = world->position(selected);
      currentFloor = std::clamp(int(body.position.y / 3), 0, world->config.floors - 1);
    }
  }
  void openNetwork() {
    if (!world)
      return;
    if (world->hasDevice(selected) || world->residents[selected].vehicle != none) {
      if (!world->hasDevice(selected))
        networkTab = 4; // Local vehicle controls do not require a personal internet device.
      phone = true;
      mouseLocked = false;
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      ui.blur();
    } else
      notify(t("network.offline"));
  }
  Vec3 facilityApproach(const Facility &f) const {
    if (f.kind == FacilityKind::Elevator)
      return world->liftDoor(currentFloor);
    if (f.kind == FacilityKind::Ramp || f.kind == FacilityKind::FreightLift) {
      for (const auto &road : world->roads)
        if (road.facility == f.id) {
          int floor = currentFloor == road.to ? road.to : road.from;
          return {road.pos.x + road.width / 2 + 1.3f, floor * 3.f,
                  road.pos.z + (road.kind == RoadKind::Ramp ? (floor == road.to ? road.length : 0.f)
                                                            : -road.length / 2 - 1.f)};
        }
    }
    if (f.kind == FacilityKind::Dealership)
      return f.pos + Vec3{1.5f, 0, -2.f};
    return f.pos;
  }
  float facilityPanel(float x, float y, float width) {
    auto &w = *world;
    ui.text(x, y, t("facility.nearby"), 23, hex(0xe2e7df));
    y += 46;
    std::vector<Id> ordered;
    for (auto &f : w.facilities)
      if (std::abs(f.floor - currentFloor) <= 5 ||
          ((f.kind == FacilityKind::Elevator || f.kind == FacilityKind::Ramp ||
            f.kind == FacilityKind::FreightLift) &&
           std::find(f.stops.begin(), f.stops.end(), currentFloor) != f.stops.end()))
        ordered.push_back(f.id);
    std::stable_sort(ordered.begin(), ordered.end(), [&](Id a, Id b) {
      return std::abs(w.facilities[a].floor - currentFloor) <
             std::abs(w.facilities[b].floor - currentFloor);
    });
    for (Id fid : ordered)
      if (auto &f = w.facilities[fid]; true) {
        ui.fill({x, y, width, 135}, hex(0x1b2c32), 6);
        ui.text(x + 12, y + 12, t(facilityKey(f.kind)), 16, hex(0xe0e6de));
        ui.text(x + 12, y + 39,
                t("status.floor") + " " + std::to_string(f.floor + 1) + " · " +
                    std::to_string(f.queue.size()) + " " + t("facility.queue"),
                12, hex(0x8da5aa));
        ui.text(x + 12, y + 69, money(f.price), 15, hex(0xa5cbb9));
        if (ui.button("visit" + std::to_string(f.id), {x + width - 110, y + 62, 98, 30},
                      t("action.walk")))
          act([&] {
            if (f.kind == FacilityKind::Elevator || f.kind == FacilityKind::Dealership ||
                f.kind == FacilityKind::Ramp || f.kind == FacilityKind::FreightLift) {
              w.travel(w.player, facilityApproach(f), TaskKind::None, none);
            } else if (f.kind == FacilityKind::Terminal || f.kind == FacilityKind::Office ||
                       f.kind == FacilityKind::Shop || f.kind == FacilityKind::Jail ||
                       f.kind == FacilityKind::Workshop || f.kind == FacilityKind::Dealership ||
                       f.kind == FacilityKind::Parking || f.kind == FacilityKind::Ramp ||
                       f.kind == FacilityKind::FreightLift || f.kind == FacilityKind::Reception ||
                       f.kind == FacilityKind::Morgue || f.kind == FacilityKind::Crematorium ||
                       f.kind == FacilityKind::FireStation)
              w.travel(w.player, facilityApproach(f), TaskKind::Work, f.id);
            else
              w.travel(w.player, f.pos, TaskKind::Service, f.id);
            paused = false;
          });
        ui.text(x + 12, y + 106,
                t(w.availability(f.id)) + " · " +
                    (f.staffNeeded ? std::to_string(w.staffOnDuty(f.id)) + "/" +
                                         std::to_string(f.staffNeeded) + " " + t("facility.staff")
                                   : t("facility.self_service")),
                11, hex(w.operational(f.id) ? 0x9ec4a2 : 0xd6a66d));
        y += 147;
        if (f.kind == FacilityKind::Washbasin)
          y = ui.wrap(x + 8, y, width - 16, t("wash.info"), 12, hex(0x9eb6bc)) + 18;
        if (f.kind == FacilityKind::Elevator) {
          Id cabin = none;
          for (auto &l : w.lifts)
            if (l.facility == f.id)
              cabin = l.id;
          if (cabin != none) {
            ui.text(x, y,
                    t("lift.cabin") + " " + std::to_string(int(w.liftHeight(cabin) / 3) + 1) +
                        " · " + std::to_string(w.lifts[cabin].calls.size()) + " " +
                        t("facility.queue"),
                    13, hex(0xb4bfa8));
            y += 28;
            for (std::size_t n = 0; n < f.stops.size(); ++n) {
              int stop = f.stops[n];
              if (ui.button("liftStop" + std::to_string(f.id) + "_" + std::to_string(stop),
                            {x + float(n % 2) * (width / 2 + 4), y, width / 2 - 4, 30},
                            t("status.floor") + " " + std::to_string(stop + 1)))
                act([&] {
                  w.goByLift(selected, cabin, stop);
                  paused = false;
                });
              if (n % 2 == 1 || n + 1 == f.stops.size())
                y += 38;
            }
            if (ui.button("liftPlan" + std::to_string(f.id), {x, y, width, 32},
                          t("lift.subscribe") + " · " + money(f.subscriptionPrice)))
              act([&] { w.subscribeLift(selected, f.id); });
            y += 42;
          }
        }
      }
    return y;
  }
  float companyPanel(float x, float y, float width) {
    auto &w = *world;
    ui.text(x, y, t("company.title"), 23, hex(0xe1e7df));
    y += 37;
    if (ui.button("companyBack", {x, y, width, 30}, t("company.back"))) {
      companyScreen = false;
      scrollOffset = 0;
    }
    y += 40;
    y = ui.wrap(x, y, width, t("company.desc"), 13, hex(0x95aaad)) + 14;
    ui.text(x, y, t("company.name"), 12, hex(0x95aaad));
    y += 22;
    ui.input("companyName", {x, y, width, 33}, companyName);
    y += 41;
    if (ui.button("companyFound", {x, y, width, 33}, t("company.found") + " · " + money(30000)))
      act([&] {
        selectedCompany = w.foundCompany(
            w.player, companyName.empty() ? t("company.default_name") : companyName, 30000);
        nextCompanyStats = 0;
        companyPage = 0;
      });
    y += 47;
    for (const auto &c : w.companies)
      if (c.owner == w.player || c.leader == w.player ||
          c.account == w.residents[w.player].employer) {
        if (ui.button("companySelect" + std::to_string(c.id), {x, y, width, 31}, c.name,
                      selectedCompany == c.id)) {
          selectedCompany = c.id;
          companyPage = 0;
          nextCompanyStats = 0;
        }
        y += 39;
        if (selectedCompany == none)
          selectedCompany = c.id;
      }
    if (selectedCompany < 0 || std::size_t(selectedCompany) >= w.companies.size())
      return y;
    auto &c = w.companies[selectedCompany];
    bool owner = w.directsCompany(w.player, c.id, true), leads = w.directsCompany(w.player, c.id);
    if (glfwGetTime() >= nextCompanyStats) {
      reservedCompanyCash = w.companyReserve(c.id);
      nextCompanyStats = glfwGetTime() + 1;
    }
    y += 10;
    ui.text(x, y, c.name, 20, hex(0xa5cbb9));
    y += 32;
    for (const auto &[label, value] : std::vector<std::pair<std::string, std::string>>{
             {t("company.owner"), w.displayName(c.owner, locale.language)},
             {t("company.cash"), money(w.accounts[c.account].balance)},
             {t("company.reserve"), money(reservedCompanyCash)},
             {t("company.revenue"), money(c.revenue)},
             {t("company.leader"),
              c.leader == none ? t("company.vacant") : w.displayName(c.leader, locale.language)}})
      y = ui.wrap(x, y, width, label + " · " + value, 14, hex(0xb7c8c4)) + 10;
    if (owner) {
      if (ui.button("companyInvest", {x, y, width / 2 - 4, 32},
                    t("company.invest") + " " + money(10000)))
        act([&] { w.investCompany(w.player, c.id, 10000); });
      if (ui.button("companyDividend", {x + width / 2 + 4, y, width / 2 - 4, 32},
                    t("company.dividend") + " " + money(10000)))
        act([&] { w.companyDividend(w.player, c.id, 10000); });
      y += 44;
    }
    if (owner) {
      if (ui.button("companyBuild", {x, y, width, 32}, t("company.build"))) {
        companyConstruction = true;
        buildKind = int(FacilityKind::Office);
        tab = 4;
        scrollOffset = 0;
      }
      y += 44;
    }
    ui.text(x, y, t("company.workspaces"), 18, hex(0xa5cbb9));
    y += 31;
    for (const auto &f : w.facilities)
      if (!f.demolished && f.kind == FacilityKind::Office &&
          w.operatingAccount(f.id) == c.account) {
        bool leased = w.leasedWorkspaces.contains(f.id);
        y = ui.wrap(x, y, width,
                    t("status.floor") + " " + std::to_string(f.floor + 1) + " · " +
                        t(leased ? "company.leased" : "company.owned") + " · " +
                        std::to_string(f.employees.size()) + " " + t("company.staff"),
                    14, hex(0xc5d0c9)) +
            9;
        if (leased) {
          const auto &l = w.workspaceLeases[w.leasedWorkspaces.at(f.id)];
          y = ui.wrap(x, y, width,
                      t("company.rent") + " " + money(l.dailyRent) + " · " + t("company.arrears") +
                          " " + money(l.arrears),
                      13, hex(0xbdb09b)) +
              8;
          if (owner && ui.button("companyReturn" + std::to_string(f.id), {x, y, width, 31},
                                 t("company.return")))
            act([&] { w.endWorkspaceLease(w.player, l.id); });
          if (owner)
            y += 40;
        } else if (owner) {
          if (ui.button("companyOffer" + std::to_string(f.id), {x, y, width, 31},
                        t(w.workspaceOffers.contains(f.id) ? "company.unlist" : "company.offer")))
            act([&] {
              w.offerWorkspace(w.player, f.id,
                               w.workspaceOffers.contains(f.id) ? 0 : 2500 + f.stations * 500);
            });
          y += 40;
        }
        if (leads) {
          if (ui.button("companyRecruit" + std::to_string(f.id), {x, y, width, 31},
                        t("company.recruit")))
            act([&] {
              for (const auto &candidate : w.residents)
                if (candidate.automatic && candidate.alive && candidate.present &&
                    !candidate.leaving && candidate.role == Role::Unemployed &&
                    std::abs(candidate.home - f.floor) <= 8 && candidate.carrying == none) {
                  w.hireEmployee(w.player, f.id, candidate.id, Role::Programmer,
                                 6 + (f.employees.size() % 3) * 8);
                  return;
                }
              throw std::runtime_error(t("company.no_applicant"));
            });
          y += 40;
        }
        if (leads && ui.button("companyManage" + std::to_string(f.id), {x, y, width, 31},
                               t("company.manage"))) {
          currentFloor = f.floor;
          tab = 4;
          scrollOffset = 0;
        }
        if (leads)
          y += 40;
      }
    ui.text(x, y, t("company.team"), 18, hex(0xa5cbb9));
    y += 32;
    std::vector<Id> team;
    for (const auto &r : w.residents)
      if (r.alive && r.present && r.employer == c.account)
        team.push_back(r.id);
    companyPage =
        std::clamp(companyPage, 0, std::max(0, int(team.size() - (team.empty() ? 0 : 1)) / 8));
    for (int n = companyPage * 8; n < std::min(int(team.size()), companyPage * 8 + 8); ++n) {
      Id worker = team[n];
      const auto &r = w.residents[worker];
      y = ui.wrap(x, y, width, w.displayName(worker, locale.language) + " · " + t(roleKey(r.role)),
                  14, hex(0xd7e0dc)) +
          9;
      int count = c.completed.contains(worker) ? c.completed.at(worker) : 0;
      ui.text(x, y,
              t("company.completed") + " " + std::to_string(count) + " · " + money(r.hourlyWage) +
                  "/h",
              12, hex(0x98aaa6));
      y += 25;
      if (leads) {
        int brief = c.assignments.contains(worker) ? c.assignments.at(worker) : 0;
        if (ui.button("companyRole" + std::to_string(worker), {x, y, width / 2 - 4, 29},
                      t("company.change_role")))
          act([&] {
            w.assignCompanyWork(w.player, c.id, worker,
                                r.role == Role::Programmer    ? Role::VideoEditor
                                : r.role == Role::VideoEditor ? Role::Administrator
                                                              : Role::Programmer,
                                brief);
          });
        if (ui.button("companyBrief" + std::to_string(worker),
                      {x + width / 2 + 4, y, width / 2 - 4, 29},
                      t("company.brief") + " " + std::to_string(brief + 1)))
          act([&] { w.assignCompanyWork(w.player, c.id, worker, r.role, (brief + 1) % 3); });
        y += 37;
        if (owner && c.leader != worker) {
          if (ui.button("companyPromote" + std::to_string(worker), {x, y, width, 29},
                        t("company.promote")))
            act([&] { w.appointTeamLeader(w.player, c.id, worker); });
          y += 37;
        }
      }
      if (leads) {
        if (ui.button("companyDismiss" + std::to_string(worker), {x, y, width, 29},
                      t("company.dismiss")))
          act([&] { w.dismissEmployee(w.player, r.jobSite, worker); });
        y += 37;
      }
      y += 12;
    }
    if (team.size() > 8) {
      if (ui.button("teamPrevious", {x, y, width / 2 - 4, 29}, "←"))
        --companyPage;
      if (ui.button("teamNext", {x + width / 2 + 4, y, width / 2 - 4, 29}, "→"))
        ++companyPage;
      y += 42;
    }
    if (!owner)
      return y;
    ui.text(x, y, t("company.market"), 18, hex(0xa5cbb9));
    y += 32;
    y = ui.wrap(x, y, width, t("company.terms"), 13, hex(0x95aaad)) + 14;
    int shown = 0;
    for (const auto &f : w.facilities)
      if (!f.demolished && f.open && f.kind == FacilityKind::Office &&
          std::abs(f.floor - currentFloor) <= 16 && f.owner != c.account &&
          !w.leasedWorkspaces.contains(f.id) && ++shown <= 12) {
        ui.text(x, y,
                t("status.floor") + " " + std::to_string(f.floor + 1) + " · #" +
                    std::to_string(f.id),
                15, hex(0xc5d0c9));
        y += 28;
        if (w.workspaceOffers.contains(f.id)) {
          Money rent = w.workspaceOffers.at(f.id);
          if (ui.button("companyLease" + std::to_string(f.id), {x, y, width, 32},
                        t("company.lease") + " · " + money(rent) + t("company.per_day")))
            act([&] { w.leaseWorkspace(w.player, c.id, f.id); });
          y += 41;
        }
        if (f.askingPrice > 0 || f.owner == w.residents[w.player].account) {
          if (ui.button("companyBuy" + std::to_string(f.id), {x, y, width, 32},
                        t("company.acquire") + " · " +
                            money(f.owner == w.residents[w.player].account ? 0 : f.askingPrice)))
            act([&] { w.acquireWorkspace(w.player, c.id, f.id); });
          y += 41;
        }
      }
    return y;
  }
  float workPanel(float x, float y, float width) {
    if (companyScreen)
      return companyPanel(x, y, width);
    auto &w = *world;
    auto &r = w.residents[w.player];
    ui.text(x, y, t("work.title"), 24, hex(0xe1e7df));
    y += 42;
    y = ui.wrap(x, y, width, t("work.desc"), 14, hex(0x95aaad)) + 20;
    if (ui.button("openCompanies", {x, y, width, 36}, t("company.title"))) {
      companyScreen = true;
      selectedCompany = none;
      nextCompanyStats = 0;
      scrollOffset = 0;
    }
    y += 48;
    for (auto role :
         {Role::Courier, Role::Warden, Role::Builder, Role::VideoEditor, Role::Programmer,
          Role::Administrator, Role::Chef, Role::Medic, Role::Vendor, Role::Lifeguard,
          Role::Cleaner, Role::Pilot, Role::Repairer, Role::Firefighter, Role::TowDriver}) {
      auto kind = role == Role::Firefighter ? FacilityKind::FireStation
                  : role == Role::Cleaner   ? FacilityKind::Morgue
                  : role == Role::Pilot     ? FacilityKind::Infirmary
                  : role == Role::Chef      ? FacilityKind::Food
                  : role == Role::Medic     ? FacilityKind::Infirmary
                  : role == Role::Vendor    ? FacilityKind::Shop
                  : role == Role::Lifeguard ? FacilityKind::Pool
                  : role == Role::Courier   ? FacilityKind::Shop
                  : role == Role::Warden    ? FacilityKind::Jail
                  : (role == Role::Builder || role == Role::Repairer || role == Role::TowDriver)
                      ? FacilityKind::Workshop
                      : FacilityKind::Office;
      Id fid = w.nearest(w.player, kind);
      ui.text(x, y, t(roleKey(role)), 16, hex(0xd6dfdb));
      if (fid != none &&
          ui.button("job" + std::to_string(int(role)), {x + width - 105, y - 4, 105, 31},
                    t("action.accept"), false, r.role == role))
        act([&] { w.chooseJob(w.player, role, fid); });
      y += 47;
    }
    y += 10;
    ui.line(x, y, x + width, y, hex(0x34464b));
    y += 22;
    if (r.role == Role::VideoEditor || r.role == Role::Programmer ||
        r.role == Role::Administrator) {
      if (ui.button("videoOpen", {x, y, width, 38},
                    t(r.role == Role::VideoEditor ? "work.video" : "office.open"), true)) {
        networkTab = 3;
        openNetwork();
      }
      y += 48;
      if (ui.button("toOffice", {x, y, width, 36}, t("action.walk")))
        act([&] {
          w.travel(w.player, w.facilities[r.jobSite].pos, TaskKind::Work, r.jobSite);
          paused = false;
        });
    } else if (r.role == Role::Repairer) {
      for (auto &job : w.repairs)
        if (!job.complete && (job.worker == none || job.worker == w.player)) {
          y = ui.wrap(x, y, width,
                      t("repair.contract") + " · " +
                          t(facilityKey(w.facilities[job.facility].kind)) + " · " +
                          t("status.floor") + " " +
                          std::to_string(w.facilities[job.facility].floor + 1),
                      15, hex(0xa5cbb9)) +
              10;
          if (job.worker == none && ui.button("repairTake" + std::to_string(job.id),
                                              {x, y, width, 32}, t("action.accept")))
            act([&] {
              w.assignRepair(w.player, job.id);
              paused = false;
            });
          y += 40;
          if (job.worker == w.player) {
            ui.text(x, y,
                    t(job.parts ? "repair.onsite" : "repair.parts") + " · " +
                        std::to_string(int(100 * (1 - job.remaining / 3600))) + "%",
                    13, hex(0xa8c7b5));
            y += 28;
            Vec3 destination = !job.parts ? w.facilities[job.workshop].pos
                               : w.items[job.partItem].carrier == w.player
                                   ? w.facilities[job.facility].pos
                                   : w.items[job.partItem].pos;
            if (ui.button("repairWalk" + std::to_string(job.id), {x, y, width, 32},
                          t("action.walk")))
              act([&] {
                w.travel(w.player, destination, TaskKind::None, none);
                paused = false;
              });
            y += 40;
            if (w.near(w.player, destination, 3) &&
                ui.button("repairWork" + std::to_string(job.id), {x, y, width, 32},
                          t("build.start"))) {
              r.activity = Activity::Working;
              paused = false;
            }
            y += 40;
          }
        }
    } else if (r.role == Role::Courier) {
      ui.text(x, y, t("work.deliveries"), 18, hex(0xa5cbb9));
      y += 38;
      bool any = false;
      for (auto &o : w.orders)
        if (o.state == OrderState::Waiting || o.courier == w.player) {
          any = true;
          ui.text(x, y,
                  "#" + std::to_string(o.id + 1) + " · " + w.displayName(o.buyer, locale.language),
                  14, hex(0xdce3dd));
          y += 27;
          if (o.state == OrderState::Waiting &&
              ui.button("deliver" + std::to_string(o.id), {x, y, width, 34}, t("action.pickup")))
            act([&] {
              w.assignDelivery(o.id, w.player);
              paused = false;
            });
          else if (o.courier == w.player) {
            ui.text(x, y,
                    t(o.state == OrderState::ToPickup     ? "order.pickup"
                      : o.state == OrderState::ToCustomer ? "order.delivering"
                                                          : "order.delivered"),
                    14, hex(0xa5cbb9));
          }
          y += 48;
        }
      if (!any)
        ui.wrap(x, y, width, t("work.no_orders"), 14, hex(0x91a6a8));
      for (auto &s : w.freight)
        if (!s.waste && s.state != FreightState::Delivered &&
            (s.worker == none || s.worker == w.player)) {
          ui.text(x, y,
                  t("business.freight") + " #" + std::to_string(s.id + 1) + " · " + money(s.fee),
                  14, hex(0xddc18e));
          y += 27;
          if (s.readyAt > w.time) {
            ui.text(x, y,
                    t("business.inbound") + " " + std::to_string(int((s.readyAt - w.time) / 60)) +
                        " min",
                    13, hex(0xd6ba81));
            y += 30;
          }
          if (s.worker == none && s.readyAt <= w.time &&
              ui.button("freightTake" + std::to_string(s.id), {x, y, width, 32},
                        t("action.accept")))
            act([&] {
              w.assignFreight(w.player, s.id);
              paused = false;
            });
          if (s.worker == w.player)
            ui.text(x, y,
                    t(s.state == FreightState::Carrying ? "order.delivering" : "order.pickup"), 14,
                    hex(0x9fb6b4));
          y += 42;
        }
    } else if (r.role == Role::Cleaner) {
      for (auto &s : w.freight)
        if (s.waste && s.state != FreightState::Delivered &&
            (s.worker == none || s.worker == w.player)) {
          ui.text(x, y,
                  t("business.waste_job") + " #" + std::to_string(s.id + 1) + " · " + money(s.fee),
                  14, hex(0xb4c3a3));
          y += 28;
          if (s.worker == none &&
              ui.button("wasteTake" + std::to_string(s.id), {x, y, width, 32}, t("action.accept")))
            act([&] {
              w.assignFreight(w.player, s.id);
              paused = false;
            });
          if (s.worker == w.player)
            ui.text(x, y,
                    t(s.state == FreightState::Carrying ? "order.delivering" : "order.pickup"), 14,
                    hex(0x9fb6b4));
          y += 42;
        }
      for (Id id = 0; id < Id(w.remains.size()); ++id) {
        auto &b = w.remains[id];
        if (b.cremated || b.lost || w.airborneRemains.contains(id) || b.carrier != none ||
            b.ambulance != none || b.worker != none)
          continue;
        if (b.storage != none && w.facilities[b.storage].kind == FacilityKind::Crematorium)
          continue;
        ui.text(x, y, t("medical.body_task") + " · #" + std::to_string(b.resident + 1), 14,
                hex(0xc7c6b0));
        y += 28;
        if (ui.button("bodyJob" + std::to_string(id), {x, y, width, 34}, t("action.accept")))
          act([&] {
            w.transportBody(w.player, id);
            paused = false;
          });
        y += 44;
      }
    } else if (r.role == Role::Warden) {
      if (ui.button("respond", {x, y, width, 38}, t("action.respond"), true))
        act([&] {
          w.wardenRespond(w.player);
          paused = false;
        });
      y += 50;
      for (auto &c : w.incidents)
        if (!c.resolved) {
          ui.text(x, y,
                  "#" + std::to_string(c.id + 1) + " · " +
                      w.displayName(c.offender, locale.language),
                  14, hex(0xe3b485));
          y += 32;
        }
    }
    return y;
  }
  float housingPanel(float x, float y, float width) {
    auto &w = *world;
    ui.text(x, y, t("housing.title"), 23, hex(0xe2e7df));
    y += 42;
    y = ui.wrap(x, y, width, t("housing.desc"), 14, hex(0x98acaf)) + 22;
    int n = 0;
    for (std::size_t i = 0; i < w.housing.size(); ++i) {
      auto &h = w.housing[i];
      if ((h.floor != currentFloor && h.tenant != w.player) || !h.active)
        continue;
      ++n;
      ui.fill({x, y, width, 125}, hex(0x1b2c32), 6);
      ui.text(x + 12, y + 11,
              t("housing.bed") + " " + std::to_string(h.bed + 1) + " / " + t("status.floor") + " " +
                  std::to_string(h.floor + 1),
              16, hex(0xe1e6dc));
      ui.text(x + 12, y + 40, money(h.rent) + " / " + t("housing.rent"), 13, hex(0x94aaad));
      ui.text(x + 12, y + 65, money(h.price), 16, hex(0xa5cbb9));
      if (h.tenant == none &&
          ui.button("rent" + std::to_string(i), {x + 12, y + 91, 90, 25}, t("action.rent")))
        act([&] { w.rentHome(w.player, Id(i)); });
      if (h.listed && ui.button("buyhome" + std::to_string(i), {x + width - 100, y + 91, 88, 25},
                                t("action.buy")))
        act([&] { w.buyHome(w.player, Id(i)); });
      if (h.tenant == w.player)
        ui.text(x + 12, y + 94, t("housing.yours"), 12, hex(0x8eb9a3));
      if (h.owner == w.residents[w.player].account && !h.listed &&
          ui.button("listHome" + std::to_string(i), {x + width - 116, y + 91, 104, 25},
                    t("housing.list")))
        act([&] { w.listHome(w.player, Id(i), h.price); });
      y += 137;
      if ((h.owner == w.residents[w.player].account || w.player == w.dormOwner) &&
          ui.button("removeHome" + std::to_string(i), {x, y, width, 32}, t("housing.remove")))
        act([&] {
          w.proposeHomeRemoval(w.player, Id(i), h.rent * 7 + 30000);
          tab = 4;
          scrollOffset = 0;
        });
      y += 40;
      if (h.tenant == w.player && h.arrears > 0) {
        ui.text(x, y, t("housing.arrears") + " " + money(h.arrears), 14, hex(0xd9a176));
        y += 30;
      }
    }
    return y;
  }
  float structuresPanel(float x, float y, float width) {
    auto &w = *world;
    if (ui.button("structuralOpen", {x, y, width, 36}, t("structure.title"), structuralOpen))
      structuralOpen = !structuralOpen;
    y += 46;
    if (!structuralOpen)
      return y;
    y = ui.wrap(x, y, width, t("structure.help"), 13, hex(0x9fb4b7)) + 15;
    ui.text(x, y, t("structure.start"), 13, hex(0xb8c8c9));
    y += 24;
    ui.input("structureX", {x, y, width / 2 - 6, 32}, buildX, true);
    ui.input("structureZ", {x + width / 2 + 6, y, width / 2 - 6, 32}, buildZ, true);
    y += 42;
    ui.text(x, y, t("structure.length"), 13, hex(0xb8c8c9));
    y += 24;
    ui.input("wallLength", {x, y, 90, 32}, wallLength, true);
    if (ui.button("wallAxis", {x + 100, y, width - 100, 32}, wallAlongX ? "X →" : "Z →"))
      wallAlongX = !wallAlongX;
    y += 42;
    if (ui.button("wallDoor", {x, y, width, 32}, t(wallDoor ? "structure.door" : "structure.solid"),
                  wallDoor))
      wallDoor = !wallDoor;
    y += 42;
    if (ui.button("wallPropose", {x, y, width, 34}, t("structure.partition")))
      act([&] {
        Vec3 a{std::stof(buildX), currentFloor * 3.f, std::stof(buildZ)};
        Vec3 b = a + (wallAlongX ? Vec3{std::stof(wallLength), 0, 0}
                                 : Vec3{0, 0, std::stof(wallLength)});
        w.proposePartition(w.player, currentFloor, a, b, wallDoor);
      });
    y += 44;
    ui.text(x, y, t("traffic.build"), 16, hex(0xb9cfc4));
    y += 30;
    ui.text(x, y, t("traffic.end_floor"), 13, hex(0x9fb4b7));
    y += 24;
    ui.input("roadEndFloor", {x, y, width / 2 - 6, 32}, roadEndFloor, true);
    ui.input("roadLength", {x + width / 2 + 6, y, width / 2 - 6, 32}, roadLength, true);
    y += 42;
    for (auto kind : {RoadKind::Ramp, RoadKind::FreightLift}) {
      if (ui.button("roadBuild" + std::to_string(int(kind)), {x, y, width, 32},
                    t(kind == RoadKind::Ramp ? "facility.ramp" : "facility.freight_lift")))
        act([&] {
          w.proposeRoad(w.player, kind, currentFloor, std::stoi(roadEndFloor) - 1,
                        {std::stof(buildX), currentFloor * 3.f, std::stof(buildZ)},
                        std::stof(roadLength));
        });
      y += 40;
    }
    for (const auto &road : w.roads)
      if (road.active && (road.from == currentFloor || road.to == currentFloor)) {
        if (ui.button("roadRemove" + std::to_string(road.id), {x, y, width, 32},
                      t("traffic.remove") + " #" + std::to_string(road.id + 1)))
          act([&] { w.proposeRoadRemoval(w.player, road.id); });
        y += 40;
      }
    if (currentFloor > 0) {
      if (ui.button("floorChange", {x, y, width, 34},
                    t(w.floorPlans[currentFloor].deck ? "structure.remove_floor"
                                                      : "structure.restore_floor")))
        act([&] {
          w.proposeFloorChange(w.player, currentFloor, !w.floorPlans[currentFloor].deck);
        });
      y += 44;
      y = ui.wrap(x, y, width, t("structure.clearance_help"), 13, hex(0xa9b8bb)) + 12;
    }
    for (const auto &wall : w.interiorWalls)
      if (wall.active && wall.floor == currentFloor) {
        if (ui.button("removeWall" + std::to_string(wall.id), {x, y, width, 32},
                      t("structure.remove_wall") + " #" + std::to_string(wall.id + 1)))
          act([&] { w.proposePartitionRemoval(w.player, wall.id); });
        y += 40;
      }
    if (ui.button("becomeForeman", {x, y, width, 34}, t("structure.become_foreman")))
      act([&] {
        if (w.builderCommitted(w.player))
          throw std::runtime_error("structure.error.busy");
        Id shop = w.nearestAt(w.position(w.player), FacilityKind::Workshop);
        if (shop == none)
          throw std::runtime_error("structure.error.workshop");
        w.chooseJob(w.player, Role::Builder, shop);
        selectedTeam = w.createConstructionTeam(w.player, shop, w.player);
      });
    y += 46;
    for (auto &team : w.constructionTeams) {
      if (team.members.empty())
        continue;
      if (ui.button("crew" + std::to_string(team.id), {x, y, width, 32},
                    t("structure.team") + " #" + std::to_string(team.id + 1) + " · " +
                        std::to_string(team.members.size()),
                    selectedTeam == team.id))
        selectedTeam = team.id;
      y += 40;
      if (selectedTeam != team.id)
        continue;
      y = ui.wrap(x, y, width,
                  t("structure.foreman") + " · " + w.displayName(team.foreman, locale.language), 14,
                  hex(0xb9cfc4)) +
          12;
      if (ui.button("recruitCrew", {x, y, width, 32}, t("structure.recruit")))
        act([&] {
          for (Id candidate : w.facilities[team.workshop].employees)
            if (candidate != team.foreman && w.residents[candidate].automatic &&
                team.members.size() < 8)
              try {
                w.recruitConstructionWorker(w.player, team.id, candidate);
              } catch (const std::runtime_error &) {
              }
        });
      y += 40;
      if (ui.button("joinCrew", {x, y, width, 32}, t("structure.join")))
        act([&] {
          if (w.builderCommitted(w.player))
            throw std::runtime_error("structure.error.busy");
          w.chooseJob(w.player, Role::Builder, team.workshop);
          w.recruitConstructionWorker(w.player, team.id, w.player);
        });
      y += 40;
      if (selected != team.foreman && team.members.contains(selected)) {
        if (ui.button("appointCrew", {x, y, width, 32}, t("structure.appoint")))
          act([&] { w.appointForeman(w.player, team.id, selected); });
        y += 40;
      }
      if (selected != w.player && team.members.contains(selected)) {
        if (ui.button("dismissCrew", {x, y, width, 32}, t("structure.dismiss")))
          act([&] { w.removeConstructionWorker(w.player, team.id, selected); });
        y += 40;
      }
      for (Id worker : team.members)
        y = ui.wrap(x, y, width, w.displayName(worker, locale.language), 13, hex(0xa1b7bb)) + 8;
      if (team.members.contains(w.player)) {
        if (ui.button("leaveCrew", {x, y, width, 32}, t("structure.leave")))
          act([&] { w.removeConstructionWorker(w.player, team.id, w.player); });
        y += 40;
      }
    }
    for (auto &job : w.structuralWorks) {
      if (job.state == StructuralState::Completed || job.state == StructuralState::Cancelled ||
          (job.floor != currentFloor && job.actor != w.player &&
           (job.team == none || !w.constructionTeams[job.team].members.contains(w.player))))
        continue;
      const char *kind = job.kind == StructuralKind::Transport         ? "traffic.build"
                         : job.kind == StructuralKind::RemoveTransport ? "traffic.remove"
                         : job.kind == StructuralKind::Partition       ? "structure.partition"
                         : job.kind == StructuralKind::RemovePartition ? "structure.remove_wall"
                         : job.kind == StructuralKind::RemoveFloor     ? "structure.remove_floor"
                                                                       : "structure.restore_floor";
      y = ui.wrap(x, y, width, t(kind) + " #" + std::to_string(job.id + 1), 16, hex(0xe0bc76)) + 10;
      ui.text(x, y,
              money(job.budget) + " · " +
                  std::to_string(int(100 * (1 - job.remaining / job.total))) + "%",
              13, hex(0xa5cbb9));
      y += 28;
      if (!job.blocked.empty())
        y = ui.wrap(x, y, width, t(job.blocked), 13, hex(0xd9a081)) + 10;
      if (job.state == StructuralState::Proposed && job.actor == w.player) {
        if (job.kind == StructuralKind::RemoveFloor) {
          if (ui.button("prepareClearance" + std::to_string(job.id), {x, y, width, 32},
                        t("structure.prepare_clearance")))
            act([&] { w.planFloorClearance(w.player, job.id); });
          y += 40;
        }
        if (ui.button("fundStructure" + std::to_string(job.id), {x, y, width, 32}, t("build.fund")))
          act([&] { w.fundStructuralWork(w.player, job.id); });
        y += 40;
      }
      if (job.state != StructuralState::Proposed && job.team == none && selectedTeam != none) {
        if (ui.button("assignStructure" + std::to_string(job.id), {x, y, width, 32},
                      t("structure.hire")))
          act([&] { w.assignStructuralTeam(w.player, job.id, selectedTeam); });
        y += 40;
      }
      if (job.team != none && w.constructionTeams[job.team].members.contains(w.player)) {
        y = ui.wrap(x, y, width, t(job.supplied ? "build.materials_ready" : "build.materials_wait"),
                    13, hex(0x9fb4b7)) +
            12;
        if (ui.button("walkStructure" + std::to_string(job.id), {x, y, width, 32},
                      t("action.walk")))
          act([&] {
            w.travel(w.player, w.structuralDestination(w.player, job.id), TaskKind::None, none);
            paused = false;
          });
        y += 40;
        if (ui.button("startStructure" + std::to_string(job.id), {x, y, width, 32},
                      t("build.start")))
          act([&] {
            w.beginStructuralWork(w.player, job.id);
            paused = false;
          });
        y += 40;
      }
      if (job.actor == w.player) {
        if (ui.button("cancelStructure" + std::to_string(job.id), {x, y, width, 32},
                      t("structure.cancel")))
          act([&] { w.cancelStructuralWork(w.player, job.id); });
        y += 44;
      }
    }
    for (const auto &job : w.structuralWorks)
      if (job.state == StructuralState::Cancelled && job.material != none &&
          !w.items[job.material].consumed && w.items[job.material].reservedAt == none &&
          (job.actor == w.player || w.residents[w.player].jobSite == job.workshop)) {
        y = ui.wrap(x, y, width, t("structure.salvage") + " #" + std::to_string(job.id + 1), 14,
                    hex(0xd4b975)) +
            12;
        const auto &item = w.items[job.material];
        Vec3 goal =
            item.carrier == w.player ? w.facilities[job.workshop].pos + Vec3{1.6f, 0, 0} : item.pos;
        if (ui.button("salvageWalk" + std::to_string(job.id), {x, y, width, 32}, t("action.walk")))
          act([&] {
            w.travel(w.player, goal, TaskKind::None, none);
            paused = false;
          });
        y += 40;
        if (ui.button("salvageAction" + std::to_string(job.id), {x, y, width, 32},
                      t(item.carrier == w.player ? "structure.return_materials"
                                                 : "structure.collect_materials")))
          act([&] {
            if (item.carrier == w.player)
              w.returnStructuralSalvage(w.player, job.id);
            else
              w.collectStructuralSalvage(w.player, job.id);
          });
        y += 40;
      }
    return y + 12;
  }
  float managementPanel(float x, float y, float width) {
    auto &w = *world;
    ui.text(x, y, t("manage.title"), 22, hex(0xe4e8e0));
    y += 45;
    if (w.config.layoutVersion > 1) {
      y = structuresPanel(x, y, width);
      ui.text(x, y, t("build.new_facility"), 18, hex(0xa5cbb9));
      y += 32;
      if (ui.button("buildKindPrev", {x, y, 32, 34}, "←"))
        buildKind = (buildKind + int(FacilityKind::Washbasin)) % (int(FacilityKind::Washbasin) + 1);
      ui.text(x + 42, y + 9, t(facilityKey(FacilityKind(buildKind))), 13, hex(0xd3dfda));
      if (ui.button("buildKindNext", {x + width - 32, y, 32, 34}, "→"))
        buildKind = (buildKind + 1) % (int(FacilityKind::Washbasin) + 1);
      y += 48;
      ui.text(x, y, "X / Z (m)", 13, hex(0x9fb4b7));
      y += 22;
      ui.input("buildX", {x, y, width / 2 - 6, 34}, buildX, true);
      ui.input("buildZ", {x + width / 2 + 6, y, width / 2 - 6, 34}, buildZ, true);
      y += 46;
      ui.text(x, y, t("build.stations"), 13, hex(0x9fb4b7));
      y += 24;
      ui.input("buildStations", {x, y, 70, 34}, buildStations, true);
      ui.text(x + 82, y + 9, t("build.price"), 12, hex(0xa5cbb9));
      y += 46;
      if (buildKind == int(FacilityKind::Office) &&
          w.directsCompany(w.player, selectedCompany, true)) {
        if (ui.button("companyConstruction", {x, y, width, 32},
                      t("company.fund_build") + " · " + w.companies[selectedCompany].name,
                      companyConstruction))
          companyConstruction = !companyConstruction;
        y += 42;
      }
      if (ui.button("buildCommission", {x, y, width, 36}, t("build.commission"), true))
        act([&] {
          w.commissionFacility(w.player, FacilityKind(buildKind), currentFloor,
                               {std::stof(buildX), currentFloor * 3.f, std::stof(buildZ)},
                               std::stoi(buildStations),
                               companyConstruction && buildKind == int(FacilityKind::Office) &&
                                       w.directsCompany(w.player, selectedCompany, true)
                                   ? selectedCompany
                                   : none);
          notify(t("build.facility_commissioned"));
        });
      y += 46;
      if (ui.button("buildBed", {x, y, width, 34}, t("housing.build_bed")))
        act([&] {
          w.commissionBed(w.player, currentFloor,
                          {std::stof(buildX), currentFloor * 3.f, std::stof(buildZ)});
        });
      y += 44;
      if (w.player == w.dormOwner || w.config.mode == Mode::Cheat) {
        auto l = w.layout(currentFloor);
        if (ui.button("buildFloor", {x, y, width, 34}, t("housing.add_floor")))
          act([&] { w.commissionFloor(w.player, int(l.width), int(l.depth)); });
        y += 42;
        y = ui.wrap(x, y, width,
                    money(Money(l.width) * Money(l.depth) * 3500) + " · " +
                        std::to_string(int(l.width)) + " × " + std::to_string(int(l.depth)) + " m",
                    13, hex(0x9fb6b6)) +
            18;
      }
    }
    for (auto &job : w.housingWorks)
      if (job.state != ProjectState::Completed &&
          (job.floor == currentFloor || job.actor == w.player || job.worker == w.player)) {
        y = ui.wrap(x, y, width,
                    t(job.kind == HousingWorkKind::AddFloor    ? "housing.add_floor"
                      : job.kind == HousingWorkKind::RemoveBed ? "housing.remove"
                                                               : "housing.build_bed") +
                        " #" + std::to_string(job.id + 1),
                    15, hex(0xe0bc76)) +
            10;
        if (job.state == ProjectState::AwaitingConsent) {
          y = ui.wrap(x, y, width, t("housing.relocation_offer") + " " + money(job.offer), 13,
                      hex(0xaabec0)) +
              10;
          const auto &home = w.housing[job.home];
          if ((home.tenant == w.player || home.owner == w.residents[w.player].account) &&
              !job.consented.contains(w.player)) {
            if (ui.button("agreeRelocation" + std::to_string(job.id), {x, y, width, 32},
                          t("build.consent_yes")))
              act([&] { w.consentHomeRemoval(w.player, job.id); });
            y += 40;
          }
          if (home.tenant == w.player || home.owner == w.residents[w.player].account) {
            if (ui.button("refuseRelocation" + std::to_string(job.id), {x, y, width, 32},
                          t("structure.refuse")))
              act([&] { w.replyHomeRemoval(w.player, job.id, false); });
            y += 40;
            ui.input("counterRelocation" + std::to_string(job.id), {x, y, width, 32},
                     relocationCounter, true);
            y += 40;
            if (ui.button("sendCounter" + std::to_string(job.id), {x, y, width, 32},
                          t("structure.counter")))
              act([&] { w.counterHomeRemoval(w.player, job.id, std::stoll(relocationCounter)); });
            y += 40;
          }
          for (auto [who, demand] : job.demands)
            y = ui.wrap(x, y, width,
                        w.displayName(who, locale.language) + " · " + t("structure.counter") + " " +
                            money(demand),
                        13, hex(0xe0bc76)) +
                10;
          if (job.actor == w.player) {
            auto [input, fresh] = relocationInputs.try_emplace(job.id);
            if (fresh)
              input->second = {std::to_string(job.offer),
                               std::to_string(job.ownerOffer < 0 ? home.price : job.ownerOffer),
                               std::to_string(job.replacement + 1)};
            const char *labels[] = {"housing.relocation_offer", "structure.buyout",
                                    "structure.replacement"};
            for (int field = 0; field < 3; ++field) {
              ui.text(x, y, t(labels[field]), 13, hex(0xaabec0));
              y += 24;
              ui.input("relocationField" + std::to_string(job.id) + "-" + std::to_string(field),
                       {x, y, width, 32}, input->second[field], true);
              y += 40;
            }
            if (ui.button("reviseRelocation" + std::to_string(job.id), {x, y, width, 32},
                          t("structure.revise")))
              act([&] {
                w.reviseHomeRemoval(w.player, job.id, std::stoll(input->second[0]),
                                    std::stoll(input->second[1]), std::stoi(input->second[2]) - 1);
              });
            y += 40;
            if (ui.button("cancelRelocation" + std::to_string(job.id), {x, y, width, 32},
                          t("structure.withdraw")))
              act([&] { w.cancelHomeRemoval(w.player, job.id); });
            y += 40;
            if (ui.button("fundRemoval" + std::to_string(job.id), {x, y, width, 32},
                          t("build.fund")))
              act([&] { w.fundHomeRemoval(w.player, job.id); });
            y += 40;
            if (home.tenant != none && w.residents[home.tenant].surrendered &&
                !job.consented.contains(home.tenant)) {
              y = ui.wrap(x, y, width, t("housing.force_warning"), 13, hex(0xd9a081)) + 10;
              if (ui.button("forceRemoval" + std::to_string(job.id), {x, y, width, 32},
                            t("housing.force")))
                act([&] { w.fundHomeRemoval(w.player, job.id, true); });
              y += 40;
            }
          }
        } else {
          ui.text(x, y,
                  std::to_string(int(100 * (1 - job.remaining / job.total))) + "% · " +
                      t(job.supplied ? "build.materials_ready" : "build.materials_wait"),
                  13, hex(0x9cbdab));
          y += 28;
          if (job.worker == none && w.residents[w.player].role == Role::Builder &&
              ui.button("housingWork" + std::to_string(job.id), {x, y, width, 32},
                        t("action.accept")))
            act([&] {
              w.assignHousingWork(w.player, job.id);
              paused = false;
            });
          y += 40;
          if (job.worker == w.player) {
            if (ui.button("housingWalk" + std::to_string(job.id), {x, y, width, 32},
                          t("action.walk")))
              act([&] {
                w.travel(w.player, w.housingWorkDestination(job.id), TaskKind::None, none);
                paused = false;
              });
            y += 40;
            if (w.near(w.player, w.housingWorkDestination(job.id), 2) &&
                ui.button("housingStart" + std::to_string(job.id), {x, y, width, 32},
                          t("build.start"))) {
              w.residents[w.player].activity = Activity::Working;
              paused = false;
            }
            y += 40;
          }
        }
      }
    ui.text(x, y, t("ownership.title"), 19, hex(0xa5cbb9));
    y += 30;
    y = ui.wrap(x, y, width, w.displayName(w.dormOwner, locale.language), 16, hex(0xd8e4df)) + 12;
    if (w.player == w.dormOwner) {
      if (ui.button("listDorm", {x, y, width, 34},
                    t(w.governance.askingPrice > 0 ? "ownership.withdraw" : "ownership.list")))
        act([&] {
          w.listDorm(w.player, w.governance.askingPrice > 0 ? 0 : w.dormValuation() * 2);
        });
      y += 42;
      if (selected != w.player &&
          ui.button("nominateDorm", {x, y, width, 34}, t("ownership.nominate")))
        act([&] { w.nominateSuccessor(w.player, selected); });
      y += 42;
    } else if (w.governance.askingPrice > 0) {
      y = ui.wrap(x, y, width, t("ownership.price") + " " + money(w.governance.askingPrice), 14,
                  hex(0xc6bc91)) +
          8;
      if (ui.button("buyDorm", {x, y, width, 34}, t("ownership.buy")))
        act([&] { w.buyDorm(w.player); });
      y += 42;
    } else {
      y = ui.wrap(x, y, width, t("ownership.unlisted"), 14, hex(0x98aeb1)) + 12;
    }
    if (w.governance.successor == w.player &&
        ui.button("succeedDorm", {x, y, width, 34}, t("ownership.accept")))
      act([&] { w.acceptSuccession(w.player); });
    y += 42;
    if (w.player != w.dormOwner &&
        (!w.residents[w.dormOwner].alive || w.residents[w.dormOwner].surrendered)) {
      y = ui.wrap(x, y, width, t("ownership.force_warning"), 13, hex(0xdca081)) + 10;
      if (ui.button("seizeDorm", {x, y, width, 34}, t("ownership.seize")))
        act([&] { w.seizeDorm(w.player); });
      y += 46;
    }
    ui.text(x, y, t("manage.treasury"), 13, hex(0x91a7ab));
    ui.text(x, y + 28, money(w.accounts[w.dormAccount].balance), 29, hex(0xa5cbb9));
    y += 76;
    if (w.config.layoutVersion > 1) {
      auto l = w.layout(currentFloor);
      int fw = int(l.halfWidth() * 2), fd = int(l.halfDepth() * 2);
      ui.text(x, y, std::to_string(fw) + " × " + std::to_string(fd) + " m", 19, hex(0xa5cbb9));
      y += 33;
      if (w.player == w.dormOwner || w.config.mode == Mode::Cheat) {
        if (fw + 16 <= 2048 && ui.button("expandWidth", {x, y, width, 38},
                                         t("build.expand_width") + " · " +
                                             money(w.expansionQuote(currentFloor, fw + 16, fd))))
          act([&] {
            w.commissionExpansion(w.player, currentFloor, fw + 16, fd);
            notify(t("build.commissioned"));
          });
        y += 46;
        if (fd + 16 <= 2048 && ui.button("expandDepth", {x, y, width, 38},
                                         t("build.expand_depth") + " · " +
                                             money(w.expansionQuote(currentFloor, fw, fd + 16))))
          act([&] {
            w.commissionExpansion(w.player, currentFloor, fw, fd + 16);
            notify(t("build.commissioned"));
          });
        y += 50;
      }
      for (auto &p : w.expansions)
        if (p.floor == currentFloor) {
          ui.text(x, y,
                  t(p.complete ? "build.complete" : "build.progress") + " " +
                      std::to_string(int(100 * (1 - p.remaining / p.totalWork))) + "%",
                  15, hex(0xa8c7ac));
          y += 30;
          if (!p.complete && p.worker == none && w.residents[w.player].role == Role::Builder &&
              ui.button("buildTake" + std::to_string(p.id), {x, y, width, 32}, t("build.work")))
            act([&] {
              w.assignExpansion(w.player, p.id);
              paused = false;
            });
          if (!p.complete && p.worker == w.player) {
            if (w.near(w.player, p.site, 2) && ui.button("buildStart" + std::to_string(p.id),
                                                         {x, y, width, 32}, t("build.start"))) {
              act([&] { w.beginExpansionWork(w.player, p.id); });
              paused = false;
            }
          }
          y += 42;
        }
    }
    if (w.player == w.dormOwner && ui.button("dividend", {x, y, width, 36}, t("action.dividend")))
      act([&] { w.pay(w.dormAccount, w.residents[w.player].account, 100000, "owner-dividend"); });
    y += 50;
    y = ui.wrap(x, y, width, t("manage.consent"), 14, hex(0x91a7ab)) + 20;
    int n = 0;
    for (auto &f : w.facilities)
      if (!f.demolished && f.floor == currentFloor) {
        ui.text(x, y, t(facilityKey(f.kind)), 17, hex(0xdce4de));
        y += 30;
        if (f.askingPrice > 0 && ui.button("buyfac" + std::to_string(f.id), {x, y, width, 34},
                                           t("action.buy") + " · " + money(f.askingPrice)))
          act([&] { w.buyFacility(w.player, f.id); });
        y += 43;
        if (ui.button("propose" + std::to_string(f.id), {x, y, width, 34}, t("manage.propose")))
          act([&] { w.proposeProject(w.player, f.id, false, 100000); });
        y += 43;
        if (ui.button("demolish" + std::to_string(f.id), {x, y, width, 32}, t("build.demolish")))
          act([&] { w.proposeProject(w.player, f.id, true, 150000); });
        y += 40;
        if (f.manager == w.player || f.owner == w.residents[w.player].account ||
            (w.player == w.dormOwner && f.owner == w.dormAccount) ||
            w.directsCompany(w.player, w.companyForAccount(w.operatingAccount(f.id)))) {
          ui.text(x, y, t("repair.condition") + " " + std::to_string(int(f.condition * 100)) + "%",
                  13, hex(0xa9c2b5));
          y += 27;
          if (f.condition < .99f && ui.button("repairOrder" + std::to_string(f.id),
                                              {x, y, width, 30}, t("repair.commission")))
            act([&] { w.requestRepair(w.player, f.id); });
          y += 38;
          ui.text(x, y,
                  t("business.stock") + " " + std::to_string(f.stock) + " · " +
                      money(w.accounts[w.operatingAccount(f.id)].balance),
                  14, hex(0xa9c2b5));
          y += 30;
          ui.text(x, y, t("business.waste") + " " + std::to_string(f.waste), 13, hex(0xb9bf94));
          y += 27;
          if (f.waste > 0 && ui.button("wasteNow" + std::to_string(f.id), {x, y, width, 30},
                                       t("business.collect_waste")))
            act([&] { w.requestWaste(w.player, f.id); });
          y += 38;
          ui.text(x, y,
                  t("business.price") + " " + money(f.price) + " / " + t("business.wage") + " " +
                      money(f.wageOffer),
                  12, hex(0xc9b78e));
          y += 27;
          if (ui.button("priceDown" + std::to_string(f.id), {x, y, width / 2 - 4, 30},
                        t("business.cheaper")))
            act([&] {
              w.setBusiness(w.player, f.id, std::max(Money(0), f.price - f.price / 10 - 1),
                            f.wageOffer, f.hiring, f.autoRestock);
            });
          if (ui.button("priceUp" + std::to_string(f.id), {x + width / 2 + 4, y, width / 2 - 4, 30},
                        t("business.dearer")))
            act([&] {
              w.setBusiness(w.player, f.id, f.price + f.price / 10 + 1, f.wageOffer, f.hiring,
                            f.autoRestock);
            });
          y += 38;
          if (ui.button("hireToggle" + std::to_string(f.id), {x, y, width, 30},
                        t(f.hiring ? "business.hiring_on" : "business.hiring_off")))
            act([&] {
              w.setBusiness(w.player, f.id, f.price, f.wageOffer, !f.hiring, f.autoRestock);
            });
          y += 38;
          if (ui.button("restockToggle" + std::to_string(f.id), {x, y, width, 30},
                        t(f.autoRestock ? "business.stock_on" : "business.stock_off")))
            act([&] {
              w.setBusiness(w.player, f.id, f.price, f.wageOffer, f.hiring, !f.autoRestock);
            });
          y += 38;
          if (ui.button("stockNow" + std::to_string(f.id), {x, y, width, 30},
                        t("business.order_stock")))
            act([&] { w.requestStock(w.player, f.id, 1000); });
          y += 38;
          ui.text(x, y, hoursLabel(f.opensAt, f.openHours), 13, hex(0xadc1bc));
          y += 26;
          if (ui.button("hours" + std::to_string(f.id), {x, y, width, 30},
                        t(f.openHours == 24 ? "facility.daytime" : "facility.all_day")))
            act([&] {
              w.setFacilityHours(w.player, f.id, f.openHours == 24 ? 8 : 0,
                                 f.openHours == 24 ? 12 : 24, f.publicAccess);
            });
          y += 38;
          if (ui.button("access" + std::to_string(f.id), {x, y, width, 30},
                        t(f.publicAccess ? "facility.make_private" : "facility.make_public")))
            act([&] {
              w.setFacilityHours(w.player, f.id, f.opensAt, f.openHours, !f.publicAccess);
            });
          y += 38;
        }
        y += 12;
      }
    for (auto &p : w.projects) {
      ui.text(x, y,
              "#" + std::to_string(p.id + 1) + " · " +
                  t(p.state == ProjectState::Completed ? "manage.completed"
                    : p.state == ProjectState::Working ? "manage.working"
                    : p.state == ProjectState::Funded  ? "manage.funded"
                                                       : "manage.pending"),
              14, hex(0xa5cbb9));
      y += 30;
      if (p.newConstruction) {
        ui.text(x, y, t(p.materialsDelivered ? "build.materials_ready" : "build.materials_wait"),
                13, hex(0xb1c9bd));
        y += 27;
      }
      if (p.state == ProjectState::AwaitingConsent) {
        ui.text(x, y,
                t("build.consent") + " " + std::to_string(p.consented.size()) + " / " +
                    std::to_string(p.affected.size()),
                13, hex(0x9fb6ba));
        y += 28;
        if (p.affected.contains(w.player) && !p.consented.contains(w.player) &&
            ui.button("consent" + std::to_string(p.id), {x, y, width, 32}, t("build.consent_yes")))
          act([&] { w.consentProject(p.id, w.player); });
        y += 38;
        if (p.owner == w.player && ui.button("offer" + std::to_string(p.id), {x, y, width, 32},
                                             t("build.offer") + " · " + money(p.offer * 2)))
          act([&] { w.offerCompensation(w.player, p.id, p.offer * 2); });
        y += 40;
        if (p.owner == w.player &&
            ui.button("fund" + std::to_string(p.id), {x, y, width, 32}, t("build.fund")))
          act([&] { w.fundProject(p.id, w.player); });
        y += 40;
      }
      if (p.state == ProjectState::Funded && w.residents[w.player].role == Role::Builder &&
          ui.button("takeProject" + std::to_string(p.id), {x, y, width, 32}, t("build.work")))
        act([&] {
          w.assignBuilder(p.id, w.player);
          paused = false;
        });
      if (p.state == ProjectState::Working && p.worker == w.player &&
          w.near(w.player, w.facilities[p.facility].pos) &&
          ui.button("startProject" + std::to_string(p.id), {x, y, width, 32}, t("build.start")))
        act([&] {
          w.beginProjectWork(w.player, p.id);
          paused = false;
        });
      y += 44;
    }
    return y;
  }
  void sidebar() {
    float width = 440, x = ui.width - width - 20 + (1 - drawerSlide) * 480, y = 86;
    ui.fill({x, y, width, float(ui.height) - y - 84}, hex(0x11151b, .98f), 10);
    ui.text(x + 22, y + 18, t("hud.menu"), 20, hex(0xf0f2f4));
    if (ui.button("drawerClose", {x + width - 54, y + 12, 34, 32}, "×"))
      drawerOpen = false;
    const char *keys[] = {"nav.life",   "nav.facilities", "nav.work",    "nav.housing",
                          "nav.manage", "hud.journal",    "hud.settings"};
    for (int i = 0; i < 7; ++i) {
      float bx = x + 16 + (i % 4) * 102, by = y + 57 + (i / 4) * 37;
      if (ui.button("drawerTab" + std::to_string(i), {bx, by, 98, 32}, t(keys[i]), false,
                    tab == i)) {
        tab = i;
        scrollOffset = 0;
        ui.blur();
      }
    }
    Rect content{x + 22, y + 145, width - 44, float(ui.height) - y - 247};
    if (ui.hit(content) && ui.scroll != 0)
      scrollOffset = std::max(0.f, scrollOffset - float(ui.scroll) * 52);
    ui.clip(content);
    float start = content.y - scrollOffset, bottom = start;
    if (tab == 0)
      bottom = lifePanel(content.x, start, content.w);
    if (tab == 1)
      bottom = facilityPanel(content.x, start, content.w);
    if (tab == 2)
      bottom = workPanel(content.x, start, content.w);
    if (tab == 3)
      bottom = housingPanel(content.x, start, content.w);
    if (tab == 4) {
      auto &w = *world;
      bottom =
          ui.wrap(content.x, bottom, content.w,
                  t("hud.population") + "  " + std::to_string(cachedLiving) + "   ·   " +
                      t("hud.happiness") + "  " + std::to_string(int(cachedHappiness * 100)) + "%",
                  15, hex(0x8db9a8)) +
          14;
      bottom = ui.wrap(content.x, bottom, content.w,
                       timeLabel(w.time) + " · " + t("status.waiting") + " " + number(cachedQueued),
                       14, hex(0xa0abb5)) +
               18;
      bottom = managementPanel(content.x, bottom, content.w);
    }
    if (tab == 5) {
      for (auto it = world->news.rbegin(); it != world->news.rend(); ++it) {
        std::string label = t(it->key);
        if (it->subject >= 0 && std::size_t(it->subject) < world->names.size())
          label += " · " + world->displayName(it->subject, locale.language);
        bottom = ui.wrap(content.x, bottom, content.w, label, 15, hex(0xc2c8cf)) + 18;
      }
    }
    if (tab == 6) {
      bottom =
          ui.wrap(content.x, bottom, content.w, t("hud.settings_help"), 16, hex(0xc2c8cf)) + 24;
      for (int i = 0; i < 3; ++i) {
        const char *lang = i == 0 ? "en" : i == 1 ? "fr" : "zh";
        if (ui.button("settingsLang" + std::to_string(i), {content.x + i * 130, bottom, 120, 36},
                      i == 0   ? "English"
                      : i == 1 ? "Français"
                               : "中文",
                      false, locale.language == lang))
          locale.language = lang;
      }
      bottom += 54;
      if (ui.button("settingsSaveBrowser", {content.x, bottom, content.w, 38},
                    t("save.browser_save")))
        openSaves(true);
      bottom += 54;
      if (!world->importReport.empty()) {
        if (ui.button("importReportAgain", {content.x, bottom, content.w, 38},
                      t("import.summary"))) {
          importOpen = importSummary = true;
          importWasPaused = paused;
          paused = true;
          ui.blur();
        }
        bottom += 54;
      }
      ui.text(content.x, bottom, t("hud.draw_distance"), 16, hex(0xf0f2f4));
      bottom += 34;
      for (int i = 0; i < 3; ++i) {
        int count = i == 0 ? 32 : i == 1 ? 96 : 192;
        if (ui.button("renderFloors" + std::to_string(count),
                      {content.x + i * 130, bottom, 120, 36}, std::to_string(count), false,
                      visibleFloors == count))
          visibleFloors = count;
      }
      bottom += 55;
      bool skinEnabled = ui.enabled;
      ui.enabled = skinEnabled && !renderer->software;
      if (ui.button("gpuSkinningSetting", {content.x, bottom, content.w, 38},
                    t(gpuSkinning && !renderer->software ? "graphics.gpu_skinning"
                                                         : "graphics.cpu_skinning")))
        gpuSkinning = !gpuSkinning;
      ui.enabled = skinEnabled;
      bottom = ui.wrap(content.x, bottom + 52, content.w, t("graphics.skinning_help"), 14,
                       hex(0x8d99a5)) +
               24;
      bottom =
          ui.wrap(content.x, bottom, content.w, t("hud.graphics_help"), 14, hex(0x8d99a5)) + 24;
      if (ui.button("cutaway", {content.x, bottom, content.w, 38},
                    t(cutaway ? "hud.cutaway_on" : "hud.cutaway_off"), false, cutaway)) {
        cutaway = !cutaway;
        nextInstances = 0;
      }
      bottom += 54;
      if (ui.button("settingsGuide", {content.x, bottom, content.w, 40}, t("help.open")))
        helpOpen = true;
      bottom += 60;
      if (ui.button("settingsCamera", {content.x, bottom, content.w, 40},
                    t(firstPerson ? "action.observe" : "action.possess"))) {
        setFirstPerson(!firstPerson);
        drawerOpen = false;
      }
      bottom += 60;
    }
    ui.clip({0, 0, float(ui.width), float(ui.height)});
    float extent = std::max(content.h, bottom - start + 20),
          maximum = std::max(0.f, extent - content.h);
    scrollOffset = std::clamp(scrollOffset, 0.f, maximum);
    if (maximum > 0) {
      float thumb = std::max(24.f, content.h * content.h / extent);
      ui.fill({x + width - 8, content.y + (content.h - thumb) * scrollOffset / maximum, 3, thumb},
              hex(0x82968f), 1);
    }
  }
  void toolbar() {
    auto &w = *world;
    float h = float(ui.height), width = float(ui.width);
    ui.fill({0, h - 58, width, 58}, hex(0x11151b, .99f));
    ui.line(0, h - 58, width, h - 58, hex(0x333c47));
    if (ui.button("pause", {20, h - 45, 105, 32}, t(paused ? "action.play" : "action.pause"), true))
      paused = !paused;
    for (int i = 0; i < 3; ++i) {
      int val = i == 0 ? 1 : i == 1 ? 10 : 100;
      if (ui.button("speed" + std::to_string(i), {138.f + i * 49, h - 45, 44, 32},
                    std::to_string(val) + "x", false, speed == val))
        speed = float(val);
    }
    if (ui.button("slotminus", {width - 472, h - 45, 30, 32}, "−"))
      slot = std::max(1, slot - 1);
    ui.text(width - 432, h - 37, t("save.slot") + " " + std::to_string(slot), 14, hex(0xb7c5c4));
    if (ui.button("slotplus", {width - 330, h - 45, 30, 32}, "+"))
      slot = std::min(8, slot + 1);
    if (ui.button("save", {width - 287, h - 45, 84, 32}, t("nav.save")))
      act([&] { openSaves(true); });
    if (ui.button("load", {width - 193, h - 45, 84, 32}, t("nav.load")))
      act([&] { openSaves(false); });
    if (ui.button("menu", {width - 99, h - 45, 82, 32}, t("nav.menu"))) {
      menu = true;
      phone = false;
      setFirstPerson(false);
    }
  }
  void worldLabels() {
    if (!world || !firstPerson || phone || helpOpen)
      return;
    auto m = camera();
    auto eye = world->position(selected);
    auto label = [&](Vec3 p, const std::string &text, Color c) {
      float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
      if (cw <= .1f)
        return;
      float x = (m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) / cw;
      float y = (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) / cw;
      x = (x + 1) * ui.width * .5f;
      y = (y + 1) * ui.height * .5f;
      float width = std::min(230.f, ui.textWidth(text, 13) + 20);
      if (x - width / 2 < 12 || x + width / 2 > ui.width - (drawerOpen ? 480 : 16) || y < 90 ||
          y > ui.height - 155)
        return;
      ui.fill({x - width / 2, y, width, 27}, hex(0x10232a, .92f), 4);
      ui.text(x - width / 2 + 10, y + 5, text, 13, c);
    };
    for (auto &f : world->facilities)
      if (f.floor == currentFloor && length(f.pos - eye) < 45)
        label(f.pos + Vec3{0, 2.6f, 0},
              t(facilityKey(f.kind)) + " · " + std::to_string(int(length(f.pos - eye))) + " m",
              hex(0xe4c38a));
    for (int i = 0; i < world->layout().ladderCount(); ++i) {
      auto p = world->layout().ladder(i, currentFloor * 3.f);
      if (length(p - eye) < 45)
        label(p + Vec3{0, 2.2f, 0},
              t("ladder.label") + " · " + std::to_string(int(length(p - eye))) + " m",
              hex(0xc3ddcf));
    }
  }
  void viewportHUD() {
    auto &w = *world;
    auto &r = w.residents[selected];
    float width = ui.width, h = ui.height;
    ui.fill({20, 20, 272, 52}, hex(0x10141a, .84f), 8);
    ui.text(36, 29, "MEGA / " + t("status.floor") + " " + std::to_string(currentFloor + 1), 15,
            hex(0xe8ecf0));
    int minutes = int(w.time / 60) % 1440;
    char clock[32];
    std::snprintf(clock, sizeof(clock), "%02d:%02d", minutes / 60, minutes % 60);
    ui.text(36, 50,
            std::string(clock) + "  ·  " +
                t(w.looseBodies.contains(r.id) ? "physics.falling" : activityKey(r.activity)),
            12, hex(0xa0abb5));
    if (ui.button("hudPause", {width - 326, 20, 98, 38},
                  t(paused ? "action.play" : "action.pause")))
      paused = !paused;
    if (ui.button("hudGuide", {width - 220, 20, 44, 38}, "H"))
      helpOpen = true;
    if (ui.button("hudMenu", {width - 168, 20, 148, 38}, t("hud.menu") + "  [Tab]", false,
                  drawerOpen)) {
      drawerOpen = !drawerOpen;
      mouseLocked = false;
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    if (!w.activeFires.empty()) {
      bool danger = w.dangerAt(w.position(selected)) > .04f;
      if (ui.button("buildingAlarm", {20, 84, std::min(440.f, width - 40), 34},
                    t(danger ? "fire.danger" : "fire.alarm") + " · " +
                        std::to_string(w.activeFires.size()),
                    danger)) {
        emergencyScreen = true;
        tab = 0;
        scrollOffset = 0;
        drawerOpen = true;
        mouseLocked = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      }
    }
    if (!firstPerson) {
      if (ui.button("floorDown", {310, 20, 36, 38}, "−")) {
        currentFloor = std::max(0, currentFloor - 1);
        observerHeight = 0;
      }
      if (ui.button("floorUp", {352, 20, 36, 38}, "+")) {
        currentFloor = std::min(w.config.floors - 1, currentFloor + 1);
        observerHeight = 0;
      }
      if (ui.button("entireFloor", {400, 20, 160, 38}, t("view.entire"))) {
        cameraFocus = {};
        observerHeight = 0;
        cutaway = true;
        nextInstances = 0;
        distance = w.layout(currentFloor).halfWidth() * 2;
        pitch = .95f;
      }
      if (ui.button("returnResident", {568, 20, 180, 38}, t("view.resident"))) {
        cameraFocus = w.position(selected);
        currentFloor = std::clamp(int(cameraFocus.y / 3), 0, w.config.floors - 1);
        observerHeight = 0;
        distance = 10;
        pitch = .12f;
      }
    }
    if (!firstPerson && !drawerOpen && !savesOpen && !importOpen && !helpOpen && !phone) {
      float hintWidth = std::min(width - 40, 720.f), hintY = w.activeFires.empty() ? 84.f : 128.f;
      ui.fill({20, hintY, hintWidth, 34}, hex(0x10141a, .78f), 6);
      ui.text(32, hintY + 10, t("observer.controls"), 12, hex(0xacbfc3));
    }
    // Survival essentials stay visible; finances, jobs and world controls live in the drawer.
    float hudBottom = h - (drawerOpen && tab == 6 ? 62.f : 0.f);
    ui.fill({20, hudBottom - 135, 408, 112}, hex(0x10141a, .87f), 8);
    if (r.vehicle != none) {
      const auto &vehicle = w.vehicles[r.vehicle];
      float vehicleY = (!firstPerson && !drawerOpen && !phone) ? 128.f : 84.f;
      ui.fill({20, vehicleY, 272, 69}, hex(0x111a22), 8);
      ui.text(36, vehicleY + 13,
              t("traffic.fuel") + " " + std::to_string(int(vehicle.fuel)) + " L · " +
                  t("traffic.condition") + " " + std::to_string(int(vehicle.condition * 100)) + "%",
              13, hex(0xa5cbb9));
      ui.text(36, vehicleY + 40, std::to_string(int(std::abs(vehicle.speed) * 3.6f)) + " km/h", 13,
              hex(0xdce5df));
    }
    meter(36, hudBottom - 121, 176, t("life.health"), r.health / 100, hex(0xc6dfd0));
    meter(232, hudBottom - 121, 176, t("life.energy"), r.energy, hex(0xc0d2e5));
    meter(36, hudBottom - 72, 176, t("life.hunger"), r.hunger,
          hex(r.hunger > .8f ? 0xe49b84 : 0xd6b791));
    meter(232, hudBottom - 72, 176, t("life.thirst"), r.thirst,
          hex(r.thirst > .8f ? 0xe49b84 : 0x8eb6d5));
    if (!drawerOpen) {
      if (ui.button("hudLife", {width - 390, h - 61, 112, 38}, t("nav.life"))) {
        tab = 0;
        drawerOpen = true;
      }
      if (ui.button("hudWork", {width - 270, h - 61, 112, 38}, t("nav.work"))) {
        tab = 2;
        drawerOpen = true;
      }
      if (ui.button("hudNet", {width - 150, h - 61, 130, 38}, t("nav.network")))
        openNetwork();
    }
    if (firstPerson && !drawerOpen && !phone && !helpOpen) {
      float cx = width / 2, cy = h / 2;
      ui.fill({cx - 2, cy - 2, 4, 4}, hex(0xffffff, .85f), 2);
      std::string prompt = paused                          ? t("help.paused")
                           : r.ladder >= 0                 ? t("ladder.controls")
                           : w.nearbyLadder(selected) >= 0 ? t("ladder.grab")
                                                           : t("hud.controls");
      float tw = std::min(width - 500, ui.textWidth(prompt, 14) + 32);
      ui.fill({(width - tw) / 2, h - 181, tw, 36}, hex(0x10141a, .78f), 6);
      ui.text((width - tw) / 2 + 16, h - 171, prompt, 14, hex(0xd7dee4));
    }
  }
  void helpPanel() {
    auto &w = *world;
    float x = (ui.width - 800) / 2.f, y = (ui.height - 690) / 2.f;
    ui.enabled = true;
    ui.fill({0, 70, float(ui.width), float(ui.height) - 70}, hex(0x090d12, .75f));
    ui.fill({x, y, 800, 690}, hex(0x1a2028, .99f), 12);
    ui.text(x + 30, y + 24, t("help.title"), 29, hex(0xc3ddcf));
    Rect content{x + 30, y + 75, 735, 470};
    if (ui.hit(content) && ui.scroll != 0)
      helpScroll = std::max(0.f, helpScroll - float(ui.scroll) * 48);
    ui.clip(content);
    float start = content.y - helpScroll;
    float line = ui.wrap(content.x, start, content.w, t("help.intro"), 17, hex(0xc4d3d0)) + 25;
    for (auto key : {"help.walk", "help.combat", "help.ladder", "help.camera", "help.services",
                     "help.expansion"})
      line = ui.wrap(content.x, line, content.w, t(key), 16, hex(0xaabfc1)) + 18;
    ui.clip({0, 0, float(ui.width), float(ui.height)});
    float extent = std::max(content.h, line - start), maximum = std::max(0.f, extent - content.h);
    helpScroll = std::clamp(helpScroll, 0.f, maximum);
    if (maximum > 0) {
      float thumb = std::max(24.f, content.h * content.h / extent);
      ui.fill({x + 779, content.y + (content.h - thumb) * helpScroll / maximum, 3, thumb},
              hex(0x6e858b), 1);
    }
    if (ui.button("guideLadder", {x + 30, y + 569, 355, 42}, t("help.to_ladder"), true))
      act([&] {
        auto p = w.position(selected);
        int l = p.z < 0 ? 2 : 0;
        if (w.config.layoutVersion == 1)
          l = 0;
        w.travel(selected, w.layout().landing(l, std::round(p.y / 3) * 3), TaskKind::None, none);
        helpOpen = false;
        setFirstPerson(true);
        paused = false;
        speed = 1;
      });
    if (ui.button("guideReception", {x + 405, y + 569, 365, 42}, t("help.to_reception")))
      act([&] {
        auto f = w.nearest(selected, FacilityKind::Reception);
        if (f != none) {
          w.travel(selected, w.facilities[f].pos + Vec3{0, 0, 1.7f}, TaskKind::None, none);
          helpOpen = false;
          setFirstPerson(true);
          paused = false;
        }
      });
    if (ui.button("guideClose", {x + 30, y + 624, 740, 38}, t("help.close")))
      helpOpen = false;
  }
  void vehiclePanel(float x, float y, float width) {
    auto &w = *world;
    auto &r = w.residents[selected];
    y = ui.wrap(x, y, width, t("vehicle.intro"), 16, hex(0xa4b7b9)) + 20;
    for (auto &v : w.vehicles)
      if (v.askingPrice > 0 || v.owner == r.account ||
          (v.towTruck && r.role == Role::TowDriver && r.employer == v.owner)) {
        std::string id = std::to_string(v.id);
        bool owned = v.owner == r.account ||
                     (v.towTruck && r.role == Role::TowDriver && r.employer == v.owner);
        ui.fill({x, y, width, 145}, hex(0x202730), 8);
        ui.text(x + 16, y + 12, t(vehicleKey(v.kind)), 22, hex(0xe1e6df));
        ui.text(x + 16, y + 46,
                owned ? t(v.towTruck && v.owner != r.account ? "traffic.fleet" : "vehicle.owned")
                      : money(v.askingPrice),
                17, hex(0xa5cbb9));
        ui.text(x + 16, y + 74,
                v.bay == none ? t("vehicle.no_parking")
                              : t("parking.bay") + " #" + std::to_string(v.bay + 1),
                14, hex(0x98b0b4));
        float bx = x + 16;
        if (ui.button("locateVehicle" + id, {bx, y + 105, 160, 30},
                      t(owned ? "vehicle.walk" : "vehicle.dealer")))
          act([&] {
            w.travel(selected, owned ? v.pos : w.facilities[v.dealer].pos + Vec3{1.5f, 0, -2.f},
                     TaskKind::None, none);
            phone = false;
            paused = false;
            currentFloor = 0;
          });
        bx += 174;
        if (!owned) {
          if (ui.button("buyVehicle" + id, {bx, y + 105, 165, 30}, t("action.buy"), true))
            act([&] { w.buyVehicle(selected, v.id); });
        } else {
          if (ui.button("driveVehicle" + id, {bx, y + 105, 165, 30},
                        t(r.vehicle == v.id ? "vehicle.exit" : "vehicle.drive"), true))
            act([&] {
              if (r.vehicle == v.id)
                w.exitVehicle(selected);
              else
                w.enterVehicle(selected, v.id);
              phone = false;
              setFirstPerson(true);
              paused = false;
            });
          bx += 179;
          if (v.bay == none && v.owner == r.account &&
              ui.button("sellVehicle" + id, {bx, y + 105, 150, 30}, t("vehicle.sell")))
            act([&] {
              w.sellVehicle(selected, v.id, w.nearest(selected, FacilityKind::Dealership));
            });
        }
        y += 157;
        if (owned) {
          ui.text(x, y,
                  t("traffic.condition") + " " + std::to_string(int(v.condition * 100)) + "% · " +
                      t("traffic.fuel") + " " + std::to_string(int(v.fuel)) + " L",
                  14, hex(0xa5cbb9));
          y += 30;
          Id shop = w.nearestAt(v.pos, FacilityKind::Workshop);
          if (shop != none) {
            if (v.kind != VehicleKind::Bicycle &&
                ui.button("fuelVehicle" + id, {x, y, width / 3 - 6, 32}, t("traffic.refuel")))
              act([&] {
                w.refuelVehicle(
                    selected, v.id, shop,
                    std::min(5.f, (v.kind == VehicleKind::Scooter ? 8.f : 40.f) - v.fuel));
              });
            if (ui.button("repairVehicle" + id, {x + width / 3, y, width / 3 - 6, 32},
                          t("traffic.repair")))
              act([&] { w.requestVehicleJob(selected, v.id, shop, false); });
            if (ui.button("towVehicle" + id, {x + width * 2 / 3, y, width / 3 - 6, 32},
                          t("traffic.tow")))
              act([&] { w.requestVehicleJob(selected, v.id, shop, true); });
            y += 42;
            if (v.kind == VehicleKind::DeliveryVan && !v.towTruck &&
                (w.facilities[shop].owner == r.account || w.facilities[shop].manager == selected)) {
              if (ui.button("convertTowTruck" + id, {x, y, width, 32}, t("traffic.convert")))
                act([&] { w.convertServiceTruck(selected, v.id, shop); });
              y += 40;
            }
          }
          if (r.vehicle == v.id) {
            ui.text(x, y, t("traffic.destination"), 13, hex(0xb8c8c9));
            y += 24;
            ui.input("driveX", {x, y, width / 3 - 6, 32}, driveX, true);
            ui.input("driveZ", {x + width / 3, y, width / 3 - 6, 32}, driveZ, true);
            ui.input("driveFloor", {x + width * 2 / 3, y, width / 3 - 6, 32}, driveFloor, true);
            y += 42;
            if (ui.button("autodrive" + id, {x, y, width / 2 - 6, 32}, t("traffic.autodrive")))
              act([&] {
                w.driveTo(selected,
                          {std::stof(driveX), (std::stoi(driveFloor) - 1) * 3.f, std::stof(driveZ)},
                          true);
                paused = false;
              });
            if (ui.button("cancelDrive" + id, {x + width / 2, y, width / 2 - 6, 32},
                          t("traffic.stop")))
              act([&] { w.cancelDrive(selected); });
            y += 42;
            for (const auto &road : w.roads)
              if (road.active && road.kind == RoadKind::FreightLift &&
                  std::hypot(v.pos.x - road.pos.x, v.pos.z - road.pos.z) < 25) {
                if (ui.button("freightCall" + id + "-" + std::to_string(road.id), {x, y, width, 32},
                              t("traffic.call_lift") + " #" + std::to_string(road.id + 1)))
                  act([&] {
                    int here = int(std::round(v.pos.y / 3));
                    w.requestVehicleLift(selected, road.id,
                                         here == road.from ? road.to : road.from);
                    paused = false;
                  });
                y += 40;
              }
          }
        }
      }
    for (const auto &j : w.vehicleJobs)
      if (j.customer == selected || j.worker == selected ||
          (j.worker == none && j.workshop == r.jobSite &&
           r.role == (j.tow ? Role::TowDriver : Role::Repairer) &&
           j.state != VehicleJobState::Complete && j.state != VehicleJobState::Cancelled)) {
        static const char *jobKeys[] = {"traffic.job0", "traffic.job1", "traffic.job2",
                                        "traffic.job3", "traffic.job4", "traffic.job5"};
        y = ui.wrap(x, y, width,
                    t(j.tow ? "traffic.tow" : "traffic.repair") + " #" + std::to_string(j.id + 1) +
                        " · " + t(jobKeys[int(j.state)]),
                    14, hex(0xd6ba7f)) +
            12;
        if (j.state != VehicleJobState::Complete && j.state != VehicleJobState::Cancelled) {
          if (j.customer == selected && ui.button("cancelVehicleJob" + std::to_string(j.id),
                                                  {x, y, width, 32}, t("traffic.cancel_job")))
            act([&] { w.cancelVehicleJob(selected, j.id); });
          y += 40;
          if (j.worker == none && ui.button("takeVehicleJob" + std::to_string(j.id),
                                            {x, y, width, 32}, t("action.accept")))
            act([&] { w.assignVehicleJob(selected, j.id); });
          y += 40;
          if (j.worker == selected && ui.button("attendVehicleJob" + std::to_string(j.id),
                                                {x, y, width, 32}, t("build.start")))
            act([&] {
              w.vehicleWorkDecision(selected);
              paused = false;
            });
          y += 40;
        }
      }
    for (const auto &f : w.facilities)
      if (f.kind == FacilityKind::Workshop && (f.owner == r.account || f.manager == selected)) {
        ui.text(x, y, t("traffic.pump_stock") + " " + std::to_string(int(f.fuelStock)) + " L", 14,
                hex(0xa5cbb9));
        y += 28;
        if (ui.button("stockFuel" + std::to_string(f.id), {x, y, width, 32},
                      t("traffic.import_fuel")))
          act([&] { w.stockFuel(selected, f.id, 100); });
        y += 40;
      }
    y += 16;
    ui.text(x, y, t("parking.title"), 24, hex(0xa5cbb9));
    y += 42;
    for (auto &bay : w.parking) {
      bool ours = bay.vehicle != none && w.vehicles[bay.vehicle].owner == r.account;
      ui.fill({x, y, width, 110}, hex(0x202730), 8);
      ui.text(x + 16, y + 12,
              t("parking.bay") + " #" + std::to_string(bay.id + 1) + " · " + money(bay.dailyPrice) +
                  " / " + t("parking.day"),
              18, hex(0xe0e5df));
      ui.text(x + 16, y + 44,
              t(bay.vehicle == none ? "parking.available"
                : ours              ? "parking.yours"
                                    : "parking.occupied"),
              14, hex(ours ? 0xa5cbb9 : 0x98b0b4));
      if (bay.vehicle == none) {
        Id candidate = none;
        for (auto &v : w.vehicles)
          if (v.owner == r.account && v.bay == none) {
            candidate = v.id;
            break;
          }
        if (candidate != none && ui.button("reserveBay" + std::to_string(bay.id),
                                           {x + 16, y + 73, 220, 27}, t("parking.reserve"), true))
          act([&] { w.reserveParking(selected, candidate, bay.id); });
      } else if (ours) {
        if (ui.button("parkBay" + std::to_string(bay.id), {x + 16, y + 73, 220, 27},
                      t("parking.park")))
          act([&] { w.parkVehicle(selected, bay.vehicle, bay.id); });
        if (ui.button("releaseBay" + std::to_string(bay.id), {x + 250, y + 73, 220, 27},
                      t("parking.release")))
          act([&] { w.releaseParking(selected, bay.vehicle); });
      }
      y += 122;
    }
  }
  void customizationPanel(float x, float y, float width) {
    auto &w = *world;
    auto &r = w.residents[selected];
    ui.text(x, y, t("custom.title"), 27, hex(0xe4e7de));
    y += 40;
    y = ui.wrap(x, y, width, t("custom.desc"), 14, hex(0x9fb3b5)) + 20;
    for (int i = 0; i < 3; ++i)
      if (ui.button("customCategory" + std::to_string(i),
                    {x + i * width / 3, y, width / 3 - 10, 36},
                    t(i == 0   ? "custom.home"
                      : i == 1 ? "custom.clothes"
                               : "custom.vehicle"),
                    customCategory == i))
        customCategory = i;
    y += 60;
    auto swatches = [&](const std::string &key, int value, auto change) {
      ui.text(x, y, t(key), 15, hex(0xc4d1ca));
      float px = x + std::min(230.f, width * .34f);
      for (int c = 0; c < 8; ++c) {
        if (ui.button(key + std::to_string(c), {px + c * 39, y - 6, 33, 33}, "", c == value))
          act([&] { change(c); });
        ui.fill({px + c * 39 + 6, y, 21, 21}, palette(c), 3);
        if (c == value) {
          ui.line(px + c * 39 + 11, y + 10, px + c * 39 + 15, y + 15, hex(0xffffff));
          ui.line(px + c * 39 + 15, y + 15, px + c * 39 + 23, y + 6, hex(0xffffff));
        }
      }
      y += 53;
    };
    if (customCategory == 0) {
      Id h = w.residentHomes[selected];
      if (h == none) {
        ui.text(x, y, t("housing.title"), 18, hex(0xc0b09a));
        return;
      }
      auto &unit = w.housing[h];
      swatches("custom.linen", unit.decor.linen, [&](int c) {
        auto d = unit.decor;
        d.linen = c;
        w.customizeHome(selected, h, d);
      });
      swatches("custom.blanket", unit.decor.blanket, [&](int c) {
        auto d = unit.decor;
        d.blanket = c;
        w.customizeHome(selected, h, d);
      });
      if (unit.owner == r.account) {
        swatches("custom.frame", unit.decor.frame, [&](int c) {
          auto d = unit.decor;
          d.frame = c;
          w.customizeHome(selected, h, d);
        });
        swatches("custom.closet", unit.decor.closet, [&](int c) {
          auto d = unit.decor;
          d.closet = c;
          w.customizeHome(selected, h, d);
        });
      } else {
        y = ui.wrap(x, y, width, t("custom.tenant"), 14, hex(0xd4ae7a)) + 24;
      }
      if (ui.button("curtain", {x, y, 250, 35}, t("custom.curtain"), unit.decor.curtain))
        act([&] {
          auto d = unit.decor;
          d.curtain = !d.curtain;
          w.customizeHome(selected, h, d);
        });
      if (ui.button("closetDoor", {x + 270, y, 250, 35}, t("custom.door"), unit.decor.closetOpen))
        act([&] {
          auto d = unit.decor;
          d.closetOpen = !d.closetOpen;
          w.customizeHome(selected, h, d);
        });
      y += 55;
      if (ui.button("showHome", {x, y, 300, 38}, t("custom.view"), true)) {
        phone = false;
        currentFloor = unit.floor;
        setFirstPerson(false);
        distance = 24;
        orbit = -.7f;
      }
    } else if (customCategory == 1) {
      Id item = r.outfit;
      if (item == none) {
        ui.text(x, y, t("custom.no_clothes"), 17, hex(0xd4ae7a));
        return;
      }
      auto &garment = w.items[item];
      swatches("custom.fabric", garment.look.primary, [&](int c) {
        auto a = garment.look;
        a.primary = c;
        w.customizeClothes(selected, item, a);
      });
      swatches("custom.trousers", garment.look.secondary, [&](int c) {
        auto a = garment.look;
        a.secondary = c;
        w.customizeClothes(selected, item, a);
      });
      for (int i = 0; i < 3; ++i)
        if (ui.button("cut" + std::to_string(i), {x + i * width / 3, y, width / 3 - 10, 38},
                      t("custom.cut" + std::to_string(i)), garment.look.style == i))
          act([&] {
            auto a = garment.look;
            a.style = i;
            w.customizeClothes(selected, item, a);
          });
      y += 56;
      for (int i = 0; i < 3; ++i)
        if (ui.button("accessory" + std::to_string(i), {x + i * width / 3, y, width / 3 - 10, 38},
                      t("custom.accessory" + std::to_string(i)), garment.look.accessory == i))
          act([&] {
            auto a = garment.look;
            a.accessory = i;
            w.customizeClothes(selected, item, a);
          });
      y += 60;
      meter(x, y, width, t("life.hygiene"), garment.cleanliness, hex(0x9bb69b));
    } else {
      Id car = none;
      for (const auto &v : w.vehicles)
        if (v.owner == r.account) {
          car = v.id;
          break;
        }
      if (car == none) {
        ui.wrap(x, y, width, t("custom.no_vehicle"), 18, hex(0xd4ae7a));
        return;
      }
      std::vector<Id> owned;
      for (const auto &v : w.vehicles)
        if (v.owner == r.account)
          owned.push_back(v.id);
      if (std::find(owned.begin(), owned.end(), customVehicle) == owned.end())
        customVehicle = car;
      if (ui.button("nextCustomVehicle", {x + width - 120, y, 120, 30}, "→")) {
        auto i = std::find(owned.begin(), owned.end(), customVehicle) - owned.begin();
        customVehicle = owned[(i + 1) % owned.size()];
      }
      car = customVehicle;
      auto &v = w.vehicles[car];
      ui.text(x, y, t(vehicleKey(v.kind)) + " #" + std::to_string(car + 1), 20, hex(0xa5cbb9));
      y += 40;
      swatches("custom.paint", v.look.primary, [&](int c) {
        auto a = v.look;
        a.primary = c;
        w.customizeVehicle(selected, car, a);
      });
      swatches("custom.trim", v.look.secondary, [&](int c) {
        auto a = v.look;
        a.secondary = c;
        w.customizeVehicle(selected, car, a);
      });
      for (int i = 0; i < 3; ++i)
        if (ui.button("carPart" + std::to_string(i), {x + i * width / 3, y, width / 3 - 10, 38},
                      t("custom.car" + std::to_string(i)), v.look.accessory == i))
          act([&] {
            auto a = v.look;
            a.accessory = i;
            w.customizeVehicle(selected, car, a);
          });
    }
  }
  void medicalPanel(float x, float y, float width) {
    auto &w = *world;
    auto &r = w.residents[selected];
    ui.text(x, y, t("medical.title"), 26, hex(0xe2e8e0));
    y += 42;
    y = ui.wrap(x, y, width, t("medical.desc"), 15, hex(0x9eb2b2)) + 20;
    Id provider = r.medicalProvider == none ? w.nearest(selected, FacilityKind::Infirmary)
                                            : r.medicalProvider;
    if (provider == none) {
      ui.text(x, y, t("medical.no_provider"), 17, hex(0xd7ab78));
      return;
    }
    auto &f = w.facilities[provider];
    ui.text(x, y,
            t("facility.infirmary") + " · " + t("status.floor") + " " +
                std::to_string(f.floor + 1) + " · " + t(w.availability(provider)),
            15, hex(0xa9c7b7));
    y += 34;
    Money prices[] = {40000, 150000, 350000};
    for (int tier = 0; tier < 3; ++tier) {
      ui.fill({x, y, width, 64}, hex(0x202730), 6);
      ui.text(x + 14, y + 11, t("medical.tier" + std::to_string(tier)), 17, hex(0xe1e6db));
      ui.text(x + 14, y + 36, t("medical.features" + std::to_string(tier)), 12, hex(0x9db0b2));
      if (ui.button("medicalPlan" + std::to_string(tier), {x + width - 166, y + 15, 150, 34},
                    money(prices[tier]) + " / 30d",
                    r.medicalTier == tier && r.medicalUntil > w.time))
        act([&] { w.subscribeMedical(selected, provider, tier); });
      y += 74;
    }
    if (r.medicalUntil > w.time) {
      ui.text(x, y,
              t("medical.covered") + " " +
                  std::to_string(int(std::ceil((r.medicalUntil - w.time) / 86400))) + "d",
              14, hex(0x9dc4ab));
      y += 30;
    }
    for (auto &a : w.ambulances)
      if (a.patient == selected && a.state != RescueState::Idle) {
        ui.text(x, y, t("medical.stage" + std::to_string(int(a.state))), 18, hex(0xe1b873));
        y += 32;
      }
    if (ui.button("callAmbulance", {x, y, width, 40}, t("medical.call"), true))
      act([&] {
        w.callAmbulance(selected);
        paused = false;
      });
    y += 52;
    if (r.infectedAt >= 0)
      ui.text(x, y, t("medical.infected"), 16, hex(0xdb9b7e));
  }
  void network() {
    auto &w = *world;
    float left = std::max(24.f, (ui.width - 1000.f) / 2), top = 100,
          width = std::min(1000.f, ui.width - 48.f), height = ui.height - 160.f;
    ui.enabled = true;
    ui.fill({0, 70, float(ui.width), float(ui.height) - 70}, hex(0x071015, .7f));
    ui.fill({left, top, width, height}, hex(0x1a2028), 12);
    ui.fill({left, top, width, 92}, hex(0x22353c), 12);
    ui.text(left + 28, top + 20, "DORMNET", 25, hex(0xe3ba78));
    ui.text(left + 192, top + 27, t("network.tag"), 12, hex(0x9ab0b4));
    if (ui.button("netclose", {left + width - 64, top + 18, 40, 32}, "×")) {
      phone = false;
      return;
    }
    const char *keys[] = {"market", "messages", "orders", "jobs", "vehicles", "style", "medical"};
    for (int i = 0; i < 7; ++i)
      if (ui.button("nettab" + std::to_string(i),
                    {left + 24 + i * (width - 48) / 7, top + 58, (width - 48) / 7 - 7, 30},
                    t(std::string("network.") + keys[i]), false, networkTab == i)) {
        networkTab = i;
        networkScroll = 0;
      }
    float x = left + 28, y = top + 116;
    ui.clip({left + 16, top + 99, width - 32, height - 116});
    if (networkTab == 4 || networkTab == 6 || networkTab == 3 || networkTab == 0) {
      int count = 0;
      for (auto &v : w.vehicles)
        if (v.askingPrice > 0 || v.owner == w.residents[selected].account)
          ++count;
      int saleRows = 0;
      for (auto &i : w.items)
        if ((i.seller != none && i.askingPrice > 0) ||
            (i.owner == w.residents[selected].account && !i.consumed && i.carrier == selected))
          ++saleRows;
      float contentHeight = networkTab == 0   ? 840 + std::min(80, saleRows) * 95
                            : networkTab == 3 ? 760
                            : networkTab == 6 ? 640
                                              : 100 + count * 157 + 58 + w.parking.size() * 122;
      float maximum = std::max(0.f, contentHeight - (height - 116));
      if (ui.hit({left + 16, top + 99, width - 32, height - 116}))
        networkScroll -= float(ui.scroll) * 55;
      networkScroll = std::clamp(networkScroll, 0.f, maximum);
      if (maximum > 0) {
        float track = height - 130, thumb = std::max(32.f, track * (height - 116) / contentHeight);
        ui.fill({left + width - 10, top + 106, 3, track}, hex(0x2e434a), 1);
        ui.fill(
            {left + width - 10, top + 106 + (track - thumb) * networkScroll / maximum, 3, thumb},
            hex(0xa5cbb9), 1);
      }
    }
    Id dealer = w.nearest(selected, FacilityKind::Dealership),
       garage = w.nearest(selected, FacilityKind::Parking);
    bool atDesk =
        networkTab == 4 && ((dealer != none && w.near(selected, w.facilities[dealer].pos, 3)) ||
                            (garage != none && w.near(selected, w.facilities[garage].pos, 3)));
    if (!w.hasDevice(selected) && !atDesk && networkTab != 5) {
      ui.wrap(x, y, width - 56, t("network.offline"), 20, hex(0xd2dfda));
      ui.clip({0, 0, float(ui.width), float(ui.height)});
      return;
    }
    if (networkTab == 6)
      medicalPanel(x, y - networkScroll, width - 56);
    if (networkTab == 5)
      customizationPanel(x, y, width - 56);
    if (networkTab == 4)
      vehiclePanel(x, y - networkScroll, width - 56);
    if (networkTab == 0) {
      y -= networkScroll;
      y = ui.wrap(x, y, width - 56, t("network.desc"), 16, hex(0xa4b7b9)) + 28;
      std::array<ItemKind, 12> products = {
          ItemKind::Food,  ItemKind::Phone,     ItemKind::Laptop,     ItemKind::Medicine,
          ItemKind::Tools, ItemKind::Handgun,   ItemKind::Ammunition, ItemKind::Extinguisher,
          ItemKind::Fan,   ItemKind::Tableware, ItemKind::RiceCooker, ItemKind::Clothes};
      const char *names[] = {"food",  "phone",     "laptop",     "medicine",
                             "tools", "handgun",   "ammunition", "extinguisher",
                             "fan",   "tableware", "ricecooker", "clothes"};
      float card = (width - 80) / 3;
      for (int i = 0; i < int(products.size()); ++i) {
        float cx = x + (i % 3) * (card + 12), cy = y + (i / 3) * 182;
        ui.fill({cx, cy, card, 166}, hex(0x202730), 8);
        ui.fill({cx + 15, cy + 16, 36, 36}, hex(i % 2 ? 0x446168 : 0x645748), 8);
        ui.text(cx + 25, cy + 24, std::to_string(i + 1), 21, hex(0xe6d2b0));
        ui.text(cx + 64, cy + 24, t(std::string("item.") + names[i]), 18, hex(0xe2e6dc));
        ui.wrap(cx + 16, cy + 65, card - 32, t(std::string("item.desc.") + names[i]), 14,
                hex(0x9bb0b3));
        ui.text(cx + 16, cy + 128, money(w.productPrice(products[i])), 20, hex(0xa5cbb9));
        if (ui.button("product" + std::to_string(i), {cx + card - 102, cy + 122, 86, 31},
                      t("action.buy"), true))
          act([&] {
            w.order(selected, w.nearest(selected, FacilityKind::Shop), products[i]);
            networkTab = 2;
            paused = false;
          });
      }
      float productHeight = float((products.size() + 2) / 3) * 182;
      ui.text(x, y + productHeight + 14, t("network.escrow"), 13, hex(0x9ab0b2));
      y += productHeight + 64;
      ui.text(x, y, t("trade.title"), 21, hex(0xa5cbb9));
      y += 35;
      y = ui.wrap(x, y, width - 56, t("trade.desc"), 14, hex(0xa4b7b9)) + 20;
      int rows = 0;
      for (auto &item : w.items)
        if (!item.consumed && item.owner == w.residents[selected].account &&
            item.carrier == selected && item.reservedAt == none &&
            w.residents[selected].outfit != item.id && item.kind != ItemKind::Materials &&
            item.kind != ItemKind::Waste && rows++ < 40) {
          ui.text(x, y, t(itemLabel(item.kind)) + " #" + std::to_string(item.id + 1), 15,
                  hex(0xd1ddd8));
          y += 26;
          Money price = item.kind == ItemKind::Laptop    ? 30000
                        : item.kind == ItemKind::Phone   ? 7000
                        : item.kind == ItemKind::Handgun ? 60000
                                                         : 1000;
          if (ui.button("listItem" + std::to_string(item.id), {x, y, width - 56, 30},
                        item.askingPrice ? t("trade.withdraw")
                                         : t("trade.list") + " · " + money(price)))
            act([&] { w.listItem(selected, item.id, item.askingPrice ? 0 : price); });
          y += 48;
        }
      rows = 0;
      for (auto &item : w.items)
        if (!item.consumed && item.seller != none && item.seller != selected &&
            item.askingPrice > 0 && rows++ < 40) {
          y = ui.wrap(x, y, width - 56,
                      t(itemLabel(item.kind)) + " · " + money(item.askingPrice) + " · " +
                          w.displayName(item.seller, locale.language),
                      15, hex(0xd1ddd8)) +
              8;
          if (ui.button("meetSeller" + std::to_string(item.id), {x, y, (width - 64) / 2, 30},
                        t("trade.meet")))
            act([&] {
              w.travel(selected, w.position(item.seller), TaskKind::None, none);
              paused = false;
            });
          if (ui.button("buyUsed" + std::to_string(item.id),
                        {x + (width - 64) / 2 + 8, y, (width - 64) / 2, 30}, t("trade.handover")))
            act([&] { w.buyItem(selected, item.id); });
          y += 48;
        }
    }
    if (networkTab == 1) {
      ui.text(x, y, t("network.recipient"), 14, hex(0xa0b4b7));
      ui.input("recipient", {x, y + 27, 160, 37}, recipient, true);
      ui.input("msg", {x + 178, y + 27, width - 354, 37}, message);
      if (ui.button("send", {x + width - 156, y + 27, 100, 37}, t("action.send"), true))
        act([&] {
          w.sendMessage(selected, std::stoi(recipient) - 1, message);
          message.clear();
        });
      y += 100;
      int n = 0;
      for (auto it = w.messages.rbegin(); it != w.messages.rend() && n < 8; ++it)
        if (it->from == selected || it->to == selected) {
          ui.fill({x, y, width - 56, 73}, hex(0x202730), 7);
          ui.text(x + 14, y + 10,
                  w.displayName(it->from, locale.language) + " → " +
                      w.displayName(it->to, locale.language),
                  13, hex(0xa5cbb9));
          ui.text(x + 14, y + 36, it->text, 16, hex(0xe0e5de));
          y += 83;
          ++n;
        }
      if (!n)
        ui.text(x, y, t("network.no_messages"), 16, hex(0x91aaaf));
    }
    if (networkTab == 2) {
      const char *state[] = {"waiting", "pickup", "delivering", "delivered", "cancelled", "lost"};
      int n = 0;
      for (auto it = w.orders.rbegin(); it != w.orders.rend() && n < 8; ++it)
        if (it->buyer == selected) {
          auto &o = *it;
          ui.fill({x, y, width - 56, 86}, hex(0x202730), 8);
          ui.text(x + 16, y + 13,
                  "#" + std::to_string(o.id + 1) + " · " +
                      t(std::string("order.") + state[int(o.state)]),
                  19, hex(0xe2e6de));
          ui.text(x + 16, y + 48,
                  o.courier == none ? t("order.waiting")
                                    : w.displayName(o.courier, locale.language),
                  14, hex(0x9db1b5));
          ui.text(x + width - 205, y + 34, money(o.price + o.deliveryFee), 22, hex(0xa5cbb9));
          y += 100;
          ++n;
        }
      if (!n)
        ui.text(x, y, t("network.no_orders"), 18, hex(0x93abad));
    }
    if (networkTab == 3) {
      y -= networkScroll;
      auto &r = w.residents[selected];
      if (video == none || std::size_t(video) >= w.videos.size() ||
          w.videos[video].worker != selected)
        for (const auto &v : w.videos)
          if (v.worker == selected && !v.submitted) {
            video = v.id;
            break;
          }
      bool legacyVideo = video != none && std::size_t(video) < w.videos.size() &&
                         w.videos[video].worker == selected && !w.videos[video].submitted;
      if ((r.role == Role::Programmer || r.role == Role::Administrator ||
           r.role == Role::VideoEditor) &&
          !legacyVideo) {
        ui.text(x, y, t("office.title"), 25, hex(0xe0e7dc));
        y += 38;
        if (office == none || std::size_t(office) >= w.officeOrders.size() ||
            w.officeOrders[office].worker != selected) {
          y = ui.wrap(x, y, width - 56, t("office.contract_desc"), 16, hex(0xa4bab9)) + 25;
          if (ui.button("officeContract", {x, y, width - 56, 40}, t("office.start"), true))
            act([&] {
              office = w.officeOrder(selected);
              program.clear();
              assignments = {-1, -1, -1, -1};
              paused = false;
            });
        } else {
          auto &o = w.officeOrders[office];
          auto requestLabel = [&](int slot) {
            Id target = o.tickets[slot];
            if (target == none)
              return t("office.no_request");
            int type = (slot + o.brief) % 4;
            std::string subject =
                (type == 0 || type == 3)
                    ? t(facilityKey(w.facilities[target].kind))
                    : w.displayName(type == 2 ? target : w.incidents[target].offender,
                                    locale.language);
            return t("office.ticket" + std::to_string(type)) + " · " + subject + " #" +
                   std::to_string(target + 1);
          };
          if (o.submitted) {
            ui.text(x, y, t("office.score") + " " + std::to_string(int(o.quality)) + " / 100", 22,
                    hex(0xb8ce9d));
            y += 45;
            if (o.liveRequests)
              for (int slot = 0; slot < 4; ++slot)
                if (o.tickets[slot] != none) {
                  y = ui.wrap(x, y, width - 56, requestLabel(slot), 15, hex(0xc8d7d0)) + 8;
                  y = ui.wrap(x, y, width - 56,
                              t("office.result" + std::to_string(o.outcomes[slot])), 13,
                              o.outcomes[slot] >= 3 ? hex(0xd8a77c) : hex(0xa8c9b2)) +
                      20;
                }
            if (ui.button("nextContract", {x, y, 320, 38}, t("office.next")))
              office = none;
          } else {
            y = ui.wrap(x, y, width - 56,
                        t(o.role == Role::Programmer ? "office.brief" + std::to_string(o.brief)
                          : o.role == Role::VideoEditor
                              ? "office.video_brief" + std::to_string(o.brief)
                              : "office.admin_brief"),
                        16, hex(0xb9c8bf)) +
                18;
            ui.text(x, y,
                    t("office.attendance") + " " +
                        std::to_string(int((o.total - o.remaining) / 60)) + " min · " +
                        money(o.budget),
                    13, hex(0xd2b57f));
            y += 32;
            if (o.role == Role::Programmer) {
              for (int op = 0; op < 7; ++op) {
                if (ui.button("codeOp" + std::to_string(op),
                              {x + (op % 4) * (width - 56) / 4, y + (op / 4) * 42,
                               (width - 56) / 4 - 8, 34},
                              t("office.op" + std::to_string(op))) &&
                    program.size() < 8)
                  program.push_back(op);
              }
              y += 94;
              ui.text(x, y, t("office.pipeline"), 13, hex(0x99b4b6));
              y += 26;
              std::string pipeline;
              for (int op : program) {
                if (!pipeline.empty())
                  pipeline += " → ";
                pipeline += t("office.op" + std::to_string(op));
              }
              y = ui.wrap(x, y, width - 56, pipeline.empty() ? t("office.empty") : pipeline, 17,
                          hex(0xe3d3b0)) +
                  18;
              if (ui.button("codeUndo", {x, y, 110, 32}, t("office.undo")) && !program.empty())
                program.pop_back();
              y += 46;
              auto result = w.runProgram({-2, 1, 3, 0, 5}, program);
              std::string values;
              for (int n : result) {
                if (!values.empty())
                  values += ", ";
                values += std::to_string(n);
              }
              ui.text(x, y, "[-2, 1, 3, 0, 5] → [" + values + "]", 17, hex(0xaac8b3));
              y += 35;
            } else if (o.role == Role::VideoEditor) {
              for (std::size_t i = 0; i < clips.size(); ++i) {
                ui.fill({x, y, width - 240, 42}, hex(0x202730), 6);
                ui.text(x + 15, y + 12, t("clip." + std::to_string(clips[i])), 16, hex(0xdce4db));
                if (ui.button("clipUp" + std::to_string(i), {x + width - 218, y, 54, 42}, "↑") &&
                    i > 0)
                  std::swap(clips[i], clips[i - 1]);
                if (ui.button("clipDown" + std::to_string(i), {x + width - 154, y, 54, 42}, "↓") &&
                    i + 1 < clips.size())
                  std::swap(clips[i], clips[i + 1]);
                y += 50;
              }
            } else {
              for (int ticket = 0; ticket < 4; ++ticket) {
                int type = (ticket + o.brief) % 4;
                if (o.liveRequests && o.tickets[ticket] == none)
                  continue;
                y = ui.wrap(x, y, width - 56,
                            o.liveRequests ? requestLabel(ticket)
                                           : t("office.ticket" + std::to_string(type)),
                            16, hex(0xd0d9d1)) +
                    10;
                for (int dept = 0; dept < 4; ++dept)
                  if (ui.button("routeTicket" + std::to_string(ticket) + "_" + std::to_string(dept),
                                {x + dept * (width - 56) / 4, y, (width - 56) / 4 - 8, 30},
                                t("office.dept" + std::to_string(dept)),
                                assignments[ticket] == dept))
                    assignments[ticket] = dept;
                y += 42;
              }
            }
            const auto &answer = o.role == Role::Programmer    ? program
                                 : o.role == Role::VideoEditor ? clips
                                                               : assignments;
            ui.text(x, y,
                    t("office.tests") + " " + std::to_string(int(w.scoreOffice(office, answer))) +
                        " / 100",
                    18, hex(0xe2b56c));
            y += 35;
            bool ready = o.total - o.remaining >= 600 && w.operational(o.facility) &&
                         w.near(selected, w.facilities[o.facility].pos, 3) && w.hasDevice(selected);
            bool enabled = ui.enabled;
            ui.enabled = enabled && ready;
            if (ui.button("submitOffice", {x, y, width - 56, 40},
                          t(ready ? "work.submit" : "office.work_required"), ready))
              act([&] { w.submitOffice(office, answer); });
            ui.enabled = enabled;
          }
        }
      } else if (r.role != Role::VideoEditor) {
        ui.wrap(x, y, width - 56, t("work.desc"), 20, hex(0xb2c1c0));
        if (ui.button("netToJobs", {x, y + 70, 260, 40}, t("nav.work"), true)) {
          phone = false;
          tab = 2;
          scrollOffset = 0;
        }
      } else {
        ui.text(x, y, t("work.timeline"), 13, hex(0xa5cbb9));
        y += 30;
        y = ui.wrap(x, y, width - 56, t("work.brief"), 16, hex(0xb2c1c0)) + 20;
        if (video == none || std::size_t(video) >= w.videos.size() ||
            w.videos[video].worker != selected) {
          if (ui.button("commission", {x, y, 330, 42}, t("work.commission"), true))
            act([&] { video = w.commissionVideo(selected, w.dormAccount, r.employer, 24000); });
        } else if (w.videos[video].submitted) {
          ui.text(x, y, t("work.submitted"), 20, hex(0xa7c4ac));
        } else {
          for (std::size_t i = 0; i < clips.size(); ++i) {
            ui.fill({x, y, width - 240, 42}, hex(i % 2 ? 0x243a40 : 0x203239), 6);
            ui.text(x + 15, y + 12, t("clip." + std::to_string(clips[i])), 16, hex(0xdce4db));
            if (ui.button("up" + std::to_string(i), {x + width - 218, y, 54, 42}, "↑") && i > 0)
              std::swap(clips[i], clips[i - 1]);
            if (ui.button("down" + std::to_string(i), {x + width - 154, y, 54, 42}, "↓") &&
                i + 1 < clips.size())
              std::swap(clips[i], clips[i + 1]);
            y += 50;
          }
          ui.text(x, y + 12,
                  t("work.preview") + " · " + std::to_string(int(w.scoreVideo(clips))) + " / 100",
                  17, hex(0xa5cbb9));
          if (ui.button("submitVideo", {x + width - 314, y, 258, 40}, t("work.submit"), true))
            act([&] { w.submitVideo(video, clips); });
        }
      }
    }
    ui.clip({0, 0, float(ui.width), float(ui.height)});
  }
  void distantFloors() {
    if (!world) {
      if (renderer->instanceCount())
        renderer->setInstances({});
      return;
    }
    if (instanceFloor == currentFloor && instanceRange == visibleFloors &&
        instanceView == firstPerson && instanceCutaway == cutaway && glfwGetTime() < nextInstances)
      return;
    instanceFloor = currentFloor;
    instanceRange = visibleFloors;
    instanceView = firstPerson;
    instanceCutaway = cutaway;
    nextInstances = glfwGetTime() + 1;
    int range = renderer->software ? std::min(visibleFloors, 24) : visibleFloors;
    int first = std::max(0, currentFloor - range / 3),
        last = std::min(world->config.floors, first + range);
    // Optional cutaway reveals the selected floor; full tower view keeps upper instances.
    if (!firstPerson && cutaway) {
      first = std::max(0, currentFloor - range + 1);
      last = currentFloor + 1;
    }
    int nearFirst = firstPerson ? std::max(0, currentFloor - 2) : currentFloor;
    int nearLast = currentFloor + (firstPerson ? 5 : 1);
    std::vector<Instance> instances;
    instances.reserve(std::size_t(range) * 400);
    auto add = [&](Vec3 p, Vec3 size, Color color) { instances.push_back({p, 0, size, 0, color}); };
    for (int floor = first; floor < last; ++floor) {
      if (floor >= nearFirst && floor < nearLast)
        continue;
      auto l = world->layout(floor);
      float y = floor * 3.f, hw = l.halfWidth(), hd = l.halfDepth(), sw = l.shaftWidth(),
            sd = l.shaftDepth();
      Color slab = hex(0x39494e), steel = hex(0x63757c);
      if (!l.deck) {
        for (int side : {-1, 1})
          for (int end : {-1, 1})
            add({side * (hw - .2f), y + 1.5f, end * (hd - .2f)}, {.4f, 3, .4f}, steel);
        for (int i = 0; i < l.ladderCount(); ++i) {
          auto p = l.ladder(i, y);
          for (float dx : {-.48f, .48f})
            add(p + Vec3{dx, 1.5f, 0}, {.08f, 3, .08f}, steel);
          for (float h = .25f; h < 3; h += .3f)
            add(p + Vec3{0, h, 0}, {1, .05f, .05f}, steel);
        }
        continue;
      }
      for (const auto &wall : world->interiorWalls)
        if (wall.active && wall.floor == floor)
          for (const auto &part : world->wallSegments(wall))
            add((part.min + part.max) * .5f, part.max - part.min, hex(0x64787a));
      for (const auto &piece : l.slabs(floor))
        add(piece.center, piece.size, slab);
      for (int side : {-1, 1})
        for (float h : {.6f, 1.f})
          add({side * (sw + .05f), y + h, 0}, {.06f, .06f, sd * 2}, steel);
      for (int a : {-1, 1})
        for (int b : {-1, 1})
          add({a * (hw - .2f), y + 1.5f, b * (hd - .2f)}, {.4f, 3, .4f}, steel);
      for (int i = 0; i < l.ladderCount(); ++i) {
        auto p = l.ladder(i, y);
        for (float dx : {-.48f, .48f})
          add(p + Vec3{dx, 1.5f, 0}, {.08f, 3, .08f}, hex(0xc9a45e));
        for (float step = .25f; step < 3; step += .3f)
          add(p + Vec3{0, step, 0}, {1, .05f, .05f}, steel);
      }
      auto found = world->floorHomes.find(floor);
      if (found != world->floorHomes.end())
        for (Id id : found->second) {
          const auto &home = world->housing[id];
          if (!home.active)
            continue;
          Vec3 p = world->homePosition(id);
          float tier = home.custom ? 0 : float(home.bed / l.tierSize());
          add(p + Vec3{0, .48f + tier * 1.25f, 0}, {1.05f, .14f, 2.45f}, palette(home.decor.linen));
          if (tier == 0)
            for (int side : {-1, 1})
              add(p + Vec3{side * .57f, 1.4f, 1.23f}, {.07f, 2.8f, .07f}, steel);
        }
      auto facilities = world->floorFacilities.find(floor);
      if (facilities != world->floorFacilities.end())
        for (Id id : facilities->second) {
          const auto &f = world->facilities[id];
          if (!f.demolished && f.kind != FacilityKind::Ramp && f.kind != FacilityKind::FreightLift)
            add(f.pos + Vec3{0, .6f, 0}, {1.9f, 1.2f, 1.3f}, hex(0x8c9488));
        }
    }
    renderer->setInstances(std::move(instances));
  }
  void buildScene(std::vector<Vertex> &v, float realTime) {
    auto ceilingLight = [&](Vec3 p, Color color) {
      std::size_t start = v.size();
      box(v, p, {2.1f, .06f, .18f}, color);
      for (std::size_t i = start; i < v.size(); ++i)
        v[i].material.x = -2;
    };
    float floorY = currentFloor * 3.f;
    float ceilingCut = !firstPerson && cutaway ? (currentFloor + 1) * 3.f : 1e9f;
    int first = firstPerson ? std::max(0, currentFloor - 2) : currentFloor;
    int last = world ? std::min(world->config.floors, currentFloor + (firstPerson ? 5 : 1)) : 10;
    std::map<std::pair<int, int>, const HousingUnit *> visibleHomes;
    if (world)
      for (const auto &h : world->housing)
        if (h.active && !h.custom && h.floor >= first && h.floor < last)
          visibleHomes[{h.floor, h.bed}] = &h;
    auto homeStyle = [&](int floor, int bed) {
      auto it = visibleHomes.find({floor, bed});
      return it == visibleHomes.end() ? HomeStyle{} : it->second->decor;
    };
    Color steel = hex(0x566b73), floor = hex(0x39494e), rail = hex(0x768b91);
    for (int f = first; f < last; ++f) {
      float y = f * 3.f;
      std::size_t slabStart = v.size();
      auto l = world ? world->layout(f) : Layout{};
      float hw = l.halfWidth(), hd = l.halfDepth(), sw = l.shaftWidth(), sd = l.shaftDepth();
      if (!l.deck) {
        for (int side : {-1, 1})
          for (int end : {-1, 1})
            box(v, {side * (hw - .2f), y + 1.5f, end * (hd - .2f)}, {.4f, 3, .4f}, steel);
        for (int i = 0; i < l.ladderCount(); ++i) {
          auto p = l.ladder(i, y);
          for (float dx : {-.48f, .48f})
            box(v, p + Vec3{dx, 1.5f, 0}, {.08f, 3, .08f}, steel);
          for (float h = .25f; h < 3; h += .3f)
            box(v, p + Vec3{0, h, 0}, {1, .05f, .05f}, steel);
        }
        continue;
      }
      for (const auto &piece : l.slabs(f))
        box(v, piece.center, piece.size, floor);
      for (std::size_t i = slabStart; i < v.size(); ++i) {
        auto &vertex = v[i];
        vertex.material = assets.meshes.at("concrete_floor_02").vertices.front().material;
        vertex.u = vertex.p.x * .08f;
        vertex.v = vertex.p.z * .08f;
        vertex.color = {.22f, .29f, .34f, 1};
        vertex.normalScale = .08f;
      }
      if (world)
        for (const auto &wall : world->interiorWalls)
          if (wall.active && wall.floor == f)
            for (const auto &part : world->wallSegments(wall))
              box(v, (part.min + part.max) * .5f, part.max - part.min, hex(0x64787a));
      for (int side : {-1, 1}) {
        for (float z = -l.shaftDepth(); z <= l.shaftDepth(); z += 4) {
          cylinder(v, {side * (l.shaftWidth() + .05f), y, z},
                   {side * (l.shaftWidth() + .05f), y + 1, z}, .045f, rail, 5);
        }
        bool liftGate =
            world && side == 1 &&
            std::any_of(world->lifts.begin(), world->lifts.end(), [&](const LiftCar &car) {
              auto &stops = world->facilities[car.facility].stops;
              return std::find(stops.begin(), stops.end(), f) != stops.end();
            });
        for (float height : {.5f, 1.f}) {
          if (liftGate)
            for (int half : {-1, 1})
              cylinder(v, {l.shaftWidth() + .05f, y + height, half * 1.6f},
                       {l.shaftWidth() + .05f, y + height, half * l.shaftDepth()}, .04f, rail, 6);
          else
            cylinder(v, {side * (l.shaftWidth() + .05f), y + height, -l.shaftDepth()},
                     {side * (l.shaftWidth() + .05f), y + height, l.shaftDepth()}, .04f, rail, 6);
        }
        if (liftGate) {
          box(v, {l.shaftWidth() + .05f, y + 1.6f, 1.5f}, {.16f, 3.2f, .16f}, hex(0xb08f56));
          box(v, {l.shaftWidth() + .05f, y + 1.6f, -1.5f}, {.16f, 3.2f, .16f}, hex(0xb08f56));
        }
        if (l.residential(f, world ? world->config.floors : 2000))
          for (int row = 0; row < l.banks() / 2; ++row)
            for (int k = 0; k < l.cells(); ++k) {
              int base = k * l.banks() + (side < 0 ? 0 : l.banks() / 2) + row;
              if (world && !visibleHomes.contains({f, base}) &&
                  !visibleHomes.contains({f, base + l.tierSize()}))
                continue;
              auto bp = l.bed(f, base);
              float x = bp.x, z = bp.z;
              if (firstPerson && world &&
                  length(Vec3{x, world->position(selected).y, z} - world->position(selected)) > 65)
                continue;
              Color linen = hex((k + row + f) % 3 == 0 ? 0x97a7aa
                                : (k + f) % 3 == 1     ? 0x798c95
                                                       : 0xb9b6a8);
              for (int level = 0; level < 2; ++level) {
                if (world && !visibleHomes.contains({f, level * l.tierSize() + base}))
                  continue;
                auto decor = homeStyle(f, level * l.tierSize() + base);
                linen = palette(decor.linen);
                Color frame = palette(decor.frame);
                float by = y + .48f + level * 1.25f;
                box(v, {x, by, z}, {1.05f, .14f, 2.45f}, linen);
                box(v, {x, by + .12f, z - .86f}, {.85f, .15f, .52f}, hex(0xd4d5c8));
                box(v, {x, by + .13f, z + .45f}, {1.0f, .12f, 1.1f}, palette(decor.blanket));
                if (decor.curtain)
                  box(v, {x + .62f, by + .52f, z}, {.025f, .8f, 2.35f}, palette(decor.blanket));
                cylinder(v, {x - .56f, by + .28f, z - 1.2f}, {x - .56f, by + .28f, z + 1.2f}, .033f,
                         frame, 5);
                cylinder(v, {x + .56f, by + .28f, z - 1.2f}, {x + .56f, by + .28f, z + 1.2f}, .033f,
                         frame, 5);
              }
              for (int a : {-1, 1})
                for (int b : {-1, 1})
                  cylinder(v, {x + a * .57f, y, z + b * 1.23f},
                           {x + a * .57f, y + 2.8f, z + b * 1.23f}, .045f, steel, 5);
              if ((k + f) % 3 == 0) {
                box(v, {x + 1.2f, y + .55f, z + .4f}, {.52f, 1.1f, .65f}, hex(0x52646b));
                box(v, {x + 1.2f, y + 1.16f, z + .4f}, {.5f, .1f, .6f}, hex(0xc0b5a1));
              }
              if (k % 4 == 1)
                box(v, {x, y + 2.35f, z + 1.26f}, {1.2f, .7f, .035f}, hex(0x51585e));
            }
      }
      for (float x = -84; x <= 84; x += 12)
        for (float z = -48; z <= 48; z += 12)
          if (std::abs(x) < hw && std::abs(z) < hd && l.supported({x, y, z}))
            ceilingLight({x, y + 2.86f, z}, hex(0xd7e7ec));
      if (!world || f + 1 < world->config.floors)
        for (int i = 0; i < l.ladderCount(); ++i) {
          auto p = l.ladder(i, y);
          for (float dx : {-.48f, .48f})
            cylinder(v, p + Vec3{dx, 0, 0}, p + Vec3{dx, 3, 0}, .055f, hex(0xc9a45e), 6);
          for (float sy = .25f; sy < 3; sy += .28f)
            cylinder(v, p + Vec3{-.48f, sy, 0}, p + Vec3{.48f, sy, 0}, .04f, rail, 6);
          auto landing = l.landing(i, y);
          box(v, landing + Vec3{0, .015f, 0}, {1.15f, .025f, 1.2f}, hex(0xb58f4d));
        }
    }
    for (int side : {-1, 1})
      for (int front : {-1, 1})
        box(v,
            {side * (world ? world->layout(currentFloor).halfWidth() - .2f : 255.8f),
             (first + last) * 1.5f,
             front * (world ? world->layout(currentFloor).halfDepth() - .2f : 255.8f)},
            {.4f, (last - first) * 3.f, .4f}, steel);
    if (!world)
      return;
    auto &w = *world;
    for (auto &f : w.facilities)
      if (!f.demolished && f.floor >= first && f.floor < last && f.kind != FacilityKind::Ramp &&
          f.kind != FacilityKind::FreightLift) {
        Color c = hex(f.kind == FacilityKind::Food        ? 0x9f8255
                      : f.kind == FacilityKind::Water     ? 0x557d8c
                      : f.kind == FacilityKind::Infirmary ? 0x859e8c
                                                          : 0x686f76);
        box(v, f.pos + Vec3{0, .55f, 0}, {1.8f, 1.1f, 1.2f}, c);
        box(v, f.pos + Vec3{0, 1.15f, 0}, {1.9f, .1f, 1.3f}, hex(0xb5b5a8));
        box(v, f.pos + Vec3{0, 2.3f, 0}, {1.9f, .5f, .2f}, c);
        for (int station = 0; station < std::min(16, f.stations); ++station) {
          Vec3 p = w.servicePosition(f.id, station);
          Color ceramic = hex(0xc9ccc1), dark = hex(0x293d44);
          if (f.kind == FacilityKind::Toilet) {
            box(v, p + Vec3{0, .23f, 0}, {.4f, .46f, .6f}, ceramic);
            sphere(v, p + Vec3{0, .48f, -.06f}, {.26f, .10f, .34f}, ceramic, 5, 8);
            box(v, p + Vec3{0, .62f, .25f}, {.48f, .65f, .18f}, ceramic);
            for (int side : {-1, 1})
              box(v, p + Vec3{side * .35f, 1.f, 0}, {.04f, 2, 1.1f}, hex(0x526971));
          } else if (f.kind == FacilityKind::Shower) {
            box(v, p + Vec3{0, .03f, 0}, {.67f, .06f, .74f}, ceramic);
            cylinder(v, p + Vec3{0, .2f, .3f}, p + Vec3{0, 2.2f, .3f}, .028f, rail, 5);
            box(v, p + Vec3{0, 2.18f, 0}, {.28f, .04f, .32f}, rail);
            bool active = std::any_of(f.serving.begin(), f.serving.end(),
                                      [&](const Service &a) { return a.station == station; });
            if (active)
              for (int drop = 0; drop < 7; ++drop) {
                float y = 2.1f - std::fmod(realTime * 1.8f + drop * .27f, 1.9f);
                cylinder(v, p + Vec3{(drop % 3 - 1) * .1f, y, 0},
                         p + Vec3{(drop % 3 - 1) * .1f, y - .15f, 0}, .007f, hex(0x9cc7cf), 4);
              }
          } else if (f.kind == FacilityKind::Washbasin) {
            Vec3 basin = p + Vec3{0, 0, -.65f};
            box(v, basin + Vec3{0, .42f, 0}, {.64f, .84f, .5f}, dark);
            box(v, basin + Vec3{0, .85f, 0}, {.65f, .08f, .53f}, ceramic);
            box(v, basin + Vec3{0, .9f, 0}, {.42f, .025f, .32f}, hex(0x577b88));
            cylinder(v, basin + Vec3{0, .9f, -.22f}, basin + Vec3{0, 1.15f, -.22f}, .022f, rail, 6);
            cylinder(v, basin + Vec3{0, 1.15f, -.22f}, basin + Vec3{0, 1.15f, -.02f}, .022f, rail,
                     6);
            box(v, basin + Vec3{.24f, .97f, .06f}, {.08f, .14f, .1f}, hex(0xb4ccab));
            bool washing =
                std::any_of(f.serving.begin(), f.serving.end(), [&](const Service &service) {
                  return service.station == station && service.until >= 0 && w.operational(f.id) &&
                         length(w.position(service.resident) - p) < .3f;
                });
            if (washing) {
              cylinder(v, basin + Vec3{.1f, .94f, .05f}, basin + Vec3{.1f, .97f, .05f}, .12f,
                       ceramic, 12);
              box(v, basin + Vec3{std::sin(realTime * 3) * .08f, .97f, -.05f}, {.1f, .04f, .07f},
                  hex(0xb7c485));
              cylinder(v, basin + Vec3{0, 1.13f, -.02f}, basin + Vec3{0, .91f, -.02f}, .009f,
                       hex(0x8ac3ce), 5);
              for (int bubble = 0; bubble < 4; ++bubble)
                sphere(v,
                       basin + Vec3{std::sin(realTime * 2 + bubble) * .13f, .93f,
                                    std::cos(realTime * 1.7f + bubble) * .09f},
                       {.025f, .015f, .025f}, hex(0xd8e4df), 3, 5);
            }
          } else if (f.kind == FacilityKind::Laundry) {
            Vec3 machine = p + Vec3{0, 0, -.65f};
            box(v, machine + Vec3{0, .45f, 0}, {.65f, .9f, .55f}, ceramic);
            cylinder(v, machine + Vec3{0, .45f, .27f}, machine + Vec3{0, .45f, .3f}, .21f, dark,
                     12);
            box(v, machine + Vec3{.2f, .79f, .29f}, {.11f, .06f, .02f}, hex(0x94b9a8));
          } else if (f.kind == FacilityKind::Food || f.kind == FacilityKind::Cafe) {
            box(v, p + Vec3{0, .42f, 0}, {.5f, .09f, .5f}, hex(0x827c67));
            box(v, p + Vec3{0, .74f, -.55f}, {.67f, .08f, .5f}, hex(0x9e9580));
            cylinder(v, p + Vec3{0, .8f, -.55f}, p + Vec3{0, .84f, -.55f}, .18f, ceramic, 10);
            sphere(v, p + Vec3{0, .85f, -.55f}, {.12f, .035f, .12f}, hex(0x927146), 4, 6);
          } else if (f.kind == FacilityKind::Infirmary) {
            box(v, p + Vec3{0, .5f, .35f}, {.62f, .12f, 1.45f}, hex(0x8cafaa));
            box(v, p + Vec3{0, .6f, -.23f}, {.51f, .12f, .3f}, ceramic);
          } else if (f.kind == FacilityKind::Arcade || f.kind == FacilityKind::Terminal ||
                     f.kind == FacilityKind::Library) {
            Vec3 desk = p + Vec3{0, 0, -.65f};
            box(v, desk + Vec3{0, .75f, 0}, {.65f, .12f, .5f}, hex(0x7b796a));
            box(v, desk + Vec3{0, 1.16f, -.15f}, {.52f, .36f, .05f}, dark);
            box(v, desk + Vec3{0, 1.16f, -.11f}, {.44f, .28f, .02f}, hex(0x80b4bc));
          }
        }
      }
    for (const auto &job : w.structuralWorks)
      if (job.floor == currentFloor && job.state != StructuralState::Completed &&
          job.state != StructuralState::Cancelled) {
        if (job.wall != none) {
          const auto &wall = w.interiorWalls[job.wall];
          cylinder(v, wall.a + Vec3{0, .05f, 0}, wall.b + Vec3{0, .05f, 0}, .04f, hex(0xddba6a), 5);
          for (auto p : {wall.a, wall.b})
            cylinder(v, p, p + Vec3{0, 2.7f, 0}, .035f, hex(0xddba6a), 5);
        }
        box(v, job.site + Vec3{0, .45f, -.8f}, {.9f, .9f, .08f}, hex(0xd4ad63));
      }
    if (currentFloor == 0 && w.config.layoutVersion > 1) {
      float front = w.layout(0).halfDepth();
      // A framed entrance hall with marked circulation aisles and service booths.
      for (int side : {-1, 1})
        for (float z = front - 92; z < front - 10; z += 14) {
          box(v, {side * 49.f, 1.5f, z}, {.35f, 3, .35f}, steel);
          box(v, {side * 25.f, 2.75f, z}, {48, .18f, .25f}, steel);
        }
      for (auto &f : w.facilities)
        if (!f.demolished && f.floor == 0 && f.kind != FacilityKind::Parking &&
            f.kind != FacilityKind::Dealership && f.kind != FacilityKind::Ramp &&
            f.kind != FacilityKind::FreightLift) {
          Color trim = f.kind == FacilityKind::Infirmary   ? hex(0x7ea89a)
                       : f.kind == FacilityKind::Reception ? hex(0xc6a76d)
                                                           : hex(0x65818a);
          box(v, f.pos + Vec3{0, 2.35f, -.5f}, {3.2f, .2f, 2.1f}, trim);
          for (int side : {-1, 1})
            box(v, f.pos + Vec3{side * 1.5f, 1.2f, -1.3f}, {.08f, 2.4f, .08f}, steel);
          if (f.kind == FacilityKind::Terminal || f.kind == FacilityKind::Reception) {
            box(v, f.pos + Vec3{.4f, 1.5f, 0}, {.55f, .36f, .06f}, hex(0x203e4b));
            box(v, f.pos + Vec3{.4f, 1.48f, .04f}, {.46f, .25f, .01f}, hex(0x7ea6a5));
          }
          if (f.kind == FacilityKind::FireStation) {
            box(v, f.pos + Vec3{1.5f, .8f, 0}, {.7f, 1.6f, .7f}, hex(0xa7422b));
            for (int n = 0; n < std::min(4, f.stock); ++n)
              cylinder(v, f.pos + Vec3{-1.3f + n * .45f, .2f, .2f},
                       f.pos + Vec3{-1.3f + n * .45f, .85f, .2f}, .13f, hex(0xbe4735), 8);
          }
          if (f.kind == FacilityKind::Water)
            for (int i = 0; i < 3; ++i)
              cylinder(v, f.pos + Vec3{-.6f + i * .6f, 1.2f, 0},
                       f.pos + Vec3{-.6f + i * .6f, 1.7f, 0}, .15f, hex(0x6092a2), 8);
        }
      for (int side : {-1, 1}) {
        assets.append(v, "painted_wooden_table", {side * 18.f, 0, front - 42}, 1, 0);
        assets.append(v, "plastic_monobloc_chair_01", {side * 18.f, 0, front - 40.5f}, 1, 3.14159f);
      }

      float z = w.layout(0).halfDepth() - 46;
      for (int side : {-1, 1})
        for (int row = 0; row < 3; ++row) {
          box(v, {side * 6.f, .4f, z - row * 3}, {3, .16f, .7f}, hex(0x8c8b79));
          box(v, {side * 6.f, .8f, z - row * 3 + .32f}, {3, .7f, .08f}, hex(0x566c72));
          for (int leg : {-1, 1})
            box(v, {side * 6.f + leg * 1.2f, .2f, z - row * 3}, {.08f, .4f, .6f}, steel);
        }
      float entrance = w.layout(0).entrance().z;
      for (int side : {-1, 1})
        box(v, {side * 7.f, 1.4f, entrance}, {.3f, 2.8f, 1.2f}, steel);
      box(v, {0, 2.7f, entrance}, {14, .45f, 1.2f}, hex(0x325965));
      for (float p = 36; p < entrance; p += 8) {
        box(v, {0, .012f, p}, {.12f, .018f, 3}, hex(0xa58f65));
        for (int side : {-1, 1})
          ceilingLight({side * 12.f, 2.86f, p}, hex(0xd7e7ec));
      }
    }
    for (auto &project : w.projects)
      if (project.newConstruction && project.state != ProjectState::Completed) {
        auto &f = w.facilities[project.facility];
        if (f.floor < first || f.floor >= last)
          continue;
        Color construction = hex(0xc8a35c);
        for (int a : {-1, 1})
          for (int b : {-1, 1}) {
            auto p = f.pos + Vec3{a * 2.f, 0, b * 1.6f};
            cylinder(v, p, p + Vec3{0, 2.6f, 0}, .05f, construction, 5);
            cylinder(v, p + Vec3{0, 2.5f, 0}, p + Vec3{-a * 4.f, 2.5f, 0}, .04f, rail, 5);
          }
        if (project.materialsDelivered) {
          float progress =
              std::clamp(float(1 - project.remaining / (6 * 3600 + f.stations * 1800)), 0.f, 1.f);
          box(v, f.pos + Vec3{0, .1f + progress * .45f, 0}, {1.8f, .2f + progress * .9f, 1.2f},
              hex(0x847c66));
        }
      }
    for (auto &project : w.expansions)
      if (!project.complete && project.floor >= first && project.floor < last) {
        for (int side : {-1, 1}) {
          auto p = project.site + Vec3{side * 2.f, 0, 0};
          cylinder(v, p, p + Vec3{0, 2.5f, 0}, .06f, hex(0xcba65c), 6);
          cylinder(v, p + Vec3{0, 1, 0}, p + Vec3{0, 1, -4}, .05f, hex(0xcba65c), 6);
        }
      }
    for (const auto &bay : w.parking)
      if (currentFloor < 3) {
        for (int side : {-1, 1})
          box(v, bay.pos + Vec3{side * 1.2f, .015f, 0}, {.05f, .025f, 4.4f},
              hex(bay.vehicle == none ? 0xb2bda4 : 0xb69261));
        box(v, bay.pos + Vec3{0, .015f, 2.2f}, {2.4f, .025f, .05f}, hex(0xb69261));
      }
    for (const auto &[fid, id] : w.activeFires) {
      const auto &fire = w.fires[id];
      Vec3 p = w.facilities[fid].pos;
      if (p.y >= ceilingCut || std::abs(p.y - floorY) > 12 || fire.contained)
        continue;
      std::size_t begin = v.size();
      float phase = float(glfwGetTime()) * 4;
      for (int n = 0; n < 7; ++n) {
        float height = (.5f + fire.heat * 1.6f) * (.8f + .2f * std::sin(phase * 2 + n));
        Vec3 at = p + Vec3{float(n % 3 - 1) * .55f, .7f + height * .5f, float(n / 3 - 1) * .4f};
        sphere(v, at, {.3f, height * .5f, .25f}, hex(n % 2 ? 0xf98322 : 0xffbf46), 4, 6);
      }
      for (std::size_t n = begin; n < v.size(); ++n)
        v[n].material.x = -2;
    }
    for (const auto &[actor, spray] : w.fireSprays) {
      Vec3 p = w.position(actor),
           target = w.facilities[w.fires[spray.fire].facility].pos + Vec3{0, 1.5f, 0};
      if (p.y >= ceilingCut || std::abs(p.y - floorY) > 12)
        continue;
      cylinder(v, p + Vec3{.2f, .6f, 0}, p + Vec3{.2f, 1.1f, 0}, .14f, hex(0xb24530), 8);
      for (int n = 0; n < 15; ++n) {
        float t = std::fmod(float(glfwGetTime()) * .8f + n / 15.f, 1.f);
        Vec3 at = p + Vec3{.2f, 1.3f, 0};
        at = at + (target - at) * t + Vec3{0, -.4f * t * t, 0};
        sphere(v, at, {.04f + t * .10f, .04f, .04f + t * .10f}, hex(0xcde3df), 3, 4);
      }
    }
    for (const auto &road : w.roads)
      if (road.active && road.from * 3.f < ceilingCut && road.to * 3.f > floorY - 12 &&
          road.from * 3.f < floorY + 15) {
        for (int floor : {road.from, road.to}) {
          if (std::abs(floor * 3.f - floorY) > 12 || floor * 3.f >= ceilingCut)
            continue;
          Vec3 terminal{road.pos.x + road.width / 2 + 1.3f, floor * 3.f,
                        road.pos.z + (road.kind == RoadKind::Ramp
                                          ? (floor == road.to ? road.length : 0.f)
                                          : -road.length / 2 - 1.f)};
          box(v, terminal + Vec3{0, .6f, 0}, {.35f, 1.2f, .3f}, hex(0x263e46));
          box(v, terminal + Vec3{0, 1.18f, -.16f}, {.28f, .24f, .02f}, hex(0x8bd1be));
        }
        if (road.kind == RoadKind::Ramp) {
          Vec3 a = road.pos + Vec3{-road.width / 2, .01f, 0},
               b = road.pos + Vec3{road.width / 2, .01f, 0},
               c = road.pos + Vec3{road.width / 2, 3.01f, road.length},
               d = road.pos + Vec3{-road.width / 2, 3.01f, road.length};
          Vec3 n = normalized(cross(d - a, b - a));
          for (auto p : {a, d, b, b, d, c})
            v.push_back({p, hex(0x657179), 0, 0, n});
          for (int side : {-1, 1}) {
            auto low = road.pos + Vec3{side * (road.width / 2 + .1f), 1, 0},
                 high = low + Vec3{0, 3, road.length};
            cylinder(v, low, high, .06f, hex(0xd6b873), 6);
            for (float z = 0; z <= road.length; z += 3)
              cylinder(v, road.pos + Vec3{side * (road.width / 2 + .1f), z / road.length * 3, z},
                       road.pos + Vec3{side * (road.width / 2 + .1f), z / road.length * 3 + 1, z},
                       .05f, rail, 5);
          }
        } else {
          float h = w.freightHeight(road.id);
          box(v, {road.pos.x, h - .12f, road.pos.z}, {road.width, .24f, road.length},
              hex(0xb29b62));
          for (int side : {-1, 1})
            for (int end : {-1, 1})
              cylinder(v,
                       {road.pos.x + side * (road.width / 2 + .15f), road.from * 3.f,
                        road.pos.z + end * road.length / 2},
                       {road.pos.x + side * (road.width / 2 + .15f), road.to * 3.f + 3,
                        road.pos.z + end * road.length / 2},
                       .12f, steel, 7);
          for (int side : {-1, 1})
            cylinder(v, {road.pos.x + side * road.width / 2, h + .9f, road.pos.z - road.length / 2},
                     {road.pos.x + side * road.width / 2, h + .9f, road.pos.z + road.length / 2},
                     .06f, hex(0xe6bf6d), 6);
        }
      }
    for (const auto &car : w.vehicles)
      if (std::abs(car.pos.y - floorY) < 7 && car.pos.y < ceilingCut) {
        std::vector<Vertex> mesh;
        bool small = car.kind <= VehicleKind::Scooter;
        Color paint = palette(car.look.primary);
        if (small) {
          cylinder(mesh, {0, .45f, -.65f}, {0, .45f, .65f}, .06f, paint, 6);
          cylinder(mesh, {0, .45f, -.65f}, {0, 1.05f, -.5f}, .05f, paint, 6);
          cylinder(mesh, {-.25f, 1.05f, -.5f}, {.25f, 1.05f, -.5f}, .04f, steel, 6);
          box(mesh, {0, .8f, .25f}, {.35f, .1f, .45f}, hex(0x28353c));
          for (float z : {-.65f, .65f})
            sphere(mesh, {0, .3f, z}, {.08f, .3f, .3f}, hex(0x222a30), 6, 8);
        } else {
          std::string asset = car.kind == VehicleKind::DeliveryVan ? "van" : "sedan";
          const auto &bounds = assets.meshes.at(asset);
          float scale = 3.5f / (bounds.maximum.z - bounds.minimum.z);
          assets.append(mesh, asset, {0, 0, 0}, scale, 0, paint);
        }
        if (car.towTruck) {
          cylinder(mesh, {0, .9f, 1}, {0, 2.3f, 1.8f}, .12f, hex(0xd4ad65), 8);
          cylinder(mesh, {0, 2.3f, 1.8f}, {0, 2.3f, 3.2f}, .1f, hex(0xd4ad65), 8);
        }
        Color trim = palette(car.look.secondary);
        if (car.look.accessory == 1) {
          box(mesh, {0, small ? 1.05f : 1.7f, .4f}, {small ? .6f : 1.3f, .12f, small ? .7f : 1.8f},
              trim);
          for (int side : {-1, 1})
            box(mesh, {side * (small ? .25f : .55f), small ? 1.1f : 1.77f, .4f},
                {.06f, .15f, small ? .7f : 1.8f}, trim);
        } else if (car.look.accessory == 2)
          box(mesh, {0, small ? 1.15f : 1.87f, .4f},
              {small ? .55f : 1.15f, .4f, small ? .6f : 1.55f}, trim);

        for (auto &vertex : mesh) {
          auto p = vertex.p;
          p = {p.x, p.y * std::cos(car.pitch) - p.z * std::sin(car.pitch),
               p.y * std::sin(car.pitch) + p.z * std::cos(car.pitch)};
          vertex.p = {car.pos.x + p.x * std::cos(car.yaw) - p.z * std::sin(car.yaw),
                      car.pos.y + p.y,
                      car.pos.z + p.x * std::sin(car.yaw) + p.z * std::cos(car.yaw)};
          auto n = vertex.normal;
          n = {n.x, n.y * std::cos(car.pitch) - n.z * std::sin(car.pitch),
               n.y * std::sin(car.pitch) + n.z * std::cos(car.pitch)};
          vertex.normal = {n.x * std::cos(car.yaw) - n.z * std::sin(car.yaw), n.y,
                           n.x * std::sin(car.yaw) + n.z * std::cos(car.yaw)};
          v.push_back(vertex);
        }
      }
    for (const auto &shot : w.shots)
      if (w.time - shot.time < 10) {
        cylinder(v, shot.from, shot.to, .015f, hex(0xf7d99b), 4);
        sphere(v, shot.from, {.09f, .07f, .12f}, hex(0xffe5b5), 4, 5);
      }
    for (const auto &shipment : w.freight)
      if (shipment.state == FreightState::Waiting || shipment.state == FreightState::Collecting) {
        if (shipment.readyAt > w.time) {
          Vec3 at = w.freightPosition(shipment.id);
          if (std::abs(at.y - floorY) < 15 && at.y < ceilingCut) {
            assets.append(v, "van", at, 1.4f, 1.5707963f, hex(0xa18d67));
            for (int side : {-1, 1})
              cylinder(v, at + Vec3{0, .1f, side * 1.4f}, at + Vec3{0, -.1f, side * 1.4f}, .35f,
                       hex(0x405965), 8);
          }
          continue;
        }

        auto &item = w.items[shipment.item];
        if (std::abs(item.pos.y - floorY) < 6 && item.pos.y < ceilingCut) {
          box(v, item.pos + Vec3{0, .4f, 0}, {.9f, .8f, .9f},
              hex(shipment.waste ? 0x354b42 : 0x9c8159));
          box(v, item.pos + Vec3{0, .81f, 0}, {.13f, .025f, .91f}, hex(0xc5bfa0));
        }
      }
    for (const auto &lift : w.lifts) {
      float y = w.liftHeight(lift.id);
      auto &facility = w.facilities[lift.facility];
      if (std::abs(y - floorY) < 12 && y < ceilingCut) {
        float half = w.layout(currentFloor).shaftWidth() - .3f;
        box(v, {0, y - .12f, 0}, {half * 2, .24f, 3.1f}, hex(0x567078));
        box(v, {0, y + 2.65f, 0}, {half * 2, .14f, 3.1f}, hex(0x546e77));
        for (int side : {-1, 1})
          box(v, {0, y + 1.25f, side * 1.5f}, {half * 2, 2.5f, .12f}, hex(0x425d66));
        box(v, {-half, y + 1.25f, 0}, {.12f, 2.5f, 3.f}, hex(0x425d66));
        bool open = !lift.moving;
        auto [door, inserted] = liftDoorAnimation.try_emplace(lift.id, open ? 1.f : 0.f);
        if (!inserted)
          door->second += ((open ? 1.f : 0.f) - door->second) * (1 - std::exp(-sceneDelta * 8));
        float gap = door->second * 1.35f;
        for (int side : {-1, 1})
          box(v, {half, y + 1.25f, side * (.75f + gap)}, {.1f, 2.5f, 1.5f}, hex(0x94a5a6));
        if (open)
          box(v, {half + 1.2f, y - .04f, 0}, {2.4f, .08f, 2.7f}, hex(0xa69365));
        for (int side : {-1, 1})
          cylinder(v, {side * 3.f, y + 2.8f, 0},
                   {side * 3.f, std::max(y + 3, facility.stops.back() * 3.f + 4), 0}, .035f, rail,
                   5);
      }
    }
    for (const auto &a : w.ambulances) {
      Vec3 p = w.ambulancePosition(a.id);
      if (std::abs(p.y - floorY) > 9 || p.y >= ceilingCut)
        continue;
      assets.append(v, "ambulance", p, 1.5f, 1.5707963f);
      float flash = .4f + .6f * std::abs(std::sin(realTime * 8));
      box(v, p + Vec3{0, 2.5f, 0}, {.9f, .15f, .25f}, {flash, .12f, .12f, 1});
      for (int side : {-1, 1}) {
        cylinder(v, p + Vec3{0, .3f, side * 1.6f}, p + Vec3{0, .1f, side * 1.6f}, .36f,
                 hex(0x394f59), 8);
        if (a.state != RescueState::Idle)
          box(v, p + Vec3{0, -.2f, side * 1.6f}, {.35f, .5f, .35f}, hex(0x73c9d1, .7f));
      }
    }
    for (const auto &b : w.remains)
      if (!b.cremated && !b.lost && b.ambulance == none) {
        Vec3 p = b.carrier == none ? b.pos : w.position(b.carrier) + Vec3{0, .7f, .9f};
        if (std::abs(p.y - floorY) > 6 || p.y >= ceilingCut)
          continue;
        box(v, p + Vec3{0, .18f, 0}, {.58f, .26f, 1.7f}, hex(0x7a8481));
        sphere(v, p + Vec3{0, .23f, -.77f}, {.17f, .17f, .18f}, hex(0xa6a08e));
        if (b.carrier != none)
          box(v, p + Vec3{0, .02f, 0}, {.7f, .08f, 2}, hex(0x6b919d));
      }
    float detailY = currentFloor * 3.f;
    for (int side : {-1, 1}) {

      assets.append(v, "painted_wooden_table", {side * 10.f, detailY, 7.5f}, 1, 0);
      assets.append(v, "plastic_monobloc_chair_01", {side * 10.f, detailY, 9}, 1, 3.14159265f);
    }
    for (Id home = 0; home < Id(w.housing.size()); ++home) {
      const auto &h = w.housing[home];
      if (!h.active || !h.custom || h.floor < first || h.floor >= last)
        continue;
      Vec3 p = w.homePosition(home);
      box(v, p + Vec3{0, .5f, 0}, {1.05f, .16f, 2.45f}, palette(h.decor.linen));
      box(v, p + Vec3{0, .65f, -.86f}, {.85f, .15f, .52f}, hex(0xd4d5c8));
      box(v, p + Vec3{0, .63f, .4f}, {1.f, .12f, 1.1f}, palette(h.decor.blanket));
      for (int a : {-1, 1})
        for (int b : {-1, 1})
          cylinder(v, p + Vec3{a * .57f, 0, b * 1.23f}, p + Vec3{a * .57f, .9f, b * 1.23f}, .045f,
                   palette(h.decor.frame), 5);
    }
    Id selectedHome = w.residentHomes[selected];
    if (selectedHome != none) {
      const auto &h = w.housing[selectedHome];
      if (h.floor == currentFloor) {
        Vec3 hp = w.homePosition(selectedHome);
        assets.append(v, "old_bed_frame", hp, 1, 0, palette(h.decor.frame));
        float &open = closetAnimation[selectedHome];
        open += (float(h.decor.closetOpen) - open) * (1 - std::exp(-sceneDelta * 7));
        assets.append(v, "painted_wooden_cabinet_02", hp + Vec3{2.2f, 0, .2f}, .9f, 0,
                      palette(h.decor.closet), open);
      }
    }
    assets.append(v, "metal_trash_can", {-18, detailY, 9.5f}, 1, 0);
    for (auto &facility : w.facilities)
      if (facility.floor == currentFloor && facility.kind == FacilityKind::Garbage) {
        assets.append(v, "metal_trash_can", facility.pos + Vec3{2, 0, 0}, 1.1f, 0);
        for (int n = 0; n < std::min(8, facility.waste / 10); ++n)
          sphere(v, facility.pos + Vec3{2 + float(n % 3) * .5f, .25f, 1 + float(n / 3) * .5f},
                 {.3f, .28f, .3f}, hex(0x3c4940), 5, 6);
      }
    for (auto &facility : w.facilities)
      if (facility.floor == currentFloor && facility.kind == FacilityKind::Infirmary)
        assets.append(v, "wheelchair_01", facility.pos + Vec3{0, 0, 2}, 1, 0);
    int count = 0;
    std::vector<Id> visible;
    for (const auto &r : w.residents)
      if (r.alive && r.present && w.position(r.id).y < ceilingCut &&
          std::abs(w.position(r.id).y - floorY) < 6.1f)
        visible.push_back(r.id);
    Vec3 viewCenter =
        firstPerson ? w.position(selected) : Vec3{cameraFocus.x, floorY, cameraFocus.z};
    std::sort(visible.begin(), visible.end(), [&](Id a, Id b) {
      return length(w.position(a) - viewCenter) < length(w.position(b) - viewCenter);
    });
    int detailedCharacters = 0, fullCharacters = 0;
    for (Id visibleId : visible) {
      auto &r = w.residents[visibleId];
      if (!r.alive || !r.present || r.ambulance != none || (firstPerson && r.id == selected))
        continue;
      Vec3 p = w.position(r.id);
      if (std::abs(p.y - floorY) > 6.1f || ++count > 640)
        continue;
      float phase =
          float(std::fmod(w.time / (1440 / w.config.dayMinutes), 1000.0)) * 5 + r.id * 1.7f;
      auto personal = w.personalUses.find(r.id);
      bool usingPersonal = personal != w.personalUses.end() && r.activity == Activity::Using;
      bool resting = r.activity == Activity::Sleeping ||
                     (usingPersonal && w.items[personal->second.item].kind == ItemKind::Fan);
      float personalTier = 0;
      if (usingPersonal && w.residentHomes[r.id] != none) {
        const auto &h = w.housing[w.residentHomes[r.id]];
        if (!h.custom)
          personalTier = float((h.bed / w.layout(r.home).tierSize()) % 2) * 1.25f;
      }
      if (usingPersonal) {
        auto kind = w.items[personal->second.item].kind;
        Vec3 prop = p + Vec3{.8f, .55f + personalTier, 0};
        if (kind == ItemKind::Fan) {
          box(v, prop, {.3f, .06f, .28f}, hex(0xb0bcb6));
          cylinder(v, prop, prop + Vec3{0, .3f, 0}, .025f, hex(0x667c82), 6);
          auto hub = prop + Vec3{0, .38f, 0};
          cylinder(v, hub, hub + Vec3{0, 0, .08f}, .18f, hex(0x667c82), 12);
          for (int blade = 0; blade < 3; ++blade) {
            float angle = phase * 4 + blade * 2.0944f;
            cylinder(v, hub + Vec3{0, 0, -.015f},
                     hub + Vec3{std::cos(angle) * .14f, std::sin(angle) * .14f, -.015f}, .03f,
                     hex(0xa6c6be), 5);
          }
        } else if (kind == ItemKind::RiceCooker) {
          box(v, prop, {.34f, .28f, .32f}, hex(0xd1d8cc));
          box(v, prop + Vec3{0, .17f, 0}, {.36f, .05f, .34f}, hex(0x506a6d));
          for (int steam = 0; steam < 3; ++steam)
            sphere(v, prop + Vec3{std::sin(phase + steam) * .04f, .3f + steam * .09f, 0},
                   {.035f, .06f, .035f}, hex(0xc2d3c9), 3, 5);
        }
      }
      bool moving = (!r.route.empty() && length(p - r.route.back().p) > .03f) || r.ladder >= 0;
      bool climbing = w.climbing(r.id);
      auto [blend, newBlend] = walkBlend.try_emplace(r.id, moving ? 1.f : 0.f);
      if (!paused && !newBlend)
        blend->second += ((moving ? 1.f : 0.f) - blend->second) * (1 - std::exp(-sceneDelta * 10));
      float stride = std::sin(phase) * .23f * blend->second;
      Appearance outfit = r.outfit == none ? Appearance{} : w.items[r.outfit].look;
      Color shirt = palette(outfit.primary), pants = palette(outfit.secondary),
            skin = hex(r.id % 2 ? 0xc19c7e : 0xb48c73);
      if (r.outfit != none) {
        float clean = .55f + .45f * w.items[r.outfit].cleanliness;
        shirt.r *= clean;
        shirt.g *= clean;
        shirt.b *= clean;
      }
      bool rescueGear =
          r.role == Role::Firefighter && (w.onShift(r.id) || r.task == TaskKind::FireResponse);
      if (rescueGear) {
        shirt = hex(0xb7793b);
        pants = hex(0x343c43);
      }
      Vec3 direction{0, 0, -1};
      if (r.task == TaskKind::FireResponse && r.target >= 0 &&
          std::size_t(r.target) < w.fires.size() && r.activity == Activity::Working)
        direction = w.facilities[w.fires[r.target].facility].pos - p;
      else if (r.opponent != none && r.activity == Activity::Fighting)
        direction = w.position(r.opponent) - p;
      else if (!r.route.empty()) {
        std::size_t next = 1;
        if (w.crowd.contains(r.id))
          next = w.crowd.at(r.id).next;
        else
          while (next < r.route.size() && r.route[next].time < w.time)
            ++next;
        if (next < r.route.size())
          direction = r.route[next].p - p;
      }
      if (r.activity == Activity::Using && r.queuedAt != none &&
          w.facilities[r.queuedAt].kind == FacilityKind::Washbasin)
        direction = {0, 0, -1};
      if (climbing)
        direction = {0, 0, p.z > 0 ? 1.f : -1.f};
      float targetFacing = std::atan2(direction.x, -direction.z);
      auto [heading, inserted] = characterHeading.try_emplace(r.id, targetFacing);
      if (!inserted && !paused) {
        float difference = std::remainder(targetFacing - heading->second, 6.2831853f);
        heading->second += difference * (1 - std::exp(-sceneDelta * 12));
      }
      float facing = heading->second, co = std::cos(facing), si = std::sin(facing);
      CharacterPose pose = moving ? CharacterPose::Walk : CharacterPose::Idle;
      if (resting || r.carriedBy != none)
        pose = CharacterPose::Sleep;
      else if (climbing)
        pose = CharacterPose::Climb;
      else if (w.looseBodies.contains(r.id))
        pose = CharacterPose::Fall;
      else if (r.surrendered)
        pose = CharacterPose::Surrender;
      else if (r.activity == Activity::Fighting) {
        pose = CharacterPose::Fight;
        auto victim = w.looseBodies.find(r.opponent);
        float since = float((w.time - (r.nextHit - 20)) / (1440 / w.config.dayMinutes));
        if (victim != w.looseBodies.end() && victim->second.pusher == r.id && since >= 0 &&
            since < .5f)
          pose = CharacterPose::Push;
      } else if (r.carrying != none)
        pose = CharacterPose::Carry;
      else if (!moving && usingPersonal)
        pose = CharacterPose::Eat;
      else if (!moving && r.activity == Activity::Working)
        pose = CharacterPose::Work;
      else if (!moving && r.activity == Activity::Using && r.queuedAt != none) {
        auto kind = w.facilities[r.queuedAt].kind;
        if (kind == FacilityKind::Infirmary)
          pose = CharacterPose::Sleep;
        else if (kind == FacilityKind::Food || kind == FacilityKind::Cafe)
          pose = CharacterPose::Eat;
        else if (kind == FacilityKind::Toilet || kind == FacilityKind::Cinema ||
                 kind == FacilityKind::Terminal)
          pose = CharacterPose::Sit;
        else
          pose = CharacterPose::Work;
      }
      auto [transition, newPose] = characterPoses.try_emplace(r.id, PoseTransition{pose, pose, 1});
      if (transition->second.current != pose) {
        transition->second.previous = transition->second.current;
        transition->second.current = pose;
        transition->second.blend = 0;
      }
      if (!paused)
        transition->second.blend = std::min(1.f, transition->second.blend + sceneDelta * 3);
      if (length(p - viewCenter) < 22 && detailedCharacters++ < (renderer->software ? 8 : 64)) {
        CharacterStyle style;
        style.pattern = outfit.style;
        style.shirt = shirt;
        style.trousers = pants;
        style.skin = {.86f + float(r.id % 4) * .05f, .86f + float(r.id % 3) * .04f,
                      .85f + float(r.id % 5) * .03f, 1};
        style.height =
            (1.68f + float(r.id % 9) * .018f) * (r.age < 18 ? .55f + r.age * .025f : 1.f);
        if (usingPersonal && !resting)
          p.y += personalTier;
        if (resting) {
          Id home = w.residentHomes[r.id];
          if (home != none && !w.housing[home].custom)
            p.y += float((w.housing[home].bed / w.layout(r.home).tierSize()) % 2) * 1.25f;
          facing = 0;
        }
        if (r.carriedBy != none) {
          box(v, p, {.68f, .08f, 1.95f}, hex(0x73939f));
          p.y -= .49f;
        }
        float animationPhase =
            pose == CharacterPose::Push
                ? 3.1415927f * std::clamp(float((w.time - (r.nextHit - 20)) /
                                                (1440 / w.config.dayMinutes) / .5),
                                          0.f, 1.f)
            : pose == CharacterPose::Fight
                ? (w.hasGun(r.id)
                       ? 3.1415927f
                       : 3.1415927f * (1 - std::clamp(float((r.nextHit - w.time) / 20), 0.f, 1.f)))
                : phase;
        std::string characterMesh =
            (r.sex < 0 ? r.id % 2 : r.sex) ? "resident_Female" : "resident_Male";
        if (renderer->software || length(p - viewCenter) > 5 || fullCharacters++ >= 4)
          characterMesh += "_lod";
        if (gpuSkinning && !renderer->software) {
          auto request = assets.characterSkin(
              characterMesh, std::uint32_t(v.size()), p, facing, transition->second.previous, pose,
              transition->second.blend, animationPhase, blend->second, style);
          v.resize(v.size() + request.count);
          frameSkins.push_back(std::move(request));
        } else {
          assets.appendCharacter(v, characterMesh, p, facing, transition->second.previous, pose,
                                 transition->second.blend, animationPhase, blend->second, style);
        }
        std::size_t accessories = v.size();
        if (usingPersonal && !resting)
          sphere(v, p + Vec3{.2f, .94f, -.28f}, {.13f, .07f, .13f}, hex(0xd9dfd0), 4, 8);
        if (rescueGear && pose != CharacterPose::Sleep && pose != CharacterPose::Sit &&
            pose != CharacterPose::Eat) {
          box(v, p + Vec3{0, style.height - .01f, 0}, {.36f, .14f, .37f}, hex(0xcba65c));
          cylinder(v, p + Vec3{0, .8f, .26f}, p + Vec3{0, 1.35f, .26f}, .14f, hex(0xc5b994), 8);
        }
        if (r.carrying != none)
          box(v, p + Vec3{0, 1.f, -.4f}, {.55f, .4f, .4f},
              hex(w.items[r.carrying].kind == ItemKind::Waste ? 0x354b42 : 0xa08357));
        if (r.activity == Activity::Fighting && w.hasGun(r.id)) {
          box(v, p + Vec3{.28f, 1.4f, -.45f}, {.09f, .12f, .32f}, hex(0x25343c));
          box(v, p + Vec3{.28f, 1.31f, -.37f}, {.08f, .16f, .1f}, hex(0x3b413e));
        }
        if (pose != CharacterPose::Sleep && pose != CharacterPose::Sit &&
            pose != CharacterPose::Eat) {
          if (r.role == Role::Warden || outfit.accessory == 1)
            box(v, p + Vec3{0, style.height - .03f, 0}, {.32f, .1f, .33f}, hex(0x364f60));
          if (r.role == Role::Courier || outfit.accessory == 2)
            box(v, p + Vec3{0, 1.1f, .24f}, {.38f, .5f, .2f}, palette(outfit.secondary));
        }
        for (std::size_t i = accessories; i < v.size(); ++i) {
          auto q = v[i].p - p, n = v[i].normal;
          v[i].p = p + Vec3{q.x * co - q.z * si, q.y, q.x * si + q.z * co};
          v[i].normal = {n.x * co - n.z * si, n.y, n.x * si + n.z * co};
        }
        continue;
      }
      if (r.carriedBy != none) {
        box(v, p, {.68f, .08f, 1.95f}, hex(0x73939f));
        box(v, p + Vec3{0, .16f, 0}, {.4f, .25f, 1.2f}, shirt);
        sphere(v, p + Vec3{0, .2f, -.74f}, {.17f, .19f, .17f}, skin);
        continue;
      }
      if (resting) {
        Id home = w.residentHomes[r.id];
        if (home != none)
          p.y += w.housing[home].custom
                     ? 0
                     : float((w.housing[home].bed / w.layout(r.home).tierSize()) % 2) * 1.25f;
        sphere(v, p + Vec3{0, .69f, -.7f}, {.18f, .19f, .18f}, skin);
        box(v, p + Vec3{0, .63f, 0}, {.43f, .26f, 1.05f}, shirt);
        continue;
      }
      if (r.activity == Activity::Using && r.queuedAt != none && !moving) {
        auto kind = w.facilities[r.queuedAt].kind;
        if (kind == FacilityKind::Infirmary) {
          sphere(v, p + Vec3{0, .75f, -.2f}, {.16f, .2f, .17f}, skin);
          box(v, p + Vec3{0, .69f, .35f}, {.42f, .25f, 1.f}, shirt);
          continue;
        }
        if (kind == FacilityKind::Toilet || kind == FacilityKind::Food ||
            kind == FacilityKind::Cafe || kind == FacilityKind::Cinema) {
          sphere(v, p + Vec3{0, 1.2f, 0}, {.16f, .2f, .17f}, skin);
          cylinder(v, p + Vec3{0, .5f, 0}, p + Vec3{0, .99f, 0}, .21f, shirt, 8);
          for (int side : {-1, 1}) {
            cylinder(v, p + Vec3{side * .13f, .5f, 0}, p + Vec3{side * .16f, .5f, -.35f}, .09f,
                     pants, 6);
            cylinder(v, p + Vec3{side * .16f, .5f, -.35f}, p + Vec3{side * .16f, .1f, -.36f}, .08f,
                     pants, 6);
          }
          float lift = (kind == FacilityKind::Food || kind == FacilityKind::Cafe)
                           ? (.5f + .5f * std::sin(phase)) * .3f
                           : 0;
          cylinder(v, p + Vec3{.24f, .95f, 0}, p + Vec3{.14f, .65f + lift, -.25f}, .07f, shirt, 6);
          cylinder(v, p + Vec3{-.24f, .95f, 0}, p + Vec3{-.2f, .62f, -.32f}, .07f, shirt, 6);
          continue;
        }
      }
      std::size_t bodyStart = v.size();
      sphere(v, p + Vec3{0, 1.58f, 0}, {.16f, .21f, .17f}, skin);
      sphere(v, p + Vec3{0, 1.72f, .01f}, {.164f, .105f, .17f}, hex(0x252829), 4, 7);
      if (length(p - viewCenter) < 22) {
        for (int side : {-1, 1}) {
          sphere(v, p + Vec3{side * .06f, 1.62f, -.157f}, {.035f, .018f, .013f}, hex(0xd8d5c7), 3,
                 5);
          sphere(v, p + Vec3{side * .06f, 1.62f, -.169f}, {.013f, .013f, .006f}, hex(0x343333), 3,
                 5);
        }
        sphere(v, p + Vec3{0, 1.57f, -.17f}, {.025f, .035f, .026f}, skin, 3, 5);
        box(v, p + Vec3{0, 1.51f, -.154f}, {.063f, .014f, .012f}, hex(0x855c4e));
      }
      cylinder(v, p + Vec3{0, .8f, 0}, p + Vec3{0, 1.35f, 0}, .22f, shirt, 8);
      for (int side : {-1, 1}) {
        Vec3 hip{side * .13f, .8f, 0};
        Vec3 foot{side * .14f, climbing ? .35f - side * stride : .1f,
                  climbing ? -.24f : -side * stride};
        Vec3 knee{side * .15f, climbing ? .62f - side * stride * .4f : .46f,
                  climbing ? -.36f : -side * stride * .55f - .045f};
        cylinder(v, p + hip, p + knee, .095f, pants, 6);
        cylinder(v, p + knee, p + foot, .08f, pants, 6);
      }
      cylinder(v, p + Vec3{-.25f, 1.3f, 0},
               p + Vec3{-.29f, climbing ? 1.65f + stride : .83f, climbing ? -.25f : -stride}, .07f,
               shirt, 6);
      cylinder(v, p + Vec3{.25f, 1.3f, 0},
               p + Vec3{.29f,
                        climbing                           ? 1.65f - stride
                        : r.activity == Activity::Fighting ? 1.4f
                                                           : .83f,
                        climbing ? -.25f
                        : r.activity == Activity::Fighting
                            ? -.35f - .45f * std::sin(std::clamp(float((r.nextHit - w.time) / 20),
                                                                 0.f, 1.f) *
                                                      3.14159f)
                            : stride},
               .07f, shirt, 6);
      if (outfit.style == 1) {
        box(v, p + Vec3{0, 1.12f, -.21f}, {.065f, .49f, .03f}, palette(outfit.secondary));
        box(v, p + Vec3{-.12f, 1.f, -.21f}, {.12f, .12f, .04f}, palette(outfit.secondary));
      } else if (outfit.style == 2)
        sphere(v, p + Vec3{0, 1.43f, .07f}, {.24f, .2f, .23f}, shirt, 5, 8);
      if (outfit.accessory == 1) {
        box(v, p + Vec3{0, 1.75f, 0}, {.35f, .09f, .36f}, pants);
        box(v, p + Vec3{0, 1.72f, -.19f}, {.33f, .04f, .22f}, pants);
      }
      if (outfit.accessory == 2)
        box(v, p + Vec3{0, 1.13f, .25f}, {.36f, .48f, .2f}, palette(outfit.secondary));
      if (r.activity == Activity::Fighting && w.hasGun(r.id)) {
        box(v, p + Vec3{.28f, 1.4f, -.45f}, {.09f, .12f, .32f}, hex(0x25343c));
        box(v, p + Vec3{.28f, 1.31f, -.37f}, {.08f, .16f, .1f}, hex(0x3b413e));
      }
      if (r.activity == Activity::Using)
        box(v, p + Vec3{.25f, 1.3f, -.3f}, {.13f, .2f, .13f}, hex(0xe0d8b8));
      if (r.activity == Activity::Working && (r.role == Role::Builder || r.role == Role::Repairer))
        cylinder(v, p + Vec3{.28f, 1.1f, -.15f}, p + Vec3{.28f, 1.5f + std::sin(phase) * .2f, -.4f},
                 .04f, hex(0xbca073), 6);
      if (r.carrying != none)
        box(v, p + Vec3{0, 1.f, -.4f}, {.55f, .4f, .4f},
            hex(w.items[r.carrying].kind == ItemKind::Waste ? 0x354b42 : 0xa08357));
      if (r.role == Role::Courier)
        box(v, p + Vec3{0, 1.1f, .26f}, {.43f, .56f, .2f}, hex(0xab7954));
      for (int side : {-1, 1})
        box(v,
            p + Vec3{side * .14f, climbing ? .3f - side * stride : .07f,
                     climbing ? -.32f : -.08f - side * stride},
            {.18f, .14f, .32f}, hex(0x293334));
      if (r.role == Role::Warden) {
        box(v, p + Vec3{0, 1.8f, 0}, {.36f, .1f, .36f}, hex(0x364f60));
        box(v, p + Vec3{-.1f, 1.25f, -.22f}, {.07f, .09f, .02f}, hex(0xd9bd7b));
      }
      for (std::size_t i = bodyStart; i < v.size(); ++i) {
        auto q = v[i].p - p, n = v[i].normal;
        v[i].p = p + Vec3{q.x * co - q.z * si, q.y, q.x * si + q.z * co};
        v[i].normal = {n.x * co - n.z * si, n.y, n.x * si + n.z * co};
      }
    }
  }
  Mat4 camera() {
    Vec3 target{cameraFocus.x, currentFloor * 3.f + 2 + observerHeight, cameraFocus.z}, eye;
    if (firstPerson && world) {
      const auto &r = world->residents[selected];
      Vec3 offset{0, 1.6f, 0};
      bool personal = world->personalUses.contains(selected) && r.activity == Activity::Using;
      bool rest =
          personal && world->items[world->personalUses.at(selected).item].kind == ItemKind::Fan;
      if (r.activity == Activity::Sleeping || rest) {
        offset = {0, .8f, -.7f};
        Id home = world->residentHomes[selected];
        if (home != none && !world->housing[home].custom)
          offset.y +=
              float((world->housing[home].bed / world->layout(r.home).tierSize()) % 2) * 1.25f;
      } else if (personal) {
        offset = {0, 1.12f, 0};
        Id home = world->residentHomes[selected];
        if (home != none && !world->housing[home].custom)
          offset.y +=
              float((world->housing[home].bed / world->layout(r.home).tierSize()) % 2) * 1.25f;
      } else if (r.carriedBy != none)
        offset = {0, .3f, -.7f};
      else if (r.activity == Activity::Using && r.queuedAt != none) {
        auto kind = world->facilities[r.queuedAt].kind;
        if (kind == FacilityKind::Infirmary)
          offset = {0, .8f, -.7f};
        else if (kind == FacilityKind::Toilet || kind == FacilityKind::Food ||
                 kind == FacilityKind::Cafe || kind == FacilityKind::Cinema ||
                 kind == FacilityKind::Terminal)
          offset.y = 1.15f;
      }
      if (cameraResident != selected) {
        cameraResident = selected;
        firstPersonOffset = offset;
      } else
        firstPersonOffset =
            firstPersonOffset + (offset - firstPersonOffset) * (1 - std::exp(-sceneDelta * 8));
      eye = world->position(selected) + firstPersonOffset;
      target = eye + Vec3{std::sin(playerYaw) * std::cos(playerPitch), std::sin(playerPitch),
                          -std::cos(playerYaw) * std::cos(playerPitch)};
    } else {
      eye = observerEye(target, orbit, pitch, distance);
    }
    cameraEye = eye;
    return mul(
        observerProjection(float(ui.width), float(ui.height), firstPerson ? 0 : 460 * drawerSlide),
        lookAt(eye, target));
  }
  void endingPanel() {
    float width = std::min(600.f, float(ui.width) - 48), height = 310;
    float x = (ui.width - width) / 2, y = (ui.height - height) / 2;
    ui.enabled = true;
    ui.fill({0, 0, float(ui.width), float(ui.height)}, hex(0x080d14, .83f));
    ui.fill({x, y, width, height}, hex(0x18212a), 12);
    ui.text(x + 28, y + 27, t("end.title"), 28, hex(0xe5c299));
    ui.wrap(x + 28, y + 80, width - 56,
            t(world->endReason == "population-empty" ? "end.empty"
              : world->endReason == "player-died"    ? "end.died"
                                                     : "end.departed"),
            18, hex(0xc5d2cf));
    if (ui.button("endingSlotPrevious", {x + 28, y + 155, 36, 32}, "−"))
      slot = std::max(1, slot - 1);
    ui.text(x + 78, y + 164, t("save.slot") + " " + std::to_string(slot), 15, hex(0xd2ddd5));
    if (ui.button("endingSlotNext", {x + width - 64, y + 155, 36, 32}, "+"))
      slot = std::min(8, slot + 1);
    if (ui.button("endingLoad", {x + 28, y + 201, width - 56, 36}, t("save.browser_load"), true))
      openSaves(false);
    ui.enabled = true;
    if (ui.button("endingMenu", {x + 28, y + 249, width - 56, 34}, t("nav.menu"))) {
      menu = true;
      helpOpen = false;
      phone = false;
      drawerOpen = false;
      setFirstPerson(false);
      ui.blur();
    }
  }
  void frame(float dt, const std::filesystem::path &capture) {
    sceneDelta = dt;
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    double rawMouseX = mx, rawMouseY = my;
    int logicalW, logicalH;
    glfwGetWindowSize(window, &logicalW, &logicalH);
    mx *= double(width) / std::max(1, logicalW);
    my *= double(height) / std::max(1, logicalH);
    float uiScale = std::min(
        {1.f, float(width) / (menu ? 1440.f : 1200.f), float(height) / (menu ? 900.f : 800.f)});
    uiScale = std::max(.1f, uiScale);
    width = int(width / uiScale);
    height = int(height / uiScale);
    mx /= uiScale;
    my /= uiScale;
    bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool click = down && !previousMouse;
    previousMouse = down;
    ui.begin(width, height, mx, my, click, dt);
    if (!menu && world && !world->gameOver && !phone && !helpOpen && !drawerOpen && !savesOpen &&
        !importOpen && !ui.typing() && !firstPerson) {
      if (mx < width - 20 && my > 85 && my < height - 145) {
        distance = std::clamp(distance * std::exp(-float(ui.scroll) * .12f), 2.5f, 1800.f);
        bool dragging = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (dragging && observerDragging) {
          orbit = std::remainder(orbit + float(rawMouseX - observerMouseX) * .006f, 6.2831853f);
          pitch = std::clamp(pitch + float(rawMouseY - observerMouseY) * .003f, -1.35f, 1.35f);
        }
        observerDragging = dragging;
      }
      float pan = std::max(10.f, distance * .7f) * dt *
                  (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 3.f : 1.f);
      Vec3 forward{-std::sin(orbit), 0, -std::cos(orbit)},
          right{std::cos(orbit), 0, -std::sin(orbit)}, move{};
      if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        move = move + forward;
      if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        move = move - forward;
      if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        move = move - right;
      if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        move = move + right;
      cameraFocus = cameraFocus + normalized(move) * pan;
      float vertical = float(glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) -
                       float(glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS);
      if (world && vertical != 0)
        moveObserverHeight(currentFloor, observerHeight, vertical * pan, world->config.floors);
    } else {
      observerDragging = false;
    }
    observerMouseX = rawMouseX;
    observerMouseY = rawMouseY;
    if (!menu && world && !world->gameOver && !phone && !helpOpen && !drawerOpen && !savesOpen &&
        !importOpen && firstPerson && click && mx < width - 20 && my > 85 && my < height - 145) {
      mouseLocked = true;
      fireHeld = true;
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      glfwGetCursorPos(window, &oldMx, &oldMy);
    }
    bool saveEscape = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (importOpen && saveEscape && !savesEscapeHeld) {
      closeImporter();
      escapeHeld = true;
    } else if (savesOpen && saveEscape && !savesEscapeHeld) {
      closeSaves();
      escapeHeld = true;
    }
    savesEscapeHeld = saveEscape;
    if (!savesOpen && !importOpen)
      update(dt);
    auto &scene = sceneGeometry;
    double sceneStart = glfwGetTime();
    scene.clear();
    frameSkins.clear();
    if (scene.capacity() < 250000)
      scene.reserve(250000);
    buildScene(scene, float(glfwGetTime()));
    if (!frameSkins.empty() && !renderer->canSkinVertices(scene.size())) {
      gpuSkinning = false;
      frameSkins.clear();
      scene.clear();
      buildScene(scene, float(glfwGetTime()));
      notify(t("graphics.skinning_fallback"));
    }
    renderer->setSkinRequests(std::move(frameSkins));
    sceneBuildSeconds += glfwGetTime() - sceneStart;
    distantFloors();
    if (world && world->gameOver && !menu) {
      phone = false;
      helpOpen = false;
      drawerOpen = false;
      drawerSlide = 0;
      mouseLocked = false;
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    ui.enabled =
        !savesOpen && !importOpen && !phone && !helpOpen && (!world || !world->gameOver || menu);
    if (menu) {
      header();
      newWorld();
    } else {
      worldLabels();
      viewportHUD();
      drawerSlide += ((drawerOpen ? 1.f : 0.f) - drawerSlide) * std::min(1.f, dt * 18);
      if (drawerSlide > .005f) {
        bool enabled = ui.enabled;
        ui.enabled = enabled && drawerOpen;
        sidebar();
        if (drawerOpen && tab == 6)
          toolbar();
        ui.enabled = enabled;
      }
      if (world->gameOver && !savesOpen && !importOpen)
        endingPanel();
    }
    if (savesOpen)
      saveBrowser();
    if (importOpen)
      importPanel();
    if (phone)
      network();
    if (helpOpen && !menu)
      helpPanel();
    if (!toast.empty() && glfwGetTime() < toastUntil) {
      float toastWidth = std::min(float(width) - 48, 720.f);
      float lines =
          std::clamp(std::ceil(ui.textWidth(toast, 14) / (toastWidth - 28)) + 1, 1.f, 5.f);
      float toastHeight = 24 + lines * 19, toastY = float(height) - 145 - toastHeight;
      ui.fill({24, toastY, toastWidth, toastHeight}, hex(0x614f37, .97f), 6);
      ui.wrap(38, toastY + 12, toastWidth - 28, toast, 14, hex(0xf0e4cc));
    }
    ui.end();
    if (font.dirty) {
      renderer->updateFont(font.atlas, font.atlasSize);
      font.dirty = false;
    }
    for (auto &vertex : ui.vertices) {
      vertex.p.x *= uiScale;
      vertex.p.y *= uiScale;
    }
    for (auto &batch : ui.batches) {
      batch.clip.x *= uiScale;
      batch.clip.y *= uiScale;
      batch.clip.w *= uiScale;
      batch.clip.h *= uiScale;
    }
    auto cameraMatrix = camera();
    std::vector<std::pair<float, std::array<float, 8>>> sources;
    if (world && !menu)
      for (const auto &[fid, id] : world->activeFires) {
        const auto &fire = world->fires[id];
        auto p = world->facilities[fid].pos;
        if (length(p - cameraEye) < 160)
          sources.push_back({length(p - cameraEye),
                             {p.x, p.y + 1.3f, p.z, fire.contained ? 0.f : fire.heat, p.x, p.y, p.z,
                              fire.smoke}});
      }
    std::sort(sources.begin(), sources.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<std::array<float, 8>> lights;
    for (std::size_t n = 0; n < std::min(std::size_t(16), sources.size()); ++n)
      lights.push_back(sources[n].second);
    renderer->setEmergencyLights(std::move(lights));
    renderer->draw(scene, ui.vertices, ui.batches, cameraMatrix, cameraEye, capture);
  }
};
int runApplication(int argc, char **argv) {
  bool softwareRequested = false;
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--software")
      softwareRequested = true;
#ifdef __linux__
  if (softwareRequested) {
    const char *icd = "/usr/share/vulkan/icd.d/lvp_icd.json";
    if (!std::filesystem::exists(icd))
      throw std::runtime_error("Install Mesa's lavapipe Vulkan driver for --software");
    setenv("VK_DRIVER_FILES", icd, 1);
    // Avoid a libwayland shutdown failure in this Mesa/GLFW combination.
    if (std::getenv("DISPLAY") && glfwPlatformSupported(GLFW_PLATFORM_X11))
      glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  }
#endif
  if (!glfwInit())
    throw std::runtime_error("Cannot initialize desktop window system");
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  int initialWidth = 1440, initialHeight = 900;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--width")
      initialWidth = std::clamp(std::stoi(argv[i + 1]), 1000, 7680);
    if (std::string(argv[i]) == "--height")
      initialHeight = std::clamp(std::stoi(argv[i + 1]), 720, 4320);
  }
  GLFWwindow *window = glfwCreateWindow(initialWidth, initialHeight,
                                        "MEGA DORMITORY — 100,000 Lives", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    throw std::runtime_error("Cannot create game window");
  }
  glfwSetWindowSizeLimits(window, 1000, 720, GLFW_DONT_CARE, GLFW_DONT_CARE);
  int result = 0;
  try {
    Application app(window);
    int frames = 0, maxFrames = 0;
    bool demo = false, computeTest = false, skinTest = false, animate = false, playAutonomy = false,
         playConstruction = false, playVehicles = false;
    Id playCourier = none, constructionJob = none;
    int vehicleStage = 0, vehicleFinishFrame = 0;
    bool rampCaptured = false, liftCaptured = false;
    int constructionStage = 0, constructionFinishedFrame = 0;
    int playStage = 0, playHandoverFrame = 0;
    std::string panel, browserImport;
    float demoYaw = 0, demoPitch = 0, demoScroll = 0;
    bool demoLook = false;
    float fixedStep = 0;
    std::filesystem::path capture;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      auto value = [&]() {
        if (i + 1 >= argc)
          throw std::runtime_error("Missing argument value");
        return std::string(argv[++i]);
      };
      if (arg == "--playtest-autonomy") {
        playAutonomy = demo = animate = true;
        panel = "autonomy";
        app.config.population = 600;
        app.config.floors = 20;
      } else if (arg == "--playtest-construction") {
        playConstruction = demo = animate = true;
        panel = "construction";
        app.config.population = 600;
        app.config.floors = 20;
        app.config.mode = Mode::Cheat;
        app.config.accidentRate = 0;
      } else if (arg == "--playtest-vehicles") {
        playVehicles = demo = animate = true;
        panel = "traffic";
        app.config.population = 600;
        app.config.floors = 20;
        app.config.mode = Mode::Cheat;
        app.config.accidentRate = 0;
      } else if (arg == "--demo")
        demo = true;
      else if (arg == "--animate")
        animate = true;
      else if (arg == "--frames")
        maxFrames = std::stoi(value());
      else if (arg == "--fixed-step")
        fixedStep = std::clamp(std::stof(value()), .001f, .05f);
      else if (arg == "--menu-scroll")
        demoScroll = std::max(0.f, std::stof(value()));
      else if (arg == "--look-yaw") {
        demoYaw = std::stof(value());
        demoLook = true;
      } else if (arg == "--look-pitch") {
        demoPitch = std::stof(value());
        demoLook = true;
      } else if (arg == "--capture")
        capture = value();
      else if (arg == "--lang")
        app.locale.language = value();
      else if (arg == "--floor-width") {
        app.floorWidth = value();
        app.config.floorWidth = std::stoi(app.floorWidth);
      } else if (arg == "--floor-depth") {
        app.floorDepth = value();
        app.config.floorDepth = std::stoi(app.floorDepth);
      } else if (arg == "--lobby-width") {
        app.lobbyWidth = value();
        app.config.lobbyWidth = std::stoi(app.lobbyWidth);
      } else if (arg == "--lobby-depth") {
        app.lobbyDepth = value();
        app.config.lobbyDepth = std::stoi(app.lobbyDepth);
      } else if (arg == "--population") {
        app.config.population = std::stoi(value());
        app.population = std::to_string(app.config.population);
      } else if (arg == "--floors") {
        app.config.floors = std::stoi(value());
        app.floors = std::to_string(app.config.floors);
      } else if (arg == "--panel")
        panel = value();
      else if (arg == "--import-browser")
        browserImport = value();
      else if (arg == "--saves")
        app.saveRoot = value();
      else if (arg == "--width" || arg == "--height") {
        (void)value();
      } else if (arg == "--compute-test")
        computeTest = true;
      else if (arg == "--skin-test")
        skinTest = true;
      else if (arg == "--cpu-skinning")
        app.gpuSkinning = false;
      else if (arg == "--skin-budget")
        app.renderer->skinVertexBudget = std::stoull(value());
    }
    if (playAutonomy || playConstruction || playVehicles) {
      std::cout << std::unitbuf;
      if (!browserImport.empty())
        throw std::runtime_error("Playtests use an isolated generated world");
      if (maxFrames == 0)
        maxFrames = playVehicles ? 7200 : playConstruction ? 1800 : 1200;
      if (fixedStep == 0)
        fixedStep = 1.f / 30;
    }
    if (!browserImport.empty()) {
      app.setWorld(World::importBrowser(browserImport));
      app.importOpen = app.importSummary = true;
      app.helpOpen = false;
      app.paused = true;
    }
    if (demo && browserImport.empty()) {
      app.setWorld(World::generate(app.config));
      app.paused = true;
      app.helpOpen = panel == "help";
      app.drawerOpen = panel == "work" || panel == "facilities" || panel == "housing" ||
                       panel == "manager" || panel == "settings" || panel == "life";
      app.drawerSlide = app.drawerOpen ? 1.f : 0.f;
      if (panel == "settings")
        app.tab = 6;
      if (panel == "traffic") {
        auto &w = *app.world;
        auto &r = w.residents[w.player];
        w.pay(0, r.account, 5000000, "playtest-starting-capital");
        r.automatic = false;
        w.leaveQueue(r.id);
        w.clockOut(r.id);
        r.pos = w.position(r.id);
        r.route.clear();
        ++r.routeVersion;
        r.activity = Activity::Idle;
        r.task = TaskKind::None;
        r.hunger = r.thirst = r.bladder = .1f;
        r.hygiene = r.energy = 1;
        r.needsAt = w.time;
        auto dealer = w.vehicles[2].dealer;
        w.travel(r.id, w.facilities[dealer].pos + Vec3{1.5f, 0, -2.f}, TaskKind::None, none);
        app.setFirstPerson(false);
        app.drawerOpen = false;
        app.phone = false;
        app.currentFloor = r.home;
        app.cameraFocus = r.pos;
        app.distance = 13;
        app.pitch = .3f;
        app.observerHeight = 0;
        std::cout << "VEHICLE PLAYTEST: walking from actual home to buy a car at the dealership\n";
      } else if (panel == "construction") {
        auto &w = *app.world;
        constructionJob = w.proposePartition(w.player, 0, {70, 0, 160}, {78, 0, 160}, true);
        auto &job = w.structuralWorks[constructionJob];
        if (w.accounts[job.payer].balance < job.budget)
          w.pay(0, job.payer, job.budget - w.accounts[job.payer].balance,
                "playtest-starting-capital");
        for (Id worker : {2, 3}) {
          auto &r = w.residents[worker];
          w.leaveQueue(worker);
          w.clockOut(worker);
          r.pos = w.position(worker);
          r.route.clear();
          ++r.routeVersion;
          r.activity = Activity::Idle;
          r.task = TaskKind::None;
          r.age = std::max(18, r.age);
          w.chooseJob(worker, Role::Builder, job.workshop);
          r.routine.shiftStart = 0;
          r.routine.shiftHours = 24;
          r.routine.daysOff = 0;
          r.automatic = true;
          r.hunger = r.thirst = r.bladder = .1f;
          r.hygiene = r.energy = 1;
          r.needsAt = w.time;
          w.schedule(w.time + 1, EventKind::Decide, worker);
        }
        Id team = w.createConstructionTeam(2, job.workshop, 2);
        w.recruitConstructionWorker(2, team, 3);
        w.fundStructuralWork(w.player, constructionJob);
        w.assignStructuralTeam(w.player, constructionJob, team);
        app.buildX = "70";
        app.buildZ = "160";
        app.selectedTeam = team;
        app.structuralOpen = true;
        app.drawerOpen = true;
        app.drawerSlide = 1;
        app.tab = 4;
        app.setFirstPerson(false);
        app.selected = 2;
        app.currentFloor = 0;
        app.cameraFocus = w.position(2);
        app.observerHeight = -.5f;
        app.distance = 11;
        app.pitch = .28f;
        app.orbit = .6f;
        std::cout << "CONSTRUCTION PLAYTEST: funded partition; two hired builders start at their "
                     "actual homes\n";
      } else if (panel == "autonomy") {
        auto &w = *app.world;
        w.config.mode = Mode::Cheat;
        Id shop = none, vendor = none;
        for (const auto &f : w.facilities)
          if (f.floor == 0 && f.kind == FacilityKind::Shop) {
            shop = f.id;
            break;
          }
        for (const auto &r : w.residents) {
          if (r.id > 1 && r.role == Role::Unemployed && vendor == none)
            vendor = r.id;
          if (r.role == Role::Courier && playCourier == none)
            playCourier = r.id;
        }
        if (shop == none || vendor == none || playCourier == none)
          throw std::runtime_error("Autonomy playtest needs a shop, a vendor and a courier");
        w.setFacilityHours(w.facilities[shop].manager, shop, 0, 24, true);
        for (auto [id, role] :
             {std::pair{vendor, Role::Vendor}, std::pair{playCourier, Role::Courier}}) {
          w.leaveQueue(id);
          w.chooseJob(id, role, shop);
          auto &worker = w.residents[id];
          worker.routine.shiftStart = 0;
          worker.routine.shiftHours = 24;
          worker.routine.daysOff = 0;
          worker.pos =
              w.accessible(w.facilities[shop].pos + Vec3{id == vendor ? -1.6f : 2.5f, 0, -1.6f});
          worker.route.clear();
          ++worker.routeVersion;
          worker.activity = Activity::Idle;
          worker.task = TaskKind::None;
          worker.hunger = worker.thirst = worker.bladder = .1f;
          worker.energy = worker.hygiene = 1;
          worker.needsAt = w.time;
          worker.automatic = true;
          if (role == Role::Vendor)
            w.clockIn(id);
        }
        auto &resident = w.residents[w.player];
        w.leaveQueue(w.player);
        w.clockOut(w.player);
        resident.role = Role::Unemployed;
        resident.jobSite = resident.employer = none;
        resident.pos = w.accessible(w.facilities[shop].pos + Vec3{0, 0, 8});
        resident.route.clear();
        ++resident.routeVersion;
        resident.activity = Activity::Idle;
        resident.task = TaskKind::None;
        resident.hunger = .4f;
        resident.thirst = resident.bladder = .1f;
        resident.energy = resident.hygiene = 1;
        resident.needsAt = w.time;
        resident.automatic = false;
        auto &plan = w.residentPlans[w.player];
        plan.nextShopping = w.time;
        plan.nextCareer = plan.nextBusiness = plan.nextListing = w.time + 3 * 86400;
        w.crowd.clear();
        w.travel(w.player, w.facilities[shop].pos + Vec3{0, 0, 3}, TaskKind::None, none);
        app.setFirstPerson(false);
        app.currentFloor = 0;
        app.cameraFocus = resident.pos;
        app.observerHeight = -.8f;
        app.distance = 10;
        app.pitch = .18f;
        app.orbit = .6f;
        app.householdOpen = true;
        app.drawerOpen = true;
        app.drawerSlide = 1;
        app.tab = 0;
        if (demoScroll == 0)
          demoScroll = 570;
        std::cout << "PLAYTEST: walking to the shop; household funds="
                  << w.accounts[resident.account].balance << "\n";
      } else if (panel == "washbasin" || panel == "washing") {
        auto &w = *app.world;
        w.config.mode = Mode::Cheat;
        auto &r = w.residents[w.player];
        w.leaveQueue(w.player);
        w.clockOut(w.player);
        r.route.clear();
        ++r.routeVersion;
        r.automatic = false;
        r.activity = Activity::Idle;
        Id fid = w.nearestAt({0, 0, 0}, FacilityKind::Washbasin);
        auto &basin = w.facilities[fid];
        r.pos = w.accessible(basin.pos + Vec3{0, 0, 2.4f});
        w.crowd.clear();
        for (auto kind : {ItemKind::Tableware, ItemKind::RiceCooker}) {
          Item item;
          item.id = Id(w.items.size());
          item.kind = kind;
          item.owner = r.account;
          item.carrier = r.id;
          item.cleanliness = .15f;
          item.pos = r.pos;
          r.items.push_back(item.id);
          w.items.push_back(item);
        }
        w.joinQueue(r.id, fid);
        if (basin.serving.empty())
          throw std::runtime_error("Washbasin demo failed to reserve a station");
        w.advance(std::max(0., basin.serving.front().until - w.time - basin.duration) + 1, 100000);
        app.setFirstPerson(false);
        app.currentFloor = basin.floor;
        app.cameraFocus = w.position(r.id);
        app.observerHeight = -.9f;
        app.distance = 4.5f;
        app.pitch = .15f;
        app.orbit = .7f;
        app.drawerOpen = panel == "washing";
        app.drawerSlide = app.drawerOpen ? 1.f : 0.f;
        app.tab = 0;
        if (panel == "washing" && demoScroll == 0)
          demoScroll = 570;
      } else if (panel == "personal") {
        auto &w = *app.world;
        w.leaveQueue(w.player);
        auto &r = w.residents[w.player];
        for (Id home = 0; home < Id(w.housing.size()); ++home)
          if (w.housing[home].floor == r.home && w.housing[home].active &&
              w.housing[home].tenant == none &&
              w.housing[home].bed >= w.layout(r.home).tierSize()) {
            w.rentHome(r.id, home);
            break;
          }
        r.route.clear();
        r.activity = Activity::Idle;
        r.pos = w.homePosition(w.residentHomes[w.player]);
        w.crowd.erase(w.player);
        Id cooker = none;
        for (auto kind :
             {ItemKind::Fan, ItemKind::Tableware, ItemKind::RiceCooker, ItemKind::Food}) {
          Item item;
          item.id = Id(w.items.size());
          item.kind = kind;
          item.owner = r.account;
          item.carrier = r.id;
          item.pos = r.pos;
          if (kind == ItemKind::RiceCooker)
            cooker = item.id;
          r.items.push_back(item.id);
          w.items.push_back(item);
        }
        w.usePersonalItem(r.id, cooker);
        app.setFirstPerson(false);
        app.currentFloor = r.home;
        app.cameraFocus = r.pos + Vec3{-4, 0, 0};
        app.distance = 10;
        app.pitch = .08f;
        app.drawerOpen = true;
        app.drawerSlide = 1;
      } else if (panel == "falling") {
        auto &w = *app.world;
        w.config.mode = Mode::Cheat;
        Id victim = w.player == 1 ? 2 : 1;
        for (Id id : {w.player, victim}) {
          w.leaveQueue(id);
          w.clockOut(id);
          auto &r = w.residents[id];
          r.route.clear();
          ++r.routeVersion;
          r.activity = Activity::Idle;
          r.task = TaskKind::None;
          r.automatic = false;
          r.energy = 1;
        }
        w.residents[w.player].pos = {0, 9, 22};
        w.residents[victim].pos = {0, 9, 20.5f};
        w.residents[w.player].strength = 100;
        w.residents[victim].strength = 10;
        w.crowd.clear();
        w.shove(w.player, victim);
        w.advance(.2 * (1440 / w.config.dayMinutes), 100000);
        app.setFirstPerson(false);
        app.currentFloor = 3;
        app.cameraFocus = {3, 9, 25};
        app.distance = 8;
        app.orbit = .45f;
        app.pitch = .32f;
        app.drawerOpen = false;
      } else if (panel == "observer") {
        app.setFirstPerson(false);
        auto &w = *app.world;
        Id resident = std::min<Id>(1, Id(w.residents.size()) - 1);
        app.cameraFocus = w.position(resident);
        app.currentFloor = std::clamp(int(app.cameraFocus.y / 3), 0, w.config.floors - 1);
        app.distance = 5;
        app.pitch = -.2f;
      } else if (panel == "import") {
        app.openImporter();
      } else if (panel == "saves" || panel == "save-browser") {
        app.openSaves(panel == "saves");
      } else if (panel == "lobby") {
        app.setFirstPerson(true);
        app.playerYaw = -.35f;
        app.playerPitch = -.08f;
      } else if (panel == "ladder") {
        app.setFirstPerson(true);
        app.world->residents[0].pos = app.world->layout().landing(0, 0);
        app.playerYaw = 3.14159f;
      } else if (panel == "tower") {
        app.setFirstPerson(false);
        app.cutaway = false;
        app.currentFloor = 25;
        app.cameraFocus = {};
        app.distance = 750;
        app.pitch = .2f;
      } else if (panel == "atrium") {
        app.setFirstPerson(true);
        app.world->residents[0].pos = {0, 33, 0};
        app.currentFloor = 11;
        app.playerYaw = 1.5707963f;
        app.playerPitch = 1.15f;
      } else if (panel == "characters") {
        app.setFirstPerson(true);
        auto &w = *app.world;
        app.currentFloor = 0;
        app.playerYaw = 0;
        app.playerPitch = -.12f;
        w.residents[0].pos = {0, 0, 6};
        w.residents[0].route.clear();
        for (Id id = 1; id < w.residents.size(); ++id) {
          auto &r = w.residents[id];
          r.route.clear();
          r.activity = Activity::Idle;
          r.pos = {float(int(id % 5) - 2) * 1.15f, 0, 2 - float(id / 5) * 2};
          app.characterHeading[id] = 3.1415927f;
        }
      } else if (panel == "crowd") {
        app.setFirstPerson(true);
        auto &w = *app.world;
        Id kitchen = w.nearestAt({0, 30, 0}, FacilityKind::Food);
        w.residents[0].pos = w.accessible(w.facilities[kitchen].pos + Vec3{0, 0, 9});
        app.currentFloor = w.facilities[kitchen].floor;
        app.playerYaw = 0;
      } else if (panel == "assets") {
        app.setFirstPerson(true);
        app.world->residents[app.selected].pos = {-11, 0, 13};
        app.playerYaw = .45f;
        app.playerPitch = -.18f;
      } else if (panel == "style") {
        app.phone = true;
        app.networkTab = 5;
      } else if (panel == "closet") {
        auto &h = app.world->housing[app.world->residentHomes[0]];
        h.decor.closetOpen = true;
        app.setFirstPerson(true);
        app.world->residents[0].pos = {-2.5f, 0, -6.5f};
        app.playerYaw = -.6f;
        app.playerPitch = -.14f;
      } else if (panel == "firstperson")
        app.setFirstPerson(true);
      else if (panel == "parking") {
        app.phone = true;
        app.networkTab = 4;
        app.networkScroll = 760;
      } else if (panel == "programming" || panel == "editing" || panel == "administration" ||
                 panel == "administration-done") {
        app.phone = true;
        app.networkTab = 3;
        auto &w = *app.world;
        Id f = w.nearest(0, FacilityKind::Office);
        w.chooseJob(0,
                    panel == "programming" ? Role::Programmer
                    : panel == "editing"   ? Role::VideoEditor
                                           : Role::Administrator,
                    f);
        w.residents[0].routine.shiftStart = 0;
        w.residents[0].routine.shiftHours = 24;
        w.residents[0].pos = w.facilities[f].pos + Vec3{0, 0, -1.6f};
        w.residents[0].route.clear();
        if (panel == "administration" || panel == "administration-done") {
          w.facilities[w.nearest(0, FacilityKind::Food)].stock = 20;
          w.facilities[w.nearest(0, FacilityKind::Water)].condition = .35f;
        }
        app.office = w.officeOrder(0);
        if (panel == "administration-done") {
          w.residents[0].automatic = false;
          w.clockIn(0);
          w.officeTick();
          for (int n = 0; n < 11; ++n) {
            w.time += 60;
            w.officeTick();
          }
          std::vector<int> answer;
          for (int slot = 0; slot < 4; ++slot)
            answer.push_back((slot + w.officeOrders[app.office].brief) % 4);
          w.submitOffice(app.office, answer);
        }
      } else if (panel == "medical") {
        app.phone = true;
        app.networkTab = 6;
      } else if (panel == "network")
        app.phone = true;
      else if (panel == "ending") {
        app.world->config.mode = Mode::Survival;
        app.world->kill(app.world->player);
      } else if (panel == "fire" || panel == "emergency" || panel == "response" ||
                 panel == "emergency-error") {
        auto &w = *app.world;
        Id f = w.nearest(w.player, FacilityKind::Food);
        Id fire = w.ignite(f, .65f);
        w.fires[fire].smoke = .65f;
        if (panel == "response") {
          Id station = w.nearest(2, FacilityKind::FireStation);
          w.chooseJob(2, Role::Firefighter, station);
          auto &crew = w.residents[2];
          crew.routine.shiftStart = 0;
          crew.routine.shiftHours = 24;
          crew.routine.daysOff = 0;
          crew.pos = w.accessible(w.facilities[station].pos + Vec3{0, 0, -1.6f});
          crew.route.clear();
          w.requestFireResponse(w.dormOwner, fire);
          w.assignFirefighter(2, fire);
          crew.pos = w.fireApproach(fire);
          crew.route.clear();
          w.beginExtinguish(2, fire);
        }
        w.residents[w.player].pos = w.accessible(
            w.facilities[f].pos + (panel == "response" ? Vec3{8, 0, -8} : Vec3{0, 0, 12}));
        w.residents[w.player].route.clear();
        app.currentFloor = w.facilities[f].floor;
        app.setFirstPerson(true);
        app.playerYaw = panel == "response" ? -2.3561945f : 0;
        if (panel == "emergency-error")
          app.act([&] { w.beginExtinguish(w.player, fire); });
        app.drawerOpen = panel == "emergency" || panel == "emergency-error";
        app.drawerSlide = app.drawerOpen ? 1 : 0;
        app.tab = 0;
        app.emergencyScreen = true;
      } else if (panel == "companies") {
        app.drawerOpen = true;
        app.drawerSlide = 1;
        app.tab = 2;
        app.companyScreen = true;
        auto &w = *app.world;
        w.pay(0, w.residents[w.player].account, 100000, "demo-company-capital");
        app.selectedCompany = w.foundCompany(w.player, app.t("company.default_name"), 60000);
        Id f = w.nearest(w.player, FacilityKind::Office);
        w.leaseWorkspace(w.player, app.selectedCompany, f);
      } else if (panel == "work")
        app.tab = 2;
      else if (panel == "facilities")
        app.tab = 1;
      else if (panel == "housing")
        app.tab = 3;
      else if (panel == "manager")
        app.tab = 4;
      else if (panel == "vehicles") {
        app.phone = true;
        app.networkTab = 4;
      }
    }
    if (demo && demoLook) {
      app.setFirstPerson(true);
      app.playerYaw = demoYaw;
      app.playerPitch = demoPitch;
    }
    if (demo && animate) {
      app.paused = false;
      if (playAutonomy || playConstruction || playVehicles)
        app.speed = 6;
    }
    if (computeTest) {
      std::vector<std::array<float, 4>> needs(100000);
      for (std::size_t i = 0; i < needs.size(); ++i)
        needs[i] = {float(i % 100) / 100, .4f, .7f, .5f};
      auto result = app.renderer->pressure(needs);
      float error = 0;
      for (std::size_t i = 0; i < needs.size(); ++i) {
        auto n = needs[i];
        float expected =
            std::clamp(n[0] * .3f + n[1] * .35f + (1 - n[2]) * .15f + (1 - n[3]) * .2f, 0.f, 1.f);
        error = std::max(error, std::abs(result[i] - expected));
      }
      std::cout << "Compute parity: " << needs.size() << " residents; maximum error " << error
                << '\n';
      if (error > 1e-5f)
        throw std::runtime_error("GPU/CPU mismatch");
    }
    if (skinTest) {
      float maximum = 0;
      std::size_t checked = 0;
      for (const char *name :
           {"resident_Male", "resident_Female", "resident_Male_lod", "resident_Female_lod"})
        for (int pose = 0; pose < int(CharacterPose::Count); ++pose)
          for (int pattern = 0; pattern < 3; ++pattern) {
            CharacterStyle style;
            style.pattern = pattern;
            style.height = 1.53f;
            style.shirt = {.25f, .6f, .3f, 1};
            style.trousers = {.2f, .15f, .5f, 1};
            style.skin = {.9f, .8f, .7f, 1};
            auto previous = CharacterPose((pose + int(CharacterPose::Count) - 1) %
                                          int(CharacterPose::Count)),
                 current = CharacterPose(pose);
            std::vector<Vertex> reference;
            app.assets.appendCharacter(reference, name, {3, 12, -7}, .39f, previous, current, .63f,
                                       .7f, .75f, style);
            auto request = app.assets.characterSkin(name, 0, {3, 12, -7}, .39f, previous, current,
                                                    .63f, .7f, .75f, style);
            auto gpu = app.renderer->testSkinVertices({request}, request.count);
            if (gpu.size() != reference.size())
              throw std::runtime_error("GPU character count mismatch");
            for (std::size_t n = 0; n < gpu.size(); ++n) {
              std::array<float, 16> actual, expected;
              std::memcpy(actual.data(), &gpu[n], sizeof(Vertex));
              std::memcpy(expected.data(), &reference[n], sizeof(Vertex));
              for (int k = 0; k < 16; ++k) {
                if (!std::isfinite(actual[k]))
                  throw std::runtime_error("Non-finite GPU skin output");
                maximum = std::max(maximum, std::abs(actual[k] - expected[k]));
              }
            }
            checked += gpu.size();
          }
      std::vector<SkinRequest> batch;
      std::vector<Vertex> batchReference;
      for (const char *name : {"resident_Male", "resident_Female_lod", "resident_Male_lod"}) {
        auto first = std::uint32_t(batchReference.size());
        CharacterStyle style;
        style.pattern = int(batch.size());
        Vec3 position{float(batch.size()) * 3, batch.size() == 2 ? 5997.f : 0.f, -2};
        app.assets.appendCharacter(batchReference, name, position, .8f, CharacterPose::Walk,
                                   CharacterPose::Climb, .3f, 1.7f, .8f, style);
        batch.push_back(app.assets.characterSkin(name, first, position, .8f, CharacterPose::Walk,
                                                 CharacterPose::Climb, .3f, 1.7f, .8f, style));
      }
      auto batchOutput = app.renderer->testSkinVertices(batch, batchReference.size());
      for (std::size_t n = 0; n < batchReference.size(); ++n)
        if (length(batchOutput[n].p - batchReference[n].p) >
                (batchReference[n].p.y > 1000 ? .002f : .0001f) ||
            length(batchOutput[n].normal - batchReference[n].normal) > .0001f)
          throw std::runtime_error("Batched GPU skinning palette/output range failure");
      bool rejected = false;
      batch[1].first = 0;
      try {
        app.renderer->testSkinVertices(batch, batchReference.size());
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      if (!rejected)
        throw std::runtime_error("Overlapping GPU skin output was accepted");
      app.renderer->setSkinRequests({});
      std::cout << "GPU skin parity: " << checked << " vertices; maximum component error "
                << maximum << "\n";
      if (maximum > .0001f)
        throw std::runtime_error("GPU skinning parity failure");
    }
    double last = glfwGetTime(), renderStart = last;
    if (demo) {
      app.scrollOffset = demoScroll;
      if (panel == "help")
        app.helpScroll = demoScroll;
    }
    while (!glfwWindowShouldClose(window) && (maxFrames <= 0 || frames < maxFrames)) {
      glfwPollEvents();
      double now = glfwGetTime();
      float dt = fixedStep > 0 ? fixedStep : std::clamp(float(now - last), .001f, .05f);
      last = now;
      std::filesystem::path frameCapture =
          !capture.empty() && maxFrames > 0 && frames == maxFrames - 1 ? capture
                                                                       : std::filesystem::path{};
      if (playVehicles) {
        auto &w = *app.world;
        auto &r = w.residents[w.player];
        auto &car = w.vehicles[2];
        if (vehicleStage == 0 && r.route.empty() &&
            w.near(r.id, w.facilities[car.dealer].pos, 3.f) && w.operational(car.dealer)) {
          w.buyVehicle(r.id, car.id);
          w.travel(r.id, car.pos + Vec3{1.5f, 0, 0}, TaskKind::None, none);
          vehicleStage = 1;
          std::cout << "VEHICLE PLAYTEST: purchased at the counter; walking to the actual car\n";
        } else if (vehicleStage == 1 && r.route.empty() && w.near(r.id, car.pos, 2.5f)) {
          w.enterVehicle(r.id, car.id);
          w.driveTo(r.id, {80, 3, 116}, false);
          vehicleStage = 2;
          std::cout << "VEHICLE PLAYTEST: driving to the upper floor through the paid ramp\n";
        }
        if (vehicleStage == 2) {
          if (car.pos.y > .8f && car.pos.y < 2.5f && !rampCaptured) {
            rampCaptured = true;
            if (!capture.empty())
              frameCapture = capture.parent_path() / (capture.stem().string() + "-ramp.ppm");
          }
          if (!w.vehicleJourneys.contains(car.id) && length(car.pos - Vec3{80, 3, 116}) < 2) {
            w.driveTo(r.id, {110, 30, 64}, true);
            vehicleStage = 3;
            std::cout << "VEHICLE PLAYTEST: ramp completed; returning to the cargo-lift landing\n";
          }
        }
        if (vehicleStage == 3) {
          if (car.freightLift != none && w.roads[car.freightLift].moving && car.pos.y > 9 &&
              car.pos.y < 22 && !liftCaptured) {
            liftCaptured = true;
            if (!capture.empty())
              frameCapture = capture.parent_path() / (capture.stem().string() + "-freight.ppm");
          }
          if (r.vehicle == none && length(car.pos - Vec3{110, 30, 64}) < 2) {
            vehicleStage = 4;
            vehicleFinishFrame = frames;
            std::cout << "VEHICLE PLAYTEST: freight ride completed and driver exited beside the "
                         "stopped car\n";
          }
        }
        app.cameraFocus = vehicleStage < 2 ? w.position(r.id) : car.pos;
        app.currentFloor = std::clamp(int(app.cameraFocus.y / 3), 0, w.config.floors - 1);
        if (vehicleStage == 4 && frames >= vehicleFinishFrame + 30) {
          if (!capture.empty())
            app.frame(dt, capture);
          break;
        }
      }
      if (playConstruction) {
        auto &w = *app.world;
        const auto &job = w.structuralWorks[constructionJob];
        if (job.material != none && constructionStage == 0) {
          constructionStage = 1;
          std::cout << "CONSTRUCTION PLAYTEST: foreman physically collected materials "
                    << job.material << "\n";
        }
        if (job.supplied && constructionStage == 1) {
          constructionStage = 2;
          std::cout << "CONSTRUCTION PLAYTEST: materials delivered; crew assembling partition\n";
          if (!capture.empty())
            frameCapture = capture.parent_path() / (capture.stem().string() + "-working.ppm");
        }
        if (job.state == StructuralState::Completed && constructionStage == 2) {
          constructionStage = 3;
          constructionFinishedFrame = frames;
          std::cout << "CONSTRUCTION PLAYTEST: partition completed; checking door, wall and budget "
                       "settlement\n";
          if (!capture.empty())
            frameCapture = capture.parent_path() / (capture.stem().string() + "-completed.ppm");
        }
        app.cameraFocus = constructionStage >= 2 ? job.site : w.position(2);
        app.currentFloor = std::clamp(int(app.cameraFocus.y / 3), 0, w.config.floors - 1);
        if (constructionStage == 3 && frames >= constructionFinishedFrame + 30) {
          if (!capture.empty())
            app.frame(dt, capture);
          break;
        }
      }
      if (playAutonomy) {
        auto &w = *app.world;
        if (playStage == 0 && w.residents[w.player].route.empty()) {
          w.residents[w.player].automatic = true;
          w.residentPlans[w.player].nextShopping = w.time;
          w.schedule(w.time + .01, EventKind::Decide, w.player);
          playStage = 1;
          std::cout << "PLAYTEST: arrived on foot; enabled resident decisions\n";
        }
        for (const auto &order : w.orders)
          if (order.buyer == w.player && playStage == 1) {
            playStage = 2;
            std::cout << "PLAYTEST: purchase " << order.id
                      << " escrow=" << w.accounts[order.escrow].balance << "\n";
            if (!capture.empty())
              frameCapture = capture.parent_path() / (capture.stem().string() + "-ordered.ppm");
          }
        for (const auto &order : w.orders)
          if (order.buyer == w.player && order.state == OrderState::Delivered && playStage == 2) {
            playStage = 3;
            playHandoverFrame = frames;
            std::cout << "PLAYTEST: courier " << order.courier << " physically handed over item "
                      << order.item << "; escrow=" << w.accounts[order.escrow].balance << "\n";
            if (!capture.empty())
              frameCapture = capture.parent_path() / (capture.stem().string() + "-handover.ppm");
          }
        Id focus = playStage == 2 ? playCourier : w.player;
        if (playStage == 2)
          for (const auto &o : w.orders)
            if (o.buyer == w.player && o.courier != none && o.state != OrderState::Delivered) {
              focus = o.courier;
              break;
            }
        app.cameraFocus = w.position(focus);
        app.currentFloor = std::clamp(int(app.cameraFocus.y / 3), 0, w.config.floors - 1);
        if (playStage == 3 && frames >= playHandoverFrame + 60) {
          if (!capture.empty())
            app.frame(dt, capture);
          break;
        }
      }
      app.frame(dt, frameCapture);
      ++frames;
    }
    if (playAutonomy) {
      app.world->validate();
      if (playStage != 3) {
        for (const auto &o : app.world->orders)
          if (o.buyer == app.world->player) {
            std::cout << "PLAYTEST TRACE order=" << o.id << " state=" << int(o.state)
                      << " courier=" << o.courier << "\n";
            if (o.courier != none) {
              const auto &r = app.world->residents[o.courier];
              auto p = app.world->position(r.id);
              std::cout << "PLAYTEST TRACE activity=" << int(r.activity) << " task=" << int(r.task)
                        << " position=" << p.x << "," << p.y << "," << p.z
                        << " route=" << r.route.size() << " hunger=" << r.hunger
                        << " thirst=" << r.thirst << " carrying=" << r.carrying << "\n";
            }
          }
        if (!capture.empty())
          app.world->save(capture.string() + ".mdsave");
        throw std::runtime_error("Autonomy playthrough did not complete a real delivery");
      }
      std::cout << "PLAYTEST PASS: walking, budgeted checkout, real courier delivery, live UI and "
                   "conserved state\n";
    }
    if (playVehicles) {
      auto &w = *app.world;
      w.validate();
      if (vehicleStage != 4 || !rampCaptured || !liftCaptured) {
        if (!capture.empty())
          w.save(capture.string() + ".mdsave");
        throw std::runtime_error("Vehicle playthrough did not complete real road/lift travel");
      }
      std::cout << "VEHICLE PLAYTEST PASS: real walking, purchase, ramp traversal, fuel/tolls, "
                   "cargo boarding/motion/unloading and safe exit\n";
    }
    if (playConstruction) {
      auto &w = *app.world;
      w.validate();
      const auto &job = w.structuralWorks[constructionJob];
      if (constructionStage != 3 || !w.interiorWalls[job.wall].active ||
          !w.clearPosition({74, 0, 160}) || w.clearPosition({71, 0, 160}) ||
          w.accounts[job.escrow].balance != 0) {
        if (!capture.empty())
          w.save(capture.string() + ".mdsave");
        throw std::runtime_error("Construction playthrough did not complete physical work");
      }
      std::cout << "CONSTRUCTION PLAYTEST PASS: real walking, material collection/delivery, paid "
                   "crew work, rendered partition and walkable doorway\n";
    }
    std::cout << "Render loop seconds: " << glfwGetTime() - renderStart << "\n";
    std::cout << "CPU scene build seconds: " << app.sceneBuildSeconds << "\n";
    std::cout << "GPU floor instances: " << app.renderer->instanceCount() << "\n";
    std::cout << "GPU skinned vertices: " << app.renderer->skinnedVertexCount() << "\n";
    std::cout << "Rendered " << frames << " frames\n";
  } catch (...) {
    glfwDestroyWindow(window);
    glfwTerminate();
    throw;
  }
  glfwDestroyWindow(window);
  glfwTerminate();
  return result;
}
} // namespace mega
