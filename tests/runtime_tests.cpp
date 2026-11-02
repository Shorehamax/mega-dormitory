#include "mega/runtime_paths.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace mega;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main() {
  auto root = std::filesystem::temp_directory_path() /
              ("mega-runtime-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    std::filesystem::create_directories(root / "assets/locales");
    std::filesystem::create_directories(root / "assets/fonts");
    std::filesystem::create_directories(root / "assets/models");
    std::filesystem::create_directories(root / "shaders");
    std::ofstream(root / "assets/locales/en.json") << "{}";
    std::ofstream(root / "assets/fonts/NotoSansCJK-Regular.ttc") << "test-font";
    require(!RuntimePaths::complete(root), "Incomplete shaders accepted");
    for (const char *shader :
         {"scene.vert", "scene.frag", "shadow.vert", "shadow.frag", "pressure.comp", "skin.comp"})
      std::ofstream(root / "shaders" / (std::string(shader) + ".spv")) << "test-shader";
    require(RuntimePaths::complete(root), "Complete package rejected");
    auto p = RuntimePaths::discover(root / "mega_dormitory", "/nonexistent-source/assets",
                                    "/nonexistent-source/shaders", "/nonexistent-source");
    require(p.installed && p.assets == root / "assets" && p.shaders == root / "shaders",
            "Package depends on source directory");
    require(p.saves != root / "userdata" && p.saves.is_absolute(),
            "Package saves in its installation");
    auto moved = root.parent_path() / (root.filename().string() + " moved folder");
    std::filesystem::rename(root, moved);
    root = moved;
    auto q = RuntimePaths::discover(root / "mega_dormitory", "/source/assets", "/source/shaders",
                                    "/source");
    require(q.assets == root / "assets" && q.saves == p.saves,
            "Moving package changed saves or broke assets");
    std::filesystem::remove(root / "shaders/skin.comp.spv");
    bool rejected = false;
    try {
      RuntimePaths::discover(root / "mega_dormitory", "/source/assets", "/source/shaders",
                             "/source");
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "Broken install silently used developer assets");
    std::filesystem::remove_all(root / "assets");
    std::filesystem::remove_all(root / "shaders");
    std::ofstream(root / "runtime-layout.json") << "{\"layout\":1}";
    rejected = false;
    try {
      RuntimePaths::discover(root / "mega_dormitory", "/source/assets", "/source/shaders",
                             "/source");
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "Marked installation with missing data used developer paths");
    std::filesystem::remove_all(root);
    std::cout << "PASS missing resources, installed paths, independent saves, relocation with "
                 "spaces and broken install detection\n";
    return 0;
  } catch (const std::exception &e) {
    std::filesystem::remove_all(root);
    std::cerr << e.what() << '\n';
    return 1;
  }
}
