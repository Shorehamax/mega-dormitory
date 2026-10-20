#include "mega/world.hpp"
#include <chrono>
#include <iostream>
#include <string>
namespace mega {
int runApplication(int, char **);
}
int main(int argc, char **argv) {
  try {
    bool headless = false, autoplay = false;
    for (int i = 1; i < argc; ++i)
      if (std::string(argv[i]) == "--headless")
        headless = true;
    if (!headless)
      return mega::runApplication(argc, argv);
    mega::Config cfg;
    double seconds = 3600;
    std::string save, load;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      auto value = [&]() {
        if (i + 1 >= argc)
          throw std::runtime_error("Missing argument value");
        return std::string(argv[++i]);
      };
      if (a == "--autoplay")
        autoplay = true;
      else if (a == "--mode") {
        auto m = value();
        if (m == "cheat")
          cfg.mode = mega::Mode::Cheat;
        else if (m == "manager")
          cfg.mode = mega::Mode::Manager;
        else if (m != "survival")
          throw std::runtime_error("Unknown mode");
      } else if (a == "--population")
        cfg.population = std::stoi(value());
      else if (a == "--floors")
        cfg.floors = std::stoi(value());
      else if (a == "--floor-width")
        cfg.floorWidth = std::stoi(value());
      else if (a == "--floor-depth")
        cfg.floorDepth = std::stoi(value());
      else if (a == "--lobby-width")
        cfg.lobbyWidth = std::stoi(value());
      else if (a == "--lobby-depth")
        cfg.lobbyDepth = std::stoi(value());
      else if (a == "--seconds")
        seconds = std::stod(value());
      else if (a == "--seed")
        cfg.seed = std::stoull(value());
      else if (a == "--save")
        save = value();
      else if (a == "--load")
        load = value();
    }
    auto start = std::chrono::steady_clock::now();
    auto w = load.empty() ? mega::World::generate(cfg) : mega::World::load(load);
    auto created = std::chrono::steady_clock::now();
    if (autoplay)
      w.residents[w.player].automatic = true;
    double initialTime = w.time, end = w.time + seconds;
    while (w.time < end && !w.gameOver)
      w.advance(std::min(3600., end - w.time));
    w.validate();
    if (!save.empty())
      w.save(save);
    auto done = std::chrono::steady_clock::now();
    std::size_t queued = 0;
    for (auto &f : w.facilities)
      queued += f.queue.size();
    std::cout << "{\"population\":" << w.residents.size() << ",\"living\":" << w.living()
              << ",\"floors\":" << w.config.floors << ",\"facilities\":" << w.facilities.size()
              << ",\"queued\":" << queued << ",\"simulated_seconds\":" << (w.time - initialTime)
              << ",\"happiness\":" << w.happiness()
              << ",\"internal_money_cents\":" << w.internalMoney() << ",\"generation_seconds\":"
              << std::chrono::duration<double>(created - start).count()
              << ",\"run_seconds\":" << std::chrono::duration<double>(done - created).count()
              << "}\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "MEGA DORMITORY: " << e.what() << '\n';
    return 1;
  }
}
